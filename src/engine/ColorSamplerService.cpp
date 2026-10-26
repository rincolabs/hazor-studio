#include "ColorSamplerService.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"

#include <cmath>
#include <QImage>
#include <QPainter>
#include <algorithm>

namespace {
// Which pixel does a continuous coordinate fall in? Document, layer and
// framebuffer coordinates all put integers on pixel EDGES (screenToDocument,
// layerPixelToDocumentPixel, screenPos * devicePixelRatio), so the pixel
// containing x is floor(x) — the convention Fill Bucket already follows.
// std::round() is wrong twice over: it picks the neighbour whenever the
// fraction reaches 0.5, which is three quarters of every pixel's area (only
// the top-left quadrant rounds back to its own pixel), and it truncates
// toward zero's wrong side for negative coordinates. Zoomed in, that made the
// same visible pixel report two different colours depending on where inside it
// the cursor sat.
inline int pixelIndex(double coordinate)
{
    return static_cast<int>(std::floor(coordinate));
}
} // namespace

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

    // This averages neighbouring pixels, so it has to weight colour by alpha
    // (RENDER-PIPELINE.md 4.3). The colour stored in a transparent pixel is
    // arbitrary — 0 in a composite that starts from fill(Qt::transparent) — yet
    // a plain RGBA mean gave it full weight, so sampling across a soft or
    // antialiased edge reported a colour dragged toward black: an 11x11 window
    // half covered by a white stroke came back mid-grey. Alpha itself stays a
    // plain mean, because it is the coverage of the region, not a colour.
    double r = 0, g = 0, b = 0, a = 0;
    int count = 0;

    // RGBA8888 stores bytes in order R, G, B, A per pixel
    for (int y = y1; y <= y2; ++y) {
        const uchar* row = source.constScanLine(y);
        for (int x = x1; x <= x2; ++x) {
            const uchar* px = row + x * 4;
            const double w = px[3];
            r += px[0] * w;
            g += px[1] * w;
            b += px[2] * w;
            a += w;
            ++count;
        }
    }

    if (count == 0) return {};

    // An entirely transparent window has no colour to report; returning the
    // straight mean of arbitrary colours is what this used to do.
    const double norm = a > 0.0 ? 1.0 / a : 0.0;
    return QColor(static_cast<int>(r * norm + 0.5),
                  static_cast<int>(g * norm + 0.5),
                  static_cast<int>(b * norm + 0.5),
                  static_cast<int>(a / count + 0.5));
}

QColor ColorSamplerService::sampleLayer(const Layer* layer, QPointF imagePos,
                                         SampleSize size)
{
    if (!layer || layer->cpuImage.isNull()) return {};

    const int px = pixelIndex(imagePos.x());
    const int py = pixelIndex(imagePos.y());

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
        if (!node->visible) continue;
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
        p.setOpacity(node->opacity);
        p.drawImage(QPointF(0, 0), drawImg);
        p.restore();
    }
    p.end();

    const int dx = pixelIndex(docPos.x());
    const int dy = pixelIndex(docPos.y());

    int radius = sampleRadius(size);
    return averageRegion(composite, dx, dy, radius);
}

QColor ColorSamplerService::sampleCompositeFramebuffer(const QImage& fb,
                                                         QPointF screenPos,
                                                         SampleSize size)
{
    if (fb.isNull()) return {};

    // screenPos arrives already multiplied by devicePixelRatio, so it is a
    // continuous framebuffer-pixel coordinate and the same floor rule applies.
    const int sx = pixelIndex(screenPos.x());
    const int sy = pixelIndex(screenPos.y());

    int radius = sampleRadius(size);
    return averageRegion(fb, sx, sy, radius);
}
