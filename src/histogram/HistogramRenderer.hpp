#pragma once

#include <QColor>
#include <QRectF>
#include <array>

#include "HistogramTypes.hpp"

class QPainter;
class QPainterPath;

// Builds the QPainterPath shapes for a histogram and paints them with
// anti-aliasing and (for Colors mode) additive RGB blending.
class HistogramRenderer {
public:
    struct Style {
        QColor background;
        QColor frame;
        QColor singleFill;   // RGB / Luminosity neutral fill
        bool   smooth = true;
        qreal  topInset = 2.0;
        qreal  bottomInset = 0.0;
    };

    // Paints the histogram for `channel` into `rect` (already inside the panel's
    // plot area). Assumes the background/frame are drawn by the caller.
    static void render(QPainter& p, const QRectF& rect,
                       const HistogramData& data,
                       HistogramChannel channel,
                       const Style& style);

private:
    static std::array<float, 256> processBins(const std::array<int, 256>& bins, bool smooth);
    static float maxValue(const std::array<float, 256>& values);
    static QPainterPath buildPath(const std::array<float, 256>& values,
                                  const QRectF& rect, double maxVal);
};
