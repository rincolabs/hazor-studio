#pragma once

#include <QToolBar>
#include <QColor>
#include <QHash>
#include <QPointer>
#include <QDialog>
#include <QVector>
#include "brush/BrushPreset.hpp"
#include "engine/ColorSamplerService.hpp"
#include "gradient/GradientTypes.hpp"
#include "ai/AiRemoveTypes.hpp"

enum class AiToolPageMode {
    ObjectSelection = 0,
    RemoveObject = 1
};

class QStackedWidget;
class QWidget;
class QHBoxLayout;
class QSlider;
class QLabel;
class QPushButton;
class QButtonGroup;
class QComboBox;
class QCheckBox;
class QLineEdit;
class AppCheckBox;
class QFontComboBox;
class QDoubleSpinBox;
class QSpinBox;
class ScrubbableValueInput;
class BrushPresetPicker;
class BrushPresetManager;
class GradientPresetManager;
class GradientPreviewWidget;
class PopupPanel;
class QToolButton;
class AiObjectSelectionController;
class AiRemoveObjectController;
class TransformFieldsWidget;

class ToolOptionsBar : public QToolBar {
    Q_OBJECT

public:
    explicit ToolOptionsBar(const QString& title, QWidget* parent = nullptr);

    void setAuxiliaryOptionsWidget(QWidget* widget);
    bool setAuxiliaryOptionsVisible(bool visible);
    void setTool(int tool);
    void setSubToolsForTool(int tool, const QVector<int>& subTools, int activeSubTool);
    void setActiveSubTool(int tool, int subTool);
    // Clone Stamp sub-mode (0 = Clone, 1 = Healing) — selects which Clone Stamp
    // options page is shown.
    void setStampMode(int mode);

    int brushSize() const;
    int brushOpacity() const;
    int brushHardness() const;
    int brushFlow() const;
    QColor brushColor() const;
    BrushPresetManager* presetManager() const { return m_presetManager; }
    QPushButton* customShapesButton() const { return m_customShapesBtn; }

    void setBrushSize(int size);
    void setBrushOpacity(int opacity);
    void setBrushHardness(int hardness);
    void setBrushFlow(int flow);
    int brushMinPressure() const;
    void setBrushMinPressure(int percent);
    // Reflect the active brush's pen-pressure enable in the options-bar toggle
    // without re-emitting (used to mirror the panel / preset / QAction state).
    void setBrushPressureEnabled(bool on);
    bool brushPressureEnabled() const;
    // Reflect an externally selected preset in the options-bar picker(s).
    void setCurrentBrushPreset(const BrushPreset& preset);

    QFont textFont() const;
    int textSize() const;
    bool textBold() const;
    bool textItalic() const;
    bool textUnderline() const;
    bool textStrikethrough() const;
    QColor textColor() const;
    int textAlign() const;
    double textTracking() const;
    double textLeading() const;

    // Crop contextual shortcuts (Etapa 2): driven by the ShortcutManager while
    // the Crop tool is active. They mutate the existing crop controls so the UI
    // and the emitted signals stay the single source of truth.
    void cycleCropGuide();     // O: None → Thirds → Golden → Grid → None
    void swapCropAspect();     // X: swap the aspect-ratio orientation (W ↔ H)

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
    void setForegroundColor(const QColor& color);
    void setBackgroundColor(const QColor& color);
    GradientDefinition gradientDefinition() const;
    QPushButton* brushSettingsButton() const { return m_brushSettingsBtn; }

    bool moveAutoSelect() const;
    int  moveAutoSelectTarget() const;
    bool showTransformControls() const;
    bool transformProportionsLocked() const;

signals:
    void brushSizeChanged(int size);
    void brushOpacityChanged(int opacity);
    void brushHardnessChanged(int hardness);
    void brushFlowChanged(int flow);
    // Global minimum-pressure floor in percent (0..100). Overrides preset pressure.
    void brushMinPressureChanged(int percent);
    void brushColorChanged(const QColor& color);
    void brushPressureToggled(bool enabled);
    void presetSelected(const BrushPreset& preset);
    void brushSettingsRequested();
    void openBrushPanelRequested();
    void importBrushesRequested();
    void cloneAlignedChanged(bool aligned);
    void cloneSampleModeChanged(int mode);
    void cloneBlendModeChanged(int mode);
    void healingDiffusionChanged(int diffusion);
    void selectTypeChanged(int type);
    void selectModeChanged(int mode);
    void selectAntiAliasChanged(bool enabled);
    void selectAllClicked();
    void deselectClicked();
    void invertClicked();
    void generativeFillClicked();
    void cropToSelectionClicked();
    void refineEdgeClicked();
    void toleranceChanged(int value);
    void featherClicked(int radius);
    void growClicked(int pixels);
    void shrinkClicked(int pixels);
    void borderClicked(int pixels);
    void smoothClicked(int radius);

    void textFontChanged(const QFont& font);
    void textSizeChanged(int size);
    void textBoldChanged(bool bold);
    void textItalicChanged(bool italic);
    void textUnderlineChanged(bool underline);
    void textStrikethroughChanged(bool strikethrough);
    void textColorChanged(const QColor& color);
    void textAlignChanged(int align);
    void textTrackingChanged(double tracking);
    void textLeadingChanged(double leading);

    // Crop tool signals
    void cropAspectRatioChanged(const QSizeF& ratio);
    void cropCustomSizeChanged(int w, int h);
    void cropStraightenChanged(float angle);
    void cropGuideChanged(int type);
    void cropOverlayOpacityChanged(float opacity);
    void cropResetClicked();
    void cropCommitClicked();

    // Fill Bucket signals
    void fillBucketToleranceChanged(int value);
    void fillBucketColorChanged(const QColor& color);

    // Gradient tool signals
    void gradientDefinitionChanged(const GradientDefinition& definition);
    void gradientBlendModeChanged(int mode);
    void gradientOpacityChanged(int opacity);

    // Eyedropper tool signals
    void eyedropperSampleModeChanged(int mode);
    void eyedropperSampleSizeChanged(int size);
    void eyedropperForegroundPicked(const QColor& color);
    void eyedropperBackgroundPicked(const QColor& color);

    // Shape tool signals
    void shapeTypeChanged(int type);
    void shapeFillColorChanged(const QColor& color);
    void shapeFillEnabledChanged(bool enabled);
    void shapeStrokeColorChanged(const QColor& color);
    void shapeStrokeEnabledChanged(bool enabled);
    void shapeStrokeWidthChanged(int width);
    void shapeOpacityChanged(int opacity);
    void shapeAntiAliasChanged(bool enabled);
    void shapeCornerRadiusChanged(int radius);
    void shapeSidesChanged(int sides);
    void openCustomShapesPanelRequested();

    // Zoom tool signals
    void zoomFitClicked();
    void zoomOriginalClicked();

    // AI Object Selection tool: only Settings access needs MainWindow; all other
    // interactions go straight to the bound AiObjectSelectionController.
    void aiOpenSettingsRequested();

    // Move tool signals
    void moveAutoSelectChanged(bool enabled);
    void moveAutoSelectTargetChanged(int target);
    void moveShowTransformControlsChanged(bool show);
    void transformFieldEdited(int field, double value);
    void transformCancelClicked();
    void transformApplyClicked();
    void foregroundColorChanged(const QColor& color);
    void distortClicked();
    void perspectiveClicked();
    void distortResetClicked();
    void distortApplyClicked();

public slots:
    void setMoveAutoSelect(bool enabled);
    void setMoveAutoSelectTarget(int target);
    void setShowTransformControls(bool show);
    // Enable/disable + checked-state for the Distort/Perspective buttons.
    void setDistortControlsEnabled(bool enabled);
    void setDistortModeActive(int mode); // -1 none, 3 Distort, 4 Perspective
    // Reset button is shown only when the active layer actually has a warp.
    void setDistortResetEnabled(bool enabled);
    // Apply button is enabled while a distort edit session is active.
    void setDistortApplyEnabled(bool enabled);
    void showTransformOptions(bool show);
    void setTransformValues(double widthPx, double heightPx, double posX,
                            double posY, double rotationDeg);
    void setTransformFieldsEnabled(bool enabled);
    void setShapeType(int type);
    void setShapeFillColor(const QColor& color);
    void setShapeFillEnabled(bool enabled);
    void setShapeStrokeColor(const QColor& color);
    void setShapeStrokeEnabled(bool enabled);
    void setShapeStrokeWidth(int width);
    void setShapeOpacity(int opacity);
    void setShapeAntiAlias(bool enabled);
    void setShapeCornerRadius(int radius);
    void setShapeSides(int sides);

public:
    // Binds the AI Object Selection tool's options page to the active canvas's
    // controller (one per document). The page is this tool's view: it talks to
    // the controller directly for options/actions and reflects its status, while
    // settings access is surfaced to MainWindow via aiOpenSettingsRequested().
    void bindAiController(AiObjectSelectionController* controller);
    void bindAiRemoveController(AiRemoveObjectController* controller);
    void setAiToolPageMode(AiToolPageMode mode);

private slots:
    void pickColor();

private:
    QWidget* createBrushPage();
    QWidget* createAiSelectPage();
    QWidget* createAiModelsContent();   // popup body: selection / refine / bg-removal model selectors
    QWidget* createAiRefineContent();   // popup body: edge sliders + cleanup toggles
    QWidget* createAiRemoveContent();   // popup body: remove-object mask/prompt/inference options
    void refreshAiAvailability();
    void refreshAiModels();
    void refreshAiRemoveModels();
    void refreshAiToolPageMode();
    void pushAiRemoveOptions();
    void pushAiRefineOptions();         // collect popup widgets → controller
    QWidget* createSelectPage();
    QWidget* createSelectionOptionsContent();
    QWidget* createZoomPage();
    QWidget* createTextPage();
    QWidget* createMovePage();
    QWidget* createTransformPage();
    QWidget* createSkewPage();
    QWidget* createCropPage();
    QWidget* createFillBucketPage();
    QWidget* createEyedropperPage();
    QWidget* createShapePage();
    QWidget* createGradientPage();
    QWidget* createCloneStampPage();
    QWidget* createHealingBrushPage();
    void setGradientDefinition(const GradientDefinition& definition);
    void refreshGradientPreview();
    void refreshGradientPresetMenu();
    void openGradientEditor();

    QCheckBox* m_moveAutoSelect = nullptr;
    QComboBox* m_moveAutoSelectTarget = nullptr;
    QCheckBox* m_moveShowTransformControls = nullptr;
    TransformFieldsWidget* m_transformFields = nullptr;
    QPushButton* m_transformCancelBtn = nullptr;
    QPushButton* m_transformApplyBtn = nullptr;
    QPushButton* m_distortBtn = nullptr;
    QPushButton* m_perspectiveBtn = nullptr;
    QPushButton* m_distortResetBtn = nullptr;
    QPushButton* m_distortApplyBtn = nullptr;

    QWidget* m_optionsHost = nullptr;
    QHBoxLayout* m_optionsLayout = nullptr;
    QStackedWidget* m_stack = nullptr;
    QWidget* m_auxiliaryOptionsWidget = nullptr;
    int m_stampMode = 0;  // Clone Stamp sub-mode: 0 = Clone, 1 = Healing

    // ── AI Object Selection tool page ──
    AiToolPageMode m_aiToolPageMode = AiToolPageMode::ObjectSelection;
    AiObjectSelectionController* m_aiController = nullptr;
    AiRemoveObjectController* m_aiRemoveController = nullptr;
    QComboBox*   m_aiModelCombo = nullptr;
    QComboBox*   m_aiSampleCombo = nullptr;
    QButtonGroup* m_aiOpGroup = nullptr;
    QWidget*     m_aiOpRow = nullptr;
    AppCheckBox* m_aiAntiAliasCb = nullptr;
    AppCheckBox* m_aiRefineCb = nullptr;   // Etapa 3: enable edge refinement / matting
    QComboBox*   m_aiRefineModelCombo = nullptr; // matting model (Auto / installed / None)
    QComboBox*   m_aiBgEngineCombo = nullptr;    // Remove Background engine
    QComboBox*   m_aiInpaintModelCombo = nullptr; // remove/inpaint model (Auto / installed)
    QComboBox*   m_aiRemoveEngineCombo = nullptr;
    QComboBox*   m_aiRemoveOutputCombo = nullptr;
    QLabel*      m_aiRemoveOutputLabel = nullptr;
    QComboBox*   m_aiRemoveSampleCombo = nullptr;
    QPushButton* m_aiRemoveOptionsBtn = nullptr;
    QPushButton* m_aiRemoveCancelBtn = nullptr;
    QPushButton* m_aiRefineOptionsBtn = nullptr; // opens the refine popup
    QPushButton* m_aiRefineNowBtn = nullptr;     // refine the active selection
    QPushButton* m_aiSelectSubjectBtn = nullptr;
    QPushButton* m_aiRemoveBgBtn = nullptr;
    QLabel*      m_aiStatusLabel = nullptr;
    QWidget*     m_aiWarningRow = nullptr;
    QPushButton* m_aiOpenSettingsBtn = nullptr;

    // Model-settings popup: groups the object-selection, mask-refinement and
    // background-removal model selectors behind the ai-models button.
    QPushButton* m_aiModelsBtn = nullptr;
    PopupPanel*  m_aiModelsPanel = nullptr;

    // Refine popup body widgets (edge shaping + cleanup).
    PopupPanel*  m_aiRefinePanel = nullptr;
    PopupPanel*  m_aiRemovePanel = nullptr;
    ScrubbableValueInput* m_aiRemoveGrowSlider = nullptr;
    ScrubbableValueInput* m_aiRemoveFeatherSlider = nullptr;
    ScrubbableValueInput* m_aiRemovePaddingSlider = nullptr;
    ScrubbableValueInput* m_aiRemoveStepsSlider = nullptr;
    ScrubbableValueInput* m_aiRemoveStrengthSlider = nullptr;
    ScrubbableValueInput* m_aiRemoveGuidanceSlider = nullptr;
    ScrubbableValueInput* m_aiRemoveSeedSlider = nullptr;
    QLineEdit* m_aiRemovePromptEdit = nullptr;
    QLineEdit* m_aiRemoveNegativePromptEdit = nullptr;
    AppCheckBox* m_aiRemoveRandomSeedCb = nullptr;
    ScrubbableValueInput* m_aiSmoothSlider = nullptr;
    ScrubbableValueInput* m_aiFeatherSlider = nullptr;
    ScrubbableValueInput* m_aiContrastSlider = nullptr;
    ScrubbableValueInput* m_aiShiftSlider = nullptr;
    AppCheckBox* m_aiRemoveIslandsCb = nullptr;
    AppCheckBox* m_aiFillHolesCb = nullptr;
    AppCheckBox* m_aiPreserveSoftCb = nullptr;

    ScrubbableValueInput* m_sizeSlider = nullptr;
    ScrubbableValueInput* m_opacitySlider = nullptr;
    ScrubbableValueInput* m_hardnessSlider = nullptr;
    ScrubbableValueInput* m_flowSlider = nullptr;
    ScrubbableValueInput* m_minPressureSlider = nullptr;
    BrushPresetPicker* m_presetPicker = nullptr;
    BrushPresetPicker* m_clonePresetPicker = nullptr;
    BrushPresetManager* m_presetManager = nullptr;
    QPushButton* m_colorBtn = nullptr;
    QPushButton* m_brushSettingsBtn = nullptr;
    QPushButton* m_brushPressureBtn = nullptr;
    ScrubbableValueInput* m_cloneSizeSlider = nullptr;
    ScrubbableValueInput* m_cloneOpacitySlider = nullptr;
    ScrubbableValueInput* m_cloneHardnessSlider = nullptr;
    ScrubbableValueInput* m_cloneFlowSlider = nullptr;
    QComboBox* m_cloneBlendModeCombo = nullptr;
    AppCheckBox* m_cloneAlignedCb = nullptr;
    QComboBox* m_cloneSampleCombo = nullptr;

    BrushPresetPicker* m_healingPresetPicker = nullptr;
    ScrubbableValueInput* m_healingSizeSlider = nullptr;
    ScrubbableValueInput* m_healingHardnessSlider = nullptr;
    ScrubbableValueInput* m_healingOpacitySlider = nullptr;
    ScrubbableValueInput* m_healingFlowSlider = nullptr;
    ScrubbableValueInput* m_healingDiffusionSlider = nullptr;
    AppCheckBox* m_healingAlignedCb = nullptr;
    QComboBox* m_healingSampleCombo = nullptr;

    ScrubbableValueInput* m_fillToleranceSlider = nullptr;
    QPushButton* m_fillColorBtn = nullptr;
    QColor m_foreground = Qt::black;
    QColor m_background = Qt::white;

    GradientPresetManager* m_gradientPresetManager = nullptr;
    QToolButton* m_gradientPreviewBtn = nullptr;
    QButtonGroup* m_gradientKindGroup = nullptr;
    QComboBox* m_gradientBlendModeCombo = nullptr;
    ScrubbableValueInput* m_gradientOpacityInput = nullptr;
    AppCheckBox* m_gradientReverseCb = nullptr;
    AppCheckBox* m_gradientDitherCb = nullptr;
    AppCheckBox* m_gradientTransparencyCb = nullptr;
    QComboBox* m_gradientMethodCombo = nullptr;
    GradientDefinition m_gradientDefinition;

    QButtonGroup* m_selectTypeGroup = nullptr;
    QButtonGroup* m_selectModeGroup = nullptr;
    QCheckBox* m_selectAntiAliasCb = nullptr;
    QHash<int, QPushButton*> m_selectTypeButtons;
    ScrubbableValueInput* m_selectToleranceSlider = nullptr;
    ScrubbableValueInput* m_selectSizeSlider = nullptr;
    QPushButton* m_selectionOptionsBtn = nullptr;
    PopupPanel* m_selectionOptionsPanel = nullptr;
    bool m_selectionOptionsSuppressClick = false;

    // Crop tool widgets
    QComboBox* m_cropRatioCombo = nullptr;
    QSpinBox* m_cropWidthSpin = nullptr;
    QSpinBox* m_cropHeightSpin = nullptr;
    ScrubbableValueInput* m_cropStraightenSlider = nullptr;
    QComboBox* m_cropGuideCombo = nullptr;
    ScrubbableValueInput* m_cropOverlaySlider = nullptr;
    QPushButton* m_cropResetBtn = nullptr;
    QPushButton* m_cropCommitBtn = nullptr;

    // Shape tool widgets
    QButtonGroup* m_shapeTypeGroup = nullptr;
    QPushButton* m_customShapesBtn = nullptr;
    AppCheckBox* m_shapeFillCb = nullptr;
    QPushButton* m_shapeFillColorBtn = nullptr;
    AppCheckBox* m_shapeStrokeCb = nullptr;
    QPushButton* m_shapeStrokeColorBtn = nullptr;
    ScrubbableValueInput* m_shapeStrokeWidthSlider = nullptr;
    QCheckBox* m_shapeAntiAliasCb = nullptr;
    ScrubbableValueInput* m_shapeOpacitySlider = nullptr;
    ScrubbableValueInput* m_shapeCornerRadiusSlider = nullptr;
    ScrubbableValueInput* m_shapeSidesInput = nullptr;
    QLabel* m_shapeCornerRadiusLabel = nullptr;
    QColor m_shapeFillColor{Qt::transparent};
    QColor m_shapeStrokeColor{Qt::black};

    QFontComboBox* m_fontCombo = nullptr;
    ScrubbableValueInput* m_fontSizeSlider = nullptr;
    ScrubbableValueInput* m_trackingSlider = nullptr;
    ScrubbableValueInput* m_leadingSlider = nullptr;
    QPushButton* m_boldBtn = nullptr;
    QPushButton* m_italicBtn = nullptr;
    QPushButton* m_underlineBtn = nullptr;
    QPushButton* m_strikethroughBtn = nullptr;
    QPushButton* m_textColorBtn = nullptr;
    QButtonGroup* m_alignGroup = nullptr;

    QPointer<QDialog> m_brushColorDlg;
    QPointer<QDialog> m_fillColorDlg;
    QPointer<QDialog> m_textColorDlg;
    QPointer<QDialog> m_shapeFillDlg;
    QPointer<QDialog> m_shapeStrokeDlg;
};
