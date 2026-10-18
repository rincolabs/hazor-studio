#pragma once

#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QColor>
#include <QSize>
#include <QHash>
#include <QStringList>
#include <memory>
#include <string>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <atomic>
#include "core/Document.hpp"
#include "core/Clipboard.hpp"
#include "core/LayerEffect.hpp"
#include "controller/CommandHistory.hpp"
#include "text/TextTypes.hpp"
#include "core/ShapeTypes.hpp"
#include "tools/ToolCall.hpp"
#include "async/AsyncJob.hpp"
#include "agent/AgentConfig.hpp"
#include "color/ColorProfile.hpp"
#include "ai/AiRemoveTypes.hpp"
#include "ai/upscale/UpscaleTypes.hpp"
#include "raw/RawLayerData.hpp"
#include "smartobject/SmartObjectRenderService.hpp"

namespace cv { class Mat; }
namespace nlohmann { class json; }
class ImageGenProvider;
class UpscaleService;
class DocumentContainer;
class RawDevelopService;
class RawDevelopParams;
class LayerTreeNode;
struct RawDevelopResult;
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

    // ── Smart Objects ────────────────────────────────────────────────────
    // The .hzs this controller's document belongs to, and which document of it
    // this controller edits. Needed to resolve a Smart Object's docId; the
    // container is shared (never owned) so the controller cannot outlive-dangle
    // on it.
    void setDocumentContainer(std::shared_ptr<DocumentContainer> container,
                              const SmartObjectDocId& docId);
    DocumentContainer* documentContainer() const { return m_container.get(); }
    const std::shared_ptr<DocumentContainer>& documentContainerPtr() const { return m_container; }
    const SmartObjectDocId& documentId() const { return m_docId; }

    // Re-renders every STALE instance of `docId` anywhere in the container,
    // then — once each render commits — chains to the documents those instances
    // live in. The cascade is chained BY COMMIT, not merely ordered at
    // enqueue time: if the outer document rendered concurrently with the inner
    // one, its snapshot would still hold the inner instance's old pixels and it
    // would bake stale content. Both outcomes of that race are bad (re-render
    // thrash with a visible flash, or a permanently stale instance), so the
    // next level is only scheduled after the previous one has committed.
    // `force` re-renders even instances that look up to date — used when the
    // user explicitly commits content and expects to see it propagate now.
    void refreshSmartObjectInstances(const SmartObjectDocId& docId, bool force = false);
    // Every document in the container. `afterLoad` marks the pass as "this is
    // regenerating what is already on disk", so its commits do not flag any
    // document as unsaved.
    void refreshAllSmartObjectInstances(bool afterLoad = false);

    // Commits the isolated working copy a content tab has been editing into the
    // document it stands for, and pushes it through to every instance at once.
    // This — not an edit in the content tab — is the moment the project's
    // content actually changes; the caller is what marks the project unsaved.
    // False when `docId` has no open edit session, or no stored document.
    bool commitSmartObjectEditSession(const SmartObjectDocId& docId);

    // The payload of the instance at `flatIndex`, or null when that layer is
    // not a Smart Object. Returned as shared_ptr<const ...>: callers read it,
    // they never mutate through it (see SmartObjectData.hpp).
    std::shared_ptr<const SmartObjectData> smartObjectDataAt(int flatIndex) const;
    SmartObjectDocId smartObjectDocIdAt(int flatIndex) const;
    // How many instances in the whole container display the same document as
    // the instance at `flatIndex` (for the Properties page). 0 when not a
    // Smart Object.
    int smartObjectInstanceCountAt(int flatIndex) const;
    // Resolved absolute path of a Linked instance, empty otherwise.
    QString smartObjectLinkedPathAt(int flatIndex) const;

    // Moves the selected nodes into a NEW document in the container and leaves
    // a single instance in their place, as one undo step. Returns the
    // instance's flat index, or -1.
    int convertToSmartObject(const std::vector<int>& flatIndices);
    // Places another .hzs as a Smart Object. Embedded imports a private copy of
    // its documents into this container; Linked keeps referencing the file.
    int placeEmbeddedDocument(const QString& path);
    int placeLinkedDocument(const QString& path);

    // Duplicates the instance with an INDEPENDENT copy of its document (fresh
    // docId), as opposed to Ctrl+J, which shares the same document. A Linked
    // instance's copy comes back Embedded: a private copy is the only form of
    // "independent" when the content lives in someone else's file.
    int newSmartObjectViaCopy(int flatIndex);
    // Replaces the content every instance of this document shows. On a Linked
    // instance it is a relink; on an Embedded one the stored document is
    // swapped under the same docId, so all instances follow at once.
    bool replaceSmartObjectContents(int flatIndex, const QString& path);
    // Re-points a Linked instance at another file.
    bool relinkSmartObject(int flatIndex, const QString& path);
    // "Update Modified Content": re-reads a Linked file and re-renders. There
    // is no file watcher anywhere in Hazor, so this is the explicit refresh.
    bool updateSmartObjectContent(int flatIndex);
    // Storage conversion. These move documents between containers and change
    // what gets persisted — they are not payload swaps, which is why they are
    // three operations and not one flag.
    bool convertSmartObjectToLinked(int flatIndex, const QString& path);
    bool convertSmartObjectToEmbedded(int flatIndex);

    Layer* activeLayer() const;
    int activeLayerIndex() const { return m_doc ? m_doc->activeFlatIndex : -1; }
    void deselectActive() { setActiveNode(-1); }

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
    // Brackets a live composite drag so the canvas renders through the GPU
    // per-layer compositor (fast) instead of a full CPU projection recomposite
    // every frame, and the layer panel holds its last frame instead of re-baking
    // thumbnails per step. The editor calls true on gesture begin, false on
    // commit. Also used by the Layer Styles dialog's slider drags, which mutate
    // the composite per frame in exactly the same way.
    void setAdjustmentLiveEdit(bool active) {
        m_liveCompositeEdit = active;
        emit adjustmentLiveEditChanged(active);
        // A style bake still running when the gesture ENDS is a wait the user
        // should see; one in the middle of the gesture must not raise a modal
        // dialog over the slider being dragged.
        updateStyleBakeProgress();
    }

    // Renderer → UI: a layer-style bake (StyleBakePool) started or finished.
    // Wired from CanvasView. Not every bake is a wait worth reporting — a brush
    // stroke on a styled layer dispatches one per dab — so this only surfaces
    // the app's delayed progress dialog while a Layer Styles edit is waiting
    // for its result and no live gesture is in progress. The 220 ms delay in
    // MainWindow::beginProgressDelayed keeps it invisible on layers whose bake
    // is quick.
    void setStyleBakeBusy(bool busy);
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
    // Re-reveals a RAW Layer's PP3 off the UI thread (RawDevelopService,
    // Fase 4.8) and, on success, pushes a RawDevelopCommand so the result is
    // undoable. No-op (returns false) when flatIndex isn't a RAW layer, the
    // layer is pixel-locked, or a re-reveal for it is already in flight —
    // callers retry once rawLayerDevelopFinished() fires for that layer.
    bool rerevealRawLayer(int flatIndex, const RawDevelopParams& params);
    // Creates the editable pixel copy a paint tool needs when the user aims it
    // at a RAW Layer: the layer's already-developed pixels become a new Pixel
    // Layer inserted directly above the RAW node, the RAW node is hidden with
    // its RawLayerData/RawSource untouched (Hazor Develop keeps working on it),
    // and the copy becomes the active/selected layer. Returns the copy's flat
    // index, or -1 when nothing was created (not a RAW layer, pixel-locked, or
    // a re-reveal is in flight — the last two report via operationBlocked()).
    int rasterCopyRawLayer(int flatIndex);
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
    // RAW paths (RawEngine::isSupportedExtension()) get a non-destructive RAW
    // Layer instead of a flattened import, same as the dedicated RAW open
    // path. outRawLayerFlatIndices, when given, collects the final flat
    // index of each one so the caller can open Hazor Develop for it.
    bool importExternalImages(const QStringList& paths, const QPointF& dropCanvasNdc,
                              QList<int>* outRawLayerFlatIndices = nullptr);
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
    void toolExecuted(const QString& toolName, bool success);
    void clipboardChanged();
    void historyChanged();
    // Toggles the canvas live-edit render path during an adjustment-param drag.
    void adjustmentLiveEditChanged(bool active);
    void maskEditingChanged(bool editing);
    void maskOverlayChanged();
    void guidesChanged();
    void operationBlocked(const QString& reason);
    // RawDevelopService finished re-revealing `node`'s RAW layer (see
    // rerevealRawLayer()). `ok` false means the render failed or the layer
    // was gone by the time it finished; `message` carries the reason then,
    // and on success is either empty or the non-fatal reframe warning below.
    //
    // `node` identifies the layer and is only ever meant to be *compared*,
    // never dereferenced: Hazor Develop is an independent window, so the
    // layer can be reordered (or deleted) between dispatch and completion,
    // which is exactly why a flat index cannot identify it. `flatIndex` is
    // the layer's index at completion time, or the dispatch-time index when
    // the layer could no longer be resolved.
    void rawLayerDevelopFinished(LayerTreeNode* node, int flatIndex, bool ok, QString message);
    // A Smart Object render landed and every live instance of `docId` in the
    // container was updated. Carries NO command and NO history entry (see
    // docs/SMART-OBJECTS.md): updating a regenerable cache is not a
    // destructive edit, and recording it would fight the invalidation check
    // that triggers it — an undo would restore old pixels, the staleness test
    // would immediately see them and re-render, undoing the undo.
    // `affectedDocuments` are the container documents whose instances changed,
    // so other tabs can repaint.
    // `marksDirty` is false only for the pass that regenerates renders already
    // stored on disk (right after a load); true whenever the render changed
    // because something was edited, in which case the documents it landed in
    // now differ from their files.
    void smartObjectRenderCommitted(SmartObjectDocId docId,
                                    QStringList affectedDocuments,
                                    bool marksDirty);
    // A Linked instance's reference became resolvable, or stopped being so.
    // Panel badge only — not a pixel change and not a history step: the cached
    // render is precisely what stays on screen when a link breaks.
    void smartObjectLinkStateChanged(int flatIndex, bool broken);
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
    void onRawLayerDevelopFinished(LayerTreeNode* node, RawDevelopResult result);

    // State captured at rerevealRawLayer() dispatch time, needed to build
    // the RawDevelopCommand once onRawLayerDevelopFinished() fires. Keyed by
    // node identity (not flatIndex, which may shift while the render runs).
    struct PendingRawDevelop {
        QImage beforeImage;
        QTransform beforeTransform;
        QImage beforeMask;
        QPoint beforeMaskOrigin;
        std::shared_ptr<RawLayerData> beforeData;
        QString pp3;
        int flatIndex = -1;
    };
    std::unordered_map<const LayerTreeNode*, PendingRawDevelop> m_pendingRawDevelops;

    // ── Smart Object rendering ──
    void onSmartObjectRenderFinished(SmartObjectRenderResult result);
    // Which instances a dispatch should consider.
    using SmartObjectInstanceFilter =
        std::function<bool(const Document& host, LayerTreeNode*, const SmartObjectData&)>;
    // Groups the matching STALE instances by render key and dispatches one job
    // per key — not one per instance (see SmartObjectRenderKey). Also refreshes
    // each visited instance's link state. `openLinked` allows loading a Linked
    // target that is not open yet; false keeps the pass I/O-free.
    // Returns the number of jobs dispatched.
    int dispatchSmartObjectRenders(const SmartObjectInstanceFilter& filter,
                                   bool force, bool openLinked, bool marksDirty);
    void requestSmartObjectRenders(const SmartObjectDocId& docId,
                                   bool force, bool marksDirty);
    bool renderLinkedSmartObject(const LayerTreeNode* node, bool force);
    // Embedded → a document of this container; Linked → the target file's
    // document via ContainerRegistry, never a second copy of it.
    std::shared_ptr<Document> resolveSmartObjectDocument(const SmartObjectData& data,
                                                         bool openIfNeeded) const;
    // Cheap existence test, no file read: tells "gone" apart from "not loaded".
    bool smartObjectReferenceExists(const SmartObjectData& data) const;
    void markSmartObjectLinkBroken(LayerTreeNode* node, bool broken);
    // Every document whose instances a dispatch or a commit has to visit: the
    // container's stored documents PLUS the uncommitted working copy of each
    // open edit session. The working copies are in because a nested Smart
    // Object inside one still has to re-render when its own content is
    // committed — and out of everything persistence-related, because they are
    // not part of the file (see DocumentContainer::beginEditSession).
    std::vector<std::pair<SmartObjectDocId, std::shared_ptr<Document>>>
        smartObjectHosts() const;
    static SmartObjectRenderKey smartObjectRenderKey(const SmartObjectData& data,
                                                     const Document& host);
    std::unique_ptr<LayerTreeNode> makeSmartObjectNode(const SmartObjectDocId& docId,
                                                       SmartObjectStorageMode mode,
                                                       const QString& linkedPath,
                                                       const Document& source,
                                                       const QString& name);
    int insertSmartObjectInstance(const SmartObjectDocId& docId,
                                  SmartObjectStorageMode mode,
                                  const QString& linkedPath,
                                  const QString& name,
                                  const QString& commandName);
    SmartObjectRenderService* ensureSmartObjectRenderService();

    std::unique_ptr<SmartObjectRenderService> m_smartObjectRenderService;
    std::shared_ptr<DocumentContainer> m_container;
    SmartObjectDocId m_docId;
    // Newest jobGeneration already committed per key, so a result built from a
    // state the document has already moved past is dropped instead of shown.
    QHash<SmartObjectRenderKey, std::uint64_t> m_committedSmartObjectGenerations;

    Document* m_doc = nullptr;
    CommandHistory m_history;
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

    // ── Layer-style bake progress (see setStyleBakeBusy) ──
    void updateStyleBakeProgress();
    bool m_styleEditPending = false;   // a style edit is waiting for its bake
    bool m_styleBakeBusy = false;
    bool m_liveCompositeEdit = false;  // a slider/curve drag is in progress
    uint64_t m_styleBakeJobId = 0;     // non-zero while the progress op is open
    uint64_t m_styleBakeIdleToken = 0;

    std::unordered_map<uint64_t, std::shared_ptr<AsyncJob>> m_pendingAsyncJobs;
    std::unordered_map<uint64_t, std::shared_ptr<std::atomic_bool>> m_pendingDocumentOperations;
    uint64_t m_nextDocumentOperationId = 1000000;

    std::unique_ptr<UpscaleService> m_upscaleService;
    std::unique_ptr<RawDevelopService> m_rawDevelopService;
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
