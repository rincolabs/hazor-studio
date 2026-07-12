#include "ShapeLayerUpdater.hpp"

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "engine/ShapeRenderer.hpp"

#include <QDebug>
#include <algorithm>

QTransform ShapeLayerUpdater::rasterTransformForShape(const ShapeData& data,
                                                      const QImage& rendered,
                                                      const QSize& documentSize)
{
    const float halfW = static_cast<float>(rendered.width())
        / static_cast<float>(std::max(1, documentSize.width()));
    const float halfH = static_cast<float>(rendered.height())
        / static_cast<float>(std::max(1, documentSize.height()));

    const QRectF bounds = ShapeRenderer::rasterBounds(data);

    QTransform t;
    t.setMatrix(
        halfW, 0.0, 0.0,
        0.0, halfH, 0.0,
        bounds.center().x(),
        bounds.center().y(),
        1.0);

    return t;
}

bool ShapeLayerUpdater::rebuildShapeRaster(Document& document, LayerTreeNode& node)
{
    if (!node.layer || !node.layer->shapeData)
        return false;

    QImage rendered = ShapeRenderer::render(*node.layer->shapeData, document.size);
    if (rendered.isNull())
        return false;

    rendered = rendered.convertToFormat(QImage::Format_RGBA8888);
    node.layer->cpuImage = rendered;
    node.setBaseTransform(rasterTransformForShape(*node.layer->shapeData, rendered, document.size));
    node.layer->textureOutdated = true;
    node.layer->shapeCache.dirty = true;
    node.invalidateEffects();
    node.thumbnailDirty = true;
    ++document.compositionGeneration;
    return true;
}
