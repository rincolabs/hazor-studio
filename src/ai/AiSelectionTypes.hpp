#pragma once

#include <QRectF>
#include <QPointF>
#include <QImage>
#include <QString>
#include <QVector>

// Shared value types for the AI selection / masking layer (Etapa 2). These carry
// no ONNX, Document or Layer types: the compute layer (pipeline / segmenter)
// speaks in document-pixel coordinates and plain images, and the editor adapts
// them to its existing SelectionMask / layer-mask APIs on the UI side.

// How a produced mask is combined with the document's current selection. Mirrors
// the editor's SelectMode (Replace/Add/Subtract/Intersect) without depending on
// it, so the AI layer stays self-contained.
enum class AiSelectionOperation {
    Replace = 0,
    Add = 1,
    Subtract = 2,
    Intersect = 3
};

// Which pixels the segmenter sees as input.
enum class AiSampleSource {
    CurrentLayer = 0,    // only the active layer, rendered into document space
    AllVisible = 1       // the visible composition of the whole document
};

// Top-level action of the AI Object Selection tool.
enum class AiToolMode {
    SelectObject = 0,    // click / box prompt → active selection
    RemoveBackground = 1, // main-subject mask → non-destructive layer mask
    RefineMask = 2,      // refine an existing mask/selection with a matting model
    SelectSubject = 3    // auto main-subject mask → active selection (SAM only)
};

// A single foreground/background hint point in document-pixel coordinates.
struct AiPromptPoint {
    QPointF pos;            // document pixels
    bool foreground = true; // true = include (label 1), false = exclude (label 0)
};

// A user (or heuristic) prompt expressed in document-pixel coordinates. The
// segmenter maps these into model space; the tool never deals with model
// coordinates directly.
struct AiPrompt {
    enum class Kind { None, Points, Box, Auto } kind = Kind::None;

    QVector<AiPromptPoint> points;  // for Kind::Points (and box can add center hints)
    QRectF box;                     // for Kind::Box (document pixels)

    bool isEmpty() const
    {
        return kind == Kind::None
            || (kind == Kind::Points && points.isEmpty())
            || (kind == Kind::Box && box.isEmpty());
    }
};

// Tunables for a selection request. Defaults are conservative and safe.
struct AiSelectionOptions {
    AiSelectionOperation operation = AiSelectionOperation::Replace;
    AiSampleSource sample = AiSampleSource::AllVisible;
    bool antiAlias = true;
    float maskThreshold = 0.0f;   // SAM logits are thresholded at 0
    // Subject-selection heuristic guards (fractions of the snapshot area).
    float minSubjectAreaFrac = 0.0015f; // discard tiny specks
    float maxSubjectAreaFrac = 0.95f;   // distrust "selected the whole frame"
};

// Result of one segmentation: a grayscale (8-bit) mask in DOCUMENT pixel space
// where 255 = selected. The bounds are the mask's non-zero extent (document
// pixels). ok=false means the model produced nothing usable.
struct AiMaskResult {
    QImage mask;          // QImage::Format_Grayscale8, size == document size
    QRectF bounds;        // document-space bounding box of the selection
    float score = 0.0f;   // model confidence (iou prediction) when available
    bool ok = false;

    bool isValid() const { return ok && !mask.isNull(); }
};
