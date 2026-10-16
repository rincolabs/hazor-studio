#include "raw/RawGeometry.hpp"

#include <QSize>

namespace {

// Sensor-frame size a geometry's post-rotation fullWidth/fullHeight implies
// (coarse rotation of 90/270 swaps the axes — see transformRect() in
// rtengine/rawimagesource.cc, which this mirrors). Used only to sanity-check
// that `before`/`after` describe the same underlying file.
QSize sensorSize(const RawGeometry& g)
{
    if (g.coarseRotationDegrees == 90 || g.coarseRotationDegrees == 270)
        return QSize(g.fullHeight, g.fullWidth);
    return QSize(g.fullWidth, g.fullHeight);
}

// Sensor-space (fixed, un-rotated) -> `g`'s post-rotation, pre-crop full
// frame (fullWidth x fullHeight), rotate first then flip — the exact order
// rtengine::RawImageSource::transformRect() undoes to go the other way.
QTransform coarseTransform(const RawGeometry& g, const QSize& sensor)
{
    const int w = sensor.width();
    const int h = sensor.height();

    QTransform rotate;
    switch (g.coarseRotationDegrees) {
    case 90:  rotate = QTransform(0, 1, -1, 0, h, 0); break;
    case 180: rotate = QTransform(-1, 0, 0, -1, w, h); break;
    case 270: rotate = QTransform(0, -1, 1, 0, 0, w); break;
    default:  rotate = QTransform(); break;
    }

    // Post-rotation frame size (what hflip/vflip mirror within).
    const bool swapped = (g.coarseRotationDegrees == 90 || g.coarseRotationDegrees == 270);
    const int outW = swapped ? h : w;
    const int outH = swapped ? w : h;

    QTransform flip;
    if (g.mirrorHorizontal) flip = flip * QTransform(-1, 0, 0, 1, outW, 0);
    if (g.mirrorVertical)   flip = flip * QTransform(1, 0, 0, -1, 0, outH);

    // Row-vector convention (QTransform::map applies the left operand
    // first): rotate, then flip.
    return rotate * flip;
}

// Full sensor-space -> final rendered-pixel-space forward transform for one
// geometry snapshot: coarse rotate+flip, then crop, then resize.
QTransform forwardTransform(const RawGeometry& g, const QSize& sensor)
{
    QTransform t = coarseTransform(g, sensor);
    if (g.cropEnabled)
        t = t * QTransform(1, 0, 0, 1, -g.cropX, -g.cropY);
    if (g.resizeScale != 1.0)
        t = t * QTransform::fromScale(g.resizeScale, g.resizeScale);
    return t;
}

} // namespace

std::optional<QTransform> reframeTransform(const RawGeometry& before, const RawGeometry& after)
{
    if (!before.reframeExact || !after.reframeExact)
        return std::nullopt;

    const QSize sensor = sensorSize(before);
    if (sensor != sensorSize(after) || sensor.isEmpty())
        return std::nullopt;

    const QTransform fBefore = forwardTransform(before, sensor);
    const QTransform fAfter = forwardTransform(after, sensor);

    bool invertible = false;
    const QTransform fBeforeInv = fBefore.inverted(&invertible);
    if (!invertible)
        return std::nullopt;

    return fBeforeInv * fAfter;
}
