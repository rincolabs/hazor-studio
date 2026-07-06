#pragma once

#include <QOpenGLWidget>
#include <QPointF>
#include <QColor>
#include <QCursor>
#include <QElapsedTimer>
#include <QTimer>
#include <QImage>
#include <QPainterPath>
#include <QVariantMap>
#include <QTabletEvent>
#include <QLabel>
#include <QTimer>
#include <QRect>
#include <unordered_map>
#include <vector>
#include <memory>
#include <functional>
#include <cstdint>
#include <limits>
#include <algorithm>
#include <optional>

#include "brush/BrushTypes.hpp"
#include "engine/ColorSamplerService.hpp"
#include "brush/BrushEngine.hpp"
#include "brush/BrushRenderer.hpp"
#include "brush/BrushInputState.hpp"
#include "core/SelectionMask.hpp"
#include "core/GuideTypes.hpp"
#include "ai/AiSelectionTypes.hpp"
#include "transform/TransformTypes.hpp"
#include "text/TextEditorController.hpp"
#include "text/TextTypes.hpp"
#include "core/ShapeTypes.hpp"
#include "shape/ShapeIconLibrary.hpp"
#include "gradient/GradientTypes.hpp"
#include "TileRenderer.hpp"
#include "PointerInputEvent.hpp"
#include "CanvasInputState.hpp"
#include "transform/SnapEngine.hpp"
#include "transform/TransformController.hpp"
#include "processing/PreviewRenderer.hpp"

class Document;
class Layer;
class LayerTreeNode;
class ImageController;
class GPUViewport;
class SelectionDragOverlay;
class ShapePreviewOverlay;
class DistortPreviewOverlay;
class BrushPreviewOverlay;
class GradientDragOverlay;
class RulerGuideOverlay;
class AiObjectSelectionController;
class AiRemoveObjectController;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;
class QScrollBar;

class CanvasView : public QOpenGLWidget {
    Q_OBJECT

public:
    // Skew is an independent tool: it is "distort edit mode" with two internal
    // modes (Distort/Perspective). Selecting it immediately begins a distort
    // session on the active layer; leaving it cancels the session.
    enum class Tool { Move = 0, Brush = 1, Eraser = 2, Select = 3, Zoom = 4, Hand = 5, Text = 6, Crop = 7, FillBucket = 8, Eyedropper = 9, Shape = 10, CloneStamp = 11, Gradient = 12, Skew = 13, AiSelect = 14, AiRemove = 15 };
    // Clone Stamp sub-mode (flyout sub-tool, like Magic Wand under Select).
    enum class StampMode { Clone = 0, Healing = 1 };

    explicit CanvasView(Document* doc, ImageController* controller = nullptr,
                        QWidget* parent = nullptr);
    ~CanvasView() override;

    void setImageController(ImageController* ctrl) { m_controller = ctrl; }
    void setTool(Tool t);
    // The tool the user actually picked in the toolbar. The toolbar, options bar
    // and all persistent tool state mirror THIS. A temporary contextual shortcut
    // (e.g. Alt over a paint tool) never changes it. Kept named currentTool() for
    // the many existing call-sites that mean "the selected tool".
    Tool currentTool() const { return m_currentTool; }
    Tool selectedTool() const { return m_currentTool; }
    // The tool that should actually process canvas input and drive the cursor
    // right now — the temporary override if one is armed, otherwise the selected
    // tool. Use this for event dispatch and cursor, never for toolbar/state sync.
    Tool effectiveTool() const {
        return m_temporaryOverrideTool ? *m_temporaryOverrideTool : m_currentTool;
    }

    // Lazily-created brain of the AI Object Selection tool (Tool::AiSelect). The
    // options bar connects to it for status/options; the canvas forwards click/
    // box gestures here. Returns nullptr until an ImageController is attached.
    AiObjectSelectionController* aiSelectionController();
    AiRemoveObjectController* aiRemoveController();
    void setEditingMask(bool editing);
    bool isEditingMask() const { return m_editingMask; }
    // View-state toggles migrated off keyPressEvent so the ShortcutManager owns
    // the keys (Q / "\") — centralized and remappable. Each keeps its own gate.
    void toggleQuickMask();      // Q: needs an active, non-empty selection
    void toggleMaskRubylith();   // "\": needs the active layer to have a mask

    // Fit the document inside the viewport (used when opening an image so large
    // images aren't shown at an over-zoomed 100%). Never upscales past 100%.
    // If the widget has no valid size yet, the fit is deferred to the first
    // resizeGL.
    void fitToView();
    void zoomToOriginal();
    void setZoom(float zoom);
    void zoomIn();
    void zoomOut();
    // Enable/disable overscroll. Caches the (global) preference
    // for this canvas and immediately re-clamps the current pan. MainWindow calls
    // this on every tab when the View > Overscroll toggle changes.
    void setOverscrollEnabled(bool enabled);
    bool isTextEditing() const { return m_currentTool == Tool::Text && m_textToolState == TextToolState::Editing; }
    void setGrayscaleMaskView(bool enabled);
    bool isGrayscaleMaskView() const { return m_grayscaleMaskView; }

    void setBrushSize(float s);
    void setBrushHardness(float h);
    void setBrushOpacity(float o);
    void setBrushFlow(float f);
    // Global minimum-pressure floor (0..1), overrides the preset's pressure response.
    void setBrushMinPressure(float p) { m_brushEngine.setMinPressure(p); }
    float brushMinPressure() const { return m_brushEngine.minPressure(); }
    void setBrushColor(const QColor& c) { m_brushSettings.color = c; }
    void setBrushType(BrushType t);
    void setBrushTipSource(BrushTipSource source);
    void setBrushTipImage(const QImage& img);
    void setBrushApplication(BrushApplication a) { m_brushSettings.application = a; }
    void setBrushPaintMode(BrushPaintMode m) { m_brushSettings.paintMode = m; }
    void setBrushMode(BrushMode m) { m_brushSettings.mode = m; }
    void setBrushTexture(const QImage& tex);
    void setBrushTextureConfig(const TextureConfig& config);
    void setBrushSpacing(float s) { m_brushSettings.spacing = s; }
    void setBrushSmoothingMode(SmoothingMode m) { m_brushSettings.smoothingMode = m; }
    void setBrushSmoothingRadius(float r) { m_brushSettings.smoothingRadius = r; }
    void setBrushAirbrush(bool a) { m_brushSettings.airbrush = a; }
    void setBrushAirbrushRate(float r) { m_brushSettings.airbrushRate = std::clamp(r, 1.0f, 200.0f); }
    void setBrushWetEdges(bool w) { m_brushSettings.wetEdges = w; }
    void setBrushBlendMode(BrushBlendMode m);
    void setBrushDualBrushConfig(const DualBrushConfig& d) { m_brushSettings.dualBrushConfig = d; }
    void setBrushScatter(const CurveOption& o, const ScatterConfig& axes) {
        m_brushSettings.scatterOption = o;
        m_brushSettings.scatter = axes;
    }
    void setBrushColorDynamics(const ColorDynamics& d) { m_brushSettings.colorDynamics = d; }
    void setBrushAngle(float a) { m_brushSettings.angle = a; }
    void setBrushRoundness(float r) { m_brushSettings.roundness = r; }
    void setBrushFlipX(bool f) { m_brushSettings.flipX = f; }
    void setBrushFlipY(bool f) { m_brushSettings.flipY = f; }
    // Layer A/B/C additions (extended engine). The CPU raster path reads
    // m_brushSettings live; only the AutoBrush profile/spikes/density change the
    // GPU/mask stamp, so just that one refreshes it.
    void setBrushAutoBrushConfig(const AutoBrushConfig& a) { m_brushSettings.autoBrush = a; m_brushRenderer.updateStamp(m_brushSettings); }
    void setBrushAutoSpacing(bool on, float coeff) { m_brushSettings.useAutoSpacing = on; m_brushSettings.autoSpacingCoeff = coeff; }
    void setBrushTipRemap(float bright, float contrast, float mid) { m_brushSettings.tipBrightness = bright; m_brushSettings.tipContrast = contrast; m_brushSettings.tipMidpoint = mid; }
    void setBrushSizeOption(const CurveOption& o) { m_brushSettings.sizeOption = o; }
    void setBrushOpacityOption(const CurveOption& o) { m_brushSettings.opacityOption = o; }
    void setBrushFlowOption(const CurveOption& o) { m_brushSettings.flowOption = o; }
    void setBrushRotationOption(const CurveOption& o) { m_brushSettings.rotationOption = o; }
    void setBrushRatioOption(const CurveOption& o) { m_brushSettings.ratioOption = o; }
    // "Use Pen Pressure" toggle. Drives the Pressure sensor of the
    // Size/Opacity/Flow curve options.
    // ON turns on the Pressure sensor for any axis whose curve option already exists
    // (Size always, so an untouched brush gets the default pressure→size behaviour);
    // OFF turns the Pressure sensor off on all three, preserving any non-pressure
    // sensors/curves.
    void setBrushPressureEnabled(bool on) {
        // Size is the default pressure axis, so it always follows the toggle.
        m_brushSettings.sizeOption.setPressureSensorEnabled(on);
        // Opacity/Flow only follow when the brush already maps them to pressure, so
        // turning the toggle on doesn't silently add dynamics the user never set.
        if (!on || m_brushSettings.opacityOption.hasActivePressureSensor())
            m_brushSettings.opacityOption.setPressureSensorEnabled(on);
        if (!on || m_brushSettings.flowOption.hasActivePressureSensor())
            m_brushSettings.flowOption.setPressureSensorEnabled(on);
    }
    bool brushPressureEnabled() const {
        return ::brushPressureEnabled(m_brushSettings);
    }
    void setCloneSource(QPointF documentPoint);
    void setCloneSampleMode(CloneSampleMode mode);
    void setCloneSampleMode(int mode) { setCloneSampleMode(static_cast<CloneSampleMode>(mode)); }
    void setCloneAligned(bool aligned);
    void beginCloneStroke(QPointF documentPoint);
    void updateCloneStroke(QPointF documentPoint);
    void endCloneStroke();

    // Healing Brush is a sub-mode of the Clone Stamp tool (selected from the
    // tool flyout, like Magic Wand under Select). It reuses all of the Clone
    // Stamp's source/offset/aligned/sample-mode state and stroke machinery; the
    // stamp mode only selects whether a dab clones pixels or heals (frequency
    // separation). The agent entry points switch to the Clone Stamp tool in
    // healing mode first.
    void setStampMode(int mode);   // 0 = Clone, 1 = Healing
    bool isHealingMode() const { return m_stampHealing; }
    void setHealingDiffusion(float diffusion);
    void setHealingSource(QPointF documentPoint);
    void beginHealingStroke(QPointF documentPoint);
    void updateHealingStroke(QPointF documentPoint);
    void endHealingStroke();

    void setSelectType(SelectType t);
    void setSelectMode(SelectMode m) { m_selectMode = m; }
    void setSelectAntiAlias(bool enabled) { m_selectAntiAlias = enabled; }
    void setQuickSelectTolerance(float t) { m_quickSelectTolerance = t; }
    // Null region = re-upload the whole mask texture; a valid region uploads
    // only that subrect (used by quick-select dabs).
    void markSelectMaskDirty(const QRect& region = QRect());

    void cleanupDocumentLayers();
    void syncLayersToGpu();

    void showPreview(const QString& toolName, const QVariantMap& params);
    void clearPreview();
    bool hasPreview() const { return m_hasPreview; }

    void setTextFont(const QFont& font);
    void setTextSize(int size);
    void setTextBold(bool bold);
    void setTextItalic(bool italic);
    void setTextUnderline(bool underline);
    void setTextStrikethrough(bool strikethrough);
    void setTextColor(const QColor& color);
    void setTextAlign(int align);
    void setTextTracking(double tracking);
    void setTextLeading(double leading);

    // Fills the character/paragraph style at the current caret (or the active
    // text layer when not editing). Returns false when there is no text layer.
    bool currentTextContext(TextSpan& outChar, ParagraphStyle& outPara);

    QPointF screenToImage(QPointF screenPos, Layer* layer);
    QPointF screenToCanvasNdc(QPointF screenPos) const;
    QPointF documentToScreen(QPointF docPos) const;
    QRectF documentRectToScreen(const QRectF& docRect) const;

    // When tightToContent is true, hit-testing uses the layer's painted (dab)
    // bounds instead of its full image rect — matching the transform outline.
    int pickLayerAtScreenPos(QPointF screenPos, bool tightToContent = false);

    void setFillBucketTolerance(int t) { m_fillBucketTolerance = t; }
    void setFillBucketColor(const QColor& c) { m_fillBucketColor = c; }

    void setGradientDefinition(const GradientDefinition& definition);
    void setGradientBlendMode(BlendMode mode) { m_gradientBlendMode = mode; }
    void setGradientOpacity(float opacity) { m_gradientOpacity = std::clamp(opacity, 0.0f, 1.0f); }

    void setEyedropperSampleMode(SampleMode mode) { m_eyedropperSampleMode = mode; }
    void setEyedropperSampleSize(SampleSize size) { m_eyedropperSampleSize = size; }
    SampleMode eyedropperSampleMode() const { return m_eyedropperSampleMode; }
    SampleSize eyedropperSampleSize() const { return m_eyedropperSampleSize; }

    // ── Curves editor image pick ──
    // Arms the Curves editor's eyedropper/target tools (0 = disarmed; 1=black,
    // 2=gray, 3=white one-shot; 4=target drag). A left-click reports the
    // document-space position via curvesPickRequested / curvesTargetBegan,
    // regardless of the active tool; the editor samples its bypassed input
    // composite there.
    void setCurvesPickMode(int mode);
    int curvesPickMode() const { return m_curvesPickMode; }

    // ── Hue/Saturation editor eyedropper ──
    // Arms the canvas to sample the active Hue/Saturation adjustment's INPUT
    // colour (0 = disarmed; 1 = main/recenter, 2 = add to range, 3 = subtract).
    // Main is a one-shot click; add/subtract are press-drag-release gestures.
    // Picks fire regardless of the active tool. Switching tool or pressing Esc
    // disarms and emits hueSatPickCancelled so the editor unchecks its button.
    void setHueSatPickMode(int mode);
    int hueSatPickMode() const { return m_hueSatPickMode; }

    // While an adjustment layer (e.g. Curves) is being dragged in the Properties
    // panel, render through the GPU per-layer compositor (liveEdit) instead of
    // recompositing the whole document on the CPU every frame. Committed on
    // gesture end, when the projection recomposites once.
    void setAdjustmentLiveEdit(bool on);

    // ── Shape tool ──
    void setShapeType(int type);
    void setShapeFillColor(const QColor& c);
    void setShapeFillEnabled(bool enabled);
    void setShapeStrokeColor(const QColor& c);
    void setShapeStrokeEnabled(bool enabled);
    void setShapeStrokeWidth(float w);
    void setShapeOpacity(float opacity);
    void setShapeCornerRadius(float r);
    void setShapeSides(int s);
    void setShapeAntiAlias(bool aa);
    void setCustomShapeIcon(const ShapeIconInfo& icon);

    void setAutoSelect(bool enabled);
    void setAutoSelectGroup(bool group);
    void setShowTransformControls(bool show);
    void setCropAspectRatio(const QSizeF& ratio);
    void setCropGuideType(int type);
    void setCropOverlayOpacity(float opacity);
    void setCropStraightenAngle(float angle);
    void setCropCustomSize(int w, int h);
    void resetCrop();
    void commitCropAction();
    void cancelCrop();
    bool isCropActive() const { return m_cropActive; }
    bool beginFreeTransform();
    void commitFreeTransform();
    void cancelFreeTransform();
    bool isFreeTransformActive() const { return m_freeTransformActive; }

    // ── Distort / Perspective ──
    // Enter a quad-edit transform mode on the active raster layer. Returns false
    // if the layer is unsupported (text/shape/locked). Commit bakes the warp,
    // cancel restores. Both are routed through commit/cancelFreeTransform too.
    bool beginDistort(TransformMode mode);
    void commitDistort();
    void cancelDistort();
    bool isDistortActive() const { return m_distortActive; }
    TransformMode distortMode() const { return m_distortMode; }
    bool activeLayerSupportsDistort() const;
    // True when the active layer carries a distort/perspective warp that can be
    // reset to its original (undeformed) shape.
    bool activeLayerHasDistort() const;
    // Reset the active distort layer back to its original quad (full quality),
    // as one undo step. Works whether or not an editing session is open.
    void resetDistort();
    void alignLeft();
    void alignCenterH();
    void alignRight();
    void alignTop();
    void alignMiddleV();
    void alignBottom();
    void setAlignTarget(int target);

    // Applies a fully-formed local transform to the active layer as a single undo
    // step, routed through the SAME commit/bake machinery as a manual free
    // transform (text re-bakes its font size, shapes re-render crisp). Used by the
    // Properties panel's numeric W/H/X/Y/rotation fields so transform logic stays
    // centralized and the panel is a pure shortcut. Returns false when the active
    // node can't be transformed (no layer / position-locked).
    bool applyTransformFromPanel(const QTransform& newTransform);
    void reloadRulerGuideSettings();

private slots:
    void onPreviewReady(const QImage& image);

signals:
    void toolChanged(int tool);
    void freeTransformStateChanged(bool active);
    void activeTransformChanged();
    void selectTypeChanged(int type);
    void colorSampled(const QColor& color);
    void backgroundColorSampled(const QColor& color);
    // Curves-editor eyedropper pick: the document-space click position + mode
    // (1=black, 2=gray, 3=white). The editor samples its own bypassed input
    // composite at this position.
    void curvesPickRequested(const QPointF& docPos, int mode);
    // Target (on-image) tool drag: press reports the doc-space position once,
    // drag reports the vertical pixel delta (up = positive), release ends the
    // gesture. The Curves editor turns these into a single curve edit.
    void curvesTargetBegan(const QPointF& docPos);
    void curvesTargetDragged(int dyPixels);
    void curvesTargetEnded();
    // Hue/Saturation eyedropper. Main (mode 1) reports a single click; add/
    // subtract (mode 2/3) report press → 0+ moves → release, which the editor
    // turns into one undo step. docPos is in document pixels.
    void hueSatPickClicked(const QPointF& docPos, int mode);
    void hueSatPickDragBegan(const QPointF& docPos, int mode);
    void hueSatPickDragMoved(const QPointF& docPos);
    void hueSatPickDragEnded();
    // The canvas disarmed the eyedropper itself (tool switch / Esc): the editor
    // should uncheck its eyedropper button.
    void hueSatPickCancelled();
    void zoomChanged(float zoom);
    void mouseImageCoordChanged(QPointF imageCoord);
    void toolCallRequested(int tool, QPointF imagePos, QPointF screenPos);
    void externalImagesDropped(const QStringList& paths, QPointF screenPos);
    void undoRequested();
    void redoRequested();
    // Emitted whenever Distort/Perspective starts, commits, or cancels, so the
    // options bar can refresh its button enabled/checked state.
    void distortStateChanged();
    // Emitted while editing text whenever the caret/selection moves or the
    // styling changes, so the options bar can reflect the character/paragraph
    // style at the insertion point.
    void textStyleContextChanged();
    void brushSizeChanged(float size);
    void brushHardnessChanged(float hardness);
    void contextMenuRequested(QPoint globalPos);

protected:
    void initializeGL() override;
    void paintGL() override;
    void resizeGL(int w, int h) override;
    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* event) override;

    void wheelEvent(QWheelEvent* e) override;
    void mousePressEvent(QMouseEvent* e) override;
    void mouseMoveEvent(QMouseEvent* e) override;
    void mouseReleaseEvent(QMouseEvent* e) override;
    void mouseDoubleClickEvent(QMouseEvent* e) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;
    void tabletEvent(QTabletEvent* e) override;
    void keyPressEvent(QKeyEvent* e) override;
    void keyReleaseEvent(QKeyEvent* e) override;
    void leaveEvent(QEvent* e) override;
    void focusOutEvent(QFocusEvent* e) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void initQuad();
    void initShaderCheck();
    void beginBrushStroke(const BrushInputState& input, QPointF screenPos);
    void continueBrushStroke(const BrushInputState& input, QPointF screenPos);
    void endBrushStroke();

    // Unified pointer pipeline. Both mouseEvent and tabletEvent normalise their
    // native event into a PointerInputEvent (canvasPos/imagePos filled here from
    // the single coordinate path) and route the brush/eraser/clone painting +
    // cursor through these, so the visual cursor and the dab can never diverge.
    void fillPointerCanvasCoords(PointerInputEvent& ev, Layer* layer) const;
    void updateBrushCursorFromPointer(const PointerInputEvent& ev);
    void dispatchBrushPointer(const PointerInputEvent& ev);
    void logPointerInput(const PointerInputEvent& ev) const;
    // Non-paint tools still run their existing QMouseEvent logic. Rather than rely
    // on the OS-synthesised mouse (unreliable from some tablet drivers), we build a
    // QMouseEvent from the pen's PointerInputEvent and feed it to our own mouse
    // handlers, so every tool (Hand, Move, Select, Crop, ...) reacts to the pen.
    void dispatchSynthesizedMouse(const PointerInputEvent& ev, QEvent::Type type);
    // Single-Layer-Mode adjustment masks are baked on the CPU, so a live mask
    // dab on the GPU FBO isn't visible until synced back. Called after each dab
    // (requires the GL context current) to preview the effect in real time.
    void previewAdjustmentMaskEdit(Layer* layer);
    bool isBrushBasedTool() const;
    float clampedBrushSize(float size) const;
    float clampedBrushHardness(float hardness) const;
    void applyBrushSizeFromShortcut(float size);
    void applyBrushHardnessFromShortcut(float hardness);
    void adjustBrushSizeByStep(int direction);
    void adjustBrushHardnessByStep(int direction);
    bool handleBrushShortcutKey(QKeyEvent* event);
    bool beginBrushAdjustDrag(QPointF screenPos);
    void updateBrushAdjustDrag(QPointF globalPos);
    void finishBrushAdjustDrag(bool cancel);
    void beginCloneStampStroke(const BrushInputState& input, QPointF screenPos,
                               QPointF documentPos);
    void continueCloneStampStroke(const BrushInputState& input, QPointF screenPos,
                                  QPointF documentPos);
    bool canPaintActiveRasterLayer() const;

    // Centralized entry gate for the pixel-painting tools (Brush/Eraser, Fill
    // Bucket, Gradient). Confirms the canvas is in an editable state and that a
    // paint target exists; when NO layer is selected it auto-creates a
    // transparent, document-sized pixel layer and selects it, opening a grouped
    // undo step named `undoName` so the layer creation folds into the same entry
    // as the paint action (close it with m_controller->history().endMacro()).
    // Returns false when the action must be aborted (no document, a blocking mode
    // such as crop/transform/text-edit, etc.). When a layer is already selected
    // it returns true without creating anything — each tool's own locks /
    // rasterization rules then decide whether the action proceeds.
    bool ensurePaintTargetLayer(const QString& undoName);

    // The Clone Stamp tool (in either Clone or Healing sub-mode) shares one set
    // of source/stroke code paths; this gates them.
    bool isCloneOrHealingTool() const {
        return m_currentTool == Tool::CloneStamp;
    }
    QImage cloneSourceSnapshot() const;
    QImage clonePreviewSourceSnapshot();
    QImage cloneStampPreviewImage(QPointF destinationScreen,
                                  QPointF destinationDoc,
                                  QPointF sourceDoc,
                                  const QImage& sourceImage);
    QImage brushTipPreviewImage(int side) const;
    // Silhouette brush cursor: trace the tip silhouette ONCE into a normalized unit
    // path (cached, keyed by the tip), then only QTransform it per hover frame — the
    // same compute-once/transform-per-frame strategy as KisBrush::outline().
    QPainterPath brushOutlineUnitPath() const;
    QPainterPath brushOutlineScreenPath(const QPointF& centerScreen) const;
    void invalidateClonePreviewCache();
    void updateBrushPreviewOverlay();
    void onAirbrushTick();
    void setViewportCursor(const QCursor& cursor);
    // Sets the hover cursor for the effective tool. Some tools flip their cursor on a
    // held modifier without swapping tools (clone/healing Alt → source crosshair), so
    // the cursor depends on the modifier state. The no-arg form reads the live state;
    // the explicit form lets callers pass a settled state (key event filter / focus
    // loss) where the live state is unreliable.
    void updateToolCursor();
    void updateToolCursor(Qt::KeyboardModifiers mods);
    void syncTabletViewportCursor();
    void releaseTabletViewportCursor();
    void updateCanvasRect();
    // Clamps m_doc->panOffset (stored in viewport NDC) to the limits computed by
    // ViewportCamera::clampPan. Single enforcement point: called after every
    // pan/zoom/fit/resize/overscroll change and once per frame in paintGL().
    void clampPanOffset();
    // Viewport + on-screen canvas size in screen pixels. Returns false when the
    // widget/document is not measurable yet (so callers can no-op).
    bool viewportCanvasMetrics(double& vw, double& vh,
                               double& cw, double& ch) const;
    // Syncs the overlay scrollbars (range/value/visibility/geometry) to the
    // current pan/zoom using the same ViewportCamera::panBounds() as the clamp,
    // so the Hand tool and the bars can never disagree.
    void updateScrollBars();
    // Applies a scrollbar position back to panOffset (then re-clamps).
    void onScrollBarMoved();
    void updateSelectionAnimation();
    void setupLayerFBO(Layer* layer);
    void expandLayer(QPointF imagePos);
    // Grow a (flattened) raster layer's cpuImage so it spans requiredLayerRect
    // (in layer-pixel coords). Adjusts the node transform to keep the visual
    // position, grows the mask, and re-syncs GPU resources — same bookkeeping as
    // expandLayer(). Reports the pixel offset existing content shifted by, so the
    // caller can remap coordinates. Returns true if the layer actually grew.
    bool expandLayerToRect(LayerTreeNode* node, Layer* layer,
                           const QRect& requiredLayerRect, QPoint* outOffset);
    // Keep a tiled (rasterStorage) layer's mask locked to the exact layer bounds.
    // Called after a raster layer stroke so the mask follows tile-bound changes
    // without accumulating an independent extent.
    void growMaskToTiledLayer(Layer* layer);
    QPointF screenToDocument(QPointF screenPos);
    void commitTextEdit();
    void rerenderTextLayer();
    void fitTextLayerTransformToImage(LayerTreeNode* node);
    void resizeParagraphTextBoxToTransform(LayerTreeNode* node, bool editing);
    int textLayerCharAt(LayerTreeNode* node, QPointF screenPos);
    TextLayerData* activeTextData();
    bool activeContentEditable() const;
    // Selection-aware character styling (applies to the active selection /
    // insertion point while editing, or the whole layer otherwise).
    void applyTextCharStyle(const std::function<void(TextSpan&)>& fn);
    // Paragraph styling (applies to paragraphs touched by the selection /
    // caret while editing, or all paragraphs otherwise).
    void applyTextParagraphStyle(const std::function<void(ParagraphStyle&)>& fn);
    QRect doQuickSelectDab(const QPointF& docPos);  // returns the touched doc rect
    // Pixel source for quick select / magic wand / magnetic lasso reads:
    // rasterStorage layers keep the truth in tiles, so cpuImage is stale.
    QImage quickToolSourceImage(Layer* layer) const;
    // Pushes a SelectionCommand for a canvas selection gesture (rect/ellipse/
    // lassos/quick select) from the snapshot taken at mouse press; no-ops when
    // the selection did not actually change.
    void pushSelectionGestureUndo(const char* name);
    // Re-projects the in-progress drag overlay after a mid-gesture zoom/pan.
    void reprojectSelectionDragOverlay();
    // ── Polygonal lasso helpers ──
    void polyLassoMousePress(QMouseEvent* e);
    void finalizePolyLasso();
    void cancelPolyLasso();
    void updatePolyLassoOverlay();
    void updateMagneticEdgeMap();
    QPointF documentToLayerImage(QPointF docPos, Layer* layer) const;
    QPointF layerImageToDocument(QPointF imagePos, Layer* layer) const;
    QPointF snapToEdge(QPointF docPos);
    void bakeTextLayerResolution(LayerTreeNode* node);
    // TODO - review
    // Scans |groupNode|'s descendants for Point text and Shape layers, bakes
    // them at the current world scale, and appends TextEditCommand /
    // ModifyShapeCommand entries to |composite|.  Returns true if any child
    // was actually baked (i.e. needed a re-render).
    bool bakeGroupVectorChildrenToComposite(LayerTreeNode* groupNode,
                                            const QString& cmdName,
                                            class CompositeCommand& composite);
    // Bakes a single directly-selected Point-text or Shape node at its current
    // world scale and appends the matching TextEditCommand / ModifyShapeCommand
    // to |composite| (storing |beforeTransform| as the undo transform). Emits
    // layerChanged so the options bar follows the recalculated values. Returns
    // true if the node was a vector layer that got baked.
    bool bakeVectorNodeToComposite(LayerTreeNode* node, int flatIndex,
                                   const QTransform& beforeTransform,
                                   const QString& cmdName,
                                   class CompositeCommand& composite);
    void doAlignLayer(int alignmentType);
    void fillBucket(QPointF screenPos);
    void eyedropperSample(QPointF screenPos, Qt::MouseButton button, Qt::KeyboardModifiers modifiers);
    QColor eyedropperSampleColor(QPointF screenPos);
    bool isInRotateZone(const QPolygonF& cornersScreenNdc, QPointF screenPos) const;
    QPointF constrainedGradientPoint(QPointF start, QPointF current,
                                     Qt::KeyboardModifiers modifiers) const;
    void beginGradientDrag(QPointF screenPos);
    void updateGradientDrag(QPointF screenPos, Qt::KeyboardModifiers modifiers);
    void endGradientDrag(QPointF screenPos, Qt::KeyboardModifiers modifiers);
    void cancelGradientDrag();
    LayerTreeNode* freeTransformNode() const;
    void captureMultiResizeStartTransforms();
    std::vector<int> collectTransformableSelectedIndices(bool requireLayer);
    void invalidateMultiOutlineCache();
    std::vector<int> multiSelectionSignature() const;
    bool multiOutlineMatchesSelection() const;
    // Recomputes the cached multi-selection frame (AABB of every transformable
    // participant, groups included). Returns false (cache invalid) when the
    // selection has no transformable participant.
    bool computeMultiSelectionBbox();
    // Group transform (single active group reuses the multi-select bbox engine):
    bool activeIsSingleGroup() const;
    // The group's visual frame, ALWAYS fresh (no cache): the AABB of its leaf
    // descendants computed in the group's LOCAL space, then mapped by the group's
    // accumulatedTransform(). So the frame rotates/scales with the group (like a
    // single layer's frame) and follows any change to the children. *ok is set
    // false when the group has no visible leaf (or a degenerate transform).
    QTransform groupFrameTransform(const LayerTreeNode* group, bool* ok = nullptr) const;
    // World-NDC corners of groupFrameTransform(); empty when not valid.
    QPolygonF groupUnionCorners(const LayerTreeNode* group) const;
    void updateRulerGuideOverlay();
    QPointF canvasToRulerOverlayPoint(QPointF canvasPos) const;
    bool beginGuideInteraction(QPointF screenPos);
    void updateGuideInteraction(QPointF screenPos);
    void finishGuideInteraction(QPointF screenPos);
    bool updateGuideHover(QPointF screenPos);
    QRect cachedLayerVisiblePixelBounds(const Layer* layer) const;
    QPolygonF layerCanvasSnapCorners(const LayerTreeNode* node) const;
    QRectF nodeDocumentBounds(const LayerTreeNode* node) const;
    QRectF currentTransformDocumentBounds() const;
    QPointF documentDeltaFromScreenDelta(QPointF screenDelta) const;
    QTransform transformWithDocumentDelta(const LayerTreeNode* node,
                                          const QTransform& startTransform,
                                          QPointF deltaDoc) const;
    SnapContext currentSnapContext(const RulerGuideSettings& settings) const;
    void prepareSnapMoveBounds();
    bool applySnapMoveFromStart(QPointF currentMouseScreen,
                                Qt::KeyboardModifiers modifiers);
    void translateNodeByDocumentDelta(LayerTreeNode* node, QPointF deltaDoc);
    void applySnapForCurrentTransform(Qt::KeyboardModifiers modifiers);
    void clearSnapFeedback();
    void clearSnapBoundsCache();

    Document* m_doc;
    ImageController* m_controller = nullptr;
    // selectedTool — the real toolbar tool (see currentTool()/selectedTool()).
    Tool m_currentTool = Tool::Move;
    // temporaryOverrideTool — a tool activated for as long as a contextual
    // shortcut (modifier/key) is held (spring-loaded override). While set, the canvas
    // dispatches input and draws the cursor for THIS tool, but the toolbar,
    // options bar, command history and persistent tool state stay on the
    // selected tool. Resolved centrally by resolveTemporaryOverride() so the
    // rule lives in one place instead of being scattered across the tools.
    std::optional<Tool> m_temporaryOverrideTool;
    // Computes which temporary override (if any) the given modifiers imply for the
    // selected tool. The single source of truth for contextual shortcuts.
    std::optional<Tool> resolveTemporaryOverride(Qt::KeyboardModifiers mods) const;
    // Recomputes the override and, if it changed, refreshes the cursor/preview.
    // Never touches toolbar/history/options bar and never emits toolChanged. Call on
    // modifier key transitions and focus changes. The no-arg form reads the live
    // modifier state; the explicit form is used by the app-wide key event filter,
    // which derives the post-transition state from the event itself (the live state
    // is not yet settled while a modifier's own press/release is being filtered).
    void refreshTemporaryOverride();
    void refreshTemporaryOverride(Qt::KeyboardModifiers mods);
    // Force-disarms every spring-loaded / modifier-held interaction (contextual
    // override + Space pan) and snaps the cursor back to the selected tool. Used on
    // focus loss, where the key-release that would normally disarm them never lands.
    void disarmHeldModifierState();
    QCursor m_cursors[16];
    QCursor m_selectCursors[7]; // indexed by SelectType (0..6)

    bool m_panning = false;
    // Spring-loaded temporary Hand: Space held arms a pan-on-drag without
    // switching the active tool (so an in-progress crop/transform/text session
    // is preserved). Released in keyReleaseEvent.
    bool m_spacePanActive = false;
    bool m_rightButtonDown = false;
    QPointF m_rightPressPos;
    bool m_moving = false;
    QPointF m_lastMousePos;
    QPointF m_lastScreenPos;
    TransformState m_transformState;

    bool m_boxSelecting = false;
    QPointF m_boxSelectStart;
    QPointF m_boxSelectCurrent;
    std::vector<int> m_multiMoveIndices;
    std::vector<QTransform> m_multiMoveStartTransforms;
    std::vector<int> m_multiResizeIndices;
    std::vector<QTransform> m_multiResizeStartTransforms;
    float m_multiResizeGroupBboxHw = 0.0f;
    float m_multiResizeGroupBboxHh = 0.0f;
    QPointF m_multiResizeGroupBboxCenter{0.0f, 0.0f};
    float m_multiResizeGroupBboxRotation = 0.0f;
    bool m_multiResizeGroupBboxValid = false;
    std::vector<int> m_multiResizeGroupSelection;
    // Parent-accumulated transform of each multi-resize participant, captured
    // at gesture start: the world delta M is applied as
    // local' = startLocal * P * M * P^{-1}.
    std::vector<QTransform> m_multiResizeStartParentAccums;
    float m_multiResizeGroupStartHwVis = 0.0f;
    float m_multiResizeGroupStartHhVis = 0.0f;
    QPointF m_multiResizeGroupStartCenterVis{0.0f, 0.0f};
    // Captured at gesture start when resizing/rotating a single group: the
    // group's own local transform (G_start) and its parent-accumulated transform
    // (P), used by the conjugation G_new = G_start * P * M * P^{-1}.
    QTransform m_groupTransformStart;
    QTransform m_groupParentAccum;
    QElapsedTimer m_clickTimer;

    RulerGuideOverlay* m_rulerGuideOverlay = nullptr;
    bool m_guideDragging = false;
    bool m_guideCreating = false;
    GuideOrientation m_guideDragOrientation = GuideOrientation::Vertical;
    int m_guideDragIndex = -1;
    qreal m_guideDragStartPosition = 0.0;
    qreal m_guideDragCurrentPosition = 0.0;
    mutable std::unordered_map<const Layer*, QRect> m_snapVisiblePixelBoundsCache;
    std::vector<int> m_snapMoveIndices;
    std::vector<QTransform> m_snapMoveStartTransforms;
    QRectF m_snapMoveStartDocumentBounds;
    bool m_snapMoveStartDocumentBoundsValid = false;
    // Document bounds captured when a resize starts; used to detect which bbox
    // edges actually move (the rest are the fixed anchor) so resize snap only
    // pulls the moving edges.
    QRectF m_resizeStartDocBounds;

    bool m_brushDrawing = false;
    QPointF m_brushScreenLastPos;
    QPointF m_lastBrushImagePos;
    QImage m_brushBeforeImage;
    QPoint m_brushBeforeOrigin;
    QImage m_maskStrokeBeforeCpu;
    QTransform m_maskStrokeBeforeTransform;
    bool m_brushUsingRasterTiles = false;
    BrushSettings m_brushSettings;
    // Cached normalized brush silhouette + its cache key (tip identity / shape).
    // mutable: rebuilt lazily from the const cursor path.
    mutable QPainterPath m_brushOutlineUnit;
    mutable qint64 m_brushOutlineKey = -1;
    BrushEngine m_brushEngine;
    BrushRenderer m_brushRenderer;
    bool m_brushAdjustDragging = false;
    QPointF m_brushAdjustStartScreen;
    QPointF m_brushAdjustCurrentScreen;
    QPointF m_brushAdjustAccumulatedDelta;
    QPoint m_brushAdjustLockGlobalPos;
    float m_brushAdjustStartSize = 20.0f;
    float m_brushAdjustStartHardness = 0.8f;
    bool m_cloneSourceDefined = false;
    QPointF m_cloneSourcePoint;
    bool m_cloneAligned = true;
    bool m_cloneOffsetValid = false;
    QPointF m_cloneSourceOffset;
    CloneSampleMode m_cloneSampleMode = CloneSampleMode::CurrentAndBelow;
    CloneStampContext m_cloneStrokeContext;
    QPointF m_cloneCurrentSourcePoint;
    bool m_cloneNeedsSourceFeedback = false;
    QImage m_clonePreviewSourceImage;
    uint64_t m_clonePreviewSourceGeneration = std::numeric_limits<uint64_t>::max();
    int m_clonePreviewSourceFlatIndex = -1;
    CloneSampleMode m_clonePreviewSourceMode = CloneSampleMode::CurrentAndBelow;
    // Per-event cost bound for the clone source preview: the rendered preview is
    // reused between refreshes (position still follows every event) and the
    // scaled tip mask is cached per tip/size.
    QElapsedTimer m_clonePreviewThrottle;
    QImage m_clonePreviewImage;
    QImage m_cloneTipPreviewScaled;
    int m_cloneTipPreviewSide = -1;
    qint64 m_cloneTipPreviewKey = 0;
    bool m_stampHealing = false;          // Clone Stamp sub-mode: false=Clone, true=Healing
    float m_healingDiffusion = 0.5f;
    QElapsedTimer m_brushStrokeTimer;
    // Unified pointer state: tracks the active device + suppresses the synthetic
    // QMouseEvent the OS emits alongside each QTabletEvent. Replaces the old ad-hoc
    // m_stylusActive guard.
    CanvasInputState m_inputState;
    bool m_inputDebug = false;     // HAZOR_INPUT_DEBUG=1 enables per-event logging
    // Caps Lock toggles brush-based tools from the circular size cursor to a
    // precise crosshair. Tracked by toggling on each
    // Key_CapsLock press the OS delivers while the canvas has focus.
    bool m_capsLockActive = false;
    bool m_tabletViewportCursorOverride = false;
    // True while we re-enter a mouse handler with a QMouseEvent we synthesised from
    // a pen event. Lets those handlers tell our own synthesis from the OS's own
    // (duplicate) synthetic mouse, which we drop.
    bool m_synthesizingMouseFromTablet = false;
    QPointF m_airbrushLastPos;
    // Last full pen sample of the active stroke, so a timed airbrush dab paints
    // with the pressure/tilt the pen is holding right now (not a neutral state).
    BrushInputState m_airbrushLastState;
    QTimer* m_airbrushTimer = nullptr;
    int m_prevViewport[4] = {};

    QPointF m_canvasHalfExtents{1.0f, 1.0f};

    // Cached copy of the global Viewport/overscrollEnabled preference, refreshed
    // in the constructor and via setOverscrollEnabled() so paintGL() never hits
    // QSettings per frame.
    bool m_overscrollEnabled = true;

    // Overlay scrollbars (children of the canvas, hugging the right/bottom edge).
    // m_updatingScrollBars guards the valueChanged → onScrollBarMoved feedback
    // loop while updateScrollBars() pushes values into them.
    QScrollBar* m_hScrollBar = nullptr;
    QScrollBar* m_vScrollBar = nullptr;
    bool m_updatingScrollBars = false;

    // ── Selection state ──
    SelectType m_selectType = SelectType::Rectangular;
    SelectMode m_selectMode = SelectMode::Replace;
    bool m_selectAntiAlias = false;
    QPointF m_selectStart;
    QPointF m_selectCurrent;
    bool m_selectDragging = false;
    bool m_movingSelection = false;
    QPointF m_selectMoveStart;
    QImage m_selectMoveBefore;
    QPoint m_selectMoveCurrentDelta{0, 0};
    QRectF m_selectMoveBoundsBefore;
    bool m_transformingSelection = false;
    QImage m_selectTransformBefore;
    TransformState m_selectTransformState;
    QElapsedTimer m_antTimer;
    QTimer* m_selectAnimTimer = nullptr;

    bool m_quickSelecting = false;
    QImage m_quickSelectSrcImage;
    // Intersect-mode gesture: snapshot of the selection at press; dabs reveal
    // these values instead of writing 255 (Shift+Alt+drag).
    QImage m_quickSelectOriginal;
    float m_quickSelectTolerance = 32.0f;

    // Snapshot taken at gesture start for the SelectionCommand pushed when a
    // canvas selection gesture commits.
    QImage m_selectGestureBefore;
    bool m_selectGestureBeforeActive = false;

    std::vector<QPointF> m_lassoPoints;
    bool m_lassoDrawing = false;
    bool m_lassoCanClose = false;

    // ── Polygonal lasso state ──
    // Vertices are stored in document/image coordinates so zoom/pan never alter
    // the saved geometry; screen coordinates are used only for hit-testing the
    // first vertex (close) and for drawing the overlay preview.
    std::vector<QPointF> m_polyLassoPoints;
    QPointF m_polyLassoPreviewDoc;
    SelectMode m_polyLassoMode = SelectMode::Replace;
    bool m_polyLassoDrawing = false;
    bool m_polyLassoCanClose = false;
    bool m_magneticEdgeReady = false;
    int m_magneticEdgeW = 0;
    int m_magneticEdgeH = 0;
    std::vector<float> m_magneticEdgeMap;
    float m_magneticSearchRadius = 12.0f;

    SelectionDragOverlay* m_selectDragOverlay = nullptr;

    // ── AI Object Selection tool state (Tool::AiSelect) ──
    // A click selects the object under the cursor; a drag uses the rectangle as a
    // box prompt. The heavy SAM inference runs async via the controller; the
    // canvas only collects the gesture and reuses the selection drag overlay for
    // the box feedback.
    AiObjectSelectionController* m_aiSelectController = nullptr;
    AiRemoveObjectController* m_aiRemoveController = nullptr;
    bool m_aiBoxDragging = false;
    bool m_aiMoved = false;
    QPointF m_aiPressScreen;
    QPointF m_aiPressDoc;
    void aiSelectMousePress(QMouseEvent* e);
    void aiSelectMouseMove(QMouseEvent* e);
    void aiSelectMouseRelease(QMouseEvent* e);
    AiSelectionOperation aiResolveOperation(Qt::KeyboardModifiers mods) const;

    // ── AI Remove tool state (Tool::AiRemove) ──
    std::vector<QPointF> m_aiRemoveLassoPoints;
    bool m_aiRemoveLassoDrawing = false;
    void aiRemoveMousePress(QMouseEvent* e);
    void aiRemoveMouseMove(QMouseEvent* e);
    void aiRemoveMouseRelease(QMouseEvent* e);
    void cancelAiRemoveLasso();
    void finishAiRemoveLasso();

    bool m_quickMaskMode = false;
    bool m_quickMaskDabbed = false;
    QImage m_quickMaskBefore;

    bool m_hasPreview = false;
    QImage m_previewImage;
    unsigned int m_previewTexture = 0;
    processing::PreviewRenderer* m_previewRenderer = nullptr;
    LayerTreeNode* m_previewSourceNode = nullptr;

    bool m_editingMask = false;
    bool m_grayscaleMaskView = false;
    bool m_pendingFit = false;  // fitToView() requested before a valid size
    bool m_settingEditingMask = false;
    bool m_externalDragActive = false;
    bool m_freeTransformActive = false;
    bool m_freeTransformDirty = false;
    int m_freeTransformFlatIndex = -1;
    QTransform m_freeTransformOriginal;
    // Session-wide capture: every node touched by a gesture during the free
    // transform session, with its FIRST-seen transform. Gestures inside the
    // session never push history; commit consolidates exactly one entry from
    // these starts, and cancel restores them.
    std::vector<int> m_freeTransformSessionIndices;
    std::vector<QTransform> m_freeTransformSessionStartTransforms;

    // ── Distort / Perspective state ──
    // The quad is stored in document-pixel coordinates so zoom/pan never change
    // the geometry. Pixels are baked only on commit (OpenCV warpPerspective);
    // during the drag a projective overlay previews the warp.
    DistortPreviewOverlay* m_distortOverlay = nullptr;
    BrushPreviewOverlay* m_brushPreviewOverlay = nullptr;
    GradientDragOverlay* m_gradientOverlay = nullptr;
    bool m_distortActive = false;
    bool m_distortDirty = false;
    int m_distortFlatIndex = -1;
    TransformMode m_distortMode = TransformMode::Distort;
    TransformQuad m_distortQuad;       // current editable quad (document px)
    TransformQuad m_distortDragStartQuad;
    TransformQuad m_distortLastValidQuad;
    int m_distortDragCorner = -1;
    QPointF m_distortDragStartCorner;
    bool m_distortAxisLocked = false;
    bool m_distortAxisHorizontal = true;
    QTransform m_distortOriginalTransform;
    // Pre-session snapshots (state when distort was entered) — used by cancel and
    // to detect whether the session changed anything at commit.
    QImage m_distortBeforeImage;
    std::shared_ptr<DistortData> m_distortBeforeData;
    // Per-drag snapshots (state at the start of the current handle drag) so each
    // handle movement becomes its own undo step, pushed on release.
    QImage m_distortDragBeforeImage;
    std::shared_ptr<DistortData> m_distortDragBeforeData;
    QTransform m_distortDragBeforeTransform;
    int m_distortSessionSteps = 0; // undo entries pushed during this session
    void distortMousePress(QMouseEvent* e);
    void distortMouseMove(QMouseEvent* e);
    void distortMouseRelease(QMouseEvent* e);
    void updateDistortOverlay();
    QPolygonF distortQuadToScreen() const;
    void renderDistortLayer(bool highQuality);
    // On-screen LOD scale for the live-drag warp (≤1.0): render no more pixels
    // than the viewport shows so distorting large layers stays responsive.
    double distortPreviewScale(const QRect& footprintDoc) const;

    bool m_autoSelect = true;
    bool m_autoSelectGroup = false;
    bool m_showTransformControls = true;
    int m_alignTarget = 0; // 0=Canvas, 1=Selection

    TextToolState m_textToolState = TextToolState::Idle;
    TextEditorController m_textEditor;
    int m_textLayerIndex = -1;
    int m_textDragStart = -1;
    TextLayerData m_textBeforeSnapshot;
    QTransform m_textBeforeTransform;
    QPointF m_textCreateStart;
    bool m_textCreatingBox = false;

    // ── Fill Bucket state ──
    int m_fillBucketTolerance = 32;
    QColor m_fillBucketColor = Qt::black;

    // ── Curves editor image-pick state (0 = disarmed) ──
    int m_curvesPickMode = 0;
    bool m_curvesTargetDragging = false;
    double m_curvesTargetStartY = 0.0;

    // ── Hue/Saturation editor eyedropper state ──
    // 0 = disarmed, 1 = main (recenter range, one-shot click), 2 = add range,
    // 3 = subtract range. Add/Subtract support click-drag to sample many hues.
    int m_hueSatPickMode = 0;
    bool m_hueSatDragging = false;
    // True while an adjustment layer is being live-dragged in the Properties panel.
    bool m_adjustmentLiveEdit = false;

    // ── Gradient tool state ──
    GradientDefinition m_gradientDefinition;
    BlendMode m_gradientBlendMode = BlendMode::Normal;
    float m_gradientOpacity = 1.0f;
    bool m_gradientDragging = false;
    QPointF m_gradientStartImage;
    QPointF m_gradientCurrentImage;
    QPointF m_gradientStartScreen;
    QPointF m_gradientCurrentScreen;

    // ── Eyedropper tool state ──
    SampleMode m_eyedropperSampleMode = SampleMode::Composite;
    SampleSize m_eyedropperSampleSize = SampleSize::Point;
    bool m_eyedropperHovering = false;
    QColor m_eyedropperHoverColor;
    QPointF m_eyedropperScreenPos;
    QLabel* m_eyedropperOverlay = nullptr;
    QLabel* m_transformOverlay = nullptr;

    // ── Crop tool state ──
    enum class CropHandleId { None, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left, Inside };
    enum class CropGuideType { None, RuleOfThirds, GoldenRatio, Grid };
    QRectF m_cropRect{ -1.0f, -1.0f, 2.0f, 2.0f };
    bool m_cropActive = false;
    bool m_cropDragging = false;
    CropHandleId m_cropHandle = CropHandleId::None;
    QPointF m_cropDragStart;
    QRectF m_cropDragOrigRect;
    QSizeF m_cropLockedRatio{ 0.0f, 0.0f };
    CropGuideType m_cropGuideType = CropGuideType::RuleOfThirds;
    float m_cropOverlayOpacity = 0.40f;
    float m_cropRotateAngle = 0.0f;
    bool m_cropRotating = false;

    void resetCropToCanvas();
    void commitCrop();
    CropHandleId cropHandleAtScreen(QPointF screenPos) const;
    QCursor cropCursorForHandle(CropHandleId handle) const;

    // ── Shape tool state ──
    void beginShapeDrag(QPointF ndcStart);
    void updateShapeDrag(QPointF ndcCurrent, Qt::KeyboardModifiers mods);
    void endShapeDrag();
    void cancelShapeDrag();
    void renderShapePreview(QPainter& p, const QMatrix4x4& vm, float zoom);

    bool m_shapeDragging = false;
    QPointF m_shapeStartNdc;
    QPointF m_shapeCurrentNdc;
    ShapeData m_shapePreviewData;
    ShapePreviewOverlay* m_shapePreviewOverlay = nullptr;
    ShapeData m_customShapeTemplate;
    QString m_customShapeError;
    bool m_customShapeTemplateValid = false;
    // shape tool options (synced from options bar)
    ShapeToolMode m_shapeToolType = ShapeToolMode::Rectangle;
    QColor m_shapeFillColor{200, 200, 200, 255};
    bool m_shapeFillEnabled = true;
    QColor m_shapeStrokeColor{0, 0, 0, 255};
    bool m_shapeStrokeEnabled = true;
    float m_shapeStrokeWidth = 0.002f;
    float m_shapeOpacity = 1.0f;
    float m_shapeCornerRadius = 0.0f;
    int m_shapeSides = 6;
    bool m_shapeAntiAlias = true;

    GPUViewport* m_gpuViewport = nullptr;
};
