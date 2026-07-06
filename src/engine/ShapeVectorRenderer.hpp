#pragma once

#include "core/ShapeTypes.hpp"
#include <QImage>
#include <QPainterPath>
#include <QTransform>
#include <QRectF>
#include <QSize>

struct ShapeSprite {
    QImage image;
    QTransform spriteTransform;
};

class ShapeVectorRenderer {
public:
    static QPainterPath buildPath(const VectorPath& path);
    static QPainterPath buildPath(const ShapeData& data);

    static QRectF transformedBounds(const ShapeData& data,
                                    const QTransform& accumulatedTransform);

    static ShapeSprite renderSprite(const ShapeData& data,
                                    const QTransform& accumulatedTransform,
                                    QSize documentSize,
                                    float zoom,
                                    const QPointF& canvasHalfExtents,
                                    int viewportW,
                                    int viewportH);

    static bool sameShapeData(const ShapeData& a, const ShapeData& b);
};
