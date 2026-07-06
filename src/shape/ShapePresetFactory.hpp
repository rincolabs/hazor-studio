#pragma once

#include "core/ShapeTypes.hpp"

#include <QRectF>
#include <QPointF>
#include <QTransform>

struct ArrowOptions {
    double headLengthRatio = 0.30;
    double headWidthRatio = 0.60;
    double bodyWidthRatio = 0.25;
    bool doubleHead = false;
};

struct StarOptions {
    int points = 5;
    double innerRadiusRatio = 0.45;
};

class ShapePresetFactory {
public:
    static VectorPath createRectangle(const QRectF& localBounds);

    // Single-radius (isotropic in *local* space). Kept for callers that work
    // directly in a unit/uniform space.
    static VectorPath createRoundedRectangle(const QRectF& localBounds, double radius);

    // Anisotropic local-space corner radii. The corners are circular in canvas
    // space only if radiusX/radiusY already compensate the local->canvas scale;
    // see createRoundedRectangleForTransform() for that.
    static VectorPath createRoundedRectangle(const QRectF& localBounds,
                                             double radiusX, double radiusY);

    // Builds a rounded rectangle whose corners render as *circular* arcs (equal
    // radius in document pixels) after `localToCanvas` and the canvas->pixel
    // mapping are applied. `canvasRadius` is the stored radius in canvas x-units
    // and `canvasToPixelScale` = (docW/2, docH/2) carries the document's aspect
    // anisotropy (canvas space spans the full docW x docH rect, so 1 canvas-x
    // unit and 1 canvas-y unit differ in pixels). Both uniform and non-uniform
    // scale are handled by pre-dividing the per-axis local radii by the
    // transform's *pixel* axis scales; the radius is clamped to half the shorter
    // final pixel side so corners never overflow. `outAppliedCanvasRadius`, if
    // set, reports the clamped radius back in canvas x-units.
    static VectorPath createRoundedRectangleForTransform(const QRectF& localBounds,
                                                         double canvasRadius,
                                                         const QTransform& localToCanvas,
                                                         const QPointF& canvasToPixelScale,
                                                         double* outAppliedCanvasRadius = nullptr);

    // True when `data` is a parametric rounded rectangle (rectangle preset with
    // a positive stored cornerRadius) whose corners must be recomputed when its
    // geometry/transform changes.
    static bool isParametricRoundedRect(const ShapeData& data);

    // Rebuilds the LOCAL VectorPath of a parametric rounded rectangle so its
    // corners stay circular under `effectiveLocalToCanvas` (the transform that
    // will actually map the local path to canvas — i.e. the resting
    // transform.localToCanvas optionally composed with a live preview delta).
    // The stored canvas-space radius is kept as the source of truth; only the
    // local path control points are regenerated so the final radius remains the
    // same document-pixel value under the effective transform. `canvasToPixelScale`
    // = (docW/2, docH/2). The preserved canvas radius is reported via
    // `outPreservedCanvasRadius` for callers that need to mirror the value.
    static VectorPath roundedRectPathFor(const ShapeData& data,
                                         const QTransform& effectiveLocalToCanvas,
                                         const QPointF& canvasToPixelScale,
                                         double* outPreservedCanvasRadius = nullptr);

    static VectorPath createEllipse(const QRectF& localBounds);
    static VectorPath createLine(const QPointF& start, const QPointF& end);
    static VectorPath createPolygon(const QRectF& localBounds, int sides);
    static VectorPath createArrow(const QRectF& localBounds, const ArrowOptions& options);
    static VectorPath createStar(const QRectF& localBounds, const StarOptions& options);
};
