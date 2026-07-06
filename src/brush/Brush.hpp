#pragma once

#include <QImage>
#include "DabContext.hpp"

// ── Layer A: the brush as a mask/stamp producer ────────────────────────────
//
// A Brush turns the abstract dab geometry (DabShape) + per-dab input (DabContext)
// into coverage for ONE dab. This is the single polymorphic seam that lets the
// editor reproduce the standard tip families faithfully:
//   AutoBrush       — procedural mask generator (circle/rect, default/soft/
//                     gaussian falloff, anisotropic fade, spikes, density).
//   PredefinedBrush — bitmap tip with a scale pyramid and the three brush
//                     applications (ALPHAMASK / IMAGESTAMP / LIGHTNESSMAP).
//   (PipeBrush      — animated multi-frame tip; deferred, hence variesPerDab().)
//
// Both the canvas raster path (BrushDab::rasterize) and the panel preview build
// the SAME Brush, so a dab is pixel-identical in the preview and on the canvas.
// The Brush owns only the tip; the caller (BrushDab) applies the ink colour,
// texture, dual brush and selection mask on top of the coverage produced here.
class Brush {
public:
    virtual ~Brush() = default;

    // Fill `sprite` (must be ARGB32, side x side) with this dab's coverage,
    // centred at (cx, cy) with the given base `radius` (px) and per-dab `shape`
    // (scale/rotation/flip). Coverage goes in the alpha channel. RGB is:
    //   ALPHAMASK    → left 0 (caller multiplies in the ink colour),
    //   IMAGESTAMP   → the tip's own colour,
    //   LIGHTNESSMAP → the tip's grayscale lightness (caller modulates the ink).
    // The brush is responsible for the tip shape and geometry only.
    virtual void renderCoverage(QImage& sprite, int side, float cx, float cy,
                                float radius, const DabShape& shape,
                                const DabContext& ctx) const = 0;

    // True when the produced coverage changes from dab to dab (animated pipe or
    // per-dab procedural noise). Static brushes (the common case) return false,
    // so callers may cache a single scaled mask / GPU stamp across a stroke.
    virtual bool variesPerDab() const = 0;

    // A normalized coverage stamp (Grayscale8, diam x diam) for the GPU stamp
    // path (BrushRenderer::generateStamp) and the layer-mask path, which sample
    // it by UV and scale on the GPU. Static brushes bake their neutral shape here.
    virtual QImage staticStamp(int diam) const = 0;
};
