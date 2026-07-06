#pragma once

#include "core/ShapeTypes.hpp"

class Document;
class LayerTreeNode;

class ShapeLayerUpdater {
public:
    static QTransform rasterTransformForShape(const ShapeData& data,
                                              const QImage& rendered,
                                              const QSize& documentSize);
    static bool rebuildShapeRaster(Document& document, LayerTreeNode& node);
};
