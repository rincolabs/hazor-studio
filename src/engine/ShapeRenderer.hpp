#pragma once

#include <QImage>
#include <QPainterPath>
#include "core/ShapeTypes.hpp"
#include "ShapeVectorRenderer.hpp"

class ShapeRenderer {
public:
    static QImage render(const ShapeData& data, QSize canvasSize);
    static QRectF rasterBounds(const ShapeData& data);
    static QPainterPath pathForShape(const ShapeData& data) {
        return ShapeVectorRenderer::buildPath(data);
    }
    static QRectF shapeBounds(const ShapeData& data) {
        return pathForShape(data).boundingRect();
    }
};
