#pragma once

#include "GradientTypes.hpp"

#include <QImage>
#include <QSize>

struct GradientRenderRequest {
    GradientDefinition definition;
    QSize targetSize;
    QPointF startPoint;
    QPointF endPoint;
    double opacity = 1.0;
    BlendMode blendMode = BlendMode::Normal;
    QImage baseImage;
    QImage selectionMask;
    bool lockAlpha = false;
};

class GradientRenderer {
public:
    static QColor sampleGradientAt(const GradientDefinition& definition, double position);
    static QImage renderGradientToImage(const GradientRenderRequest& request);
    static QImage compositeGradient(const GradientRenderRequest& request);
    static QImage generateThumbnail(const GradientDefinition& definition, const QSize& size);

private:
    static double positionForPoint(const GradientDefinition& definition,
                                   QPointF point,
                                   QPointF start,
                                   QPointF end);
};

