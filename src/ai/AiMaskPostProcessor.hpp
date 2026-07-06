#pragma once

#include "ai/matting/AiRefineTypes.hpp"

#include <QImage>

// Isolated adapter for edge-shaping and cleanup of an AI-produced mask/alpha.
// Operates purely on document-sized 8-bit grayscale images (255 = selected) with
// OpenCV — it never touches the editor's own selection / layer-mask math, exactly
// as Etapa 3 requires. All ops are no-ops when their parameter is zero/false, so
// the pipeline can hand the user's AiRefineOptions straight through.
namespace AiMaskPostProcessor {

// Applies, in order: shift edge (grow/shrink), smooth, contrast (S-curve),
// feather, then cleanup (remove small islands / fill small holes), and finally a
// hard threshold when preserveSoftEdges is off. Returns a new grayscale image of
// the same size. `alpha` must be Format_Grayscale8.
QImage applyRefine(const QImage& alpha, const AiRefineOptions& options);

// Multiplies `alpha` by a (slightly dilated) base mask so a matting model's
// full-frame matte is constrained to the region the user actually selected. Both
// images must share the same size and be Format_Grayscale8; returns `alpha`
// unchanged if base is null/empty/mismatched.
QImage intersectWithBase(const QImage& alpha, const QImage* baseMask);

} // namespace AiMaskPostProcessor
