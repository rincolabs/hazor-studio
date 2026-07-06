#include "HistogramRenderer.hpp"

#include <QPainter>
#include <QPainterPath>
#include <algorithm>

float HistogramRenderer::maxValue(const std::array<float, 256>& values)
{
    float m = 0.0f;
    for (float v : values)
        m = std::max(m, v);
    return m;
}

// Returns the plotted values for a channel: raw bin counts, optionally passed
// through a light 1-2-1 smoothing (twice) for an organic, non-jagged silhouette.
std::array<float, 256> HistogramRenderer::processBins(const std::array<int, 256>& bins, bool smooth)
{
    std::array<float, 256> a{};
    for (int i = 0; i < 256; ++i)
        a[i] = static_cast<float>(bins[i]);

    if (!smooth)
        return a;

    for (int pass = 0; pass < 2; ++pass) {
        std::array<float, 256> b = a;
        for (int i = 0; i < 256; ++i) {
            const float l = b[i > 0 ? i - 1 : 0];
            const float c = b[i];
            const float r = b[i < 255 ? i + 1 : 255];
            a[i] = (l + 2.0f * c + r) * 0.25f;
        }
    }
    return a;
}

QPainterPath HistogramRenderer::buildPath(const std::array<float, 256>& values,
                                          const QRectF& rect, double maxVal)
{
    QPainterPath path;
    if (maxVal <= 0.0)
        return path;

    const double left = rect.left();
    const double bottom = rect.bottom();
    const double w = rect.width();
    const double h = rect.height();

    auto xAt = [&](int i) { return left + (w * i) / 255.0; };
    auto yAt = [&](float v) {
        double norm = std::min(1.0, v / maxVal);
        return bottom - norm * h;
    };

    path.moveTo(left, bottom);
    for (int i = 0; i < 256; ++i)
        path.lineTo(xAt(i), yAt(values[i]));
    path.lineTo(rect.right(), bottom);
    path.closeSubpath();
    return path;
}

void HistogramRenderer::render(QPainter& p, const QRectF& rectIn,
                               const HistogramData& data,
                               HistogramChannel channel,
                               const Style& style)
{
    QRectF rect = rectIn.adjusted(0, style.topInset, 0, -style.bottomInset);
    if (rect.width() <= 1.0 || rect.height() <= 1.0 || !data.valid)
        return;

    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);
    p.setPen(Qt::NoPen);

    if (channel == HistogramChannel::Colors) {
        // Process all three channels, then scale by the shared max of the
        // values actually plotted so the tallest peak reaches the top and the
        // channels stay comparable. Additive blend -> cyan/magenta/yellow/white.
        const std::array<float, 256> r = processBins(data.red, style.smooth);
        const std::array<float, 256> g = processBins(data.green, style.smooth);
        const std::array<float, 256> b = processBins(data.blue, style.smooth);
        const double maxVal = std::max({ maxValue(r), maxValue(g), maxValue(b) });

        p.setCompositionMode(QPainter::CompositionMode_Plus);
        p.fillPath(buildPath(r, rect, maxVal), QColor(220, 40, 40));
        p.fillPath(buildPath(g, rect, maxVal), QColor(40, 200, 40));
        p.fillPath(buildPath(b, rect, maxVal), QColor(40, 70, 220));
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
    } else {
        QColor fill;
        switch (channel) {
            case HistogramChannel::Red:   fill = QColor(225, 70, 70); break;
            case HistogramChannel::Green: fill = QColor(70, 200, 90); break;
            case HistogramChannel::Blue:  fill = QColor(80, 120, 230); break;
            case HistogramChannel::RGB:
            case HistogramChannel::Luminosity:
            default:                      fill = style.singleFill; break;
        }
        const std::array<float, 256> values = processBins(data.bins(channel), style.smooth);
        const double maxVal = maxValue(values);
        p.fillPath(buildPath(values, rect, maxVal), fill);
    }

    p.restore();
}
