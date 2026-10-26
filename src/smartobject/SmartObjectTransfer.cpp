#include "smartobject/SmartObjectTransfer.hpp"

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "io/ProjectFileService.hpp"
#include "smartobject/DocumentContainer.hpp"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>

#include <deque>

namespace smartobject {
namespace {

template <typename Fn>
void forEachInstance(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes, Fn&& fn)
{
    for (const auto& node : nodes) {
        if (!node) continue;
        if (node->type == LayerTreeNode::Type::Layer && node->layer
            && node->layer->smartObjectData) {
            fn(node.get());
        }
        forEachInstance(node->children, fn);
    }
}

} // namespace

std::vector<SmartObjectDocId> embeddedSubgraphOf(const DocumentContainer& src,
                                                 const SmartObjectDocId& docId)
{
    std::vector<SmartObjectDocId> out;
    if (!src.contains(docId))
        return out;

    QSet<SmartObjectDocId> seen;
    seen.insert(docId);
    std::deque<SmartObjectDocId> queue{docId};
    out.push_back(docId);
    while (!queue.empty()) {
        const SmartObjectDocId current = queue.front();
        queue.pop_front();
        const auto doc = src.document(current);
        if (!doc) continue;
        forEachInstance(doc->roots, [&](LayerTreeNode* node) {
            const auto& payload = *node->layer->smartObjectData;
            if (payload.mode != SmartObjectStorageMode::Embedded)
                return;
            if (payload.docId.isEmpty() || seen.contains(payload.docId)
                || !src.contains(payload.docId)) {
                return;
            }
            seen.insert(payload.docId);
            out.push_back(payload.docId);
            queue.push_back(payload.docId);
        });
    }
    return out;
}

std::shared_ptr<Document> cloneDocument(const Document& source)
{
    auto copy = std::make_shared<Document>();
    assignDocumentContent(*copy, source);
    return copy;
}

void assignDocumentContent(Document& dst, const Document& src)
{
    dst.name = src.name;
    dst.size = src.size;
    dst.resolutionDpi = src.resolutionDpi;
    dst.colorMode = src.colorMode;
    dst.bitDepth = src.bitDepth;
    dst.zoom = src.zoom;
    dst.panOffset = src.panOffset;
    dst.setColorProfile(src.colorProfile());
    dst.setProfileSource(src.profileSource());
    dst.guideManager.setGuides(src.guideManager.guides());
    // clone() (not shallowClone()) because the copy may end up in another
    // container — another file, even — and must not share pixel buffers whose
    // lifetime is tied to the source.
    dst.roots.clear();
    for (const auto& root : src.roots) {
        if (!root) continue;
        dst.roots.push_back(root->clone(/*preserveName=*/true));
    }
    dst.activeFlatIndex = std::min(src.activeFlatIndex,
                                   std::max(0, dst.flatCount() - 1));
    if (!dst.size.isEmpty())
        dst.selection.resize(dst.size.width(), dst.size.height());
}

namespace {

// Rewrites every embedded reference in `doc` through `remap`, and rebases every
// linked path from `fromContainerPath` onto `toContainerPath`.
void rewriteReferences(Document& doc,
                       const QHash<SmartObjectDocId, SmartObjectDocId>& remap,
                       const QString& fromContainerPath,
                       const QString& toContainerPath)
{
    forEachInstance(doc.roots, [&](LayerTreeNode* node) {
        // Local copy of the pointer: the assignment at the end of this lambda
        // releases the node's reference, and a bare `*node->layer->
        // smartObjectData` reference would dangle if the reads were ever
        // reordered below it.
        const std::shared_ptr<const SmartObjectData> held = node->layer->smartObjectData;
        const SmartObjectData& payload = *held;
        auto next = std::make_shared<SmartObjectData>(payload);
        if (payload.mode == SmartObjectStorageMode::Embedded) {
            const auto it = remap.constFind(payload.docId);
            if (it == remap.constEnd()) {
                // The reference pointed outside the imported subgraph, which
                // can only happen for a malformed source. Mark it broken rather
                // than leaving an id that resolves to nothing in the new
                // container: the cached render still shows, and the UI says why.
                next->linkBroken = true;
            } else {
                next->docId = *it;
            }
        } else if (!payload.linkedPath.isEmpty()) {
            const QString absolute = resolveLinkPath(fromContainerPath, payload.linkedPath);
            next->linkedPath = relativeLinkPath(toContainerPath, absolute);
        }
        node->layer->smartObjectData = std::move(next);
    });
}

} // namespace

QString relativeLinkPath(const QString& containerPath, const QString& targetPath)
{
    if (targetPath.isEmpty())
        return {};
    const QString absoluteTarget = QFileInfo(targetPath).absoluteFilePath();
    if (containerPath.isEmpty())
        return absoluteTarget;
    const QDir base(QFileInfo(containerPath).absolutePath());
    const QString relative = base.relativeFilePath(absoluteTarget);
    // relativeFilePath() happily returns an absolute-looking path across roots;
    // in that case keep the absolute form, which is at least resolvable.
    if (relative.isEmpty() || QFileInfo(relative).isAbsolute())
        return absoluteTarget;
    return relative;
}

QString resolveLinkPath(const QString& containerPath, const QString& storedPath)
{
    if (storedPath.isEmpty())
        return {};
    if (QFileInfo(storedPath).isAbsolute())
        return QFileInfo(storedPath).absoluteFilePath();
    if (containerPath.isEmpty())
        return QFileInfo(storedPath).absoluteFilePath();
    const QDir base(QFileInfo(containerPath).absolutePath());
    return QFileInfo(base.absoluteFilePath(storedPath)).absoluteFilePath();
}

void rebaseLinkedPaths(DocumentContainer& container, const QString& newContainerPath)
{
    const QString from = container.path();
    const QString to = QFileInfo(newContainerPath).absoluteFilePath();
    if (to.isEmpty() || from == to)
        return;

    for (const SmartObjectDocId& id : container.documentIds()) {
        const auto doc = container.document(id);
        if (!doc)
            continue;
        forEachInstance(doc->roots, [&](LayerTreeNode* node) {
            const std::shared_ptr<const SmartObjectData> held =
                node->layer->smartObjectData;   // see rewriteReferences()
            const SmartObjectData& payload = *held;
            if (payload.mode != SmartObjectStorageMode::Linked
                || payload.linkedPath.isEmpty()) {
                return;
            }
            // Resolve against the OLD location first — that is the only context
            // in which the current stored path means anything.
            const QString absolute = resolveLinkPath(from, payload.linkedPath);
            auto next = std::make_shared<SmartObjectData>(payload);
            next->linkedPath = relativeLinkPath(to, absolute);
            node->layer->smartObjectData = std::move(next);
        });
    }
}

SmartObjectDocId importDocumentsInto(DocumentContainer& dst,
                                     const DocumentContainer& src,
                                     const SmartObjectDocId& docId)
{
    const std::vector<SmartObjectDocId> subgraph = embeddedSubgraphOf(src, docId);
    if (subgraph.empty())
        return {};

    // Copy first, remap second. Doing it in one pass would need the new id of a
    // document that has not been created yet whenever two documents reference
    // each other's siblings.
    QHash<SmartObjectDocId, SmartObjectDocId> remap;
    std::vector<std::shared_ptr<Document>> copies;
    copies.reserve(subgraph.size());
    for (const SmartObjectDocId& id : subgraph) {
        const auto source = src.document(id);
        if (!source)
            continue;
        auto copy = cloneDocument(*source);
        const SmartObjectDocId newId = dst.addDocument(copy);
        if (newId.isEmpty())
            continue;
        remap.insert(id, newId);
        copies.push_back(std::move(copy));
    }

    for (const auto& copy : copies)
        rewriteReferences(*copy, remap, src.path(), dst.path());

    return remap.value(docId);
}

bool exportDocumentSubgraph(const DocumentContainer& src,
                            const SmartObjectDocId& docId,
                            const QString& path,
                            QString* error)
{
    if (!src.contains(docId)) {
        if (error) *error = QStringLiteral("The Smart Object's document is missing.");
        return false;
    }
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid path.");
        return false;
    }

    // Build a throwaway container whose ROOT is the exported document, then let
    // the ordinary container writer do the rest — including pruning anything
    // the new root cannot reach.
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    auto rootCopy = cloneDocument(*src.document(docId));
    DocumentContainer exported(rootCopy);
    exported.setPath(absolutePath);

    QHash<SmartObjectDocId, SmartObjectDocId> remap;
    remap.insert(docId, exported.rootId());
    std::vector<std::shared_ptr<Document>> copies{rootCopy};

    for (const SmartObjectDocId& id : embeddedSubgraphOf(src, docId)) {
        if (id == docId)
            continue;
        const auto source = src.document(id);
        if (!source)
            continue;
        auto copy = cloneDocument(*source);
        const SmartObjectDocId newId = exported.addDocument(copy);
        if (newId.isEmpty())
            continue;
        remap.insert(id, newId);
        copies.push_back(std::move(copy));
    }

    for (const auto& copy : copies)
        rewriteReferences(*copy, remap, src.path(), absolutePath);

    return ProjectFileService::saveContainer(exported, absolutePath, error);
}

} // namespace smartobject
