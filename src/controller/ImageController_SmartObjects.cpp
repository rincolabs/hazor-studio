// Smart Objects: rendering, invalidation and the editing operations that
// create or re-point an instance.
//
// What is NOT here, on purpose: a single line that teaches a compositor about
// Smart Objects. An instance is an ordinary Type::Layer whose cpuImage happens
// to hold another document's composite, so DocumentCompositor, LayerCompositor,
// GPUViewport, ProjectionCache and RenderCache need no knowledge of it at all —
// and mask, blend, opacity, transform and layer styles work on it for free.
// If a change here starts needing a compositor edit, the premise in
// SmartObjectData.hpp has been violated somewhere: stop and re-read it.

#include "ImageController.hpp"
#include "Commands.hpp"

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "smartobject/ContainerRegistry.hpp"
#include "smartobject/DocumentContainer.hpp"
#include "smartobject/SmartObjectCommands.hpp"
#include "smartobject/SmartObjectTransfer.hpp"

#include <QFileInfo>
#include <QSet>

#include <algorithm>

namespace {

// Every Smart Object instance in `doc`, with the payload it carries.
//
// The payload is handed over as a shared_ptr BY VALUE, not as a reference to
// the pointee. Visitors here legitimately replace a node's payload while the
// walk is in progress (markSmartObjectLinkBroken, and the render commit), and
// that assignment drops the last reference to the old value — a reference
// parameter would dangle the moment a visitor did its job. Holding a reference
// for the duration of the call costs one atomic increment and makes the whole
// class of bug unrepresentable.
template <typename Fn>
void forEachInstanceIn(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes, Fn&& fn)
{
    for (const auto& node : nodes) {
        if (!node) continue;
        if (node->type == LayerTreeNode::Type::Layer && node->layer
            && node->layer->smartObjectData) {
            // A LOCAL copy of the pointer, deliberately — binding straight to
            // the member would still dangle, because it is that member a
            // visitor reassigns.
            const std::shared_ptr<const SmartObjectData> payload =
                node->layer->smartObjectData;
            fn(node.get(), payload);
        }
        forEachInstanceIn(node->children, fn);
    }
}

int flatIndexOf(const Document* doc, const LayerTreeNode* node)
{
    if (!doc || !node)
        return -1;
    const auto flat = doc->flatten();
    const auto it = std::find(flat.begin(), flat.end(), node);
    return it == flat.end() ? -1 : static_cast<int>(it - flat.begin());
}

} // namespace

void ImageController::setDocumentContainer(std::shared_ptr<DocumentContainer> container,
                                           const SmartObjectDocId& docId)
{
    m_container = std::move(container);
    m_docId = docId;
}

SmartObjectRenderService* ImageController::ensureSmartObjectRenderService()
{
    if (!m_smartObjectRenderService) {
        m_smartObjectRenderService = std::make_unique<SmartObjectRenderService>();
        connect(m_smartObjectRenderService.get(), &SmartObjectRenderService::renderFinished,
                this, &ImageController::onSmartObjectRenderFinished);
    }
    return m_smartObjectRenderService.get();
}

void ImageController::refreshSmartObjectInstances(const SmartObjectDocId& docId, bool force)
{
    requestSmartObjectRenders(docId, force, /*marksDirty=*/true);
}

void ImageController::refreshAllSmartObjectInstances(bool afterLoad)
{
    // Also the post-load validation pass: walking every instance is what
    // refreshes each one's link state, so a Linked reference whose file is gone
    // gets its badge without a separate traversal. Linked targets are NOT
    // opened here — a persisted render is trusted until the user asks for
    // "Update Modified Content" (there is no file watcher, by design).
    //
    // `afterLoad` only decides whether the resulting commits mark documents
    // unsaved. Regenerating a render that already matches what is on disk must
    // not make a freshly opened project look edited.
    dispatchSmartObjectRenders(
        [](const Document&, LayerTreeNode*, const SmartObjectData&) { return true; },
        /*force=*/false, /*openLinked=*/false, /*marksDirty=*/!afterLoad);
}

std::vector<std::pair<SmartObjectDocId, std::shared_ptr<Document>>>
ImageController::smartObjectHosts() const
{
    std::vector<std::pair<SmartObjectDocId, std::shared_ptr<Document>>> hosts;
    if (!m_container)
        return hosts;
    for (const SmartObjectDocId& id : m_container->documentIds()) {
        if (auto doc = m_container->document(id))
            hosts.emplace_back(id, std::move(doc));
    }
    // The working copies come LAST and keep their docId: they are hosts (their
    // own nested instances need rendering) but never sources — an instance of
    // `docId` resolves to the STORED document, which is exactly why editing a
    // Smart Object's contents changes nothing until it is committed.
    for (auto& session : m_container->editSessions()) {
        if (session.second)
            hosts.emplace_back(session.first, std::move(session.second));
    }
    return hosts;
}

bool ImageController::commitSmartObjectEditSession(const SmartObjectDocId& docId)
{
    if (!m_container)
        return false;
    const auto working = m_container->editSession(docId);
    const auto stored = m_container->document(docId);
    if (!working || !stored)
        return false;

    // In place, so the stored Document OBJECT survives: the container, the undo
    // commands that hold a displaced document (ReplaceContentsCommand) and any
    // future holder all keep pointing at the same thing. Re-pointing the docId
    // at the working copy instead would make the tab's live document the
    // committed one, and the isolation would be gone from that moment on.
    smartobject::assignDocumentContent(*stored, *working);
    // Generations are per Document object, so the working copy's counter means
    // nothing here. Bumping the stored one is what makes the commit visible to
    // the staleness test — including for instances in documents that no tab is
    // showing, and for the next idle pass after this forced one.
    ++stored->compositionGeneration;
    refreshSmartObjectInstances(docId, /*force=*/true);
    return true;
}

std::shared_ptr<Document> ImageController::resolveSmartObjectDocument(
    const SmartObjectData& data, bool openIfNeeded) const
{
    if (!m_container)
        return nullptr;

    if (data.mode == SmartObjectStorageMode::Embedded)
        return m_container->document(data.docId);

    // Linked: always through the registry, never by loading a second copy. If
    // the target is already open in a tab, THAT container is the one we resolve
    // to — so editing B.hzs in its own tab invalidates the instances in A.hzs
    // through the same generation mechanism, with no file round-trip and no
    // chance of two divergent Documents for one file.
    const QString absolute = smartobject::resolveLinkPath(m_container->path(), data.linkedPath);
    if (absolute.isEmpty())
        return nullptr;
    auto target = ContainerRegistry::instance().find(absolute);
    if (!target) {
        if (!openIfNeeded || !QFileInfo(absolute).isFile())
            return nullptr;
        target = ContainerRegistry::instance().open(absolute);
        if (!target)
            return nullptr;
    }
    // A linked payload stores the TARGET file's docId. Falling back to its root
    // keeps pre-existing files and hand-written ids working.
    auto doc = target->document(data.docId);
    return doc ? doc : target->rootDocument();
}

SmartObjectRenderKey ImageController::smartObjectRenderKey(const SmartObjectData& data,
                                                           const Document& host)
{
    SmartObjectRenderKey key;
    key.docId = data.docId;
    key.linkedPath = data.mode == SmartObjectStorageMode::Linked ? data.linkedPath : QString();
    key.targetProfile = host.colorProfile().fingerprint();
    return key;
}

void ImageController::requestSmartObjectRenders(const SmartObjectDocId& docId,
                                                bool force, bool marksDirty)
{
    dispatchSmartObjectRenders(
        [&docId](const Document&, LayerTreeNode*, const SmartObjectData& data) {
            return data.mode == SmartObjectStorageMode::Embedded && data.docId == docId;
        },
        force, /*openLinked=*/false, marksDirty);
}

bool ImageController::renderLinkedSmartObject(const LayerTreeNode* node, bool force)
{
    if (!node)
        return false;
    return dispatchSmartObjectRenders(
        [node](const Document&, LayerTreeNode* candidate, const SmartObjectData&) {
            return candidate == node;
        },
        force, /*openLinked=*/true, /*marksDirty=*/true) > 0;
}

int ImageController::dispatchSmartObjectRenders(const SmartObjectInstanceFilter& filter,
                                                bool force, bool openLinked, bool marksDirty)
{
    if (!m_container)
        return 0;

    // One job per KEY, never one per instance: three instances of the same
    // document in the same project collapse into a single composite whose
    // QImage all three then share.
    struct Pending {
        std::shared_ptr<Document> source;
        ColorProfile target;
    };
    QHash<SmartObjectRenderKey, Pending> work;

    for (const auto& [hostId, host] : smartObjectHosts()) {
        forEachInstanceIn(host->roots,
                          [&](LayerTreeNode* node,
                              const std::shared_ptr<const SmartObjectData>& payload) {
            const SmartObjectData& data = *payload;
            if (!node->layer || !filter(*host, node, data))
                return;
            // Cycle guard at RESOLVE time, not only at creation: a hand-edited
            // or half-migrated file could carry a loop, and a cascade over a
            // loop never terminates. canReference() already answers exactly
            // "would host → docId be a legal edge?", so no second graph walk.
            if (data.mode == SmartObjectStorageMode::Embedded
                && hostId != data.docId
                && !m_container->canReference(hostId, data.docId)) {
                return;
            }

            auto source = resolveSmartObjectDocument(data, openLinked);
            if (!source || source->size.isEmpty()) {
                // Distinguish "gone" from "merely not loaded yet": a Linked
                // file that exists but has no container open is not broken, it
                // just has nothing to re-render from right now.
                markSmartObjectLinkBroken(node, !smartObjectReferenceExists(data));
                return;
            }
            markSmartObjectLinkBroken(node, false);
            const bool stale = data.state.sourceGeneration != source->compositionGeneration
                || node->layer->cpuImage.isNull();
            if (!force && !stale)
                return;

            Pending pending;
            pending.source = std::move(source);
            pending.target = host->colorProfile();
            work.insert(smartObjectRenderKey(data, *host), std::move(pending));
        });
    }

    if (work.isEmpty())
        return 0;

    auto* service = ensureSmartObjectRenderService();
    int dispatched = 0;
    for (auto it = work.constBegin(); it != work.constEnd(); ++it) {
        if (service->request(it.key(), *it.value().source, it.value().target, marksDirty))
            ++dispatched;
    }
    return dispatched;
}

bool ImageController::smartObjectReferenceExists(const SmartObjectData& data) const
{
    if (!m_container)
        return false;
    if (data.mode == SmartObjectStorageMode::Embedded)
        return m_container->contains(data.docId);
    const QString absolute = smartobject::resolveLinkPath(m_container->path(), data.linkedPath);
    return !absolute.isEmpty() && QFileInfo(absolute).isFile();
}

void ImageController::markSmartObjectLinkBroken(LayerTreeNode* node, bool broken)
{
    if (!node || !node->layer || !node->layer->smartObjectData)
        return;
    // By value: the assignment below releases the node's reference, and this is
    // called from inside a tree walk that may still be reading the old value.
    const std::shared_ptr<const SmartObjectData> data = node->layer->smartObjectData;
    if (data->linkBroken == broken)
        return;
    // The payload is const, so this builds a new value and swaps the pointer.
    auto next = std::make_shared<SmartObjectData>(*data);
    next->linkBroken = broken;
    node->layer->smartObjectData = std::move(next);
    node->thumbnailDirty = true;
    // A broken link is not a pixel change: the cached render — which is the only
    // thing left of the reference — is exactly what stays on screen. Hence no
    // generation bump, and no history entry.
    emit smartObjectLinkStateChanged(flatIndexOf(m_doc, node), broken);
}

void ImageController::onSmartObjectRenderFinished(SmartObjectRenderResult result)
{
    if (!m_container || !result.ok || result.image.isNull())
        return;

    // Revision discard: a result built from a state the key has already moved
    // past never reaches the screen. Same contract as ProjectionCache's
    // generation check and RawDevelopState::revision.
    const auto committed = m_committedSmartObjectGenerations.constFind(result.key);
    if (committed != m_committedSmartObjectGenerations.constEnd()
        && result.jobGeneration < *committed) {
        return;
    }
    m_committedSmartObjectGenerations.insert(result.key, result.jobGeneration);

    QStringList affected;
    bool touchedOwnDocument = false;

    for (const auto& [hostId, host] : smartObjectHosts()) {
        bool touched = false;
        forEachInstanceIn(host->roots,
                          [&](LayerTreeNode* node,
                              const std::shared_ptr<const SmartObjectData>& payload) {
            const SmartObjectData& data = *payload;
            if (!node->layer || !(smartObjectRenderKey(data, *host) == result.key))
                return;
            Layer* layer = node->layer.get();

            // The SAME QImage goes to every instance — Qt's implicit sharing
            // makes that free, and nothing ever writes to it (the layer has no
            // directly editable pixels), so no instance can detach it.
            if (layer->rasterStorage.isEnabled())
                layer->rasterStorage.clear();
            layer->cpuImage = result.image;
            layer->tiledSystem = false;
            layer->tileManager.clear();
            layer->dirtyRegion.clear();
            layer->invalidateContentBounds();
            layer->textureOutdated = true;
            layer->pendingGpuUpload = true;

            // The payload is const: an update BUILDS A NEW VALUE and swaps the
            // pointer. Both instances of a duplicated layer end up pointing at
            // this same new value, which is correct — they do share docId, mode
            // and render generation.
            auto next = std::make_shared<SmartObjectData>(data);
            next->state.sourceSize = result.sourceSize;
            next->state.sourceDpi = result.sourceDpi;
            next->state.sourceGeneration = result.jobGeneration;
            next->state.revision = data.state.revision + 1;
            next->linkBroken = false;
            layer->smartObjectData = std::move(next);

            node->invalidateEffects();
            node->thumbnailDirty = true;
            touched = true;
            if (host.get() == m_doc)
                touchedOwnDocument = true;
        });

        if (touched) {
            ++host->compositionGeneration;
            // A docId can appear twice — once stored, once as an open edit
            // session's working copy — and the cascade below must not render
            // the same level twice.
            if (!affected.contains(hostId))
                affected.push_back(hostId);
        }
    }

    if (affected.isEmpty())
        return;

    // imageChanged() repaints the canvas; layerChanged() is DELIBERATELY not
    // emitted. Every layerChanged listener treats it as "the user edited this
    // layer" and marks the tab dirty — which would make simply OPENING a
    // project with a Smart Object in it look modified, and would be a lie: the
    // document did not change, a regenerable cache did (see D10 in
    // docs/RENDER-PIPELINE.md). The panel and Properties refresh that a
    // layerChanged would have triggered is done by MainWindow in response to
    // smartObjectRenderCommitted instead.
    if (touchedOwnDocument)
        emit imageChanged();
    emit smartObjectRenderCommitted(result.key.docId, affected, result.marksDirty);

    // The cascade, chained by commit: the documents whose instances just
    // changed are themselves now stale for anyone displaying THEM, so the next
    // level is scheduled here and not a moment earlier.
    //
    //   C changed → render B
    //               B commits → ++B.compositionGeneration → render A
    //                                                       A commits
    //
    // Merely ordering [B, A] at enqueue time is NOT enough: run concurrently,
    // A's snapshot would still hold B's old pixels, and both outcomes are bad —
    // if B's commit bumps B's generation, A re-renders (wasted work plus a
    // visible flash of wrong content); if it does not, A stays stale forever.
    //
    // The dirty flag rides along: if this render made a document unsaved, so
    // does every render it triggers up the chain.
    for (const SmartObjectDocId& hostId : affected)
        requestSmartObjectRenders(hostId, /*force=*/false, result.marksDirty);
}

// ─────────────────────────────────────────────────────────────────────────
// Creating, editing and re-pointing instances.
//
// These ARE real edits (they change what the document references), so unlike
// the cache commit above every one of them goes through a command.
// ─────────────────────────────────────────────────────────────────────────

SmartObjectDocId ImageController::smartObjectDocIdAt(int flatIndex) const
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer
        || !node->layer->smartObjectData) {
        return {};
    }
    return node->layer->smartObjectData->docId;
}

std::shared_ptr<const SmartObjectData> ImageController::smartObjectDataAt(int flatIndex) const
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
        return nullptr;
    return node->layer->smartObjectData;
}

// Builds the instance layer for `docId`: a plain Type::Layer whose cpuImage is
// the referenced document's render and whose payload says where that came from.
std::unique_ptr<LayerTreeNode> ImageController::makeSmartObjectNode(
    const SmartObjectDocId& docId,
    SmartObjectStorageMode mode,
    const QString& linkedPath,
    const Document& source,
    const QString& name)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = name;
    node->nameIsAuto = false;
    node->layer = std::make_shared<Layer>();
    node->layer->name = name;
    node->layer->owner = node.get();

    // A transparent placeholder at the referenced document's size. The real
    // pixels arrive from the first render commit; sizing it correctly now keeps
    // the layer's bounds (and therefore the transform handles and the
    // thumbnail) right from the moment it appears.
    QImage placeholder(source.size.isEmpty() ? QSize(1, 1) : source.size,
                       QImage::Format_RGBA8888);
    placeholder.fill(Qt::transparent);
    node->layer->cpuImage = placeholder;
    node->layer->resetTransform = QTransform();
    node->layer->hasResetTransform = true;

    auto data = std::make_shared<SmartObjectData>();
    data->mode = mode;
    data->docId = docId;
    data->linkedPath = linkedPath;
    data->state.sourceSize = source.size;
    data->state.sourceDpi = source.resolutionDpi;
    // Marks the instance as never rendered, so the staleness test schedules
    // its first render on its own — the forced request below is for
    // immediacy, not correctness, and the placeholder can never get stuck.
    data->state.sourceGeneration = kSmartObjectNeverRendered;
    node->layer->smartObjectData = std::move(data);
    return node;
}

int ImageController::convertToSmartObject(const std::vector<int>& flatIndices)
{
    if (!m_doc || !m_container) {
        emit operationBlocked(tr("No document is open."));
        return -1;
    }

    // Resolve to node pointers first: every index shifts as soon as the first
    // node is detached.
    std::vector<LayerTreeNode*> targets;
    {
        const auto flat = m_doc->flatten();
        std::vector<int> indices = flatIndices;
        if (indices.empty() && m_doc->activeFlatIndex >= 0)
            indices.push_back(m_doc->activeFlatIndex);
        std::sort(indices.begin(), indices.end());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        for (int idx : indices) {
            if (idx < 0 || idx >= static_cast<int>(flat.size()))
                continue;
            auto* node = flat[idx];
            if (!node || node->type == LayerTreeNode::Type::Adjustment)
                continue;
            // Skip a node nested inside another selected node: it travels with
            // its ancestor, and moving it twice would lose it.
            bool nestedInTarget = false;
            for (auto* ancestor = node->parent; ancestor; ancestor = ancestor->parent) {
                if (std::find(targets.begin(), targets.end(), ancestor) != targets.end()) {
                    nestedInTarget = true;
                    break;
                }
            }
            if (!nestedInTarget)
                targets.push_back(node);
        }
    }
    if (targets.empty()) {
        emit operationBlocked(tr("Select one or more layers to convert."));
        return -1;
    }
    for (auto* node : targets) {
        if (node->isFullyLocked()) {
            emit operationBlocked(tr("A selected layer is fully locked."));
            return -1;
        }
    }

    auto cloneRoots = [](const std::vector<std::unique_ptr<LayerTreeNode>>& roots) {
        std::vector<std::unique_ptr<LayerTreeNode>> clones;
        clones.reserve(roots.size());
        for (const auto& r : roots) {
            if (r)
                clones.push_back(r->shallowClone());  // COW: structural op
        }
        return clones;
    };
    auto beforeRoots = cloneRoots(m_doc->roots);
    const int beforeActive = m_doc->activeFlatIndex;
    const std::set<int> beforeSelected = m_doc->selectedFlatIndices;

    // The new document inherits the parent's canvas, resolution and profile:
    // the layers moving into it were positioned in that coordinate space, so
    // any other canvas would shift them.
    auto content = std::make_shared<Document>();
    content->name = targets.size() == 1
        ? targets.front()->name
        : tr("Smart Object");
    content->size = m_doc->size;
    content->resolutionDpi = m_doc->resolutionDpi;
    content->colorMode = m_doc->colorMode;
    content->bitDepth = m_doc->bitDepth;
    content->setColorProfile(m_doc->colorProfile());
    content->setProfileSource(m_doc->profileSource());
    if (!content->size.isEmpty())
        content->selection.resize(content->size.width(), content->size.height());

    // Where the instance goes: the slot of the topmost (lowest flat index)
    // selected node, so the Smart Object lands where its content was.
    LayerTreeNode* anchor = targets.front();
    LayerTreeNode* anchorParent = anchor->parent;
    auto& anchorSiblings = anchorParent ? anchorParent->children : m_doc->roots;
    int anchorPos = 0;
    for (int i = 0; i < static_cast<int>(anchorSiblings.size()); ++i) {
        if (anchorSiblings[i].get() == anchor) { anchorPos = i; break; }
    }

    // Detach in reverse document order so each erase leaves the earlier
    // positions valid, then restore document order in the new document.
    std::vector<std::unique_ptr<LayerTreeNode>> moved;
    for (auto it = targets.rbegin(); it != targets.rend(); ++it) {
        auto& siblings = (*it)->parent ? (*it)->parent->children : m_doc->roots;
        for (auto sit = siblings.begin(); sit != siblings.end(); ++sit) {
            if (sit->get() != *it)
                continue;
            auto taken = std::move(*sit);
            siblings.erase(sit);
            taken->parent = nullptr;
            moved.push_back(std::move(taken));
            break;
        }
    }
    std::reverse(moved.begin(), moved.end());
    for (auto& node : moved)
        content->roots.push_back(std::move(node));
    content->activeFlatIndex = 0;
    content->selectedFlatIndices = {0};

    const SmartObjectDocId docId = m_container->addDocument(content);
    if (docId.isEmpty()) {
        emit operationBlocked(tr("Could not create the Smart Object's document."));
        return -1;
    }

    auto instance = makeSmartObjectNode(docId, SmartObjectStorageMode::Embedded,
                                        QString(), *content, content->name);
    LayerTreeNode* instancePtr = instance.get();
    instance->parent = anchorParent;
    anchorPos = std::min(anchorPos, static_cast<int>(anchorSiblings.size()));
    anchorSiblings.insert(anchorSiblings.begin() + anchorPos, std::move(instance));

    const int newIndex = flatIndexOf(m_doc, instancePtr);
    m_doc->activeFlatIndex = newIndex;
    m_doc->selectedFlatIndices = newIndex >= 0 ? std::set<int>{newIndex} : std::set<int>{};

    auto afterRoots = cloneRoots(m_doc->roots);
    m_history.push(std::make_unique<LayerTreeStateCommand>(
        m_doc,
        std::move(beforeRoots), beforeActive, beforeSelected,
        std::move(afterRoots), m_doc->activeFlatIndex, m_doc->selectedFlatIndices,
        tr("Convert to Smart Object")));

    ++m_doc->compositionGeneration;
    emit layerChanged(newIndex);
    emit activeLayerChanged(newIndex);
    emit selectionChanged();
    emit imageChanged();
    emit documentChanged();

    // First render. Forced: the placeholder has to be replaced even though the
    // instance was created from pixels that are already on screen.
    requestSmartObjectRenders(docId, /*force=*/true, /*marksDirty=*/true);
    return newIndex;
}

int ImageController::placeEmbeddedDocument(const QString& path)
{
    if (!m_doc || !m_container) {
        emit operationBlocked(tr("No document is open."));
        return -1;
    }

    // Load through the registry so a project already open is the SAME container
    // (and the same Documents) rather than a second copy, then import a private
    // copy of its root subgraph into this container — embedded means "stored
    // here", so later edits must not write back to the source file.
    QString error;
    // Whether the registry already held this file decides whether we may drop
    // it again below.
    const bool wasOpen = ContainerRegistry::instance().find(path) != nullptr;
    auto source = ContainerRegistry::instance().open(path, &error);
    if (!source || !source->rootDocument()) {
        emit operationBlocked(error.isEmpty()
            ? tr("Could not open \"%1\".").arg(QFileInfo(path).fileName()) : error);
        return -1;
    }
    if (source.get() == m_container.get()) {
        emit operationBlocked(tr("A project cannot be placed inside itself."));
        return -1;
    }

    const SmartObjectDocId docId =
        smartobject::importDocumentsInto(*m_container, *source, source->rootId());

    // Embedded means "copied in": nothing references the source file any more,
    // so a container we loaded only to read it must not stay in the registry
    // for the rest of the session. One opened by a tab or a link is left alone.
    if (!wasOpen)
        ContainerRegistry::instance().forget(path);

    if (docId.isEmpty()) {
        emit operationBlocked(tr("Could not import \"%1\".").arg(QFileInfo(path).fileName()));
        return -1;
    }
    return insertSmartObjectInstance(docId, SmartObjectStorageMode::Embedded, QString(),
                                     QFileInfo(path).completeBaseName(),
                                     tr("Place Embedded"));
}

int ImageController::placeLinkedDocument(const QString& path)
{
    if (!m_doc || !m_container) {
        emit operationBlocked(tr("No document is open."));
        return -1;
    }

    QString error;
    auto source = ContainerRegistry::instance().open(path, &error);
    if (!source || !source->rootDocument()) {
        emit operationBlocked(error.isEmpty()
            ? tr("Could not open \"%1\".").arg(QFileInfo(path).fileName()) : error);
        return -1;
    }
    // The cross-FILE cycle check, which DocumentContainer cannot answer: a
    // Linked reference names a path, and only the registry knows which path is
    // which container. A.hzs → B.hzs → A.hzs is caught here.
    if (ContainerRegistry::instance().wouldCreateCycle(
            m_container->path(), m_docId, path, source->rootId())) {
        emit operationBlocked(tr("That would make the project reference itself."));
        return -1;
    }

    // A Linked instance carries the TARGET's root docId so a later resolve
    // knows which document of that file it displays, plus the path it resolves
    // through. The docId alone is not a key in this container — which is why
    // every lookup checks `mode` first.
    const QString stored = smartobject::relativeLinkPath(m_container->path(), path);
    return insertSmartObjectInstance(source->rootId(), SmartObjectStorageMode::Linked,
                                     stored, QFileInfo(path).completeBaseName(),
                                     tr("Place Linked"));
}

int ImageController::insertSmartObjectInstance(const SmartObjectDocId& docId,
                                               SmartObjectStorageMode mode,
                                               const QString& linkedPath,
                                               const QString& name,
                                               const QString& commandName)
{
    SmartObjectData probe;
    probe.mode = mode;
    probe.docId = docId;
    probe.linkedPath = linkedPath;
    std::shared_ptr<Document> source = resolveSmartObjectDocument(probe, /*openIfNeeded=*/true);
    if (!source) {
        emit operationBlocked(tr("The Smart Object's document could not be resolved."));
        return -1;
    }

    auto instance = makeSmartObjectNode(docId, mode, linkedPath, *source, name);
    LayerTreeNode* instancePtr = instance.get();

    const int target = m_doc->activeFlatIndex >= 0 ? m_doc->activeFlatIndex : 0;
    auto command = std::make_unique<AddLayerCommand>(
        m_doc, target, std::move(instance), commandName);
    command->execute();
    m_history.push(std::move(command));

    const int newIndex = flatIndexOf(m_doc, instancePtr);
    emit layerChanged(newIndex);
    emit activeLayerChanged(newIndex);
    emit selectionChanged();
    emit imageChanged();
    emit documentChanged();

    if (mode == SmartObjectStorageMode::Embedded)
        requestSmartObjectRenders(docId, /*force=*/true, /*marksDirty=*/true);
    else
        renderLinkedSmartObject(instancePtr, /*force=*/true);
    return newIndex;
}

// ─────────────────────────────────────────────────────────────────────────
// Linked references and storage conversion.
//
// Three operations, not one, because only the first is really "swap the
// payload". The other two MOVE DOCUMENTS between containers and change what
// gets persisted, so the container work happens first and the command records
// only the part that has to be undoable.
// ─────────────────────────────────────────────────────────────────────────

bool ImageController::relinkSmartObject(int flatIndex, const QString& path)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer || !node->layer->smartObjectData || !m_container) {
        emit operationBlocked(tr("This layer is not a Smart Object."));
        return false;
    }
    if (!node->canEditContent()) {
        emit operationBlocked(tr("This layer is locked and cannot be relinked."));
        return false;
    }

    QString error;
    auto target = ContainerRegistry::instance().open(path, &error);
    if (!target || !target->rootDocument()) {
        emit operationBlocked(error.isEmpty()
            ? tr("Could not open \"%1\".").arg(QFileInfo(path).fileName()) : error);
        return false;
    }
    if (target.get() == m_container.get()
        || ContainerRegistry::instance().wouldCreateCycle(
               m_container->path(), m_docId, path, target->rootId())) {
        emit operationBlocked(tr("That would make the project reference itself."));
        return false;
    }

    const auto before = node->layer->smartObjectData;
    auto after = std::make_shared<SmartObjectData>(*before);
    after->mode = SmartObjectStorageMode::Linked;
    after->docId = target->rootId();
    after->linkedPath = smartobject::relativeLinkPath(m_container->path(), path);
    after->linkBroken = false;
    // The new target has never been rendered for this instance.
    after->state.sourceGeneration = kSmartObjectNeverRendered;

    // The pixels do not change here: the cached render stays on screen until the
    // new target's own render commits, which is what keeps a relink from
    // flashing an empty layer.
    const QImage beforeImage = node->layer->cpuImage;
    auto command = std::make_unique<smartobject::RelinkCommand>(
        m_doc, flatIndex, before, beforeImage, after, beforeImage,
        tr("Relink Smart Object"));
    command->execute();
    m_history.push(std::move(command));

    emit layerChanged(flatIndex);
    emit imageChanged();
    emit documentChanged();
    renderLinkedSmartObject(m_doc->nodeAt(flatIndex), /*force=*/true);
    return true;
}

bool ImageController::updateSmartObjectContent(int flatIndex)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer || !node->layer->smartObjectData) {
        emit operationBlocked(tr("This layer is not a Smart Object."));
        return false;
    }
    const auto& data = *node->layer->smartObjectData;

    // Embedded content is never out of date: editing it goes through the
    // container, which invalidates the instances immediately. "Update" exists
    // for Linked references, where the file can change behind our back and
    // there is deliberately no watcher (see docs/SMART-OBJECTS.md).
    if (data.mode == SmartObjectStorageMode::Embedded) {
        requestSmartObjectRenders(data.docId, /*force=*/true, /*marksDirty=*/true);
        return true;
    }

    const QString absolute = smartobject::resolveLinkPath(m_container ? m_container->path()
                                                                      : QString(),
                                                          data.linkedPath);
    if (absolute.isEmpty() || !QFileInfo(absolute).isFile()) {
        markSmartObjectLinkBroken(node, true);
        emit operationBlocked(tr("The linked file \"%1\" could not be found.")
                                  .arg(QFileInfo(data.linkedPath).fileName()));
        return false;
    }

    // Re-read from disk only when no tab already owns that file: if it is open,
    // the in-memory container IS the newer state, and reloading would throw
    // away the user's unsaved edits.
    if (!ContainerRegistry::instance().find(absolute)) {
        QString error;
        if (!ContainerRegistry::instance().open(absolute, &error)) {
            markSmartObjectLinkBroken(node, true);
            emit operationBlocked(error.isEmpty()
                ? tr("Could not read the linked file.") : error);
            return false;
        }
    }

    markSmartObjectLinkBroken(node, false);
    return renderLinkedSmartObject(node, /*force=*/true);
}

bool ImageController::convertSmartObjectToLinked(int flatIndex, const QString& path)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer || !node->layer->smartObjectData || !m_container) {
        emit operationBlocked(tr("This layer is not a Smart Object."));
        return false;
    }
    if (!node->canEditContent()) {
        emit operationBlocked(tr("This layer is locked."));
        return false;
    }
    const auto before = node->layer->smartObjectData;
    if (before->mode == SmartObjectStorageMode::Linked) {
        emit operationBlocked(tr("This Smart Object is already linked."));
        return false;
    }
    if (path.isEmpty())
        return false;

    // Write the external .hzs FIRST: if the export fails there is nothing to
    // undo, and the document stays exactly as it was.
    QString error;
    if (!smartobject::exportDocumentSubgraph(*m_container, before->docId, path, &error)) {
        emit operationBlocked(error.isEmpty()
            ? tr("Could not write \"%1\".").arg(QFileInfo(path).fileName()) : error);
        return false;
    }

    auto exported = ContainerRegistry::instance().open(path, &error);
    if (!exported || !exported->rootDocument()) {
        emit operationBlocked(error.isEmpty()
            ? tr("Could not re-open the exported file.") : error);
        return false;
    }

    // The documents this conversion orphans are NEVER deleted: the command
    // holds a shared_ptr to each, so undo re-points at them without rebuilding
    // anything. They stop being written to the file the moment the root can no
    // longer reach them (persistence is reachability), so nothing accumulates.
    std::vector<std::shared_ptr<Document>> retained;
    for (const SmartObjectDocId& id :
         smartobject::embeddedSubgraphOf(*m_container, before->docId)) {
        if (auto orphan = m_container->document(id))
            retained.push_back(std::move(orphan));
    }

    auto after = std::make_shared<SmartObjectData>(*before);
    after->mode = SmartObjectStorageMode::Linked;
    after->docId = exported->rootId();
    after->linkedPath = smartobject::relativeLinkPath(m_container->path(), path);
    after->linkBroken = false;

    const QImage beforeImage = node->layer->cpuImage;
    auto command = std::make_unique<smartobject::ConvertToLinkedCommand>(
        m_doc, flatIndex, before, beforeImage, after, beforeImage,
        std::move(retained), tr("Convert to Linked"));
    command->execute();
    m_history.push(std::move(command));

    emit layerChanged(flatIndex);
    emit imageChanged();
    emit documentChanged();
    return true;
}

bool ImageController::convertSmartObjectToEmbedded(int flatIndex)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer || !node->layer->smartObjectData || !m_container) {
        emit operationBlocked(tr("This layer is not a Smart Object."));
        return false;
    }
    if (!node->canEditContent()) {
        emit operationBlocked(tr("This layer is locked."));
        return false;
    }
    const auto before = node->layer->smartObjectData;
    if (before->mode == SmartObjectStorageMode::Embedded) {
        emit operationBlocked(tr("This Smart Object is already embedded."));
        return false;
    }

    const QString absolute = smartobject::resolveLinkPath(m_container->path(),
                                                          before->linkedPath);
    QString error;
    auto source = absolute.isEmpty() ? nullptr
                                     : ContainerRegistry::instance().open(absolute, &error);
    if (!source || !source->rootDocument()) {
        markSmartObjectLinkBroken(node, true);
        emit operationBlocked(error.isEmpty()
            ? tr("The linked file could not be read, so it cannot be embedded.") : error);
        return false;
    }

    // A private copy of the linked subgraph, with fresh docIds and its internal
    // references rewritten — embedded means "stored here", so later edits must
    // not write back to the file it came from.
    const SmartObjectDocId sourceDocId = source->contains(before->docId)
        ? before->docId : source->rootId();
    const SmartObjectDocId imported =
        smartobject::importDocumentsInto(*m_container, *source, sourceDocId);
    if (imported.isEmpty()) {
        emit operationBlocked(tr("Could not embed the linked document."));
        return false;
    }

    // Retained so a redo after an undo re-points at these very documents
    // instead of importing a second copy of them.
    std::vector<std::shared_ptr<Document>> retained;
    for (const SmartObjectDocId& id :
         smartobject::embeddedSubgraphOf(*m_container, imported)) {
        if (auto doc = m_container->document(id))
            retained.push_back(std::move(doc));
    }

    auto after = std::make_shared<SmartObjectData>(*before);
    after->mode = SmartObjectStorageMode::Embedded;
    after->docId = imported;
    after->linkedPath.clear();
    after->linkBroken = false;
    after->state.sourceGeneration = kSmartObjectNeverRendered;

    const QImage beforeImage = node->layer->cpuImage;
    auto command = std::make_unique<smartobject::ConvertToEmbeddedCommand>(
        m_doc, flatIndex, before, beforeImage, after, beforeImage,
        std::move(retained), tr("Convert to Embedded"));
    command->execute();
    m_history.push(std::move(command));

    emit layerChanged(flatIndex);
    emit imageChanged();
    emit documentChanged();
    requestSmartObjectRenders(imported, /*force=*/true, /*marksDirty=*/true);
    return true;
}

int ImageController::smartObjectInstanceCountAt(int flatIndex) const
{
    const auto data = smartObjectDataAt(flatIndex);
    if (!data || !m_container)
        return 0;

    // Counts across the WHOLE container, not just this document: the point of a
    // Smart Object is that the same content can appear in several places, and
    // the Properties page is where the user finds out how many.
    if (data->mode == SmartObjectStorageMode::Embedded)
        return static_cast<int>(m_container->instancesOf(data->docId).size());

    // Linked instances are counted by PATH: a linked payload stores the target
    // file's docId, which is not a key in this container, so two instances of
    // the same file are "the same content" exactly when their paths match.
    int count = 0;
    for (const SmartObjectDocId& hostId : m_container->documentIds()) {
        const auto host = m_container->document(hostId);
        if (!host)
            continue;
        forEachInstanceIn(host->roots,
                          [&](LayerTreeNode*,
                              const std::shared_ptr<const SmartObjectData>& other) {
            if (other->mode == SmartObjectStorageMode::Linked
                && other->linkedPath == data->linkedPath) {
                ++count;
            }
        });
    }
    return count;
}

QString ImageController::smartObjectLinkedPathAt(int flatIndex) const
{
    const auto data = smartObjectDataAt(flatIndex);
    if (!data || data->mode != SmartObjectStorageMode::Linked)
        return {};
    return smartobject::resolveLinkPath(m_container ? m_container->path() : QString(),
                                        data->linkedPath);
}

// ─────────────────────────────────────────────────────────────────────────
// New Smart Object via Copy, and Replace Contents.
//
// The pair that makes the sharing model explicit to the user: Ctrl+J gives a
// second INSTANCE of the same document, "via Copy" gives an INDEPENDENT one,
// and "Replace Contents" swaps the document every instance of that id shows.
// ─────────────────────────────────────────────────────────────────────────

int ImageController::newSmartObjectViaCopy(int flatIndex)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer || !node->layer->smartObjectData || !m_container) {
        emit operationBlocked(tr("This layer is not a Smart Object."));
        return -1;
    }
    const auto data = node->layer->smartObjectData;

    // Where to copy FROM. For a Linked instance the content lives in another
    // file, so the only way to make it genuinely independent is to bring a
    // private copy in — the copy therefore comes back Embedded. Leaving it
    // linked would share the file again, which is exactly what this command
    // exists not to do.
    std::shared_ptr<DocumentContainer> source = m_container;
    SmartObjectDocId sourceDocId = data->docId;
    if (data->mode == SmartObjectStorageMode::Linked) {
        const QString absolute = smartobject::resolveLinkPath(m_container->path(),
                                                              data->linkedPath);
        QString error;
        source = absolute.isEmpty() ? nullptr
                                    : ContainerRegistry::instance().open(absolute, &error);
        if (!source || !source->rootDocument()) {
            markSmartObjectLinkBroken(node, true);
            emit operationBlocked(error.isEmpty()
                ? tr("The linked file could not be read, so it cannot be copied.") : error);
            return -1;
        }
        sourceDocId = source->contains(data->docId) ? data->docId : source->rootId();
    }

    const SmartObjectDocId copyId =
        smartobject::importDocumentsInto(*m_container, *source, sourceDocId);
    if (copyId.isEmpty()) {
        emit operationBlocked(tr("Could not copy the Smart Object's document."));
        return -1;
    }

    // clone() carries everything that makes this layer look the way it does —
    // transform, mask, opacity, blend, layer styles — and shares the payload;
    // only the payload is then re-pointed at the fresh copy.
    auto instance = node->clone(/*preserveName=*/true);
    instance->name = node->name + tr(" copy");
    instance->layer->name = instance->name;
    auto copyData = std::make_shared<SmartObjectData>(*data);
    copyData->mode = SmartObjectStorageMode::Embedded;
    copyData->docId = copyId;
    copyData->linkedPath.clear();
    copyData->linkBroken = false;
    copyData->state.sourceGeneration = kSmartObjectNeverRendered;
    instance->layer->smartObjectData = std::move(copyData);

    LayerTreeNode* instancePtr = instance.get();
    auto command = std::make_unique<AddLayerCommand>(
        m_doc, flatIndex, std::move(instance), tr("New Smart Object via Copy"));
    command->execute();
    m_history.push(std::move(command));

    const int newIndex = flatIndexOf(m_doc, instancePtr);
    emit layerChanged(newIndex);
    emit activeLayerChanged(newIndex);
    emit selectionChanged();
    emit imageChanged();
    emit documentChanged();
    requestSmartObjectRenders(copyId, /*force=*/true, /*marksDirty=*/true);
    return newIndex;
}

bool ImageController::replaceSmartObjectContents(int flatIndex, const QString& path)
{
    auto* node = m_doc ? m_doc->nodeAt(flatIndex) : nullptr;
    if (!node || !node->layer || !node->layer->smartObjectData || !m_container) {
        emit operationBlocked(tr("This layer is not a Smart Object."));
        return false;
    }
    if (!node->canEditContent()) {
        emit operationBlocked(tr("This layer is locked."));
        return false;
    }
    if (path.isEmpty())
        return false;
    const auto data = node->layer->smartObjectData;

    // On a Linked instance, replacing the contents IS re-pointing the link —
    // there is no document of ours to swap. Routing it through relink keeps one
    // implementation instead of two that must agree.
    if (data->mode == SmartObjectStorageMode::Linked)
        return relinkSmartObject(flatIndex, path);

    const bool wasOpen = ContainerRegistry::instance().find(path) != nullptr;
    QString error;
    auto source = ContainerRegistry::instance().open(path, &error);
    if (!source || !source->rootDocument()) {
        emit operationBlocked(error.isEmpty()
            ? tr("Could not open \"%1\".").arg(QFileInfo(path).fileName()) : error);
        return false;
    }
    if (source.get() == m_container.get()) {
        emit operationBlocked(tr("A project cannot be placed inside itself."));
        return false;
    }

    // Import into a scratch container first, then move the finished root
    // document across: importing straight into m_container would leave the
    // replaced subgraph's copies behind as extra documents if anything failed.
    auto staging = std::make_shared<DocumentContainer>(std::make_shared<Document>());
    staging->setPath(m_container->path());
    const SmartObjectDocId stagedId =
        smartobject::importDocumentsInto(*staging, *source, source->rootId());
    if (!wasOpen)
        ContainerRegistry::instance().forget(path);
    auto replacement = stagedId.isEmpty() ? nullptr : staging->document(stagedId);
    if (!replacement) {
        emit operationBlocked(tr("Could not read \"%1\".").arg(QFileInfo(path).fileName()));
        return false;
    }

    // Nested content the replacement itself references comes along; it is
    // addressed by the ids the import already rewrote, so it can be adopted
    // as-is.
    for (const SmartObjectDocId& id : staging->documentIds()) {
        if (id == stagedId || id == staging->rootId())
            continue;
        if (auto nested = staging->document(id))
            m_container->addDocumentWithId(id, std::move(nested));
    }

    auto previous = m_container->document(data->docId);
    if (!previous) {
        emit operationBlocked(tr("This Smart Object's document is missing from the project."));
        return false;
    }

    auto command = std::make_unique<smartobject::ReplaceContentsCommand>(
        m_container, data->docId, previous, replacement, tr("Replace Contents"));
    command->execute();
    m_history.push(std::move(command));

    emit layerChanged(flatIndex);
    emit imageChanged();
    emit documentChanged();
    // Reaches every instance of this id, not just the layer the command ran on.
    requestSmartObjectRenders(data->docId, /*force=*/true, /*marksDirty=*/true);
    return true;
}
