#pragma once

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QColor>
#include <QSize>
#include <QStringList>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <vector>
#include <atomic>
#include "core/Document.hpp"
#include "core/Clipboard.hpp"
#include "core/LayerEffect.hpp"
#include "controller/CommandHistory.hpp"
#include "animation/LayerPropertyController.hpp"
#include "animation/PlaybackController.hpp"
#include "text/TextTypes.hpp"
#include "core/ShapeTypes.hpp"
#include "tools/ToolCall.hpp"
#include "async/AsyncJob.hpp"
#include "agent/AgentConfig.hpp"
#include "color/ColorProfile.hpp"
#include "ai/AiRemoveTypes.hpp"
#include "ai/upscale/UpscaleTypes.hpp"

namespace cv { class Mat; }
namespace nlohmann { class json; }
class ImageGenProvider;
class UpscaleService;
struct InpaintResult;
struct InpaintError;
struct GradientApplication;

enum class ResizeInterpolation {
    Nearest,
    Bilinear,
    Bicubic,
    BicubicAutomatic,
    Lanczos
};

enum class CanvasAnchor {
    TopLeft,
    TopCenter,
    TopRight,
    MiddleLeft,
    Center,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight
};

struct ImageResizeOptions {
    QSize targetSize;
    bool resampleImage = true;
    bool scaleStyles = true;
    ResizeInterpolation interpolation = ResizeInterpolation::BicubicAutomatic;
    bool updateResolution = false;
    double resolutionDpi = 300.0;
};

struct CanvasResizeOptions {
    QSize targetSize;
    CanvasAnchor anchor = CanvasAnchor::Center;
    QColor extensionColor = Qt::transparent;
    bool fillExtension = false;
};

class ImageController : public QObject {
    Q_OBJECT

public:
    explicit ImageController(QObject* parent = nullptr);
    ~ImageController() override;

    void setDocument(Document* doc);
    Document* document() const { return m_doc; }
    CommandHistory& history() { return m_history; }

    Layer* activeLayer() const;
    int activeLayerIndex() const { return m_doc ? m_doc->activeFlatIndex : -1; }
    void deselectActive() { setActiveNode(-1); }

    // ── Animation ─────────────────────────────────────────────
    int currentFrame() const { return m_doc ? m_doc->currentFrame() : 0; }
    // Auto-key: when on, editing an animatable property records a keyframe at the
    // current frame instead of changing its static base value. The timeline
    // toolbar toggles this (single source of truth for the whole app).
    bool autoKey() const { return m_propertyController.autoKey(); }
    void setAutoKey(bool on) { m_propertyController.setAutoKey(on); }
    anim::PlaybackController* playbackController() { return &m_playbackController; }
    const anim::PlaybackController* playbackController() const { return &m_playbackController; }
    // Central entry point for frame navigation (scrubbing, playback, timeline).
    // Delegates to Document::setCurrentFrame (clamp + evaluate) and drives the
    // existing render flow: emits currentFrameChanged for the UI and imageChanged
    // (GPU sync + repaint) only when the evaluated frame actually changed the
    // composite. Frame navigation is view state — it is NOT an undo command.
    void setCurrentFrame(int frame);
    void previewNodeTransform(LayerTreeNode* node, const QTransform& transform);

    // Ensure the active raster-animation exposure owns writable cel content.
    // Static layers are unchanged. Shared duplicated cels detach on an explicit
    // key before pixel tools write.
    bool prepareRasterCelForEdit(LayerTreeNode* node = nullptr);
    bool createRasterCel(bool duplicateCurrent = false);
    bool createEmptyRasterFrame();
    bool removeRasterCelKeyframe();
    bool moveRasterCelKeyframe(int targetFrame);
    bool pasteRasterCel(const std::optional<anim::RasterCelContent>& content);

    void newLayer();
    void newGroup();
    // Dissolves the selected Group node(s) (or the active node when it is a
    // group), promoting their children into the group's parent at the same
    // visual slot. One atomic LayerTreeStateCommand so undo restores the group.
    void ungroupSelectedNodes();
    // Creates an adjustment layer (adjustmentType is an id from
    // adjustments::registry(), e.g. "grayscale") above the active node, or at
    // the top of the stack when nothing is selected. Names follow the
    // incremental pattern "Grayscale", "Grayscale 2", ...
    // `initialParams` seeds the adjustment payload (e.g. the chosen colour for a
    // "solidcolor" fill layer); empty means the adjustment's own defaults.
    void addAdjustmentLayer(const QString& adjustmentType,
                            const QVariantMap& initialParams = {});
    // Single Layer Mode: nests an adjustment under a Pixel/Text/Shape layer so
    // it only affects that layer's render; the adjustment's mask is re-mapped
    // from document space into the target layer's pixel space.
    void moveAdjustmentToLayer(int adjFlatIndex, int targetLayerFlatIndex);
    // Back to Normal Mode: detaches the adjustment from its parent layer and
    // re-inserts it into the stack at insertFlatIndex (mask re-mapped to
    // document space).
    void moveAdjustmentToStack(int adjFlatIndex, int insertFlatIndex);

    // ── Adjustment params editing (Curves editor) ──
    // Live, undo-less param update while dragging: replaces the node's
    // adjustment params, invalidates the cache and repaints. The editor pushes a
    // single AdjustmentParamsCommand on gesture commit.
    void updateAdjustmentParamsLive(int flatIndex, const QVariantMap& params);
    // Commit one history step: `before`/`after` are full param maps captured by
    // the editor around the gesture. Applies `after` and pushes the command.
    void commitAdjustmentParams(int flatIndex, const QVariantMap& before,
                                const QVariantMap& after, const QString& label);
    // Brackets a live adjustment-param drag so the canvas renders through the GPU
    // per-layer compositor (fast) instead of a full CPU projection recomposite
    // every frame. The editor calls true on gesture begin, false on commit.
    void setAdjustmentLiveEdit(bool active) { emit adjustmentLiveEditChanged(active); }
    void removeNode(int flatIndex);
    void duplicateNode(int flatIndex);
    void setActiveNode(int flatIndex);
    void setMultiSelectNode(int flatIndex, bool addToSelection);
    void selectNodeRange(int fromFlat, int toFlat);
    void selectAllNodes();
    void removeSelectedNodes();
    void setSelectedIndices(const std::set<int>& indices);
    std::vector<int> selectedIndices() const;
    void setNodeTransforms(const std::vector<int>& flatIndices,
                           const std::vector<QTransform>& newTransforms,
                           const std::vector<QTransform>& oldTransforms,
                           const QString& name = QString());
    int addGuide(GuideOrientation orientation, qreal position);
    void moveGuide(int index, qreal position, qreal oldPosition);
    void removeGuide(int index);
    void clearGuides();
    void setNodeOpacity(int flatIndex, float opacity);
    void beginNodeOpacity(int flatIndex);
    void previewNodeOpacity(int flatIndex, float opacity);
    void commitNodeOpacity(int flatIndex, float opacity);
    void setNodeVisibility(int flatIndex, bool visible);
    // Solo/isolate (Alt+Click eye): hides every node outside the target's branch
    // (ancestors + the target subtree) and force-shows its ancestors so it
    // composites; a second call restores the snapshot. Transient view state, not
    // pushed to undo — reversible by toggling again.
    void toggleSoloVisibility(int flatIndex);
    bool isSoloVisibilityActive() const { return m_soloActive; }
    void setNodeBlendMode(int flatIndex, BlendMode mode);
    // Editing locks. setNodesLockBit toggles one LockFlag bit across a
    // (multi-)selection to a consistent on/off state; setNodeLockFlags sets the
    // full flag set on a single node. Both record one undoable history step.
    void setNodesLockBit(const std::vector<int>& flatIndices, int flagBit, bool on);
    void setNodeLockFlags(int flatIndex, int newFlags);
    void previewLayerEffects(int flatIndex, const std::vector<LayerEffect>& effects);
    void commitLayerEffects(int flatIndex,
                            const std::vector<LayerEffect>& beforeEffects,
                            const std::vector<LayerEffect>& afterEffects);
    // Layer-style clipboard (non-destructive; routed through commitLayerEffects
    // so each carries undo + Lock-All gating).
    void copyLayerEffects(int flatIndex);       // store the node's effects
    void pasteLayerEffects();                    // apply to every selected node
    void clearLayerEffects(int flatIndex);       // remove all effects
    void toggleAllLayerEffects(int flatIndex);   // enable/disable the whole stack
    bool hasCopiedEffects() const { return !m_copiedEffects.empty(); }
    void reorderNode(int fromFlat, int toFlat);
    void moveNodeIntoGroup(int nodeFlatIndex, int groupFlatIndex);

    void setEditingMask(bool editing);
    bool isEditingMask() const { return m_editingMask; }

    // ── Mask overlay (rubylith) — per-document view state ──
    void setMaskOverlayVisible(bool visible);
    void toggleMaskOverlay();
    bool isMaskOverlayVisible() const;
    void setMaskOverlayOpacity(float opacity);   // 0..1
    float maskOverlayOpacity() const;
    void pushMaskEditSnapshot(const QString& name, int flatIndex, QImage before,
                              QPoint beforeOrigin = QPoint(0,0), QPoint afterOrigin = QPoint(0,0));

    void setLayerTransform(int flatIndex, const QTransform& transform,
                           const QTransform* oldTransform = nullptr);
    QPointF screenToImage(QPointF screenPos, Layer* layer,
                          float zoom, QPointF panOffset, QPointF canvasHalfExtents,
                          QSize viewportSize);

    void paintBrushDab(Layer* layer, QPointF imagePos, float radius,
                       float opacity, QColor color, bool eraser);
    void paintStroke(Layer* layer, QPointF from, QPointF to,
                     float radius, float opacity, QColor color, bool eraser);
    void expandLayer(Layer* layer, QPointF imagePos, float brushRadius,
                     int margin = 20);
    void syncLayerToGpu(Layer* layer);
    void syncLayerFromGpu(Layer* layer);
    void syncLayerMaskToGpu(Layer* layer);
    void syncLayerMaskFromGpu(Layer* layer);
    // revealAll=true fills the new mask white (shows everything); false fills it
    // black (hides everything) — the standard Alt+Click on the add-mask button.
    void addLayerMask(int flatIndex, bool revealAll = true);
    void removeLayerMask(int flatIndex);
    void applyLayerMask(int flatIndex);
    void setLayerMaskEnabled(int flatIndex, bool enabled);
    bool hasLayerMask(int flatIndex) const;
    bool isLayerMaskEnabled(int flatIndex) const;
    void toggleLayerMask(int flatIndex);
    void loadMaskToSelection(int flatIndex);
    // Loads the layer mask into the document selection with a boolean combine
    // rule (Add/Subtract/Intersect). Replace falls back to loadMaskToSelection.
    // Ctrl(+Shift/+Alt)+Click on a mask thumbnail — the conventional combine rule.
    void combineMaskToSelection(int flatIndex, SelectMode mode);
    void createMaskFromSelection(int flatIndex);
    void invertLayerMask(int flatIndex);
    void setMaskDensity(int flatIndex, float density);
    void setMaskFeather(int flatIndex, float radius);
    void beginMaskFeather(int flatIndex);
    void previewMaskFeather(int flatIndex, float radius);
    void commitMaskFeather(int flatIndex, float radius);
    void copyLayerMask(int flatIndex);
    void pasteLayerMask(int flatIndex);
    void clearLayerMask(int flatIndex);
    bool hasCopiedMask() const { return !m_copiedMask.isNull(); }

    void pushLayerSnapshot(const QString& name, int flatIndex, QImage before);
    void pushRasterTileSnapshot(const QString& name, int flatIndex,
                                std::vector<core::RasterTileChange> changes);
    void pushCommand(std::unique_ptr<Command> cmd);
    bool convertDocumentToProfile(const ColorProfile& destinationProfile,
                                  const ColorConversionOptions& options = {});
    void fillActiveLayer(const QColor& color);
    bool applyGradient(const GradientApplication& application);
    // Mask-target variant: renders the gradient as a grayscale ramp into the
    // active layer's mask (black hides / white reveals), clipped to the active
    // selection. Called by applyGradient when the mask is the edit target.
    bool applyGradientToMask(const GradientApplication& application);
    // Mask-target Fill (Paint Bucket): writes a grayscale value (color luminance)
    // into the active layer's mask. With an active selection it fills (feather-
    // aware) within it; otherwise it flood-fills from layerPos with tolerance.
    // layerPos is in layer-pixel coords; tolerance is 0..1.
    bool applyFillBucketToMask(const QPoint& layerPos, const QColor& color,
                               float tolerance);
    bool resizeImage(const ImageResizeOptions& options);
    bool resizeCanvas(const CanvasResizeOptions& options);
    bool resizeImageAsync(const ImageResizeOptions& options);
    bool resizeCanvasAsync(const CanvasResizeOptions& options);
    void resizeDocument(const QSize& newSize);
    void cropDocument(const QRect& cropRect);
    bool cropDocumentAsync(const QRect& cropRect);
    void mergeLayers(int srcFlat, int dstFlat);
    void mergeVisibleLayers();
    void flattenImage();
    bool mergeLayersAsync(int srcFlat, int dstFlat);
    bool mergeVisibleLayersAsync();
    bool flattenImageAsync();
    // Flat index of the layer Merge Down should fold the active layer into: the
    // next real Layer node below srcFlat, skipping srcFlat's own clipped
    // adjustment children. Returns -1 when there is no layer below.
    int mergeDownTargetFlat(int srcFlat) const;
    void rasterizeNode(int flatIndex);
    bool isLayerEmpty(int flatIndex) const;
    void flushGpuChanges();

    // ── Generative Fill (src/ai/) ──
    enum class GenFillMode { FillSelection, FillAsNewLayer };
    // Preset (kind==Generative) used to build the image-gen provider.
    void setGenerativePreset(const AgentConfig& preset);
    bool hasGenerativePreset() const { return m_hasGenerativePreset; }
    // Requires an active selection. Runs asynchronously; results are applied
    // when the provider finishes. Optional overrides default to the preset.
    void generativeFill(const QString& prompt, GenFillMode mode,
                        const QString& negativePrompt = QString(),
                        double strength = -1.0, int steps = -1, int seed = -2);
    bool applyAiRemoveResult(const AiRemoveApplyRequest& request);
    uint64_t upscale(const UpscaleOptions& options);
    UpscaleBackendStatus upscaleBackendStatus();

    using JsonMap = std::unordered_map<std::string, JsonValue>;
    bool executeTool(const std::string& toolName, const JsonMap& params);
    bool executeToolChain(const std::vector<std::pair<std::string, JsonMap>>& chain);
    void cancelLongOperation(uint64_t jobId);

    bool checkDestructiveOp(Layer* layer);
    bool checkMaskEditable(Layer* layer);
    // True when any visible layer is fully locked (Lock All) — used by the UI to
    // disable document-wide merge/flatten that the controller would refuse.
    bool anyFullyLockedVisibleLayer() const { return anyFullyLockedLayer(true); }

    void copy();
    void paste();
    bool hasClipboard() const;

    void createTextLayer(const QString& initialText, const TextBox& box,
                         QPointF canvasNdcPos, float fontSize = 32.0f,
                         const QColor& color = Qt::black);
    void updateTextLayer(int flatIndex, const TextLayerData& data);
    void applyTextStyle(int flatIndex, const TextSpan& style, bool toSelection);
    // Returns true when an undo entry was pushed; false when the text data is
    // unchanged (callers that also changed the transform must then commit the
    // transform themselves).
    bool commitTextEdit(int flatIndex, const TextLayerData& before,
                        const QTransform& beforeTransform = QTransform());
    bool importExternalImages(const QStringList& paths, const QPointF& dropCanvasNdc);
    bool importImage(const QImage& img, const QString& name);

    void createShapeLayer(const ShapeData& data);
    void modifyShapeLayer(int flatIndex, const ShapeData& newData);
    void bakeShapeTransform(int flatIndex, const QTransform& beforeTransform);
    // Re-render a shape descendant at its effective WORLD scale/rotation (parent
    // group chain included), keeping its on-screen appearance unchanged. Mutates
    // the node in place and does NOT push an undo command — the caller captures
    // before/after and bundles it (used when committing a group transform).
    bool bakeShapeLayerResolutionInPlace(int flatIndex);

    cv::Mat makeLayerMask(Layer* layer) const;
    // Projects the document selection into the layer's MASK pixel space (size =
    // maskImage, offset = maskOrigin). Returns 0..255 coverage (selection value)
    // or an empty Mat when there is no mask / selection. Mirrors the affine in
    // createMaskFromSelection; used to edit the mask through the active selection.
    cv::Mat selectionInMaskSpace(Layer* layer) const;
    // Per-pixel weighted blend dst = lerp(dst, src, mask/255). copyTo treats
    // any mask value ≥ 1 as 100%, which silently discards feathered edges
    // when applying filters/fill/flip through a selection.
    static void blendByMask(cv::Mat& dst, const cv::Mat& src, const cv::Mat& mask);
    // Selected pixels of `layer` baked into document space (rotation/shear
    // included). Used by copy/float on rotated layers, where mapping only the
    // AABB corner drops the rotation. Returns false when nothing is selected.
    bool extractSelectedDocRegion(Layer* layer, cv::Mat& outCropped,
                                  QPointF& outDocPos) const;
    // Multiplies the alpha of a layer-pixel-space CV_8UC4 image by the layer's
    // visible mask (maskImage × maskDensity, placed at maskOrigin). No-op when
    // the layer has no visible mask. Keeps copied pixels consistent with the
    // composited appearance, which hides masked-away regions.
    void applyLayerMaskToCvImage(Layer* layer, cv::Mat& rgbaLayerSpace) const;
    void markLayerDirty(Layer* layer, const QRect& rect = QRect());

    // History management
    void undo();
    void redo();
    void jumpToHistoryState(int targetIndex);
    void clearHistory();
    QStringList historyStateNames() const;

signals:
    void documentChanged();
    void layerChanged(int flatIndex);
    void activeLayerChanged(int flatIndex);
    void selectionChanged();
    void imageChanged();
    // Emitted when the current animation frame changes (for the timeline UI /
    // frame readouts). Carries the clamped frame actually in effect.
    void currentFrameChanged(int frame);
    void toolExecuted(const QString& toolName, bool success);
    void clipboardChanged();
    void historyChanged();
    // Toggles the canvas live-edit render path during an adjustment-param drag.
    void adjustmentLiveEditChanged(bool active);
    void maskEditingChanged(bool editing);
    void maskOverlayChanged();
    void guidesChanged();
    void operationBlocked(const QString& reason);
    void progressOperationStarted(uint64_t jobId, const QString& message, bool cancelable);
    void progressOperationMessageChanged(uint64_t jobId, const QString& message);
    void progressOperationProgressChanged(uint64_t jobId, int progress);
    void progressOperationFinished(uint64_t jobId);
    void cloneSourceRequested(QPointF documentPoint);
    void cloneSampleModeRequested(int mode);
    void cloneAlignedRequested(bool aligned);
    void cloneStrokeBeginRequested(QPointF documentPoint);
    void cloneStrokeUpdateRequested(QPointF documentPoint);
    void cloneStrokeEndRequested();
    void healingSourceRequested(QPointF documentPoint);
    void healingSampleModeRequested(int mode);
    void healingAlignedRequested(bool aligned);
    void healingDiffusionRequested(float diffusion);
    void healingStrokeBeginRequested(QPointF documentPoint);
    void healingStrokeUpdateRequested(QPointF documentPoint);
    void healingStrokeEndRequested();
    void progressOperationCanceled(uint64_t jobId);
    void progressOperationFailed(uint64_t jobId, const QString& error);
    void upscaleNewDocumentReady(QImage image, QString name,
                                 ColorProfile profile, ColorProfileSource source);

private:
    // Non-destructive flip of a node set as a single rigid area: reflects every
    // target about the union bounding-box centre (canvas NDC) so groups and
    // multi-selections mirror as one block (not each layer about its own
    // centre). Folds a world reflection into each node transform via
    // setNodeTransforms; position-locked nodes are skipped.
    void flipNodesAsUnit(const std::vector<int>& flatIndices, bool horizontal);

    void applyFilterWithSelection(Layer* layer, const std::string& toolName,
                                   const std::function<cv::Mat(const cv::Mat&)>& filter);
    void applyFilterTiled(Layer* layer, const std::string& toolName,
                          const JsonMap& params);
    bool canApplyTiled(Layer* layer, const std::string& toolName) const;
    bool runDocumentStateOperationAsync(const QString& progressMessage,
                                        const QString& undoName,
                                        std::function<bool(Document&)> operation);

    static bool isHeavyTool(const std::string& toolName);
    // True when any layer (optionally only visible ones) has Lock All set —
    // used to block document-wide merge/flatten that would consume it.
    bool anyFullyLockedLayer(bool visibleOnly = false) const;
    void onAsyncJobCompleted(uint64_t jobId);
    void onProgressiveBatch(uint64_t jobId, QVector<QRect> tileRects);

    Document* m_doc = nullptr;
    CommandHistory m_history;
    // Central animatable-property editor (auto-key). Rebound in setDocument.
    anim::LayerPropertyController m_propertyController;
    // Timeline-independent, elapsed-time playback backend. It requests frames
    // through setCurrentFrame(), never by touching evaluated layer state itself.
    anim::PlaybackController m_playbackController;
    bool m_inBatch = false;
    int m_pasteCount = 0;
    bool m_editingMask = false;
    bool m_soloActive = false;
    std::vector<std::pair<int, bool>> m_soloPrevVisibility;  // (flatIndex, visible)
    QImage m_copiedMask;
    QPoint m_copiedMaskOrigin{0, 0};
    std::vector<LayerEffect> m_copiedEffects;   // layer-style clipboard

    QImage m_featherOriginalMask;
    int    m_featherLayerIdx = -1;

    float  m_opacityBefore = -1.0f;
    int    m_opacityLayerIdx = -1;

    std::unordered_map<uint64_t, std::shared_ptr<AsyncJob>> m_pendingAsyncJobs;
    std::unordered_map<uint64_t, std::shared_ptr<std::atomic_bool>> m_pendingDocumentOperations;
    uint64_t m_nextDocumentOperationId = 1000000;

    std::unique_ptr<UpscaleService> m_upscaleService;
    std::unordered_map<uint64_t, UpscaleOptions> m_pendingUpscaleJobs;
    std::unordered_map<uint64_t, int> m_pendingUpscaleLayers;

    // ── Generative Fill state ──
    void onGenerativeFinished(const InpaintResult& result);
    void onGenerativeFailed(const InpaintError& error);

    AgentConfig m_generativePreset;
    bool m_hasGenerativePreset = false;
    ImageGenProvider* m_genProvider = nullptr;
    QRect m_genRect;             // target rect in document/layer pixels
    QImage m_genMaskCrop;        // grayscale mask cropped to m_genRect
    GenFillMode m_genMode = GenFillMode::FillSelection;
    int m_genTargetIndex = -1;
    QImage m_genBefore;          // active-layer snapshot for undo (FillSelection)
    uint64_t m_genJobId = 0;
};
