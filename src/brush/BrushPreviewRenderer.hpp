#pragma once

#include <QImage>
#include <QColor>
#include <QSize>
#include "BrushTypes.hpp"

// Stateless CPU renderer that produces brush thumbnails without touching the
// GPU dab pipeline or a Layer. It deliberately reuses the *same* primitives the
// real renderer uses so previews match what the brush actually paints:
//   - brushFalloff()           (BrushTypes.hpp) for the tip shape / hardness
//   - DynamicsEvaluator        for size/angle/roundness/scatter/count dynamics
//   - the engine spacing rule  spacing = max(size * settings.spacing, 1)
//   - the per-pixel dab math    from BrushRenderer::drawDabToRasterTiles
//
// The ink colour is supplied by the caller (a theme-aware colour) so previews
// stay visible on any panel background; the brush's own opacity/flow/dynamics
// still drive the dab alpha exactly as in a real stroke.
namespace BrushPreviewRenderer {

// Single centred dab showing only the brush tip shape (column 1 of a row).
QImage renderTip(const BrushSettings& settings, const QSize& size,
                 qreal devicePixelRatio, const QColor& ink);

// Diagonal stroke rendered through the real dab-stepping pipeline so spacing,
// scatter, shape dynamics, opacity and flow all show (column 2 of a row).
QImage renderStroke(const BrushSettings& settings, const QSize& size,
                    qreal devicePixelRatio, const QColor& ink);

} // namespace BrushPreviewRenderer
