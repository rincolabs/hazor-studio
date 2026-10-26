#pragma once

#include <QColor>
#include <QList>

class QPainter;
class QRectF;

// Pure rendering helper for a horizontal colour-composition bar: a sequence of
// {position, colour} stops blended as a linear gradient. Knows nothing about
// sliders, values or Adjustment Layers — Slider widgets (GradientSlider::Track)
// embed one to paint their track; the caller builds whatever stop list encodes
// the parameter it controls (hue spectrum, saturation, lightness, an RGB
// channel, two custom endpoint colours, ...).
class ColorTrack {
public:
    struct Stop { qreal pos; QColor color; };

    void setStops(const QList<Stop>& stops) { m_stops = stops; }
    const QList<Stop>& stops() const { return m_stops; }

    // Paints the bar into `rect`: a rounded rect with a 1px border in
    // `borderColor`, filled with the stop gradient, or left unfilled (border
    // only) when no stops are set — a conventional slider without a colour
    // composition still reads as a bar.
    void paint(QPainter& p, const QRectF& rect, const QColor& borderColor,
               qreal radius = 3.0) const;

private:
    QList<Stop> m_stops;
};

// Builders for colour compositions shared by more than one control, so the
// same spectrum is never hand-rolled twice. Compositions specific to a single
// adjustment (e.g. Color Balance's per-axis endpoint colours) are just a
// two-or-three-stop list built inline at the call site — not a pattern worth
// naming here.
namespace ColorStops {

// Full hue spectrum, red → ... → red (0..360°) at full saturation/value.
QList<ColorTrack::Stop> hueSpectrum();

// Grey → fully saturated `baseHue`, for a Saturation control.
QList<ColorTrack::Stop> saturation(int baseHue);

// Black → `baseHue` at mid lightness → white, for a Lightness control.
QList<ColorTrack::Stop> lightness(int baseHue);

} // namespace ColorStops
