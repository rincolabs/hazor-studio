#include "ShapePresetFactory.hpp"

#include <QtMath>
#include <QDebug>
#include <algorithm>
#include <cmath>

namespace {

PathCommand moveTo(const QPointF& p)
{
    PathCommand cmd;
    cmd.type = PathCommandType::MoveTo;
    cmd.p1 = p;
    return cmd;
}

PathCommand lineTo(const QPointF& p)
{
    PathCommand cmd;
    cmd.type = PathCommandType::LineTo;
    cmd.p1 = p;
    return cmd;
}

PathCommand cubicTo(const QPointF& c1, const QPointF& c2, const QPointF& end)
{
    PathCommand cmd;
    cmd.type = PathCommandType::CubicTo;
    cmd.p1 = c1;
    cmd.p2 = c2;
    cmd.p3 = end;
    return cmd;
}

PathCommand closePath()
{
    PathCommand cmd;
    cmd.type = PathCommandType::ClosePath;
    return cmd;
}

// Cubic-Bezier control magnitude that approximates a circular quarter arc.
constexpr double kCornerKappa = 0.5522847498307936;

// Appends a cubic-Bezier quarter-arc rounding the corner at `corner`, going from
// `start` (on one edge) to `end` (on the adjacent edge). Using a cubic with the
// kappa factor keeps the arc visually circular (a single quadTo would visibly
// bulge), matching createEllipse() and preserving smooth vector corners.
PathCommand cornerArc(const QPointF& start, const QPointF& corner, const QPointF& end)
{
    const QPointF c1 = start + (corner - start) * kCornerKappa;
    const QPointF c2 = end + (corner - end) * kCornerKappa;
    return cubicTo(c1, c2, end);
}

// Length of the local x/y basis vectors after `t` — i.e. the per-axis scale the
// transform applies (rotation-invariant). Mirrors GPUViewport's sprite-scale
// computation so preview and commit agree on "how much" a shape is scaled.
double axisScaleX(const QTransform& t)
{
    return std::hypot(t.m11(), t.m12());
}

double axisScaleY(const QTransform& t)
{
    return std::hypot(t.m21(), t.m22());
}

} // namespace

VectorPath ShapePresetFactory::createRectangle(const QRectF& localBounds)
{
    VectorPath path;
    path.localBounds = localBounds;
    path.closed = true;
    path.commands = {
        moveTo(localBounds.topLeft()),
        lineTo(localBounds.topRight()),
        lineTo(localBounds.bottomRight()),
        lineTo(localBounds.bottomLeft()),
        closePath()
    };
    return path;
}

VectorPath ShapePresetFactory::createRoundedRectangle(const QRectF& localBounds, double radius)
{
    return createRoundedRectangle(localBounds, radius, radius);
}

VectorPath ShapePresetFactory::createRoundedRectangle(const QRectF& localBounds,
                                                      double radiusX, double radiusY)
{
    VectorPath path;
    path.localBounds = localBounds;
    path.closed = true;

    const double rx = std::clamp(radiusX, 0.0, std::abs(localBounds.width()) * 0.5);
    const double ry = std::clamp(radiusY, 0.0, std::abs(localBounds.height()) * 0.5);
    if (rx <= 0.0 || ry <= 0.0)
        return createRectangle(localBounds);

    const double l = localBounds.left();
    const double t = localBounds.top();
    const double rgt = localBounds.right();
    const double b = localBounds.bottom();
    path.commands = {
        moveTo(QPointF(l + rx, t)),
        lineTo(QPointF(rgt - rx, t)),
        cornerArc(QPointF(rgt - rx, t), QPointF(rgt, t), QPointF(rgt, t + ry)),
        lineTo(QPointF(rgt, b - ry)),
        cornerArc(QPointF(rgt, b - ry), QPointF(rgt, b), QPointF(rgt - rx, b)),
        lineTo(QPointF(l + rx, b)),
        cornerArc(QPointF(l + rx, b), QPointF(l, b), QPointF(l, b - ry)),
        lineTo(QPointF(l, t + ry)),
        cornerArc(QPointF(l, t + ry), QPointF(l, t), QPointF(l + rx, t)),
        closePath()
    };
    return path;
}

VectorPath ShapePresetFactory::createRoundedRectangleForTransform(
    const QRectF& localBounds, double canvasRadius,
    const QTransform& localToCanvas, const QPointF& canvasToPixelScale,
    double* outAppliedCanvasRadius)
{
    const double kx = canvasToPixelScale.x();   // docW / 2
    const double ky = canvasToPixelScale.y();   // docH / 2

    // Compose local->canvas with canvas->pixel so the axis scales we measure are
    // in document *pixels*. This folds in the document's aspect anisotropy
    // (canvas space spans the whole docW x docH rect) and any rotation, so equal
    // pixel corner radii actually look circular on screen.
    const QTransform localToPixel =
        localToCanvas * QTransform(kx, 0.0, 0.0, ky, 0.0, 0.0);
    const double psx = axisScaleX(localToPixel);
    const double psy = axisScaleY(localToPixel);

    const double pixelW = std::abs(localBounds.width()) * psx;
    const double pixelH = std::abs(localBounds.height()) * psy;

    // canvasRadius is a canvas x-unit radius; convert to pixels (r_px = ndc * docW/2).
    double rPx = canvasRadius * kx;
    // Clamp to the largest radius that fits the final (pixel-space) rectangle.
    const double maxRpx = 0.5 * std::min(pixelW, pixelH);
    rPx = std::clamp(rPx, 0.0, std::max(0.0, maxRpx));

    if (outAppliedCanvasRadius)
        *outAppliedCanvasRadius = (kx > 1e-9) ? (rPx / kx) : 0.0;

    if (rPx <= 0.0 || psx <= 1e-9 || psy <= 1e-9)
        return createRectangle(localBounds);

    // Pre-divide by the pixel axis scale so that, after the full local->pixel
    // mapping, both corner radii equal `rPx` in pixels (circular regardless of
    // aspect ratio or non-uniform stretch).
    return createRoundedRectangle(localBounds, rPx / psx, rPx / psy);
}

bool ShapePresetFactory::isParametricRoundedRect(const ShapeData& data)
{
    return data.metadata.presetId == QLatin1String("rectangle")
        && data.metadata.parameters.contains(QStringLiteral("cornerRadius"))
        && data.metadata.parameters.value(QStringLiteral("cornerRadius")).toDouble() > 0.0
        && !data.path.commands.empty();
}

VectorPath ShapePresetFactory::roundedRectPathFor(const ShapeData& data,
                                                  const QTransform& effectiveLocalToCanvas,
                                                  const QPointF& canvasToPixelScale,
                                                  double* outPreservedCanvasRadius)
{
    const QRectF localBounds = (data.path.localBounds.isValid()
                                && !data.path.localBounds.isEmpty())
        ? data.path.localBounds
        : QRectF(0.0, 0.0, 1.0, 1.0);

    const double storedRadius = std::max(0.0,
        data.metadata.parameters.value(QStringLiteral("cornerRadius")).toDouble());

    // The options-bar value is the source of truth. A 5 px corner must remain
    // 5 px after transform; only the local control points are regenerated to
    // compensate the effective transform and keep the corner circular.
    const double preservedRadius = storedRadius;
    if (outPreservedCanvasRadius)
        *outPreservedCanvasRadius = preservedRadius;

    return createRoundedRectangleForTransform(localBounds, preservedRadius,
                                              effectiveLocalToCanvas, canvasToPixelScale);
}

VectorPath ShapePresetFactory::createEllipse(const QRectF& localBounds)
{
    VectorPath path;
    path.localBounds = localBounds;
    path.closed = true;

    constexpr double kappa = 0.5522847498307936;
    const QPointF c = localBounds.center();
    const double rx = localBounds.width() * 0.5;
    const double ry = localBounds.height() * 0.5;
    const double ox = rx * kappa;
    const double oy = ry * kappa;

    path.commands = {
        moveTo(QPointF(c.x(), c.y() - ry)),
        cubicTo(QPointF(c.x() + ox, c.y() - ry), QPointF(c.x() + rx, c.y() - oy), QPointF(c.x() + rx, c.y())),
        cubicTo(QPointF(c.x() + rx, c.y() + oy), QPointF(c.x() + ox, c.y() + ry), QPointF(c.x(), c.y() + ry)),
        cubicTo(QPointF(c.x() - ox, c.y() + ry), QPointF(c.x() - rx, c.y() + oy), QPointF(c.x() - rx, c.y())),
        cubicTo(QPointF(c.x() - rx, c.y() - oy), QPointF(c.x() - ox, c.y() - ry), QPointF(c.x(), c.y() - ry)),
        closePath()
    };
    return path;
}

VectorPath ShapePresetFactory::createLine(const QPointF& start, const QPointF& end)
{
    VectorPath path;
    path.localBounds = QRectF(start, end).normalized();
    path.closed = false;
    path.commands = { moveTo(start), lineTo(end) };
    return path;
}

VectorPath ShapePresetFactory::createPolygon(const QRectF& localBounds, int sides)
{
    VectorPath path;
    path.localBounds = localBounds;
    path.closed = true;

    const int n = std::max(3, sides);
    const QPointF c = localBounds.center();
    const double rx = localBounds.width() * 0.5;
    const double ry = localBounds.height() * 0.5;
    for (int i = 0; i < n; ++i) {
        const double a = 2.0 * M_PI * i / n - M_PI * 0.5;
        const QPointF pt(c.x() + rx * std::cos(a), c.y() + ry * std::sin(a));
        path.commands.push_back(i == 0 ? moveTo(pt) : lineTo(pt));
    }
    path.commands.push_back(closePath());
    return path;
}

VectorPath ShapePresetFactory::createArrow(const QRectF& localBounds, const ArrowOptions& options)
{
    VectorPath path;
    path.localBounds = localBounds;
    path.closed = true;

    const double left = localBounds.left();
    const double right = localBounds.right();
    const double top = localBounds.top();
    const double bottom = localBounds.bottom();
    const double cy = localBounds.center().y();
    const double w = std::max(0.000001, std::abs(localBounds.width()));
    const double h = std::abs(localBounds.height());
    const double headLen = std::clamp(options.headLengthRatio, 0.05, 0.90) * w;
    const double bodyHalf = std::clamp(options.bodyWidthRatio, 0.02, 1.0) * h * 0.5;
    const double headHalf = std::clamp(options.headWidthRatio, 0.02, 1.0) * h * 0.5;

    if (options.doubleHead) {
        const double leftBase = left + headLen;
        const double rightBase = right - headLen;
        path.commands = {
            moveTo(QPointF(left, cy)),
            lineTo(QPointF(leftBase, cy - headHalf)),
            lineTo(QPointF(leftBase, cy - bodyHalf)),
            lineTo(QPointF(rightBase, cy - bodyHalf)),
            lineTo(QPointF(rightBase, cy - headHalf)),
            lineTo(QPointF(right, cy)),
            lineTo(QPointF(rightBase, cy + headHalf)),
            lineTo(QPointF(rightBase, cy + bodyHalf)),
            lineTo(QPointF(leftBase, cy + bodyHalf)),
            lineTo(QPointF(leftBase, cy + headHalf)),
            closePath()
        };
        return path;
    }

    const double headBase = right - headLen;
    path.commands = {
        moveTo(QPointF(left, cy - bodyHalf)),
        lineTo(QPointF(headBase, cy - bodyHalf)),
        lineTo(QPointF(headBase, cy - headHalf)),
        lineTo(QPointF(right, cy)),
        lineTo(QPointF(headBase, cy + headHalf)),
        lineTo(QPointF(headBase, cy + bodyHalf)),
        lineTo(QPointF(left, cy + bodyHalf)),
        closePath()
    };
    return path;
}

VectorPath ShapePresetFactory::createStar(const QRectF& localBounds, const StarOptions& options)
{
    VectorPath path;
    path.localBounds = localBounds;
    path.closed = true;

    const int points = std::max(3, options.points);
    const QPointF c = localBounds.center();
    const double outerX = localBounds.width() * 0.5;
    const double outerY = localBounds.height() * 0.5;
    const double innerRatio = std::clamp(options.innerRadiusRatio, 0.05, 0.95);
    for (int i = 0; i < points * 2; ++i) {
        const bool outer = (i % 2) == 0;
        const double rx = outer ? outerX : outerX * innerRatio;
        const double ry = outer ? outerY : outerY * innerRatio;
        const double a = M_PI * i / points - M_PI * 0.5;
        const QPointF pt(c.x() + rx * std::cos(a), c.y() + ry * std::sin(a));
        path.commands.push_back(i == 0 ? moveTo(pt) : lineTo(pt));
    }
    path.commands.push_back(closePath());
    return path;
}
