#pragma once

#include <QMainWindow>
#include <QTabWidget>
#include <QAction>
#include <vector>

class QMenu;
#include <memory>
#include <cstdint>
#include <functional>
#include <QPointer>
#include "core/Document.hpp"
#include "controller/ImageController.hpp"
#include "tools/ToolCatalog.hpp"
#include "agent/AgentConfig.hpp"

class QResizeEvent;
class QEvent;
class QDragEnterEvent;
class QDragMoveEvent;
class QDragLeaveEvent;
class QDropEvent;

class CanvasView;
class LayerPanel;
class PropertiesPanel;
class HistogramPanel;
class BrushDynamicsPanel;
class BrushPanel;
class CustomShapesPanel;
class HistoryPanel;
class TitleBar;
class QLabel;
class QPushButton;
class ToolOptionsBar;
class ToolsPanel;
class AlignBar;
class SwatchesPanel;
class ColorMixerPanel;
class ColorPaletteBar;
class ZoomComboBox;
class AppSettingsDialog;
class ProgressDialog;
class QTimer;
class QDockWidget;

class ImageController;
class AgentController;
class ToolExecutor;
class McpServer;
class AgentPanel;
class AgentPresetManager;
class BrushPresetManager;
class BrushImportManager;
class ColorEngine;
class GenerativeFillDialog;
class AiObjectSelectionController;
struct AiCompatibilityMessage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(QWidget* parent = nullptr);
    void createDefaultDocument();
    ToolExecutor* toolExecutor() const { return m_toolExecutor; }

    // Embedded MCP server ownership. main.cpp constructs the server and hands it
    // over; the settings page calls applyMcpSettings() to start/stop/rebind it
    // to the port and enabled flag stored in QSettings.
    void setMcpServer(McpServer* server) { m_mcpServer = server; }
    McpServer* mcpServer() const { return m_mcpServer; }
    void applyMcpSettings();

    bool isCustomShapeIconPreloadFinished() const { return m_customShapeIconPreloadFinished; }
    void openBrushImportDialog(const QStringList& files = {});

    // Open one or more Hazor project files (*.hzs) as new tabs. Used when the OS
    // launches the app with file arguments (extension association) or via a
    // QFileOpenEvent (macOS "Open With"). Returns the number of files opened.
    int openProjectFiles(const QStringList& files);

signals:
    void customShapeIconPreloadFinished();

private slots:
    void onNewFile();
    void onOpenFile();
    void onSaveFile();
    void onSaveAsFile();
    void onExportFile();
    void onImportImage();
    void onEditDocument();
    void onAddLayer();
    void onRemoveLayer(int index);
    void onActiveLayerChanged(int index);
    void onMenuAddLayer();
    void onMenuRemoveLayer();
    void onMenuDuplicateLayer();
    // Solid Color fill layer: opens the colour picker first, creating the layer
    // only if the user confirms a colour (cancel = no layer).
    void createSolidColorLayer();
    void onMenuFillLayer();
    void onMenuMergeVisible();
    void onMenuMergeDown();
    void onMenuMergeLayers();
    void onMenuFlattenImage();
    void onMenuRasterizeLayer();
    void onMenuLayerStyles();
    void onMenuColorPicker();
    void onMenuFlipHorizontal();
    void onMenuFlipVertical();
    void onMenuResizeLayer();
    void onMenuLayerMaskAdd();
    void onMenuLayerMaskRemove();
    void onMenuLayerMaskApply();
    void onMenuLayerMaskToggle();

    void onImageColorAdjustments();
    void onImageResizeImage();
    void onImageResizeCanvas();
    void onImageAutoContrast();
    void onImageGaussianBlur();
    void onImageSharpen();
    void onImageMedianBlur();
    void onImageBoxBlur();
    void onImageBilateralBlur();
    void onImageMotionBlur();
    void onImageRadialBlur();
    void onImageZoomBlur();
    void onImageEdgeDetect();
    void onImageGrayscale();
    void onImageInvert();
    void onImagePosterize();
    void onImageThreshold();
    void onImageNoiseReduce();
    void onImageRemoveBg();
    void onAiUpscaleDocument();
    void onAiUpscaleLayer();
    void showAiUpscaleDialog(UpscaleTarget target);

    void updateLayerMenuState();
    void updateAlignBarState();


    void onToolSelected(int tool);
    void onBrushSizeChanged(int size);
    void onBrushOpacityChanged(int opacity);
    void onBrushHardnessChanged(int hardness);
    void onBrushFlowChanged(int flow);
    void onBrushColorChanged(const QColor& color);
    // Single sync point for the pen-pressure master toggle: pushes it to the
    // engine (canvas), the Options Bar button, the Brush Settings panel checkbox,
    // and the QAction, then flashes a status hint. Each target's setter is
    // non-emitting so this never loops.
    void applyBrushPressureEnabled(bool on);
    // Apply a brush preset to the canvas and sync the brush UI (toolbar sliders,
    // dynamics panel). Shared by the brush browser panel.
    // switchToBrushTool: picking a brush from the panel implies wanting to paint,
    // so it switches to the Brush tool. The startup default-brush load passes false
    // to keep the initial tool (Move) unchanged.
    void applyBrushPreset(const struct BrushPreset& preset, bool switchToBrushTool = true);
    // Open (or focus) the single dedicated Brush Panel dock. First open creates
    // it floating near the window centre; later calls just raise it.
    void showBrushPanel();
    void showCustomShapesPanel();
    void showBrushSettingsPanel();

    // Docking maintenance. When a tab is dragged out of a floating tab group, Qt
    // can leave the remaining single dock wrapped in a QDockWidgetGroupWindow
    // (a stray white-bordered container); that malformed wrapper crashes Qt's
    // layout code if later dragged into another floating panel. dissolveSingle
    // DockGroups() unwraps any 1-dock group into a normal standalone floating
    // window. scheduleDockMaintenance() runs it deferred + coalesced, and it
    // NEVER touches the layout while a drag is in progress (mouse button held).
    void scheduleDockMaintenance();
    void dissolveSingleDockGroups();

    // Restore every panel to its default position: the right-side panels back
    // into the default two-group split (Histogram hidden until Window menu),
    // and the on-demand floating panels (Brush Settings, Brushes, Generative
    // Fill) back to their hidden floating state. Wired to Window → Reset Panel
    // Layout.
    void resetDockLayout();

    void onTextFontChanged(const QFont& font);
    void onTextSizeChanged(int size);
    void onTextBoldChanged(bool bold);
    void onTextItalicChanged(bool italic);
    void onTextUnderlineChanged(bool underline);
    void onTextStrikethroughChanged(bool strikethrough);
    void onTextColorChanged(const QColor& color);
    void onTextAlignChanged(int align);
    void onTextTrackingChanged(double tracking);
    void onTextLeadingChanged(double leading);

    void onZoomChanged(float zoom);
    void onMouseCoordChanged(QPointF coord);
    void onCanvasContextMenuRequested(QPoint globalPos);

    void onUndo();
    void onRedo();

    void onMinimize();
    void onMaximizeRestore();
    void onCloseWindow();

    void onTabCloseRequested(int index);
    void onTabChanged(int index);
    void onSettings();
    void onAboutDialog();
    void onOpenAiSettings();   // AI Object Selection "Open Settings" → AI/ML page
    // Routes a hard AI failure from the tool controller to a standardized alert
    // dialog (AiAlertService), then acts on the user's choice.
    void onAiAlertRequested(const AiCompatibilityMessage& message);
    void onConfigureAgent();
    void onGenerativeFill();
    void onAgentSelected(const QString& name);

    // AI Object Selection activation guard: returns true when the tool may be
    // activated; otherwise shows an alert, opens AI Settings if requested, keeps
    // the current tool selected and returns false (spec §12).
    bool guardAiObjectSelectionActivation();
    bool guardAiRemoveActivation();
    // Connects the active canvas's AI controller alert signal once (idempotent).
    void ensureAiControllerConnected();
    // Switches the Execution Provider to CPU (the "Use CPU" alert action).
    void switchAiProviderToCpu();

    // AI Select loading dialog: while an AI Select / Refine / Remove-BG job runs,
    // mirror the controller's status onto the app status bar and — if the job is
    // still running after a short delay — surface a cancelable loading dialog.
    void onAiSelectBusyChanged(bool busy);
    void onAiSelectStatusChanged(const QString& status);
    void showAiLoadingDialogIfStillBusy();

protected:
    void resizeEvent(QResizeEvent* e) override;
    void changeEvent(QEvent* e) override;
    void closeEvent(QCloseEvent* e) override;
    // Dock group windows are created/destroyed as direct children of the main
    // window; this is the only reliable signal for "the dock layout changed"
    // (the lone dock left after a tab is dragged out of a floating group emits
    // no QDockWidget signal). Used to schedule single-dock-group cleanup.
    void childEvent(QChildEvent* e) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* e) override;
    void dragMoveEvent(QDragMoveEvent* e) override;
    void dragLeaveEvent(QDragLeaveEvent* e) override;
    void dropEvent(QDropEvent* e) override;

private:
    enum class SelectRefineOp {
        None,
        Feather,
        Grow,
        Shrink,
        Border,
        Smooth
    };

    void clearSelectRefinePreview();
    void applySelectRefinePreview(SelectRefineOp op, int value);
    void openRefineEdgeDialog();

    struct DocumentTab {
        QString name;
        QString currentProjectPath;
        bool isDirty = false;
        std::unique_ptr<Document> document;
        QWidget* page = nullptr;
        CanvasView* canvas = nullptr;
        std::unique_ptr<ImageController> controller;
    };

    void showLayerStylesDialog(int flatIndex);
    void refreshLayerPanel();
    void refreshPropertiesPanel();

    // Structural layer operations shared by the Layer menu actions and the
    // ShortcutManager callbacks (single source of truth for the behaviour).
    void refreshLayersUi();         // canvas sync + panel/properties/menu refresh
    void groupSelectedLayers();     // Ctrl+G
    void ungroupSelectedLayers();   // Ctrl+Shift+G
    void arrangeActiveLayer(int kind); // 0=forward 1=backward 2=front 3=back
    void toggleActiveClipping();    // Ctrl+Alt+G (adjustment clipping)
    // Applies a numeric Transform-panel edit (W/H/X/Y/rotation) to the active
    // layer through the centralized transform path. `field` is a
    // PropertiesPanel::TransformField; `value` is px or degrees.
    void applyPropertiesTransformEdit(int field, double value);
    void refreshTransformOptionsBar();
    // Pushes the active document's canvas metrics into the Properties panel's
    // Document/Canvas page (shown when no layer is selected).
    void refreshPropertiesDocumentPage();
    // Document-page canvas edits — all routed through the existing resize logic.
    void applyCanvasSizeEdit(int field, double value);   // resize canvas (no resample)
    void applyCanvasResolutionEdit(double dpi);
    void applyCanvasOrientation(bool portrait);          // swap W/H (resize canvas)
    void refreshHistoryPanel();
    void updateStatusFromDocument();
    void updateDirtyUi();
    void markActiveTabDirty(bool dirty = true);
    bool ensureTabCanBeDiscardedOrSaved(int index);
    bool saveProjectForTab(int index, bool saveAs);
    bool loadProjectAsNewTab(const QString& path);
    void showProgress(const QString& title, const QString& message, bool cancelable);
    void beginProgressDelayed(const QString& title, const QString& message, bool cancelable);
    void setProgress(int value);
    void setMessage(const QString& message);
    void closeProgress();
    bool wasCanceled() const;
    void handleProgressCancel();
    void setupAgents();
    void reloadRulerGuideSettingsForTabs();
    void updateRulerGuideActionStates();
    // Mutates the persisted ruler/guide overlay settings, then reloads the tabs
    // and re-syncs the View-menu + Properties-panel controls. Single source of
    // truth shared by the View menu and the Properties panel's Rulers & Grids
    // section.
    void applyRulerGuideSetting(const std::function<void(RulerGuideSettings&)>& mutate);
    void updateToolBarLayout();
    void syncGenerativeFillPanelController();
    void applyViewportStatusBarTheme();
    void showViewportStatusMessage(const QString& message, int timeoutMs,
                                   const QString& iconPath = QString());
    // Sync the Move options bar's Distort/Perspective buttons (enabled state +
    // which mode is active) with the canvas and the active layer type.
    void updateDistortControls();

    // While the Move canvas tool is active, pick the options-bar page: the Skew
    // sub-mode shows Distort/Perspective, otherwise the page follows the active
    // layer type (Text/Shape/Move).
    void applyMoveOptionsPage();

    // Push a shape layer's style/metadata into the Shape options bar (and the
    // canvas shape-tool defaults) so the bar reflects the selected shape. Called
    // from every path that can make a shape layer active (layer-panel click,
    // Move-tool/canvas selection, tab switch). Returns true if `node` is a shape
    // layer.
    bool syncShapeOptionsBar(LayerTreeNode* node);

    void execToolDialog(class ToolDialog* dlg, const QString& toolName,
                         const QVariantMap& defaultParams);

    DocumentTab& createTab(const QString& name, const QSize& size);
    CanvasView* activeCanvas() const;
    Document* activeDocument() const;
    ImageController* activeController() const;

    QTabWidget* m_tabWidget = nullptr;
    std::vector<DocumentTab> m_tabs;
    int m_activeTabIndex = -1;

    Document* m_doc = nullptr;
    CanvasView* m_canvas = nullptr;
    ImageController* m_ctrl = nullptr;

    LayerPanel* m_layerPanel = nullptr;
    PropertiesPanel* m_propsPanel = nullptr;
    HistogramPanel* m_histogramPanel = nullptr;
    HistoryPanel* m_historyPanel = nullptr;
    ToolOptionsBar* m_toolBar = nullptr;
    ToolsPanel* m_toolsPanel = nullptr;
    AlignBar* m_alignBar = nullptr;
    SwatchesPanel* m_swatchesPanel = nullptr;
    ColorMixerPanel* m_colorMixerPanel = nullptr;
    ColorPaletteBar* m_colorPaletteBar = nullptr;
    QWidget* m_viewportStatusBar = nullptr;
    QLabel* m_statusIconLabel = nullptr;
    QLabel* m_statusMessageLabel = nullptr;
    ZoomComboBox* m_zoomCombo = nullptr;
    QPushButton* m_statusZoomFitBtn = nullptr;
    QPushButton* m_statusZoom100Btn = nullptr;
    BrushDynamicsPanel* m_brushDynamics = nullptr;
    BrushPanel* m_brushPanel = nullptr;
    QDockWidget* m_brushPanelDock = nullptr;
    bool m_brushPanelFirstShown = false;
    BrushImportManager* m_brushImportManager = nullptr;
    CustomShapesPanel* m_customShapesPanel = nullptr;
    QDockWidget* m_customShapesPanelDock = nullptr;
    bool m_customShapesPanelFirstShown = false;
    bool m_customShapeIconPreloadFinished = false;
    bool m_dockMaintenanceScheduled = false; // a deferred maintenance pass is queued
    bool m_inDockMaintenance = false;        // guards reentrancy during maintenance
    QDockWidget* m_generativeFillDock = nullptr;
    GenerativeFillDialog* m_generativeFillPanel = nullptr;
    AgentPanel* m_agentPanel = nullptr;

    // Right-side panel docks (each individually dockable) + the on-demand
    // floating Brush Settings dock. Held as members so resetDockLayout() can
    // restore the default arrangement.
    QDockWidget* m_layersDock = nullptr;
    QDockWidget* m_agentDock = nullptr;
    QDockWidget* m_histogramDock = nullptr;
    QDockWidget* m_propsDock = nullptr;
    QDockWidget* m_colorDock = nullptr;
    QDockWidget* m_swatchesDock = nullptr;
    QDockWidget* m_historyDock = nullptr;
    QDockWidget* m_brushSettingsDock = nullptr;

    ToolExecutor* m_toolExecutor = nullptr;
    McpServer* m_mcpServer = nullptr;
    AgentController* m_agentController = nullptr;
    AgentPresetManager* m_presetManager = nullptr;
    ColorEngine* m_colorEngine = nullptr;

    TitleBar* m_titleBar = nullptr;
    bool m_useCustomTitleBar = false;

    QLabel* m_coordLabel = nullptr;
    QLabel* m_sizeLabel = nullptr;
    QLabel* m_colorProfileLabel = nullptr;
    QTimer* m_statusMessageTimer = nullptr;

    ProgressDialog* m_progressDialog = nullptr;
    QTimer* m_progressShowTimer = nullptr;
    QString m_pendingProgressTitle;
    QString m_pendingProgressMessage;
    bool m_pendingProgressCancelable = false;
    bool m_progressActive = false;
    uint64_t m_activeProgressJobId = 0;
    QPointer<ImageController> m_progressController;

    // AI Select loading dialog (separate from the ImageController-driven progress
    // above; cancellation routes to the AI controller, not a long-operation job).
    ProgressDialog* m_aiLoadingDialog = nullptr;
    QTimer* m_aiLoadingTimer = nullptr;
    bool m_aiSelectBusy = false;
    QString m_aiLoadingMessage;

    QAction* m_importAction = nullptr;
    QAction* m_importBrushesAction = nullptr;
    QAction* m_brushPressureAction = nullptr;
    QAction* m_addLayerAction = nullptr;
    QAction* m_removeLayerAction = nullptr;
    QAction* m_duplicateLayerAction = nullptr;
    QMenu* m_adjustmentLayerMenu = nullptr;
    QAction* m_fillLayerAction = nullptr;
    QAction* m_mergeVisibleAction = nullptr;
    QAction* m_mergeDownAction = nullptr;
    QAction* m_mergeLayersAction = nullptr;
    QAction* m_flattenImageAction = nullptr;
    QAction* m_rasterizeLayerAction = nullptr;
    QAction* m_layerStylesAction = nullptr;
    QAction* m_flipHorizontalAction = nullptr;
    QAction* m_flipVerticalAction = nullptr;
    QAction* m_resizeLayerAction = nullptr;
    QAction* m_colorAdjAction = nullptr;
    QAction* m_gaussianAction = nullptr;
    QAction* m_medianAction = nullptr;
    QAction* m_boxBlurAction = nullptr;
    QAction* m_bilateralBlurAction = nullptr;
    QAction* m_motionBlurAction = nullptr;
    QAction* m_radialBlurAction = nullptr;
    QAction* m_zoomBlurAction = nullptr;
    QAction* m_sharpenAction = nullptr;
    QAction* m_posterizeAction = nullptr;
    QAction* m_thresholdAction = nullptr;
    QAction* m_edgesAction = nullptr;
    QAction* m_grayscaleAction = nullptr;
    QAction* m_invertAction = nullptr;
    QAction* m_noiseAction = nullptr;
    QAction* m_removeBgAction = nullptr;
    QAction* m_aiUpscaleDocumentAction = nullptr;
    QAction* m_aiUpscaleLayerAction = nullptr;
    QAction* m_layerMaskAddAction = nullptr;
    QAction* m_layerMaskRemoveAction = nullptr;
    QAction* m_layerMaskApplyAction = nullptr;
    QAction* m_layerMaskToggleAction = nullptr;
    QAction* m_layerMaskFromSelAction = nullptr;
    QAction* m_layerMaskInvertAction = nullptr;
    QAction* m_layerMaskCopyAction = nullptr;
    QAction* m_layerMaskPasteAction = nullptr;
    QAction* m_layerMaskClearAction = nullptr;
    QAction* m_showColorPaletteAction = nullptr;
    QAction* m_showRulersAction = nullptr;
    QAction* m_showGuidesAction = nullptr;
    QAction* m_lockGuidesAction = nullptr;
    QAction* m_enableSnapAction = nullptr;
    QAction* m_snapToCanvasAction = nullptr;
    QAction* m_snapToGuidesAction = nullptr;
    QAction* m_overscrollAction = nullptr;
    QAction* m_proofColorsAction = nullptr;

    QImage m_selectRefineBaseMask;
    bool m_selectRefineBaseActive = false;
    SelectRefineOp m_selectRefineOp = SelectRefineOp::None;
    // Guards the selectionChanged-driven base invalidation while the refine
    // itself is mutating the selection.
    bool m_inSelectRefine = false;
    void updateDockTitleBar(QDockWidget* dock);
};
