#include "ColorTrack.hpp"

#include <QLinearGradient>
#include <QPainter>
#include <QPen>
#include <QRectF>

void ColorTrack::paint(QPainter& p, const QRectF& rect, const QColor& borderColor,
                       qreal radius) const
{
    p.save();
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_stops.isEmpty()) {
        p.setBrush(Qt::NoBrush);
    } else {
        QLinearGradient grad(rect.left(), 0, rect.right(), 0);
        for (const Stop& s : m_stops)
            grad.setColorAt(s.pos, s.color);
        p.setBrush(grad);
    }
    p.setPen(QPen(borderColor, 1));
    p.drawRoundedRect(rect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
    p.restore();
}

namespace ColorStops {

QList<ColorTrack::Stop> hueSpectrum()
{
    QList<ColorTrack::Stop> stops;
    for (int i = 0; i <= 6; ++i)
        stops.append({ i / 6.0, QColor::fromHsv((i * 60) % 360, 255, 255) });
    return stops;
}

QList<ColorTrack::Stop> saturation(int baseHue)
{
    return {
        { 0.0, QColor::fromHsl(baseHue, 0,   128) },
        { 1.0, QColor::fromHsl(baseHue, 255, 128) },
    };
}

QList<ColorTrack::Stop> lightness(int baseHue)
{
    return {
        { 0.0, QColor(0, 0, 0) },
        { 0.5, QColor::fromHsl(baseHue, 255, 128) },
        { 1.0, QColor(255, 255, 255) },
    };
}

} // namespace ColorStops
