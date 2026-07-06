#include "BrushDynamicsPanel.hpp"
#include "BrushTipPreview.hpp"
#include "BrushCurveOptionEditor.hpp"
#include "CheckableRow.hpp"
#include "BrushTexturePanel.hpp"
#include "BrushResourceBrowser.hpp"
#include "BrushLibraryListView.hpp"
#include "brush/BrushPreviewCache.hpp"
#include "brush/BrushPreviewRenderer.hpp"
#include "brush/BrushTipLibrary.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/IconUtils.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QComboBox>
#include "ui/AppCheckBox.hpp"
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QListWidget>
#include <QListView>
#include <QStackedWidget>
#include <QTabWidget>
#include <QScrollArea>
#include <QFrame>
#include <QLineEdit>
#include <QBuffer>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QSignalBlocker>
#include <QTimer>
#include <QPainter>
#include <QPixmap>
#include <QResizeEvent>
#include <QShowEvent>
#include <QCloseEvent>
#include <QDockWidget>
#include <QJsonDocument>
#include <QFont>
#include <algorithm>
#include <cmath>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    const char *kSmoothingNames[] = {"Basic", "Off", "Pulled String", "Cubic Spline"};

    const char *kBlendNames[] = {
        "Normal", "Multiply", "Screen", "Overlay",
        "Darken", "Lighten", "Color Dodge", "Color Burn",
        "Hard Light", "Soft Light", "Difference", "Exclusion",
        "Hue", "Saturation", "Color", "Luminosity"};

    QWidget *makeSliderRow(const QString &label, QSlider *slider, QLabel *valueLabel,
                           QWidget *parent, int min = 0, int max = 100, int def = 0)
    {
        auto *row = new QWidget(parent);
        auto *lay = new QHBoxLayout(row);
        lay->setContentsMargins(0, 0, 0, 0);
        auto *lbl = new QLabel(label, row);
        lbl->setFixedWidth(70);
        slider->setRange(min, max);
        slider->setValue(def);
        slider->setOrientation(Qt::Horizontal);
        lay->addWidget(lbl);
        lay->addWidget(slider, 1);
        if (valueLabel)
        {
            valueLabel->setFixedWidth(38);
            valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
            lay->addWidget(valueLabel);
        }
        return row;
    }
} // namespace

// Large debounced stroke preview, rendered through the real CPU dab pipeline so
// it reflects tip / size / hardness / spacing / dynamics exactly like a stroke.
// Rendering is debounced and cached to a pixmap so dragging sliders never blocks.
class StrokePreviewWidget : public QWidget
{
public:
    explicit StrokePreviewWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(78);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_debounce = new QTimer(this);
        m_debounce->setSingleShot(true);
        m_debounce->setInterval(110);
        QObject::connect(m_debounce, &QTimer::timeout, this, [this]()
                         {
            rerender();
            update(); });
    }

    void setSettings(const BrushSettings &s)
    {
        m_settings = s;
        m_debounce->start(); // coalesce rapid slider changes into one render
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const Theme *t = ThemeManager::instance()->current();
        QPainter p(this);
        p.fillRect(rect(), t->colorSurfaceDark);
        p.setPen(t->colorBorder);
        p.setBrush(Qt::NoBrush);
        p.drawRect(rect().adjusted(0, 0, -1, -1));
        if (m_pixmap.isNull())
            rerender();
        if (!m_pixmap.isNull())
            p.drawPixmap(0, 0, m_pixmap);
    }

    void resizeEvent(QResizeEvent *e) override
    {
        QWidget::resizeEvent(e);
        m_debounce->start();
    }

private:
    void rerender()
    {
        if (width() <= 2 || height() <= 2)
        {
            m_pixmap = QPixmap();
            return;
        }
        const Theme *t = ThemeManager::instance()->current();
        const QColor ink = t->colorTextBright; // a white-ish stroke on dark bg
        const QImage img = BrushPreviewRenderer::renderStroke(
            m_settings, size(), devicePixelRatioF(), ink);
        m_pixmap = QPixmap::fromImage(img);
    }

    BrushSettings m_settings;
    QPixmap m_pixmap;
    QTimer *m_debounce = nullptr;
};

BrushDynamicsPanel::BrushDynamicsPanel(QWidget *parent)
    : QWidget(parent)
{
    const Theme *t = ThemeManager::instance()->current();
    setStyleSheet(t->brushDynamicsStyleSheet());

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(t->spaceSM, t->spaceSM, t->spaceSM, t->spaceSM);
    root->setSpacing(t->spaceSM);

    // ── Four columns: brushes | funcionalidades | sensores | curva ──
    auto *cols = new QHBoxLayout;
    cols->setContentsMargins(0, 0, 0, 0);
    cols->setSpacing(t->spaceSM);
    cols->addWidget(buildPresetColumn());     // col 1
    cols->addWidget(buildFeatureColumn());    // col 2
    cols->addWidget(buildSensorColumn(), 1);  // col 3
    cols->addWidget(buildCurveColumn(), 1);   // col 4
    root->addLayout(cols, 1);

    auto *sep = new QFrame(this);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorBorder.name()));
    root->addWidget(sep);

    // Compact stroke preview spanning the bottom.
    m_strokePreview = new StrokePreviewWidget(this);
    root->addWidget(m_strokePreview);

    // Default brush: pen pressure → size on, matching CanvasView's
    // default so the panel and the canvas agree before any preset is applied.
    m_settings.sizeOption.setPressureSensorEnabled(true);

    // Reflect the default brush in the controls + preview, then open Size.
    setFromSettings(m_settings);
    setActiveFeature(FSize);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &BrushDynamicsPanel::applyTheme);
    applyTheme();
}

BrushDynamicsPanel::~BrushDynamicsPanel() = default;

void BrushDynamicsPanel::applyTheme()
{
    const Theme *t = ThemeManager::instance()->current();
    setStyleSheet(t->brushDynamicsStyleSheet());

    if (m_strokePreview)
        m_strokePreview->update();
    if (m_tipPreview)
        m_tipPreview->update();
}

// ── Col 1: preset list ────────────────────────────────────────

QWidget *BrushDynamicsPanel::buildPresetColumn()
{
    const Theme *t = ThemeManager::instance()->current();

    auto *wrap = new QWidget(this);
    wrap->setFixedWidth(300);
    auto *lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(t->spaceXS);

    // Header: title + Preset Manager (≡) menu.
    auto *header = new QWidget(wrap);
    auto *hl = new QHBoxLayout(header);
    hl->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(tr("Brush Presets"), header);
    title->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    m_presets.menuBtn = new QToolButton(header);
    m_presets.menuBtn->setText(QStringLiteral("☰"));
    m_presets.menuBtn->setToolTip(tr("Preset Manager"));
    m_presets.menuBtn->setPopupMode(QToolButton::InstantPopup);
    m_presets.menuBtn->setAutoRaise(true);
    hl->addWidget(title);
    hl->addStretch();
    hl->addWidget(m_presets.menuBtn);
    lay->addWidget(header);

    // The brush list (with folder listing) is the shared library widget, here in
    // full management mode (save/duplicate/delete/rename/new + drag&drop).
    m_list = new BrushLibraryListView(BrushPresetListMode::SettingsManager, wrap);

    // Quick-access top bar above the list (Brush Settings only). Built after the
    // list so its buttons can target the list's actions.
    lay->addWidget(buildPresetTopBar());
    lay->addWidget(m_list, 1);

    // The Preset Manager menu and the list's Save/Save-As actions read the live
    // brush state from the panel.
    m_list->setCurrentSettingsProvider([this]() { return currentSettings(); });
    auto *presetMenu = new QMenu(m_presets.menuBtn);
    m_list->populateManagerMenu(presetMenu);
    m_presets.menuBtn->setMenu(presetMenu);

    // Keep the top-bar enabled states in sync with the selection and the library.
    connect(m_list, &BrushLibraryListView::selectionChanged, this,
            [this]() { updatePresetActionStates(); });

    connect(m_list, &BrushLibraryListView::presetSelected, this,
            [this](const BrushPreset &p) {
                if (m_updating) return;
                // Re-selecting the same preset (e.g. after a Save) reloads it
                // without prompting; setFromSettings re-establishes the baseline.
                if (p.name == m_selectedPresetName) {
                    setFromSettings(p.settings);
                    emit presetSelected(p);
                    return;
                }
                // Switching to a different preset: offer to save pending edits to
                // the one we're leaving. Cancelar keeps the previous preset both
                // live and selected.
                if (!confirmDiscardChanges()) {
                    const QString keep = m_selectedPresetName;
                    if (!keep.isEmpty()) {
                        // Restore the selection on the next tick: doing it from
                        // inside the selection model's own currentChanged emission
                        // is unreliable. setCurrentPreset() suppresses the echo.
                        QTimer::singleShot(0, this, [this, keep]() {
                            m_list->setCurrentPreset(keep);
                        });
                    }
                    return;
                }
                m_selectedPresetName = p.name;
                setFromSettings(p.settings);   // captures the new baseline
                emit presetSelected(p);
            });
    connect(m_list, &BrushLibraryListView::importBrushesRequested, this,
            [this]() { emit importBrushesRequested(); });

    return wrap;
}

// ── Preset Manager quick-access top bar (Brush Settings only) ─────────────────
// Each button is a thin visual shortcut to a BrushLibraryListView action; no
// preset logic is reimplemented here. Enabled states come from
// updatePresetActionStates().
QWidget *BrushDynamicsPanel::buildPresetTopBar()
{
    const Theme *t = ThemeManager::instance()->current();

    auto *bar = new QWidget(this);
    bar->setObjectName(QStringLiteral("presetTopBar"));
    bar->setFixedHeight(34);
    bar->setStyleSheet(
        QStringLiteral("#presetTopBar { background: %1; border-radius: %2px; }")
            .arg(t->colorBackgroundTertiary.name())
            .arg(t->radiusMD));

    auto *bl = new QHBoxLayout(bar);
    bl->setContentsMargins(t->spaceXS, t->spaceXS, t->spaceXS, t->spaceXS);
    bl->setSpacing(2);

    const QString btnStyle =
        QStringLiteral(
            "QToolButton { background: transparent; border: none; border-radius: 3px; }"
            "QToolButton:hover:enabled { background: %1; }"
            "QToolButton:pressed:enabled { background: %2; }"
            "QToolButton:disabled { background: transparent; }")
            .arg(t->colorSurfaceHover.name(), t->colorSurfacePressed.name());

    auto makeBtn = [&](const QString &icon, const QString &tip) {
        auto *b = new QToolButton(bar);
        b->setIcon(makeIcon(icon));
        b->setIconSize(QSize(26, 26));
        b->setFixedSize(28, 28);
        b->setToolTip(tip);
        b->setFocusPolicy(Qt::NoFocus);
        b->setAutoRaise(true);
        b->setStyleSheet(btnStyle);
        bl->addWidget(b);
        return b;
    };
    auto addSeparator = [&]() {
        auto *line = new QFrame(bar);
        line->setFrameShape(QFrame::VLine);
        line->setFixedWidth(1);
        line->setStyleSheet(QStringLiteral("color: %1; background: %1;")
                                .arg(t->colorBorder.name()));
        bl->addSpacing(2);
        bl->addWidget(line);
        bl->addSpacing(2);
    };

    // Group 1: create / save.
    m_presets.newBtn    = makeBtn(QStringLiteral(":/icons/ui-new.png"),       tr("New Preset"));
    m_presets.saveBtn   = makeBtn(QStringLiteral(":/icons/ui-save.png"),      tr("Save Current Preset"));
    m_presets.saveAsBtn = makeBtn(QStringLiteral(":/icons/ui-save-as.png"),   tr("Save Current Preset As..."));
    addSeparator();
    // Group 2: act on the selected item.
    m_presets.duplicateBtn = makeBtn(QStringLiteral(":/icons/ui-duplicate.png"), tr("Duplicate Preset"));
    m_presets.deleteBtn    = makeBtn(QStringLiteral(":/icons/ui-delete.png"),    tr("Delete"));
    addSeparator();
    // Group 3: organise / refresh.
    m_presets.newFolderBtn = makeBtn(QStringLiteral(":/icons/ui-folder-new.png"), tr("New Folder"));
    m_presets.reloadBtn    = makeBtn(QStringLiteral(":/icons/ui-refresh.png"),    tr("Reload Presets"));
    bl->addStretch();

    // Wire each button straight to the shared list action (single implementation).
    connect(m_presets.newBtn,       &QToolButton::clicked, m_list, &BrushLibraryListView::createNewPreset);
    connect(m_presets.saveBtn,      &QToolButton::clicked, m_list, &BrushLibraryListView::saveCurrentPreset);
    connect(m_presets.saveAsBtn,    &QToolButton::clicked, m_list, &BrushLibraryListView::saveCurrentPresetAs);
    connect(m_presets.duplicateBtn, &QToolButton::clicked, m_list, &BrushLibraryListView::duplicateSelectedPreset);
    connect(m_presets.deleteBtn,    &QToolButton::clicked, m_list, &BrushLibraryListView::deleteSelected);
    connect(m_presets.newFolderBtn, &QToolButton::clicked, m_list, &BrushLibraryListView::createNewFolder);
    connect(m_presets.reloadBtn,    &QToolButton::clicked, m_list, &BrushLibraryListView::reloadPresets);

    updatePresetActionStates();
    return bar;
}

void BrushDynamicsPanel::updatePresetActionStates()
{
    if (!m_presets.newBtn || !m_list)
        return;

    const BrushLibraryListView::Selection sel = m_list->currentSelection();
    using Kind = BrushLibraryListView::SelectionKind;
    const bool isPreset  = sel.kind == Kind::Preset;
    const bool isFolder  = sel.kind == Kind::Folder;
    const bool isNone    = sel.kind == Kind::None;
    const bool protectedItem = sel.protectedItem;

    // Always available: create a new preset, Save As..., new folder, reload.
    m_presets.newBtn->setEnabled(true);
    m_presets.saveAsBtn->setEnabled(true);
    m_presets.newFolderBtn->setEnabled(true);
    m_presets.reloadBtn->setEnabled(true);

    // Save: a writable preset with pending edits. Protected selections (the virtual
    // root folders) have nothing to overwrite, so Save stays off — Save As... covers
    // the "store it somewhere" case.
    m_presets.saveBtn->setEnabled(isPreset && !protectedItem && hasUnsavedChanges());

    // Duplicate: any preset (the copy is always user-owned and editable).
    m_presets.duplicateBtn->setEnabled(isPreset);

    // Delete: a preset or a managed (non-protected) folder. Disabled for
    // nothing selected and for the protected virtual root folders.
    const bool canMutateSelected = (isPreset || isFolder) && !protectedItem;
    m_presets.deleteBtn->setEnabled(canMutateSelected);

    Q_UNUSED(isNone);
}

// ── Naming + feature/category mapping ─────────────────────────

namespace {
const char *kFeatureNames[] = {
    "Size", "Opacity", "Flow", "Rotation", "Ratio", "Scattering",
    "Brush Tip", "Texture", "Dual Brush", "Color Dyn."};
// One entry per SensorType, in enum order.
const char *kSensorNamesUi[] = {
    "Pressure", "Tilt X", "Tilt Y", "Tilt Elev.", "Speed",
    "Direction", "Rotation", "Distance", "Time", "Fade", "Random",
    "Rnd Stroke", "Tilt Dir."};
} // namespace

CurveOption *BrushDynamicsPanel::optionForFeature(Feature f)
{
    switch (f) {
    case FSize:     return &m_settings.sizeOption;
    case FOpacity:  return &m_settings.opacityOption;
    case FFlow:     return &m_settings.flowOption;
    case FRotation: return &m_settings.rotationOption;
    case FRatio:    return &m_settings.ratioOption;
    case FScatter:  return &m_settings.scatterOption;
    default:        return nullptr;
    }
}

CurveOption *BrushDynamicsPanel::activeOption()
{
    return optionForFeature(m_activeFeature);
}

// ── Col 2: funcionalidades ────────────────────────────────────

QWidget *BrushDynamicsPanel::buildFeatureColumn()
{
    const Theme *t = ThemeManager::instance()->current();

    auto *wrap = new QWidget(this);
    wrap->setObjectName(QStringLiteral("featureColumn"));
    wrap->setFixedWidth(kSideColWidth);
    wrap->setStyleSheet(QStringLiteral("#featureColumn { background: %1; border-radius: %2px; }")
                            .arg(t->colorBackgroundTertiary.name())
                            .arg(t->radiusMD));
    auto *lay = new QVBoxLayout(wrap);
    lay->setContentsMargins(t->spaceXS, t->spaceXS, t->spaceXS, t->spaceXS);
    lay->setSpacing(2);

    auto *title = new QLabel(tr("Funcionalidades"), wrap);
    title->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    lay->addWidget(title);

    for (int i = 0; i < FeatureCount; ++i) {
        const Feature f = static_cast<Feature>(i);
        auto *row = new CheckableRow(tr(kFeatureNames[i]), wrap);
        m_featureChecks[i] = row;
        lay->addWidget(row);

        // Tip Shape is always on (no enable concept): checked + non-toggleable,
        // but still selectable to open its controls.
        if (f == FTipShape) {
            row->setChecked(true);
            row->setToggleable(false);
        }

        // Selecting the row opens its page (col 3/4) without changing enable.
        connect(row, &CheckableRow::selected, this, [this, f]() {
            setActiveFeature(f);
        });
        // The indicator toggles the feature's enabled flag.
        connect(row, &CheckableRow::toggled, this, [this, f](bool checked) {
            if (isCurveFeature(f)) {
                if (CurveOption *opt = optionForFeature(f))
                    opt->enabled = checked;
                emitAdvanced();
            } else {
                switch (f) {
                case FTexture:  emitTexture();  break;
                case FDual:     emitDual();     break;
                case FColor:    emitColor();    break;
                default: break;
                }
            }
        });
    }
    lay->addStretch();
    return wrap;
}

// ── Col 3: sensores (curve features) / feature controls (the rest) ─────────

QWidget *BrushDynamicsPanel::buildSensorColumn()
{
    const Theme *t = ThemeManager::instance()->current();

    auto wrapScroll = [this](QWidget *page) -> QWidget * {
        auto *sa = new QScrollArea(this);
        sa->setWidgetResizable(true);
        sa->setFrameShape(QFrame::NoFrame);
        sa->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        sa->setWidget(page);
        return sa;
    };

    m_col3Stack = new QStackedWidget(this);

    // Page 0: the shared sensor list, reused by every curve feature.
    auto *sensorPage = new QWidget(m_col3Stack);
    sensorPage->setObjectName(QStringLiteral("sensorList"));
    sensorPage->setStyleSheet(QStringLiteral("#sensorList { background: %1; border-radius: %2px; }")
                                  .arg(t->colorBackgroundTertiary.name())
                                  .arg(t->radiusMD));
    auto *sl = new QVBoxLayout(sensorPage);
    sl->setContentsMargins(t->spaceXS, t->spaceXS, t->spaceXS, t->spaceXS);
    sl->setSpacing(2);
    auto *sTitle = new QLabel(tr("Sensores"), sensorPage);
    sTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    sl->addWidget(sTitle);
    for (int i = 0; i < kSensorCount; ++i) {
        auto *row = new CheckableRow(tr(kSensorNamesUi[i]), sensorPage);
        m_sensorChecks[i] = row;
        sl->addWidget(row);
        // Selecting the row edits this sensor's curve (col 4) without enabling it.
        connect(row, &CheckableRow::selected, this, [this, i]() {
            bindCurveToSensor(i);
        });
        // The indicator enables/disables this sensor in the active option.
        connect(row, &CheckableRow::toggled, this, [this, i](bool checked) {
            CurveOption *opt = activeOption();
            if (!opt)
                return;
            // Find or create the sensor entry, so its curve survives toggles.
            Sensor *s = nullptr;
            for (Sensor &sen : opt->sensors)
                if (static_cast<int>(sen.type) == i) { s = &sen; break; }
            if (!s) {
                Sensor ns;
                ns.type = static_cast<SensorType>(i);
                opt->sensors.append(ns);
                s = &opt->sensors.last();
            }
            s->enabled = checked;
            bindCurveToSensor(i);   // editing this sensor's curve now
            emitAdvanced();
        });
    }
    sl->addStretch();
    const int sensorIdx = m_col3Stack->addWidget(wrapScroll(sensorPage));
    for (int f = FSize; f <= FScatter; ++f)
        m_col3Index[f] = sensorIdx;

    // Non-curve feature pages (their own controls).
    m_col3Index[FTipShape] = m_col3Stack->addWidget(createTipPage());
    m_col3Index[FTexture]  = m_col3Stack->addWidget(wrapScroll(createTexturePage()));
    m_col3Index[FDual]     = m_col3Stack->addWidget(wrapScroll(createDualPage()));
    m_col3Index[FColor]    = m_col3Stack->addWidget(wrapScroll(createColorPage()));

    return m_col3Stack;
}

// ── Col 4: curve component ────────────────────────────────────

QWidget *BrushDynamicsPanel::buildCurveColumn()
{
    const Theme *t = ThemeManager::instance()->current();

    m_curvePane = new QWidget(this);
    auto *lay = new QVBoxLayout(m_curvePane);
    lay->setContentsMargins(t->spaceSM, 0, 0, 0);
    lay->setSpacing(t->spaceSM);
    m_curvePane->setFixedWidth(250);

    m_curveTitle = new QLabel(tr("Curva"), m_curvePane);
    m_curveTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    lay->addWidget(m_curveTitle);

    m_curveEditor = new ResponseCurveEditor(m_curvePane);
    lay->addWidget(m_curveEditor, 1);

    auto writeCurve = [this]() {
        if (m_updating)
            return;
        CurveOption *opt = activeOption();
        if (!opt)
            return;
        Sensor *s = nullptr;
        for (Sensor &sen : opt->sensors)
            if (static_cast<int>(sen.type) == m_activeSensor) { s = &sen; break; }
        if (!s) {
            Sensor ns;
            ns.type = static_cast<SensorType>(m_activeSensor);
            ns.enabled = false;
            opt->sensors.append(ns);
            s = &opt->sensors.last();
        }
        s->curve = m_curveEditor->curve();
        emitAdvanced();
    };
    connect(m_curveEditor, &ResponseCurveEditor::changed, this, writeCurve);
    connect(m_curveEditor, &ResponseCurveEditor::editCommitted, this, writeCurve);

    // Option-level range + sensor combine.
    m_curveMin = new QSlider(m_curvePane);
    m_curveMinLabel = new QLabel("0%", m_curvePane);
    lay->addWidget(makeSliderRow(tr("Min:"), m_curveMin, m_curveMinLabel, m_curvePane, 0, 100, 0));
    m_curveMax = new QSlider(m_curvePane);
    m_curveMaxLabel = new QLabel("100%", m_curvePane);
    lay->addWidget(makeSliderRow(tr("Max:"), m_curveMax, m_curveMaxLabel, m_curvePane, 0, 100, 100));

    auto *combRow = new QWidget(m_curvePane);
    auto *cl = new QHBoxLayout(combRow);
    cl->setContentsMargins(0, 0, 0, 0);
    auto *cLbl = new QLabel(tr("Combine:"), combRow);
    cLbl->setFixedWidth(70);
    // All sensor-combination modes the engine implements.
    m_curveCombine = new QComboBox(combRow);
    m_curveCombine->addItems({tr("Multiply"), tr("Add"), tr("Max"), tr("Min"),
                              tr("Difference")});
    cl->addWidget(cLbl);
    cl->addWidget(m_curveCombine, 1);
    lay->addWidget(combRow);

    // Scattering only: offset axes (X = along the stroke, Y = perpendicular;
    // both = free rectangular spread). Hidden for the other curve features.
    m_scatterAxesRow = new QWidget(m_curvePane);
    auto *axLay = new QHBoxLayout(m_scatterAxesRow);
    axLay->setContentsMargins(0, 0, 0, 0);
    auto *axLbl = new QLabel(tr("Axes:"), m_scatterAxesRow);
    axLbl->setFixedWidth(70);
    m_scatterAxisX = new AppCheckBox(tr("X"), m_scatterAxesRow);
    m_scatterAxisY = new AppCheckBox(tr("Y"), m_scatterAxesRow);
    axLay->addWidget(axLbl);
    axLay->addWidget(m_scatterAxisX);
    axLay->addWidget(m_scatterAxisY);
    axLay->addStretch();
    m_scatterAxesRow->hide();
    lay->addWidget(m_scatterAxesRow);
    lay->addStretch();

    connect(m_curveMin, &QSlider::valueChanged, this, [this](int v) {
        m_curveMinLabel->setText(QString::number(v) + "%");
        if (m_updating) return;
        if (CurveOption *opt = activeOption()) { opt->minValue = v / 100.0f; emitAdvanced(); }
    });
    connect(m_curveMax, &QSlider::valueChanged, this, [this](int v) {
        m_curveMaxLabel->setText(QString::number(v) + "%");
        if (m_updating) return;
        if (CurveOption *opt = activeOption()) { opt->maxValue = v / 100.0f; emitAdvanced(); }
    });
    connect(m_curveCombine, &QComboBox::currentIndexChanged, this, [this](int idx) {
        if (m_updating) return;
        if (CurveOption *opt = activeOption()) {
            opt->combine = static_cast<CurveOption::Combine>(idx);
            emitAdvanced();
        }
    });
    connect(m_scatterAxisX, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        m_settings.scatter.axisX = on;
        emitAdvanced();
    });
    connect(m_scatterAxisY, &QCheckBox::toggled, this, [this](bool on) {
        if (m_updating) return;
        m_settings.scatter.axisY = on;
        emitAdvanced();
    });

    return m_curvePane;
}

// ── Active selection plumbing ─────────────────────────────────

void BrushDynamicsPanel::setActiveFeature(Feature f)
{
    m_activeFeature = f;
    for (int i = 0; i < FeatureCount; ++i)
        if (m_featureChecks[i])
            m_featureChecks[i]->setSelected(i == static_cast<int>(f));

    if (m_col3Stack)
        m_col3Stack->setCurrentIndex(m_col3Index[f]);

    // 4-column mode (curve feature): the Sensores column matches Funcionalidades'
    // width and the curve column takes the rest. Otherwise col 3 holds the feature
    // controls and expands to fill (curve column hidden).
    if (m_col3Stack) {
        if (isCurveFeature(f)) {
            m_col3Stack->setFixedWidth(kSideColWidth);
        } else {
            m_col3Stack->setMinimumWidth(0);
            m_col3Stack->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    }

    if (isCurveFeature(f)) {
        // The Max slider doubles as the option strength: Scattering spans 0..500%
        // (a multiple of the dab diameter), every other axis 0..100%.
        if (m_curveMax) {
            QSignalBlocker b(m_curveMax);
            m_curveMax->setRange(0, f == FScatter ? 500 : 100);
        }
        if (m_scatterAxesRow)
            m_scatterAxesRow->setVisible(f == FScatter);
        if (m_scatterAxisX && f == FScatter) {
            QSignalBlocker bx(m_scatterAxisX);
            QSignalBlocker by(m_scatterAxisY);
            m_scatterAxisX->setChecked(m_settings.scatter.axisX);
            m_scatterAxisY->setChecked(m_settings.scatter.axisY);
        }
        syncSensorChecks();
        // Default to the first enabled sensor, else the first present, else Pressure.
        int sel = static_cast<int>(SensorType::Pressure);
        if (CurveOption *opt = activeOption()) {
            bool found = false;
            for (const Sensor &s : opt->sensors)
                if (s.enabled) { sel = static_cast<int>(s.type); found = true; break; }
            if (!found && !opt->sensors.isEmpty())
                sel = static_cast<int>(opt->sensors.first().type);
        }
        bindCurveToSensor(sel);
        if (m_curvePane) m_curvePane->setVisible(true);
    } else if (m_curvePane) {
        m_curvePane->setVisible(false);
    }
}

void BrushDynamicsPanel::syncSensorChecks()
{
    CurveOption *opt = activeOption();
    for (int i = 0; i < kSensorCount; ++i) {
        if (!m_sensorChecks[i])
            continue;
        bool on = false;
        if (opt)
            for (const Sensor &s : opt->sensors)
                if (static_cast<int>(s.type) == i && s.enabled) { on = true; break; }
        m_sensorChecks[i]->setChecked(on);   // no signal (CheckableRow blocks it)
    }
}

void BrushDynamicsPanel::bindCurveToSensor(int sensorType)
{
    m_activeSensor = sensorType;
    for (int i = 0; i < kSensorCount; ++i)
        if (m_sensorChecks[i])
            m_sensorChecks[i]->setSelected(i == sensorType);

    CurveOption *opt = activeOption();
    const Sensor *s = nullptr;
    if (opt)
        for (const Sensor &sen : opt->sensors)
            if (static_cast<int>(sen.type) == sensorType) { s = &sen; break; }

    if (m_curveEditor) {
        QSignalBlocker b(m_curveEditor);
        m_curveEditor->setCurve(s ? s->curve : ResponseCurve{});
    }
    if (m_curveTitle) {
        const int fi = static_cast<int>(m_activeFeature);
        m_curveTitle->setText(QStringLiteral("%1 · %2")
            .arg(tr(kFeatureNames[fi]))
            .arg(tr(kSensorNamesUi[std::clamp(sensorType, 0, kSensorCount - 1)])));
    }

    // Reflect the option's range/combine for this feature. The Max slider's
    // maximum is per-feature (500% for Scattering), set in setActiveFeature.
    if (opt) {
        if (m_curveMin) { QSignalBlocker b(m_curveMin); m_curveMin->setValue(std::clamp(int(std::lround(opt->minValue * 100.0f)), 0, m_curveMin->maximum())); m_curveMinLabel->setText(QString::number(m_curveMin->value()) + "%"); }
        if (m_curveMax) { QSignalBlocker b(m_curveMax); m_curveMax->setValue(std::clamp(int(std::lround(opt->maxValue * 100.0f)), 0, m_curveMax->maximum())); m_curveMaxLabel->setText(QString::number(m_curveMax->value()) + "%"); }
        if (m_curveCombine) { QSignalBlocker b(m_curveCombine); m_curveCombine->setCurrentIndex(static_cast<int>(opt->combine)); }
    }
}

bool BrushDynamicsPanel::categoryEnabled(Category c) const
{
    int f = -1;
    switch (c) {
    case CatTipShape: f = FTipShape; break;
    case CatTexture:  f = FTexture;  break;
    case CatDual:     f = FDual;     break;
    case CatColor:    f = FColor;    break;
    default: return false;
    }
    return m_featureChecks[f] && m_featureChecks[f]->isChecked();
}

void BrushDynamicsPanel::setCategoryEnabled(Category c, bool on)
{
    int f = -1;
    switch (c) {
    case CatTipShape: f = FTipShape; break;
    case CatTexture:  f = FTexture;  break;
    case CatDual:     f = FDual;     break;
    case CatColor:    f = FColor;    break;
    default: return;
    }
    if (m_featureChecks[f]) {
        QSignalBlocker b(m_featureChecks[f]);
        m_featureChecks[f]->setChecked(on);
    }
}

// ── Brush Tip (Texture grid + Options) ────────────────────────
// Mirrors the Texture page: a Texture tab (global tip library grid, BrushTip mode)
// and an Options tab holding the existing Brush Tip Shape controls. Selecting a
// tip applies it as the brush's image tip and saves its library id on the preset.

QWidget *BrushDynamicsPanel::createTipPage()
{
    auto *tabs = new QTabWidget(this);
    tabs->addTab(createTipTextureTab(), tr("Texture"));

    // Options tab — the existing tip-shape controls, kept scrollable (they are long).
    auto *optScroll = new QScrollArea(tabs);
    optScroll->setWidgetResizable(true);
    optScroll->setFrameShape(QFrame::NoFrame);
    optScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    optScroll->setWidget(createTipShapePage());
    tabs->addTab(optScroll, tr("Options"));

    return tabs;
}

QWidget *BrushDynamicsPanel::createTipTextureTab()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    m_tipBrowser = new BrushResourceBrowser(BrushResourceBrowserMode::BrushTip, page);
    lay->addWidget(m_tipBrowser, 1);

    connect(m_tipBrowser, &BrushResourceBrowser::selected, this,
            [this](const QString &id) { onTipSelected(id); });
    connect(m_tipBrowser, &BrushResourceBrowser::importRequested, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Import Brush Tip"), {},
            tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
        if (path.isEmpty()) return;
        const QString id = BrushTipLibrary::instance()->importFromFile(path);
        if (!id.isEmpty())
            onTipSelected(id);   // select the freshly imported one
    });
    connect(m_tipBrowser, &BrushResourceBrowser::removeRequested, this,
            [this](const QString &id) {
        if (!id.isEmpty())
            BrushTipLibrary::instance()->remove(id);
    });
    connect(BrushTipLibrary::instance(), &BrushTipLibrary::tipsChanged, this,
            [this]() { reloadTips(); });

    BrushTipLibrary::instance()->reload();
    reloadTips();
    return page;
}

void BrushDynamicsPanel::reloadTips()
{
    if (!m_tipBrowser) return;
    QVector<BrushResourceBrowser::Item> items;
    for (const BrushTip &tip : BrushTipLibrary::instance()->tips())
        items.push_back({tip.id, tip.name, tip.image});
    m_tipBrowser->setItems(items);
    m_tipBrowser->setSelectedId(m_currentTipId);
}

void BrushDynamicsPanel::onTipSelected(const QString &id)
{
    const BrushTip *tip = BrushTipLibrary::instance()->find(id);
    if (!tip || tip->isNull()) return;

    m_currentTipId = id;
    m_settings.tipSource = BrushTipSource::Image;
    m_settings.tipImage = tip->image;
    m_settings.tipId = id;
    m_settings.tipImagePath = tip->path;
    // Keep the preset self-contained when the tip has no stable disk path (e.g. it
    // was ingested from a brush): store the pixels as a base64 PNG too.
    if (tip->path.isEmpty()) {
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        tip->image.save(&buf, "PNG");
        buf.close();
        m_settings.tipImageData = QString::fromLatin1(bytes.toBase64());
    } else {
        m_settings.tipImageData.clear();
    }

    if (m_tipBrowser) m_tipBrowser->setSelectedId(id);
    if (m_tipPreview) m_tipPreview->setBrush(m_settings);

    if (m_updating) return;
    emit tipImageChanged(tip->image);
    schedulePreview();
}

// ── Brush Tip Shape (Options tab) ─────────────────────────────

QWidget *BrushDynamicsPanel::createTipShapePage()
{
    const Theme *t = ThemeManager::instance()->current();

    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(t->spaceSM, 0, t->spaceSM, 0);
    lay->setSpacing(t->spaceSM);

    // Circular angle / roundness editor.
    m_tipPreview = new BrushTipPreview(page);
    m_tipPreview->setOrientationEditing(true);
    m_tipPreview->setFixedSize(112, 112);
    auto *tipRow = new QWidget(page);
    auto *tipLay = new QHBoxLayout(tipRow);
    tipLay->setContentsMargins(0, 0, 0, 0);
    tipLay->addStretch();
    tipLay->addWidget(m_tipPreview);
    tipLay->addStretch();
    lay->addWidget(tipRow);

    m_tip.size = new QSlider(page);
    m_tip.sizeLabel = new QLabel("20 px", page);
    lay->addWidget(makeSliderRow(tr("Size:"), m_tip.size, m_tip.sizeLabel, page, 1, kSizeSliderMax, 20));
    m_tip.hardness = new QSlider(page);
    m_tip.hardnessLabel = new QLabel("80%", page);
    lay->addWidget(makeSliderRow(tr("Hardness:"), m_tip.hardness, m_tip.hardnessLabel, page, 0, 100, 80));
    m_tip.angle = new QSlider(page);
    m_tip.angleLabel = new QLabel(QStringLiteral("0°"), page);
    lay->addWidget(makeSliderRow(tr("Angle:"), m_tip.angle, m_tip.angleLabel, page, 0, 360, 0));
    m_tip.roundness = new QSlider(page);
    m_tip.roundnessLabel = new QLabel("100%", page);
    lay->addWidget(makeSliderRow(tr("Roundness:"), m_tip.roundness, m_tip.roundnessLabel, page, 5, 100, 100));
    m_tip.spacing = new QSlider(page);
    m_tip.spacingLabel = new QLabel("10%", page);
    lay->addWidget(makeSliderRow(tr("Spacing:"), m_tip.spacing, m_tip.spacingLabel, page, 1, 1000, 10));

    auto *flipRow = new QWidget(page);
    auto *flipLay = new QHBoxLayout(flipRow);
    flipLay->setContentsMargins(0, 0, 0, 0);
    m_tip.flipX = new AppCheckBox(tr("Flip X"), flipRow);
    m_tip.flipY = new AppCheckBox(tr("Flip Y"), flipRow);
    flipLay->addWidget(m_tip.flipX);
    flipLay->addWidget(m_tip.flipY);
    flipLay->addStretch();
    lay->addWidget(flipRow);

    // ── Size / Hardness / Spacing drive the engine ──
    connect(m_tip.size, &QSlider::valueChanged, this, [this](int v)
            {
        m_tip.sizeLabel->setText(tr("%1 px").arg(v));
        if (m_updating) return;
        m_settings.size = static_cast<float>(v);
        m_tipPreview->setSize(static_cast<float>(v));
        emit sizeChanged(static_cast<float>(v));
        schedulePreview(); });
    connect(m_tip.hardness, &QSlider::valueChanged, this, [this](int v)
            {
        m_tip.hardnessLabel->setText(QString::number(v) + "%");
        if (m_updating) return;
        m_settings.hardness = v / 100.0f;
        m_tipPreview->setHardness(v / 100.0f);
        emit hardnessChanged(v / 100.0f);
        schedulePreview(); });
    connect(m_tip.spacing, &QSlider::valueChanged, this, [this](int v)
            {
        m_tip.spacingLabel->setText(QString::number(v) + "%");
        if (m_updating) return;
        m_settings.spacing = v / 100.0f;
        emit spacingChanged(v / 100.0f);
        schedulePreview(); });

    // ── Angle / Roundness / Flip drive the dab shape (engine + preview) ──
    connect(m_tip.angle, &QSlider::valueChanged, this, [this](int v)
            {
        m_tip.angleLabel->setText(QString::number(v) + QStringLiteral("°"));
        if (m_updating) return;
        m_uiAngle = v * kPi / 180.0f;
        m_settings.angle = m_uiAngle;
        m_tipPreview->setAngle(m_uiAngle);
        emit angleChanged(m_uiAngle);
        schedulePreview(); });
    connect(m_tip.roundness, &QSlider::valueChanged, this, [this](int v)
            {
        m_tip.roundnessLabel->setText(QString::number(v) + "%");
        if (m_updating) return;
        m_uiRoundness = v / 100.0f;
        m_settings.roundness = m_uiRoundness;
        m_tipPreview->setRoundness(m_uiRoundness);
        emit roundnessChanged(m_uiRoundness);
        schedulePreview(); });
    connect(m_tip.flipX, &QCheckBox::toggled, this, [this](bool on)
            {
        if (m_updating) return;
        m_uiFlipX = on;
        m_settings.flipX = on;
        emit flipXChanged(on);
        schedulePreview(); });
    connect(m_tip.flipY, &QCheckBox::toggled, this, [this](bool on)
            {
        if (m_updating) return;
        m_uiFlipY = on;
        m_settings.flipY = on;
        emit flipYChanged(on);
        schedulePreview(); });

    // ── Circular control feeds the sliders (which do the real work) ──
    connect(m_tipPreview, &BrushTipPreview::sizeChanged, this, [this](float s)
            {
        QSignalBlocker block(m_tip.size);
        m_tip.size->setValue(qBound(1, qRound(s), kSizeSliderMax));
        m_tip.sizeLabel->setText(tr("%1 px").arg(qRound(s)));
        if (m_updating) return;
        m_settings.size = s;
        emit sizeChanged(s);
        schedulePreview(); });
    connect(m_tipPreview, &BrushTipPreview::hardnessChanged, this, [this](float h)
            {
        const int pct = qBound(0, qRound(h * 100.0f), 100);
        QSignalBlocker block(m_tip.hardness);
        m_tip.hardness->setValue(pct);
        m_tip.hardnessLabel->setText(QString::number(pct) + "%");
        if (m_updating) return;
        m_settings.hardness = pct / 100.0f;
        emit hardnessChanged(pct / 100.0f);
        schedulePreview(); });
    connect(m_tipPreview, &BrushTipPreview::angleChanged, this, [this](float rad)
            {
        int deg = qRound(rad * 180.0f / kPi);
        deg = ((deg % 360) + 360) % 360;
        m_uiAngle = rad;
        QSignalBlocker block(m_tip.angle);
        m_tip.angle->setValue(deg);
        m_tip.angleLabel->setText(QString::number(deg) + QStringLiteral("°"));
        if (m_updating) return;
        m_settings.angle = rad;
        emit angleChanged(rad);
        schedulePreview(); });
    connect(m_tipPreview, &BrushTipPreview::roundnessChanged, this, [this](float r)
            {
        const int pct = qBound(5, qRound(r * 100.0f), 100);
        m_uiRoundness = r;
        QSignalBlocker block(m_tip.roundness);
        m_tip.roundness->setValue(pct);
        m_tip.roundnessLabel->setText(QString::number(pct) + "%");
        if (m_updating) return;
        m_settings.roundness = m_uiRoundness;
        emit roundnessChanged(m_uiRoundness);
        schedulePreview(); });

    // ── Procedural tip (AutoBrush) + auto-spacing + image-tip remap ──
    auto *advSep = new QFrame(page);
    advSep->setFrameShape(QFrame::HLine);
    advSep->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorBorder.name()));
    lay->addWidget(advSep);
    auto *advTitle = new QLabel(tr("Tip Detail"), page);
    advTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    lay->addWidget(advTitle);

    auto *profRow = new QWidget(page);
    auto *profLay = new QHBoxLayout(profRow);
    profLay->setContentsMargins(0, 0, 0, 0);
    auto *profLbl = new QLabel(tr("Profile:"), profRow);
    profLbl->setFixedWidth(70);
    m_adv.profile = new QComboBox(profRow);
    m_adv.profile->addItems({tr("Default"), tr("Soft"), tr("Gaussian")});
    profLay->addWidget(profLbl);
    profLay->addWidget(m_adv.profile, 1);
    lay->addWidget(profRow);

    m_adv.spikes = new QSlider(page);
    m_adv.spikesLabel = new QLabel("2", page);
    lay->addWidget(makeSliderRow(tr("Spikes:"), m_adv.spikes, m_adv.spikesLabel, page, 2, 12, 2));
    m_adv.density = new QSlider(page);
    m_adv.densityLabel = new QLabel("100%", page);
    lay->addWidget(makeSliderRow(tr("Density:"), m_adv.density, m_adv.densityLabel, page, 0, 100, 100));

    m_adv.autoSpacing = new AppCheckBox(tr("Auto Spacing"), page);
    lay->addWidget(m_adv.autoSpacing);
    m_adv.autoSpacingCoeff = new QSlider(page);
    m_adv.autoSpacingCoeffLabel = new QLabel("100%", page);
    lay->addWidget(makeSliderRow(tr("Auto Sp.:"), m_adv.autoSpacingCoeff,
                                 m_adv.autoSpacingCoeffLabel, page, 1, 300, 100));

    auto *remapLbl = new QLabel(tr("Image Tip (Lightness)"), page);
    remapLbl->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    lay->addWidget(remapLbl);
    m_adv.tipBrightness = new QSlider(page);
    m_adv.tipBrightnessLabel = new QLabel("0%", page);
    lay->addWidget(makeSliderRow(tr("Bright:"), m_adv.tipBrightness,
                                 m_adv.tipBrightnessLabel, page, -100, 100, 0));
    m_adv.tipContrast = new QSlider(page);
    m_adv.tipContrastLabel = new QLabel("0%", page);
    lay->addWidget(makeSliderRow(tr("Contrast:"), m_adv.tipContrast,
                                 m_adv.tipContrastLabel, page, -100, 100, 0));
    m_adv.tipMidpoint = new QSlider(page);
    m_adv.tipMidpointLabel = new QLabel("50%", page);
    lay->addWidget(makeSliderRow(tr("Midpoint:"), m_adv.tipMidpoint,
                                 m_adv.tipMidpointLabel, page, 0, 100, 50));

    connect(m_adv.profile, &QComboBox::currentIndexChanged, this, [this](int)
            { emitAdvanced(); });
    connect(m_adv.spikes, &QSlider::valueChanged, this, [this](int v)
            { m_adv.spikesLabel->setText(QString::number(v)); emitAdvanced(); });
    connect(m_adv.density, &QSlider::valueChanged, this, [this](int v)
            { m_adv.densityLabel->setText(QString::number(v) + "%"); emitAdvanced(); });
    connect(m_adv.autoSpacing, &QCheckBox::toggled, this, [this](bool on)
            {
        if (m_tip.spacing) m_tip.spacing->setEnabled(!on);
        if (m_adv.autoSpacingCoeff) m_adv.autoSpacingCoeff->setEnabled(on);
        emitAdvanced(); });
    connect(m_adv.autoSpacingCoeff, &QSlider::valueChanged, this, [this](int v)
            { m_adv.autoSpacingCoeffLabel->setText(QString::number(v) + "%"); emitAdvanced(); });
    connect(m_adv.tipBrightness, &QSlider::valueChanged, this, [this](int v)
            { m_adv.tipBrightnessLabel->setText(QString::number(v) + "%"); emitAdvanced(); });
    connect(m_adv.tipContrast, &QSlider::valueChanged, this, [this](int v)
            { m_adv.tipContrastLabel->setText(QString::number(v) + "%"); emitAdvanced(); });
    connect(m_adv.tipMidpoint, &QSlider::valueChanged, this, [this](int v)
            { m_adv.tipMidpointLabel->setText(QString::number(v) + "%"); emitAdvanced(); });

    // ── General brush options (kept here, not as a sidebar category) ──
    auto *sep = new QFrame(page);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorBorder.name()));
    lay->addWidget(sep);
    auto *genTitle = new QLabel(tr("General"), page);
    genTitle->setStyleSheet(QStringLiteral("color: %1;").arg(t->colorTextSecondary.name()));
    lay->addWidget(genTitle);
    lay->addWidget(createGeneralContent());
    lay->addStretch();

    return page;
}

// ── Color Dynamics ────────────────────────────────────────────

QWidget *BrushDynamicsPanel::createColorPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    m_color.hue = new QSlider(page);
    m_color.hueLabel = new QLabel(QStringLiteral("0°"), page);
    lay->addWidget(makeSliderRow(tr("Hue:"), m_color.hue, m_color.hueLabel, page, 0, 360));
    m_color.sat = new QSlider(page);
    m_color.satLabel = new QLabel("0%", page);
    lay->addWidget(makeSliderRow(tr("Sat:"), m_color.sat, m_color.satLabel, page));
    m_color.bri = new QSlider(page);
    m_color.briLabel = new QLabel("0%", page);
    lay->addWidget(makeSliderRow(tr("Bri:"), m_color.bri, m_color.briLabel, page));
    m_color.purity = new QSlider(page);
    m_color.purityLabel = new QLabel("0%", page);
    lay->addWidget(makeSliderRow(tr("Purity:"), m_color.purity, m_color.purityLabel, page));
    lay->addStretch();

    connect(m_color.hue, &QSlider::valueChanged, this, [this](int v)
            {
        m_color.hueLabel->setText(QString::number(v) + QStringLiteral("°")); emitColor(); });
    connect(m_color.sat, &QSlider::valueChanged, this, [this](int v)
            {
        m_color.satLabel->setText(QString::number(v) + "%"); emitColor(); });
    connect(m_color.bri, &QSlider::valueChanged, this, [this](int v)
            {
        m_color.briLabel->setText(QString::number(v) + "%"); emitColor(); });
    connect(m_color.purity, &QSlider::valueChanged, this, [this](int v)
            {
        m_color.purityLabel->setText(QString::number(v) + "%"); emitColor(); });

    return page;
}

void BrushDynamicsPanel::emitColor()
{
    if (m_updating)
        return;
    ColorDynamics d = m_settings.colorDynamics;   // keep fg/bg colours
    d.enabled = categoryEnabled(CatColor);
    d.hueAmount = m_color.hue->value() / 360.0f;
    d.saturationAmount = m_color.sat->value() / 100.0f;
    d.brightnessAmount = m_color.bri->value() / 100.0f;
    d.purityAmount = m_color.purity->value() / 100.0f;
    m_settings.colorDynamics = d;
    emit colorDynamicsChanged(d);
    schedulePreview();
}

// ── Texture ───────────────────────────────────────────────────

QWidget *BrushDynamicsPanel::createTexturePage()
{
    m_texturePanel = new BrushTexturePanel(this);
    connect(m_texturePanel, &BrushTexturePanel::changed, this, [this]() {
        if (m_updating) return;
        emitTexture();
    });
    return m_texturePanel;
}

void BrushDynamicsPanel::emitTexture()
{
    if (m_updating)
        return;
    TextureConfig d = m_texturePanel ? m_texturePanel->config() : m_settings.textureConfig;
    d.enabled = categoryEnabled(CatTexture);   // owned by the Funcionalidades checkbox
    m_settings.textureConfig = d;
    emit textureConfigChanged(d);
    schedulePreview();
}

// ── Dual Brush ────────────────────────────────────────────────

QWidget *BrushDynamicsPanel::createDualPage()
{
    auto *page = new QWidget(this);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(0, 0, 0, 0);

    m_dual.size = new QSlider(page);
    m_dual.sizeLabel = new QLabel("20", page);
    lay->addWidget(makeSliderRow(tr("Size:"), m_dual.size, m_dual.sizeLabel, page, 1, 200, 20));
    m_dual.hardness = new QSlider(page);
    m_dual.hardnessLabel = new QLabel("80%", page);
    lay->addWidget(makeSliderRow(tr("Hardness:"), m_dual.hardness, m_dual.hardnessLabel, page, 0, 100, 80));
    m_dual.type = new QComboBox(page);
    m_dual.type->addItems({tr("Round"), tr("Square")});
    lay->addWidget(m_dual.type);
    lay->addStretch();

    connect(m_dual.size, &QSlider::valueChanged, this, [this](int v)
            {
        m_dual.sizeLabel->setText(QString::number(v)); emitDual(); });
    connect(m_dual.hardness, &QSlider::valueChanged, this, [this](int v)
            {
        m_dual.hardnessLabel->setText(QString::number(v) + "%"); emitDual(); });
    connect(m_dual.type, &QComboBox::currentIndexChanged, this, [this](int)
            { emitDual(); });

    return page;
}

void BrushDynamicsPanel::emitDual()
{
    if (m_updating)
        return;
    DualBrushConfig d;
    d.enabled = categoryEnabled(CatDual);
    d.size = static_cast<float>(m_dual.size->value());
    d.hardness = m_dual.hardness->value() / 100.0f;
    d.tipType = m_dual.type->currentIndex();
    m_settings.dualBrushConfig = d;
    emit dualBrushConfigChanged(d);
    schedulePreview();
}

void BrushDynamicsPanel::setPressureEnabled(bool on)
{
    // The "Use Pen Pressure" toggle drives the Pressure sensor of the
    // Size/Opacity/Flow curve options (same rule as CanvasView::setBrushPressure
    // Enabled). Size always follows; Opacity/Flow only when they already map to
    // pressure, so turning it on never adds dynamics the user didn't set.
    if (brushPressureEnabled(m_settings) == on)
        return;
    m_updating = true;
    m_settings.sizeOption.setPressureSensorEnabled(on);
    if (!on || m_settings.opacityOption.hasActivePressureSensor())
        m_settings.opacityOption.setPressureSensorEnabled(on);
    if (!on || m_settings.flowOption.hasActivePressureSensor())
        m_settings.flowOption.setPressureSensorEnabled(on);

    // Reflect the curve-feature checkboxes (Size/Opacity/Flow) in col 2.
    if (m_featureChecks[FSize]) {
        const bool en[6] = { m_settings.sizeOption.enabled, m_settings.opacityOption.enabled,
                             m_settings.flowOption.enabled, m_settings.rotationOption.enabled,
                             m_settings.ratioOption.enabled, m_settings.scatterOption.enabled };
        for (int i = FSize; i <= FScatter; ++i) {
            QSignalBlocker b(m_featureChecks[i]);
            m_featureChecks[i]->setChecked(en[i]);
        }
    }
    m_updating = false;
    // Refresh the sensor/curve columns for the active curve feature.
    syncSensorChecks();
    schedulePreview();
}

void BrushDynamicsPanel::emitAdvanced()
{
    if (m_updating)
        return;
    // AutoBrush procedural knobs.
    if (m_adv.profile)
        m_settings.autoBrush.profile = m_adv.profile->currentIndex();
    if (m_adv.spikes)
        m_settings.autoBrush.spikes = m_adv.spikes->value();
    if (m_adv.density)
        m_settings.autoBrush.density = m_adv.density->value() / 100.0f;
    // Auto-spacing.
    if (m_adv.autoSpacing)
        m_settings.useAutoSpacing = m_adv.autoSpacing->isChecked();
    if (m_adv.autoSpacingCoeff)
        m_settings.autoSpacingCoeff = m_adv.autoSpacingCoeff->value() / 100.0f;
    // Image-tip lightness remap.
    if (m_adv.tipBrightness)
        m_settings.tipBrightness = m_adv.tipBrightness->value() / 100.0f;
    if (m_adv.tipContrast)
        m_settings.tipContrast = m_adv.tipContrast->value() / 100.0f;
    if (m_adv.tipMidpoint)
        m_settings.tipMidpoint = m_adv.tipMidpoint->value() / 100.0f;
    // The Layer B curve options (size/opacity/flow/rotation/ratio) are written
    // straight into m_settings by the col 3/4 handlers, so nothing to gather here.

    emit advancedSettingsChanged(currentSettings());
    schedulePreview();
}

// ── General (lives at the foot of Brush Tip Shape) ─────────────

QWidget *BrushDynamicsPanel::createGeneralContent()
{
    auto *content = new QWidget(this);
    auto *lay = new QVBoxLayout(content);
    lay->setContentsMargins(0, 0, 0, 0);

    m_general.smoothingMode = new QComboBox(content);
    for (auto *s : kSmoothingNames)
        m_general.smoothingMode->addItem(tr(s));
    auto *smRow = new QWidget(content);
    auto *smLay = new QHBoxLayout(smRow);
    smLay->setContentsMargins(0, 0, 0, 0);
    smLay->addWidget(new QLabel(tr("Smoothing:"), content));
    smLay->addWidget(m_general.smoothingMode, 1);
    lay->addWidget(smRow);

    m_general.smoothingRadius = new QSlider(content);
    m_general.smoothingRadiusLabel = new QLabel("10", content);
    auto *radRow = makeSliderRow(tr("Sm. Radius:"), m_general.smoothingRadius,
                                 m_general.smoothingRadiusLabel, content, 1, 200, 10);
    radRow->hide();
    lay->addWidget(radRow);

    m_general.blendMode = new QComboBox(content);
    for (auto *b : kBlendNames)
        m_general.blendMode->addItem(tr(b));
    auto *blRow = new QWidget(content);
    auto *blLay = new QHBoxLayout(blRow);
    blLay->setContentsMargins(0, 0, 0, 0);
    blLay->addWidget(new QLabel(tr("Blend:"), content));
    blLay->addWidget(m_general.blendMode, 1);
    lay->addWidget(blRow);

    m_general.airbrush = new AppCheckBox(tr("Airbrush"), content);
    m_general.wetEdges = new AppCheckBox(tr("Wet Edges"), content);
    lay->addWidget(m_general.airbrush);
    // Airbrush rate: timed dabs per second while the pointer is held down.
    m_general.airbrushRate = new QSlider(content);
    m_general.airbrushRateLabel = new QLabel("20/s", content);
    auto *rateRow = makeSliderRow(tr("Rate:"), m_general.airbrushRate,
                                  m_general.airbrushRateLabel, content, 1, 100, 20);
    m_general.airbrushRate->setEnabled(false);
    lay->addWidget(rateRow);
    lay->addWidget(m_general.wetEdges);

    connect(m_general.smoothingMode, &QComboBox::currentIndexChanged, this, [this, radRow](int idx)
            {
        radRow->setVisible(idx == 2);
        if (m_updating) return;
        m_settings.smoothingMode = static_cast<SmoothingMode>(idx);
        emit smoothingModeChanged(static_cast<SmoothingMode>(idx)); });
    connect(m_general.smoothingRadius, &QSlider::valueChanged, this, [this](int v)
            {
        m_general.smoothingRadiusLabel->setText(QString::number(v));
        if (m_updating) return;
        m_settings.smoothingRadius = static_cast<float>(v);
        emit smoothingRadiusChanged(static_cast<float>(v)); });
    connect(m_general.blendMode, &QComboBox::currentIndexChanged, this, [this](int idx)
            {
        if (m_updating) return;
        m_settings.blendMode = static_cast<BrushBlendMode>(idx);
        emit blendModeChanged(static_cast<BrushBlendMode>(idx)); });
    connect(m_general.airbrush, &QCheckBox::toggled, this, [this](bool on)
            {
        if (m_general.airbrushRate) m_general.airbrushRate->setEnabled(on);
        if (m_updating) return;
        m_settings.airbrush = on;
        emit airbrushChanged(on); });
    connect(m_general.airbrushRate, &QSlider::valueChanged, this, [this](int v)
            {
        m_general.airbrushRateLabel->setText(QString::number(v) + QStringLiteral("/s"));
        if (m_updating) return;
        m_settings.airbrushRate = static_cast<float>(v);
        emitAdvanced(); });
    connect(m_general.wetEdges, &QCheckBox::toggled, this, [this](bool on)
            {
        if (m_updating) return;
        m_settings.wetEdges = on;
        emit wetEdgesChanged(on); });

    return content;
}

// ── Presets ───────────────────────────────────────────────────

void BrushDynamicsPanel::schedulePreview()
{
    if (m_strokePreview)
        m_strokePreview->setSettings(currentSettings());
    // Live edits flip the unsaved-changes flag, which gates the Save button.
    updatePresetActionStates();
}

// ── Unsaved-changes tracking ──────────────────────────────────

QString BrushDynamicsPanel::presetJson(const QString &name, const QString &group,
                                       const BrushSettings &settings) const
{
    BrushPreset p;
    p.name = name;
    p.group = group;
    p.settings = settings;
    // Compact, key-stable serialization — the same one used to persist presets,
    // so the comparison sees exactly what a save would write.
    return QString::fromUtf8(QJsonDocument(p.toJson()).toJson(QJsonDocument::Compact));
}

void BrushDynamicsPanel::captureBaseline()
{
    if (m_selectedPresetName.isEmpty()) {
        m_baselinePresetJson.clear();
        m_baselinePresetGroup.clear();
        return;
    }
    m_baselinePresetGroup.clear();
    if (m_presets.manager) {
        if (const BrushPreset *p = m_presets.manager->findPreset(m_selectedPresetName))
            m_baselinePresetGroup = p->group;
    }
    m_baselinePresetJson = presetJson(m_selectedPresetName, m_baselinePresetGroup,
                                      currentSettings());
}

bool BrushDynamicsPanel::hasUnsavedChanges() const
{
    if (m_selectedPresetName.isEmpty() || m_baselinePresetJson.isEmpty())
        return false;
    return presetJson(m_selectedPresetName, m_baselinePresetGroup, currentSettings())
           != m_baselinePresetJson;
}

bool BrushDynamicsPanel::confirmDiscardChanges()
{
    if (!hasUnsavedChanges())
        return true;

    QMessageBox box(this);
    box.setIcon(QMessageBox::Question);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(tr("The preset \"%1\" has been changed.").arg(m_selectedPresetName));
    box.setInformativeText(tr("Do you want to save the changes?"));
    QPushButton *saveBtn = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    QPushButton *cancelBtn = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(saveBtn);
    box.exec();

    QAbstractButton *clicked = box.clickedButton();
    if (clicked == cancelBtn)
        return false;                       // veto the switch/close
    if (clicked == saveBtn) {
        // Overwrite the selected preset with the live state, then re-baseline.
        if (m_presets.manager)
            m_presets.manager->overwritePreset(m_selectedPresetName, currentSettings());
        captureBaseline();
    }
    // Discard (and Save) both allow the caller to proceed.
    return true;
}

// ── State sync ────────────────────────────────────────────────

void BrushDynamicsPanel::setFromSettings(const BrushSettings &s)
{
    m_updating = true;
    m_settings = s;

    setCategoryEnabled(CatColor, s.colorDynamics.enabled);
    setCategoryEnabled(CatTexture, s.textureConfig.enabled);
    setCategoryEnabled(CatDual, s.dualBrushConfig.enabled);

    // Brush Tip Shape.
    m_tip.size->setValue(qBound(1, qRound(s.size), kSizeSliderMax));
    m_tip.hardness->setValue(qBound(0, qRound(s.hardness * 100.0f), 100));
    m_tip.spacing->setValue(qBound(1, qRound(s.spacing * 100.0f), 1000));
    m_uiAngle = s.angle;
    int angleDeg = qRound(s.angle * 180.0f / kPi);
    angleDeg = ((angleDeg % 360) + 360) % 360;
    m_tip.angle->setValue(angleDeg);
    m_uiRoundness = std::clamp(s.roundness, 0.05f, 1.0f);
    m_tip.roundness->setValue(qBound(5, qRound(m_uiRoundness * 100.0f), 100));
    m_uiFlipX = s.flipX;
    m_tip.flipX->setChecked(s.flipX);
    m_uiFlipY = s.flipY;
    m_tip.flipY->setChecked(s.flipY);

    // Brush Tip (Texture grid): resolve which library tip this brush uses and
    // reflect it in the grid. A preset that only carries the pixels (no library
    // id, e.g. imported brushes) is registered into the global library on the fly
    // so its tip becomes reusable — mirroring how textures behave.
    m_currentTipId.clear();
    if (s.tipSource == BrushTipSource::Image) {
        if (!s.tipId.isEmpty() && BrushTipLibrary::instance()->find(s.tipId)) {
            m_currentTipId = s.tipId;
        } else {
            QImage tip = s.tipImage;
            if (tip.isNull() && !s.tipImageData.isEmpty())
                tip.loadFromData(QByteArray::fromBase64(s.tipImageData.toLatin1()));
            if (tip.isNull() && !s.tipImagePath.isEmpty())
                tip.load(s.tipImagePath);
            if (!tip.isNull()) {
                const QString nm = s.tipImagePath.isEmpty()
                    ? QStringLiteral("tip")
                    : QFileInfo(s.tipImagePath).completeBaseName();
                m_currentTipId = BrushTipLibrary::instance()->add(nm, tip, s.tipImagePath);
                m_settings.tipId = m_currentTipId;
                m_settings.tipImage = tip;
            }
        }
    }
    if (m_tipBrowser) m_tipBrowser->setSelectedId(m_currentTipId);

    // Scattering axes (col 4; the strength/sensors live in the scatter option).
    if (m_scatterAxisX) {
        QSignalBlocker bx(m_scatterAxisX);
        QSignalBlocker by(m_scatterAxisY);
        m_scatterAxisX->setChecked(s.scatter.axisX);
        m_scatterAxisY->setChecked(s.scatter.axisY);
    }

    // Color Dynamics.
    m_color.hue->setValue(static_cast<int>(s.colorDynamics.hueAmount * 360));
    m_color.sat->setValue(static_cast<int>(s.colorDynamics.saturationAmount * 100));
    m_color.bri->setValue(static_cast<int>(s.colorDynamics.brightnessAmount * 100));
    m_color.purity->setValue(static_cast<int>(s.colorDynamics.purityAmount * 100));

    // Texture component.
    if (m_texturePanel)
        m_texturePanel->setConfig(s.textureConfig);

    // Dual Brush.
    m_dual.size->setValue(static_cast<int>(s.dualBrushConfig.size));
    m_dual.hardness->setValue(static_cast<int>(s.dualBrushConfig.hardness * 100));
    m_dual.type->setCurrentIndex(s.dualBrushConfig.tipType);

    // General.
    m_general.smoothingMode->setCurrentIndex(static_cast<int>(s.smoothingMode));
    m_general.smoothingRadius->setValue(static_cast<int>(s.smoothingRadius));
    m_general.blendMode->setCurrentIndex(static_cast<int>(s.blendMode));
    m_general.airbrush->setChecked(s.airbrush);
    if (m_general.airbrushRate) {
        m_general.airbrushRate->setValue(qBound(1, qRound(s.airbrushRate), 100));
        m_general.airbrushRate->setEnabled(s.airbrush);
        m_general.airbrushRateLabel->setText(
            QString::number(m_general.airbrushRate->value()) + QStringLiteral("/s"));
    }
    m_general.wetEdges->setChecked(s.wetEdges);

    // Tip Detail (AutoBrush / auto-spacing / image-tip remap).
    if (m_adv.profile) {
        m_adv.profile->setCurrentIndex(qBound(0, s.autoBrush.profile, 2));
        m_adv.spikes->setValue(qBound(2, s.autoBrush.spikes, 12));
        m_adv.density->setValue(qBound(0, qRound(s.autoBrush.density * 100.0f), 100));
        m_adv.autoSpacing->setChecked(s.useAutoSpacing);
        m_adv.autoSpacingCoeff->setValue(qBound(1, qRound(s.autoSpacingCoeff * 100.0f), 300));
        m_adv.autoSpacingCoeff->setEnabled(s.useAutoSpacing);
        m_tip.spacing->setEnabled(!s.useAutoSpacing);
        m_adv.tipBrightness->setValue(qBound(-100, qRound(s.tipBrightness * 100.0f), 100));
        m_adv.tipContrast->setValue(qBound(-100, qRound(s.tipContrast * 100.0f), 100));
        m_adv.tipMidpoint->setValue(qBound(0, qRound(s.tipMidpoint * 100.0f), 100));
    }

    // Curve options (col 2): each checkbox reflects its option's enabled flag.
    if (m_featureChecks[FSize]) {
        const bool en[6] = { s.sizeOption.enabled, s.opacityOption.enabled,
                             s.flowOption.enabled, s.rotationOption.enabled,
                             s.ratioOption.enabled, s.scatterOption.enabled };
        for (int i = FSize; i <= FScatter; ++i) {
            QSignalBlocker b(m_featureChecks[i]);
            m_featureChecks[i]->setChecked(en[i]);
        }
    }

    // Circular control visual.
    m_tipPreview->setBrush(s);
    m_tipPreview->setAngle(s.angle);
    m_tipPreview->setRoundness(m_uiRoundness);

    m_updating = false;
    // Refresh col 3/4 for the active feature against the new settings.
    setActiveFeature(m_activeFeature);
    schedulePreview();

    // This is the single "load a known brush state" path, so the freshly applied
    // settings define the clean baseline for the selected preset. Subsequent user
    // edits then read as unsaved. (Callers that change the selection set
    // m_selectedPresetName before calling; external callers leave it as-is.)
    captureBaseline();
    updatePresetActionStates();
}

BrushSettings BrushDynamicsPanel::currentSettings() const
{
    // m_settings is kept in sync by every control; the enabled flags are taken
    // authoritatively from the feature checkboxes so a saved preset captures them.
    BrushSettings s = m_settings;
    s.colorDynamics.enabled = categoryEnabled(CatColor);
    s.textureConfig.enabled = categoryEnabled(CatTexture);
    s.dualBrushConfig.enabled = categoryEnabled(CatDual);
    return s;
}

QSize BrushDynamicsPanel::sizeHint() const
{
    // 800px default width; height follows the natural content height.
    QSize hint = QWidget::sizeHint();
    hint.setWidth(kPanelDefaultWidth);
    return hint;
}

void BrushDynamicsPanel::setCurrentPreset(const QString& name)
{
    if (m_list)
        m_list->setCurrentPreset(name);
}

void BrushDynamicsPanel::setPresetManager(BrushPresetManager *manager)
{
    m_presets.manager = manager;
    m_hasExternalManager = true;
    if (manager && manager->presets().empty())
        manager->loadPresets();
    if (m_list)
        m_list->setPresetManager(manager);

    // After any library change (notably a Save from the ≡ menu, which overwrites
    // the selected preset with the live state), clear the dirty flag if the stored
    // preset now matches the live state.
    if (manager) {
        connect(manager, &BrushPresetManager::presetsChanged, this, [this]() {
            if (!m_selectedPresetName.isEmpty() && m_presets.manager) {
                const BrushPreset *p = m_presets.manager->findPreset(m_selectedPresetName);
                // 'p' is null when the selected preset was deleted/renamed elsewhere.
                if (p && presetJson(p->name, p->group, currentSettings())
                             == presetJson(p->name, p->group, p->settings))
                    captureBaseline();
            }
            // The library changed (add/remove/rename/reorganise/reload) — refresh
            // the top-bar enabled states regardless of the current selection.
            updatePresetActionStates();
        });
    }
}

// ── Close interception (host dock "X") ────────────────────────

void BrushDynamicsPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // The panel lives inside a QDockWidget; its close button sends a QCloseEvent
    // to the dock, not to us. Watch the dock so we can offer to save unsaved edits
    // (and veto the close on "Cancelar"). Installed lazily — the dock parent is
    // only known once shown — and idempotent (installEventFilter de-dups).
    if (QWidget *dock = window())
        if (dock != this)
            dock->installEventFilter(this);
}

bool BrushDynamicsPanel::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::Close && watched == window()) {
        if (!confirmDiscardChanges()) {
            event->ignore();
            return true;   // veto the close
        }
    }
    return QWidget::eventFilter(watched, event);
}
