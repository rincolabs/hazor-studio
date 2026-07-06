#pragma once

#include "ai/sam/SamTypes.hpp"
#include "ai/AiSelectionTypes.hpp"

#include <QImage>
#include <QSize>
#include <QRectF>
#include <QPointF>

// Converts raw SAM decoder output (snapshot-resolution mask logits) into the
// editor's mask representation (document-sized 8-bit grayscale, 255 = selected),
// and provides the geometric metrics the subject-selection heuristic needs.
class SamPostprocessor {
public:
    // Thresholds the candidate logits and maps the result up to document space.
    // antiAlias produces soft edges via bilinear upscaling; otherwise nearest.
    static AiMaskResult toDocumentMask(const SamMaskCandidate& candidate,
                                       const QSize& documentSize,
                                       const AiSelectionOptions& options);

    // Cheap geometric description of a candidate mask, computed at snapshot
    // resolution, used to rank auto-generated subject candidates.
    struct MaskStats {
        double areaFraction = 0.0;     // selected area / total
        QPointF centroid;              // snapshot pixels
        double centerDistanceFrac = 1.0; // normalised distance of centroid to center
        int   width = 0;
        int   height = 0;
        bool  empty = true;
    };
    static MaskStats analyze(const SamMaskCandidate& candidate);

    // Post-processing primitives that operate purely on the candidate logits/mask
    // (the editor's own mask math is never touched). Used by RemoveBackground /
    // subject selection. These keep the largest blob and drop tiny specks/holes.
    static void keepLargestComponent(SamMaskCandidate& candidate);
};
