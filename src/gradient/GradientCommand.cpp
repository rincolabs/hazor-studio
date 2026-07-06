#include "GradientCommand.hpp"

#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"

#include <utility>

ApplyGradientCommand::ApplyGradientCommand(Document* doc,
                                           int flatIndex,
                                           QImage before,
                                           QImage after,
                                           GradientApplication application,
                                           QString name)
    : m_doc(doc)
    , m_flatIndex(flatIndex)
    , m_before(std::move(before))
    , m_after(std::move(after))
    , m_application(std::move(application))
    , m_name(std::move(name))
{
}

void ApplyGradientCommand::execute()
{
    apply(m_after);
}

void ApplyGradientCommand::undo()
{
    apply(m_before);
}

void ApplyGradientCommand::apply(const QImage& image)
{
    auto* node = m_doc ? m_doc->nodeAt(m_flatIndex) : nullptr;
    if (!node || node->type != LayerTreeNode::Type::Layer || !node->layer)
        return;

    auto* layer = node->layer.get();
    layer->cpuImage = image.convertToFormat(QImage::Format_RGBA8888);
    // Representation-preserving (mirrors FilterCommand::apply): the before/after
    // snapshots are full flat images, so when the layer currently uses
    // rasterStorage (dab layer) rebuild its tiles from the applied image instead
    // of flattening — keeps the content-bounds transform outline working across
    // undo/redo.
    if (layer->rasterStorage.isEnabled()) {
        layer->rasterStorage.replaceWithImage(
            layer->cpuImage, QPoint(0, 0),
            layer->rasterStorage.tileSize() > 0 ? layer->rasterStorage.tileSize()
                                                : 256);
    } else {
        layer->rasterStorage.clear();
    }
    layer->textureOutdated = true;
    layer->pendingGpuUpload = true;
    layer->dirtyRegion.clear();
    if (layer->tiledSystem)
        layer->tileManager.markAllDirty();
    node->thumbnailDirty = true;
    node->invalidateEffects();
    if (m_doc)
        ++m_doc->compositionGeneration;
}
