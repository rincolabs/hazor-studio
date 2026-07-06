#pragma once

#include <QWidget>
#include "brush/BrushTypes.hpp"
#include "brush/DynamicsConfig.hpp"
#include "brush/BrushPresetManager.hpp"

class QSlider;
class QLabel;
class QComboBox;
class QCheckBox;
class QPushButton;
class QToolButton;
class QMenu;
class QListWidget;
class QListWidgetItem;
class QListView;
class QStackedWidget;
class QTabWidget;
class QTimer;

class BrushTipPreview;
class BrushPresetListModel;
class BrushPresetItemDelegate;
class BrushPreviewCache;
class StrokePreviewWidget;
class AppCheckBox;
class CheckableRow;
class ResponseCurveEditor;
class BrushTexturePanel;
class BrushResourceBrowser;
class BrushLibraryListView;

// "Brush Settings" panel, laid out as four columns:
//   1. Brushes      — the preset list (same model/delegate/cache as the Brush
//                     panel); selecting one applies it.
//   2. Funcionalidades — a flat column of AppCheckBoxes, one per feature. The
//                     first six (Size/Opacity/Flow/Rotation/Ratio/Scattering)
//                     are sensor-driven curve options; the rest have their own
//                     control pages. The checkbox toggles the feature; clicking
//                     selects it.
//   3. Sensores     — for a curve feature, the list of sensor AppCheckBoxes (any
//                     number can be active); for a non-curve feature,
//                     that feature's own controls.
//   4. Curva        — the response-curve editor for the selected (feature, sensor)
//                     plus the option's min/max + sensor-combine (and, for
//                     Scattering, the X/Y axis switches). Empty for non-curve
//                     features.
// A compact debounced stroke preview spans the bottom.
class BrushDynamicsPanel : public QWidget {
    Q_OBJECT

public:
    explicit BrushDynamicsPanel(QWidget* parent = nullptr);
    ~BrushDynamicsPanel() override;

    void setPresetManager(BrushPresetManager* manager);
    void setFromSettings(const BrushSettings& s);
    void setCurrentPreset(const QString& name);
    BrushSettings currentSettings() const;

    QSize sizeHint() const override;   // 800px default width

    // True when the live brush state differs from the selected preset as last
    // loaded/saved (and a named preset is actually selected). Drives the
    // "save changes?" prompt on preset switch / panel close.
    bool hasUnsavedChanges() const;

    // External master-toggle sync (Options Bar / QAction). Updates the panel
    // without re-emitting; the fine pressure controls stay as configured.
    void setPressureEnabled(bool on);

signals:
    void sizeChanged(float size);
    void hardnessChanged(float hardness);
    void angleChanged(float radians);
    void roundnessChanged(float roundness);
    void flipXChanged(bool on);
    void flipYChanged(bool on);
    void colorDynamicsChanged(const ColorDynamics& d);
    void textureConfigChanged(const TextureConfig& d);
    void dualBrushConfigChanged(const DualBrushConfig& d);
    void blendModeChanged(BrushBlendMode m);
    void smoothingModeChanged(SmoothingMode m);
    void smoothingRadiusChanged(float r);
    void spacingChanged(float s);
    void airbrushChanged(bool a);
    void wetEdgesChanged(bool w);
    // Image brush-tip selection. A null image reverts to the procedural round tip.
    void tipImageChanged(const QImage& tip);
    // Everything that has no dedicated per-field signal (AutoBrush config,
    // auto-spacing, image-tip remap, the curve options incl. scatter + axes, and
    // the airbrush rate). Carries the full settings; MainWindow applies the
    // fields to the canvas.
    void advancedSettingsChanged(const BrushSettings& s);
    void presetSelected(const BrushPreset& preset);
    void presetSaveRequested();
    void importBrushesRequested();

protected:
    // Intercepts the host dock's close ("X") to offer saving unsaved preset edits;
    // a "Cancelar" answer vetoes the close.
    bool eventFilter(QObject* watched, QEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    // Non-curve feature pages whose enabled flag lives on the feature checkbox
    // (see categoryEnabled/setCategoryEnabled).
    enum Category {
        CatTipShape = 0,
        CatTexture,
        CatDual,
        CatColor,
        CatCount
    };

    // Flat "Funcionalidades" column. The first six are sensor/curve driven
    // (Scattering's option carries the offset strength; its axes live in col 4).
    enum Feature {
        FSize = 0, FOpacity, FFlow, FRotation, FRatio, FScatter,
        FTipShape, FTexture, FDual, FColor,
        FeatureCount
    };
    static bool isCurveFeature(Feature f) { return f <= FScatter; }

    static constexpr int kSensorCount = 13;   // SensorType Pressure..TiltDirection
    // Funcionalidades and (in 4-column mode) Sensores share this width; the curve
    // column always takes the remaining, larger space.
    static constexpr int kSideColWidth = 150;
    static constexpr int kPanelDefaultWidth = 1000;

    QWidget* createTipPage();          // Brush Tip feature: Texture + Options tabs
    QWidget* createTipTextureTab();    // tip library grid (BrushResourceBrowser)
    QWidget* createTipShapePage();     // the Options tab contents
    void reloadTips();                 // feed the tip browser from BrushTipLibrary
    void onTipSelected(const QString& id);
    QWidget* createColorPage();
    QWidget* createTexturePage();
    QWidget* createDualPage();
    QWidget* createGeneralContent();
    QWidget* buildPresetColumn();    // col 1
    QWidget* buildFeatureColumn();   // col 2
    QWidget* buildSensorColumn();    // col 3 (sensor page + non-curve feature pages)
    QWidget* buildCurveColumn();     // col 4

    void setActiveFeature(Feature f);
    void syncSensorChecks();              // sensor checkbox states for the active option
    void bindCurveToSensor(int sensorType); // col 4 ← option.sensors[type].curve
    CurveOption* activeOption();          // option for m_activeFeature, or nullptr
    CurveOption* optionForFeature(Feature f); // option for a given curve feature

    bool categoryEnabled(Category c) const;
    void setCategoryEnabled(Category c, bool on);

    // Push the relevant widget values into m_settings and emit the matching
    // signal; the enabled flag comes from the sidebar checkbox.
    void emitColor();
    void emitTexture();
    void emitDual();
    // Push the AutoBrush / auto-spacing / tip-remap / airbrush-rate / CurveOption
    // controls into m_settings and emit advancedSettingsChanged.
    void emitAdvanced();

    void schedulePreview();
    void applyTheme();

    BrushTipPreview* m_tipPreview = nullptr;
    StrokePreviewWidget* m_strokePreview = nullptr;
    QTimer* m_previewDebounce = nullptr;

    // Col 1: the shared brush library list (search + folder tree), reused from
    // the Brushes panel. m_selectedPresetName tracks the row for Save/Delete.
    BrushLibraryListView* m_list = nullptr;
    QString m_selectedPresetName;

    // ── Unsaved-changes tracking (Preset Manager) ─────────────────
    // Compact JSON of the selected preset as last loaded/saved; the live state is
    // "dirty" when its JSON differs. Empty when no named preset is selected.
    QString m_baselinePresetJson;
    QString m_baselinePresetGroup;
    // Captures the JSON baseline for the currently selected preset (the panel's
    // live settings are assumed to match it at call time).
    void captureBaseline();
    // Compact JSON for 'settings' as the named preset, for dirty comparison.
    QString presetJson(const QString& name, const QString& group,
                       const BrushSettings& settings) const;
    // If there are unsaved edits, ask Cancelar/Descartar/Salvar. Returns false
    // only when the user chose Cancelar (caller should abort the switch/close).
    // Salvar overwrites the selected preset; Descartar drops the edits.
    bool confirmDiscardChanges();

    // Col 2 funcionalidades.
    CheckableRow* m_featureChecks[FeatureCount] = {};
    Feature m_activeFeature = FSize;

    // Col 3 host: shared sensor page + one page per non-curve feature.
    QStackedWidget* m_col3Stack = nullptr;
    int m_col3Index[FeatureCount] = {};
    CheckableRow* m_sensorChecks[kSensorCount] = {};
    int m_activeSensor = 0;   // SensorType index for the active curve option

    // Col 4 curve component. The Max slider doubles as the option strength: its
    // range is 0..100% for most axes and 0..500% for Scattering. The axis row
    // (X/Y) is visible only for Scattering.
    QWidget* m_curvePane = nullptr;
    QLabel* m_curveTitle = nullptr;
    ResponseCurveEditor* m_curveEditor = nullptr;
    QSlider* m_curveMin = nullptr;   QLabel* m_curveMinLabel = nullptr;
    QSlider* m_curveMax = nullptr;   QLabel* m_curveMaxLabel = nullptr;
    QComboBox* m_curveCombine = nullptr;
    QWidget* m_scatterAxesRow = nullptr;
    QCheckBox* m_scatterAxisX = nullptr;
    QCheckBox* m_scatterAxisY = nullptr;

    struct {
        QSlider* size = nullptr;        QLabel* sizeLabel = nullptr;
        QSlider* hardness = nullptr;    QLabel* hardnessLabel = nullptr;
        QSlider* angle = nullptr;       QLabel* angleLabel = nullptr;
        QSlider* roundness = nullptr;   QLabel* roundnessLabel = nullptr;
        QSlider* spacing = nullptr;     QLabel* spacingLabel = nullptr;
        QCheckBox* flipX = nullptr;
        QCheckBox* flipY = nullptr;
    } m_tip;

    // Brush Tip "Texture" tab: shared resource grid (BrushTip mode) backed by the
    // global BrushTipLibrary. m_currentTipId is the selected tip's library id.
    BrushResourceBrowser* m_tipBrowser = nullptr;
    QString m_currentTipId;

    struct {
        QSlider* hue = nullptr;    QLabel* hueLabel = nullptr;
        QSlider* sat = nullptr;    QLabel* satLabel = nullptr;
        QSlider* bri = nullptr;    QLabel* briLabel = nullptr;
        QSlider* purity = nullptr; QLabel* purityLabel = nullptr;
    } m_color;

    // Texture is its own component (grid + options).
    BrushTexturePanel* m_texturePanel = nullptr;

    struct {
        QSlider* size = nullptr;       QLabel* sizeLabel = nullptr;
        QSlider* hardness = nullptr;   QLabel* hardnessLabel = nullptr;
        QComboBox* type = nullptr;
    } m_dual;

    struct {
        QComboBox* smoothingMode = nullptr;
        QSlider* smoothingRadius = nullptr; QLabel* smoothingRadiusLabel = nullptr;
        QComboBox* blendMode = nullptr;
        QCheckBox* airbrush = nullptr;
        QSlider* airbrushRate = nullptr;    QLabel* airbrushRateLabel = nullptr;
        QCheckBox* wetEdges = nullptr;
    } m_general;

    // Layer A/B/C controls (no per-field signal — funnelled through emitAdvanced).
    struct {
        QComboBox* profile = nullptr;        // AutoBrush profile (Default/Soft/Gaussian)
        QSlider* spikes = nullptr;           QLabel* spikesLabel = nullptr;
        QSlider* density = nullptr;          QLabel* densityLabel = nullptr;
        QCheckBox* autoSpacing = nullptr;
        QSlider* autoSpacingCoeff = nullptr; QLabel* autoSpacingCoeffLabel = nullptr;
        QSlider* tipBrightness = nullptr;    QLabel* tipBrightnessLabel = nullptr;
        QSlider* tipContrast = nullptr;      QLabel* tipContrastLabel = nullptr;
        QSlider* tipMidpoint = nullptr;      QLabel* tipMidpointLabel = nullptr;
    } m_adv;

    // Quick-access top bar (Brush Settings only): icon buttons that call the
    // shared BrushLibraryListView Preset Manager actions — no logic duplicated.
    QWidget* buildPresetTopBar();
    // Re-evaluate every top-bar button's enabled state from the current selection,
    // built-in/protected status and unsaved-changes flag.
    void updatePresetActionStates();

    struct {
        QToolButton* menuBtn = nullptr;       // ≡ Preset Manager menu
        BrushPresetManager* manager = nullptr;
        // Top-bar action buttons (see buildPresetTopBar).
        QToolButton* newBtn = nullptr;
        QToolButton* saveBtn = nullptr;
        QToolButton* saveAsBtn = nullptr;
        QToolButton* duplicateBtn = nullptr;
        QToolButton* deleteBtn = nullptr;
        QToolButton* newFolderBtn = nullptr;
        QToolButton* reloadBtn = nullptr;
    } m_presets;

    // Live brush state — the single source of truth the preview renders and a
    // saved preset captures.
    BrushSettings m_settings;
    // UI-only dab orientation/flip (no field in BrushSettings / preset yet — see
    // Phase 2): kept so the circular control and its sliders stay in sync.
    float m_uiAngle = 0.0f;     // radians
    float m_uiRoundness = 1.0f; // 0..1
    bool m_uiFlipX = false;
    bool m_uiFlipY = false;

    bool m_updating = false;
    bool m_hasExternalManager = false;

    static constexpr int kSizeSliderMax = 5000;
};
