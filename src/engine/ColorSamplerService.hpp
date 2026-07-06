#pragma once

#include <QColor>
#include <QPointF>
#include <QImage>

class Document;
class Layer;

enum class SampleMode { CurrentLayer = 0, Composite = 1 };
enum class SampleSize  { Point = 0, Size3x3 = 1, Size5x5 = 2, Size11x11 = 3 };

class ColorSamplerService {
public:
    ColorSamplerService() = delete;

    static QColor sampleLayer(const Layer* layer, QPointF imagePos,
                               SampleSize size = SampleSize::Point);
    static QColor sampleComposite(const Document* doc, QPointF docPos,
                                   SampleSize size = SampleSize::Point);
    static QColor sampleCompositeFramebuffer(const QImage& fb, QPointF screenPos,
                                              SampleSize size = SampleSize::Point);

private:
    static int sampleRadius(SampleSize size);
    static QColor averageRegion(const QImage& source, int cx, int cy, int radius);
};
