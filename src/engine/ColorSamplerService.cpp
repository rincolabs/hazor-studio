#include "ColorSamplerService.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"

#include <cmath>
#include <QImage>
#include <QPainter>
#include <algorithm>

int ColorSamplerService::sampleRadius(SampleSize size)
{
    switch (size) {
    case SampleSize::Point:   return 0;
    case SampleSize::Size3x3: return 1;
    case SampleSize::Size5x5: return 2;
    case SampleSize::Size11x11: return 5;
    }
    return 0;
}

QColor ColorSamplerService::averageRegion(const QImage& source, int cx, int cy, int radius)
{
    if (source.isNull()) return {};

    int w = source.width();
    int h = source.height();

    if (cx < 0 || cy < 0 || cx >= w || cy >= h)
        return {};

    if (radius == 0) {
        if (cx >= w || cy >= h) return {};
        return source.pixelColor(cx, cy);
    }

    int x1 = std::max(0, cx - radius);
    int y1 = std::max(0, cy - radius);
    int x2 = std::min(w - 1, cx + radius);
    int y2 = std::min(h - 1, cy + radius);

    double r = 0, g = 0, b = 0, a = 0;
    int count = 0;

    // RGBA8888 stores bytes in order R, G, B, A per pixel
    for (int y = y1; y <= y2; ++y) {
        const uchar* row = source.constScanLine(y);
        for (int x = x1; x <= x2; ++x) {
            const uchar* px = row + x * 4;
            r += px[0];
            g += px[1];
            b += px[2];
            a += px[3];
            ++count;
        }
    }

    if (count == 0) return {};

    return QColor(static_cast<int>(r / count + 0.5),
                  static_cast<int>(g / count + 0.5),
                  static_cast<int>(b / count + 0.5),
                  static_cast<int>(a / count + 0.5));
}

QColor ColorSamplerService::sampleLayer(const Layer* layer, QPointF imagePos,
                                         SampleSize size)
{
    if (!layer || layer->cpuImage.isNull()) return {};

    int px = static_cast<int>(std::round(imagePos.x()));
    int py = static_cast<int>(std::round(imagePos.y()));

    int radius = sampleRadius(size);
    return averageRegion(layer->cpuImage, px, py, radius);
}

QColor ColorSamplerService::sampleComposite(const Document* doc, QPointF docPos,
                                              SampleSize size)
{
    if (!doc || doc->size.isEmpty()) return {};

    // Build a flattened composite of all visible layers
    auto flat = doc->flatten();
    QImage composite(doc->size, QImage::Format_RGBA8888);
    composite.fill(Qt::transparent);

    QPainter p(&composite);
    p.setCompositionMode(QPainter::CompositionMode_SourceOver);

    // Compositing: flat[0] is the visual top, flat[size-1] is the visual bottom
    // Draw bottom-first (size-1 → 0) so top layers overlay bottom layers
    for (int i = static_cast<int>(flat.size()) - 1; i >= 0; --i) {
        auto* node = flat[i];
        if (!node->isVisible()) continue;
        if (node->type != LayerTreeNode::Type::Layer) continue;
        if (!node->layer || node->layer->cpuImage.isNull()) continue;

        QImage drawImg = node->layer->cpuImage;

        // Apply mask if present
        if (!node->layer->maskImage.isNull() && node->layer->maskVisible) {
            QImage masked(drawImg.size(), QImage::Format_RGBA8888);
            masked.fill(Qt::transparent);
            int mw = drawImg.width();
            int mh = drawImg.height();
            for (int my = 0; my < mh; ++my) {
                const QRgb* srcRow = reinterpret_cast<const QRgb*>(drawImg.constScanLine(my));
                QRgb* dstRow = reinterpret_cast<QRgb*>(masked.scanLine(my));
                for (int mx = 0; mx < mw; ++mx) {
                    const int maskX = mx - node->layer->maskOrigin.x();
                    const int maskY = my - node->layer->maskOrigin.y();
                    float maskVal = 1.0f;
                    if (maskX >= 0 && maskY >= 0
                        && maskX < node->layer->maskImage.width()
                        && maskY < node->layer->maskImage.height()) {
                        maskVal = node->layer->maskImage.constScanLine(maskY)[maskX] / 255.0f;
                    }
                    maskVal = 1.0f - (1.0f - maskVal) * (1.0f - node->layer->maskDensity);
                    QRgb src = srcRow[mx];
                    int sa = static_cast<int>(qAlpha(src) * maskVal);
                    dstRow[mx] = qRgba(qRed(src), qGreen(src), qBlue(src), sa);
                }
            }
            drawImg = masked;
        }

        p.save();
        QTransform xf = node->accumulatedTransform();

        // imgToNdc * accum * ndcToPixel
        float imgToNdcScaleW = 2.0f / drawImg.width();
        float imgToNdcScaleH = 2.0f / drawImg.height();
        QTransform imgToNdc;
        imgToNdc.scale(imgToNdcScaleW, -imgToNdcScaleH);
        imgToNdc.translate(-drawImg.width() * 0.5f, -drawImg.height() * 0.5f);

        float ndcToPixelScaleW = doc->size.width() * 0.5f;
        float ndcToPixelScaleH = doc->size.height() * 0.5f;
        QTransform ndcToPixel;
        ndcToPixel.translate(1.0f, 1.0f);
        ndcToPixel.scale(ndcToPixelScaleW, -ndcToPixelScaleH);

        QTransform full = imgToNdc * xf * ndcToPixel;
        p.setTransform(full);
        p.setOpacity(node->opacity());
        p.drawImage(QPointF(0, 0), drawImg);
        p.restore();
    }
    p.end();

    int dx = static_cast<int>(std::round(docPos.x()));
    int dy = static_cast<int>(std::round(docPos.y()));

    int radius = sampleRadius(size);
    return averageRegion(composite, dx, dy, radius);
}

QColor ColorSamplerService::sampleCompositeFramebuffer(const QImage& fb,
                                                         QPointF screenPos,
                                                         SampleSize size)
{
    if (fb.isNull()) return {};

    int sx = static_cast<int>(std::round(screenPos.x()));
    int sy = static_cast<int>(std::round(screenPos.y()));

    int radius = sampleRadius(size);
    return averageRegion(fb, sx, sy, radius);
}
