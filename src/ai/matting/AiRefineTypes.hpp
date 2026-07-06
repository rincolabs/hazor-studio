#pragma once

#include <QImage>
#include <QRectF>
#include <QString>

// Value types for the Etapa 3 refinement / matting layer. Like the rest of the
// AI compute layer these carry no ONNX, Document or Layer types: matting and
// background-removal models speak in document-pixel coordinates and plain images,
// and the editor adapts the result to its existing selection / layer-mask APIs.

// Soft alpha matte produced by a matting / background-removal model: a
// document-sized 8-bit grayscale image where 255 = full foreground, 0 =
// background and intermediate values = partial coverage (hair, soft edges).
// Structurally a grayscale mask, but kept as its own type so the pipeline can
// reason about "this is a soft alpha from a matting model" versus a hard SAM
// selection mask (AiMaskResult).
struct AiAlphaResult {
    QImage alpha;        // Format_Grayscale8, size == document size
    QRectF bounds;       // document-space bounding box of non-zero alpha
    float  score = 0.0f; // model confidence when available
    bool   ok = false;

    bool isValid() const { return ok && !alpha.isNull(); }
};

// Engine used to produce the foreground for Remove Background. The actual model
// is resolved from the installed set; "Auto" prefers the best one available.
enum class AiBackgroundRemovalEngine {
    Auto = 0,    // best installed model (RMBG → BiRefNet → SAM)
    SamRefine,   // SAM subject mask, optionally refined by a matting model
    Rmbg,        // dedicated background-removal model
    BiRefNet     // matting model used as a direct foreground segmenter
};

// Where a produced mask / alpha lands in the document. The controller maps these
// onto the editor's existing selection / layer-mask APIs (no parallel systems).
enum class AiOutputTarget {
    ActiveSelection = 0,
    NewLayerMask,
    ReplaceLayerMask,
    RefineCurrentLayerMask,
    PreviewOnly
};

// Edge-shaping and cleanup controls applied after segmentation. Shared by the
// options bar and the pipeline; every value is normalised so the UI maps sliders
// 1:1. Interpreted exclusively by AiMaskPostProcessor — never by the editor's own
// mask math.
struct AiRefineOptions {
    bool    enabled = false;
    QString mattingModelId;     // "" = Auto (pick best installed matting model)

    // Edge shaping.
    int smooth    = 0;          // 0..100  smooth the alpha edge
    int feather   = 0;          // 0..100  feather radius in px
    int contrast  = 0;          // 0..100  edge contrast (S-curve on alpha)
    int shiftEdge = 0;          // -100..100  grow (+) / shrink (-) the edge

    // Cleanup.
    bool removeSmallIslands = false;
    bool fillSmallHoles     = false;
    bool preserveSoftEdges  = true;   // when false, threshold to a hard mask
    bool decontaminateEdges = false;  // reserved (needs source colour data)

    // True when any edge/cleanup op (independent of a matting model) would change
    // the mask — lets the pipeline skip the post-process pass entirely when off.
    bool hasEdgeOps() const
    {
        return smooth || feather || contrast || shiftEdge
            || removeSmallIslands || fillSmallHoles || !preserveSoftEdges;
    }
};

// Options for a direct background-removal pass.
struct AiBackgroundRemovalOptions {
    AiBackgroundRemovalEngine engine = AiBackgroundRemovalEngine::Auto;
    AiRefineOptions refine;
};
