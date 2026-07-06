#pragma once

#include <QPointF>
#include <QRectF>
#include <QElapsedTimer>
#include <vector>

class Document;
class TextEditorController;

struct RenderParams {
    Document* doc = nullptr;

    // ── Projection ──────────────────────────────────────────
    QPointF canvasHalfExtents{1.0f, 1.0f};

    // ── Tool state ──────────────────────────────────────────
    int   currentTool            = 0;
    bool  editingMask            = false;
    bool  grayscaleMaskView      = false;
    bool  quickMaskMode          = false;
    bool  showTransformControls  = true;

    // On-demand rubylith overlay of the active layer's mask (decoupled from
    // editingMask). When set, the per-layer compositor draws the red overlay.
    bool  showMaskOverlay        = false;
    float maskOverlayOpacity     = 0.5f;

    // True while an edit is mutating the composite live without bumping the
    // document's compositionGeneration (active brush/eraser stroke, transform
    // or shape drag, text editing). The GPU falls back to its per-layer
    // compositor for these frames instead of the cached CPU projection.
    bool  liveEdit               = false;

    // ── Preview ─────────────────────────────────────────────
    bool        hasPreview       = false;
    unsigned int previewTexture  = 0;

    // ── Selection ───────────────────────────────────────────
    int          selectType          = 0;
    QPointF      selectStart;
    QPointF      selectCurrent;
    bool         selectDragging      = false;
    bool         lassoDrawing        = false;
    bool         lassoCanClose       = false;
    std::vector<QPointF>* lassoPoints = nullptr;
    QElapsedTimer*         antTimer   = nullptr;

    // ── Crop ────────────────────────────────────────────────
    QRectF cropRect;
    bool   cropActive         = false;
    int    cropGuideType      = 0;
    float  cropOverlayOpacity = 0.4f;

    // ── Transform / Move ────────────────────────────────────
    bool transformingSelection = false;
    bool  activeMultiTransform  = false;
    float multiBboxCenterX      = 0.0f;
    float multiBboxCenterY      = 0.0f;
    float multiBboxHw           = 1.0f;
    float multiBboxHh           = 1.0f;
    float multiBboxRotation     = 0.0f;

    // ── Box Selection ───────────────────────────────────────
    bool    boxSelecting = false;
    QPointF boxSelectStart;
    QPointF boxSelectCurrent;

    // ── Text ────────────────────────────────────────────────
    int   textToolState  = 0;
    int   textLayerIndex = -1;
    TextEditorController* textEditor = nullptr;

    // ── Brush indicator ─────────────────────────────────────
    float   brushRadius          = 0.0f;
    QPointF brushIndicatorCenter;

    // ── Viewport size (width/height from QOpenGLWidget) ────
    int viewportW = 0;
    int viewportH = 0;
};
