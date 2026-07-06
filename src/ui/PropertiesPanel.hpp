#pragma once

#include <QWidget>

class QLabel;
class QSlider;
class QDoubleSpinBox;
class QPushButton;
class QToolButton;
class QScrollArea;
class QWidget;
class AppCheckBox;
class AppComboBox;
class AlignBar;
class ScrubbableValueInput;
class TransformFieldsWidget;
class ImageController;
class CurvesEditorWidget;
class ColorBalanceAdjustmentWidget;
class HueSaturationAdjustmentWidget;
class SolidColorAdjustmentWidget;
class CollapsibleSection;

class PropertiesPanel : public QWidget {
    Q_OBJECT

public:
    // Which numeric transform field was committed (matches the order used by the
    // panel rows). Kept in sync with MainWindow's handler.
    enum TransformField { FieldWidth = 0, FieldHeight, FieldX, FieldY, FieldRotation };

    // Type of the active layer; drives which sections (and which header icon/label)
    // the panel shows. None hides the whole layer-transform interface.
    enum class LayerKind { None, Pixel, Text, Shape };

    explicit PropertiesPanel(QWidget* parent = nullptr);

    void setController(ImageController* ctrl);

    // Header + Transform/Align/Quick-Actions interface. Pass LayerKind::None to
    // hide everything (no layer, or an unsupported type). `canTransform` reflects
    // whether the layer may be moved/scaled (false => fields shown but disabled).
    void setLayerTransformInfo(LayerKind kind,
                               double widthPx, double heightPx,
                               double posX, double posY,
                               double rotationDeg,
                               bool canTransform);

    // Whether the constrain-proportions (link) toggle is active. MainWindow reads
    // this when handling a W/H edit so it can scale the other dimension to match.
    bool proportionsLocked() const;

    // ── Document / Canvas page (shown when no layer is selected) ──
    // Pushes the active document's canvas metrics into the Document page. The
    // panel only displays + forwards edits; MainWindow owns all resize/document
    // logic. `portraitActive`/`landscapeActive` light up the matching orientation
    // button.
    void setDocumentInfo(int widthPx, int heightPx, double resolutionDpi,
                         const QString& colorMode, int bitDepth,
                         bool portraitActive, bool landscapeActive);
    // Switches the visible view to the Document/Canvas page.
    void showDocumentProperties();
    // Whether the Canvas section's constrain-proportions toggle is active.
    bool canvasProportionsLocked() const;
    // Reflects the current ruler visibility + unit without emitting signals.
    void setRulerGuideState(bool rulersVisible, int unit);

    void setMaskInfo(bool hasMask, float density, float feather);
    // Reflects the per-document rubylith overlay state without emitting signals.
    void setMaskOverlayState(bool visible, float opacity);
    void clear();

    // Swaps the panel to the Curves editor for the adjustment at `flatIndex`
    // (hosted here per the spec — same widget is reusable in a dialog later).
    void showCurvesEditor(int flatIndex);
    // Swaps the panel to the Color Balance editor for the adjustment at
    // `flatIndex`.
    void showColorBalanceEditor(int flatIndex);
    // Swaps the panel to the Hue/Saturation editor for the adjustment at
    // `flatIndex`.
    void showHueSaturationEditor(int flatIndex);
    // Swaps the panel to the Solid Color editor for the fill layer at
    // `flatIndex`.
    void showSolidColorEditor(int flatIndex);
    // Opens the Solid Color editor's colour picker for the active fill layer
    // (used by a double-click on the layer's colour thumbnail). No-op unless the
    // Solid Color editor is the active view.
    void openSolidColorPicker();
    // Restores the default layer-properties view.
    void showLayerProperties();

    // Exposed so MainWindow can bridge the editor's image-pick requests to the
    // canvas eyedropper sampling.
    CurvesEditorWidget* curvesEditor() const { return m_curvesEditor; }
    HueSaturationAdjustmentWidget* hueSaturationEditor() const { return m_hueSaturationEditor; }

signals:
    void maskDensityChanged(float density);
    void maskFeatherBegin();
    void maskFeatherPreview(float radius);
    void maskFeatherCommit(float radius);
    void maskInvertRequested();
    void maskOverlayToggled(bool visible);
    void maskOverlayOpacityChanged(float opacity);  // 0..1

    // ── Transform / Align / Quick Actions (all shortcuts to existing editor logic) ──
    // A numeric field was committed (Enter / focus-out / scrub end). `field` is a
    // TransformField, `value` is in px (W/H/X/Y) or degrees (rotation).
    void transformFieldEdited(int field, double value);
    void flipHorizontalRequested();
    void flipVerticalRequested();
    // Alignment button — `alignmentType` matches CanvasView::doAlignLayer (0..5).
    void alignRequested(int alignmentType);
    void alignTargetChanged(int target);   // 0 = Canvas, 1 = Selection
    void resetTransformRequested();
    // Quick Actions (Pixel only) — AI Object Selection shortcuts.
    void removeBackgroundRequested();
    void selectSubjectRequested();
    void aiUpscaleRequested();

    // ── Document / Canvas page (all shortcuts to existing document logic) ──
    // A canvas dimension was committed. `field` is TransformField::FieldWidth or
    // FieldHeight; `value` is in px. MainWindow owns the resize + proportion math.
    void canvasSizeEdited(int field, double value);
    // Resolution (DPI) committed on the Document page.
    void canvasResolutionEdited(double dpi);
    void canvasColorModeChanged(const QString& colorMode);
    void canvasBitDepthChanged(int bitDepth);
    // Orientation buttons — swap W/H to portrait / landscape (no rotation).
    void canvasPortraitRequested();
    void canvasLandscapeRequested();
    // Rulers & Grids shortcuts.
    void rulersToggled(bool visible);
    void rulerUnitChanged(int unit);   // RulerUnit enum value

private:
    // Paints colorSurface on the scroll wrapper only (objectName-scoped so child
    // controls keep their own theme); re-applied on themeChanged.
    void applyWrapperSurface();
    void buildTransformInterface(class QVBoxLayout* parentLayout);
    void buildDocumentInterface(class QVBoxLayout* parentLayout);

    ImageController* m_controller = nullptr;

    QScrollArea* m_scroll = nullptr;         // surface-coloured scroll wrapper
    QWidget* m_layerSection = nullptr;       // default layer-properties view
    QWidget* m_documentSection = nullptr;    // document/canvas view (no layer)
    CurvesEditorWidget* m_curvesEditor = nullptr;
    ColorBalanceAdjustmentWidget* m_colorBalanceEditor = nullptr;
    HueSaturationAdjustmentWidget* m_hueSaturationEditor = nullptr;
    SolidColorAdjustmentWidget* m_solidColorEditor = nullptr;

    // ── Header ──
    QWidget* m_headerRow = nullptr;
    QLabel* m_headerIcon = nullptr;
    QLabel* m_headerLabel = nullptr;

    // ── Transform section ──
    CollapsibleSection* m_transformSection = nullptr;
    TransformFieldsWidget* m_transformFields = nullptr;
    QPushButton* m_flipHButton = nullptr;
    QPushButton* m_flipVButton = nullptr;

    // ── Align section ──
    CollapsibleSection* m_alignSection = nullptr;
    AlignBar* m_alignBar = nullptr;

    // ── Quick Actions section ──
    CollapsibleSection* m_quickSection = nullptr;
    QPushButton* m_removeBgButton = nullptr;
    QPushButton* m_selectSubjectButton = nullptr;
    QPushButton* m_aiUpscaleButton = nullptr;

    // ── Layer Mask section ──
    CollapsibleSection* m_maskSection = nullptr;
    QSlider* m_densitySlider = nullptr;
    QLabel* m_densityLabel = nullptr;
    QSlider* m_featherSlider = nullptr;
    QLabel* m_featherLabel = nullptr;
    QPushButton* m_invertButton = nullptr;
    AppCheckBox* m_overlayCheck = nullptr;
    QLabel* m_overlayOpacityTitle = nullptr;
    ScrubbableValueInput* m_overlayOpacity = nullptr;

    // ── Document / Canvas page ──
    QPushButton* m_canvasLinkButton = nullptr;
    QDoubleSpinBox* m_canvasWInput = nullptr;
    QDoubleSpinBox* m_canvasHInput = nullptr;
    QDoubleSpinBox* m_canvasXInput = nullptr;
    QDoubleSpinBox* m_canvasYInput = nullptr;
    QToolButton* m_portraitButton = nullptr;
    QToolButton* m_landscapeButton = nullptr;
    ScrubbableValueInput* m_resolutionInput = nullptr;
    double m_documentResolutionDpi = 300.0;
    AppComboBox* m_resolutionUnitCombo = nullptr;
    AppComboBox* m_modeCombo = nullptr;
    AppComboBox* m_depthCombo = nullptr;
    QToolButton* m_rulersToggle = nullptr;
    AppComboBox* m_unitCombo = nullptr;
};
