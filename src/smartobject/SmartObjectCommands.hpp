#pragma once

#include "controller/CommandHistory.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "smartobject/SmartObjectData.hpp"
#include "smartobject/DocumentContainer.hpp"

#include <QImage>
#include <QString>
#include <memory>
#include <utility>
#include <vector>

// Undo for the three Smart Object operations that change an instance's
// REFERENCE (as opposed to the cache behind it, which is never a history step
// — see docs/SMART-OBJECTS.md).
//
// All three share one mechanic: swap the immutable payload, and restore the
// pixels that payload described. They are separate classes because what each
// one does AROUND that swap is genuinely different (see the table in
// tmp/feat-smartobjects.md D9/Phase 7) and because the history entry should say
// which one the user ran.
namespace smartobject {

// Applies one (payload, pixels) pair to the instance at `flatIndex`.
inline void applyInstanceState(Document* doc, int flatIndex,
                               const std::shared_ptr<const SmartObjectData>& data,
                               const QImage& image)
{
    auto* node = doc ? doc->nodeAt(flatIndex) : nullptr;
    if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
        return;
    Layer* layer = node->layer.get();
    if (!image.isNull()) {
        if (layer->rasterStorage.isEnabled())
            layer->rasterStorage.clear();
        layer->cpuImage = image;
        layer->tiledSystem = false;
        layer->tileManager.clear();
        layer->dirtyRegion.clear();
        layer->invalidateContentBounds();
    }
    layer->smartObjectData = data;
    layer->textureOutdated = true;
    layer->pendingGpuUpload = true;
    node->invalidateEffects();
    node->thumbnailDirty = true;
    if (doc) ++doc->compositionGeneration;
}

// Base for the three: before/after payload + the render each side had.
class InstancePayloadCommand : public Command {
public:
    InstancePayloadCommand(Document* doc, int flatIndex,
                           std::shared_ptr<const SmartObjectData> before, QImage beforeImage,
                           std::shared_ptr<const SmartObjectData> after, QImage afterImage,
                           QString commandName)
        : m_doc(doc)
        , m_flatIndex(flatIndex)
        , m_before(std::move(before))
        , m_beforeImage(std::move(beforeImage))
        , m_after(std::move(after))
        , m_afterImage(std::move(afterImage))
        , m_name(std::move(commandName))
    {}

    void execute() override { applyInstanceState(m_doc, m_flatIndex, m_after, m_afterImage); }
    void undo() override { applyInstanceState(m_doc, m_flatIndex, m_before, m_beforeImage); }
    QString name() const override { return m_name; }

protected:
    Document* m_doc;
    int m_flatIndex;
    std::shared_ptr<const SmartObjectData> m_before;
    QImage m_beforeImage;
    std::shared_ptr<const SmartObjectData> m_after;
    QImage m_afterImage;
    QString m_name;
};

// Re-points a Linked instance at a different file. The payload swap really is
// the whole operation here: no document moves between containers.
class RelinkCommand : public InstancePayloadCommand {
public:
    using InstancePayloadCommand::InstancePayloadCommand;
};

// Embedded → Linked. The external .hzs was already written and registered
// before this command was pushed; the swap is the undoable part.
//
// `retained` holds the subgraph the conversion orphaned. Keeping a
// shared_ptr<Document> to it is what makes undo free: the documents are still
// in the container (nothing is ever deleted on conversion), so an undo only has
// to restore the payload — and the refcount guarantees they are still there to
// restore to. An orphan held only by history survives in MEMORY and is simply
// not written to the file (persistence is reachability from the root), so
// saving, closing and reopening loses nothing that still mattered.
class ConvertToLinkedCommand : public InstancePayloadCommand {
public:
    ConvertToLinkedCommand(Document* doc, int flatIndex,
                           std::shared_ptr<const SmartObjectData> before, QImage beforeImage,
                           std::shared_ptr<const SmartObjectData> after, QImage afterImage,
                           std::vector<std::shared_ptr<Document>> retained,
                           QString commandName)
        : InstancePayloadCommand(doc, flatIndex, std::move(before), std::move(beforeImage),
                                 std::move(after), std::move(afterImage),
                                 std::move(commandName))
        , m_retained(std::move(retained))
    {}

private:
    std::vector<std::shared_ptr<Document>> m_retained;
};

// Linked → Embedded. The subgraph was already imported into this container
// before the command was pushed; `retained` keeps the imported copies alive so
// a redo after an undo re-points at the same documents instead of importing a
// second set.
class ConvertToEmbeddedCommand : public InstancePayloadCommand {
public:
    ConvertToEmbeddedCommand(Document* doc, int flatIndex,
                             std::shared_ptr<const SmartObjectData> before, QImage beforeImage,
                             std::shared_ptr<const SmartObjectData> after, QImage afterImage,
                             std::vector<std::shared_ptr<Document>> retained,
                             QString commandName)
        : InstancePayloadCommand(doc, flatIndex, std::move(before), std::move(beforeImage),
                                 std::move(after), std::move(afterImage),
                                 std::move(commandName))
        , m_retained(std::move(retained))
    {}

private:
    std::vector<std::shared_ptr<Document>> m_retained;
};

// Replace Contents: swaps the DOCUMENT stored under a docId, keeping the id.
// Every instance of that id therefore shows the new content at once — which is
// the whole point of the command, and the reason this is not just another
// payload swap.
//
// Nothing is destroyed: the command holds the displaced document, so undo puts
// the original object back rather than reconstructing it.
class ReplaceContentsCommand : public Command {
public:
    ReplaceContentsCommand(std::shared_ptr<DocumentContainer> container,
                           SmartObjectDocId docId,
                           std::shared_ptr<Document> before,
                           std::shared_ptr<Document> after,
                           QString commandName)
        : m_container(std::move(container))
        , m_docId(std::move(docId))
        , m_before(std::move(before))
        , m_after(std::move(after))
        , m_name(std::move(commandName))
    {}

    void execute() override { install(m_after); }
    void undo() override { install(m_before); }
    QString name() const override { return m_name; }

private:
    void install(const std::shared_ptr<Document>& doc)
    {
        if (!m_container || !doc)
            return;
        m_container->replaceDocument(m_docId, doc);
        // Bumped on every swap so the incoming document's generation can never
        // coincide with the one the instances last rendered. Without it, undoing
        // back to a document whose generation happens to match would leave every
        // instance showing the other document's pixels, with nothing marking
        // them stale.
        ++doc->compositionGeneration;
    }

    std::shared_ptr<DocumentContainer> m_container;
    SmartObjectDocId m_docId;
    std::shared_ptr<Document> m_before;
    std::shared_ptr<Document> m_after;
    QString m_name;
};

} // namespace smartobject
