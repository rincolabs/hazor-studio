#include "ToolOptionsBar.hpp"
#include "BrushPresetPicker.hpp"
#include "PopupPanel.hpp"
#include "ScrubbableValueInput.h"
#include "IconUtils.hpp"
#include "ui/ShortcutManager.hpp"
#include "ui/TransformFieldsWidget.hpp"
#include "brush/BrushPresetManager.hpp"
#include "gradient/GradientPresetManager.hpp"
#include "gradient/GradientRenderer.hpp"
#include "ui/gradient/GradientEditorDialog.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QStackedWidget>
#include <QLabel>
#include <QPushButton>
#include <QToolButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QButtonGroup>
#include <QComboBox>
#include <QCheckBox>
#include <QFontComboBox>
#include <QAbstractItemModel>
#include <QAbstractItemView>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLineEdit>
#include "colorpicker/ColorPickerDialog.hpp"
#include <QInputDialog>
#include "ui/AppCheckBox.hpp"
#include "ui/ToolBarSeparator.hpp"
#include "ai/tool/AiObjectSelectionController.hpp"
#include "ai/tool/AiRemoveObjectController.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include <QPainter>
#include <QPixmap>
#include <QFrame>
#include <QMenu>
#include <QAction>
#include <QSignalBlocker>
#include <algorithm>
#include <cmath>

static QString colorBtnStyle(const QColor& c)
{
    auto* t = ThemeManager::instance()->current();
    return QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;")
        .arg(c.name())
        .arg(t->colorBorder.name())
        .arg(t->radiusSM);
}

static int scrubInt(double value)
{
    return static_cast<int>(std::lround(value));
}

static void bindShortcutTooltip(QWidget* widget, const QString& actionId, const QString& baseText)
{
    auto update = [widget, actionId, baseText]() {
        if (widget)
            widget->setToolTip(ShortcutManager::instance()->shortcutTooltipText(actionId, baseText));
    };
    update();
    QObject::connect(ShortcutManager::instance(), &ShortcutManager::shortcutsChanged,
                     widget, update);
}

static constexpr int kToolOptionSpacing = 4;
static constexpr int kFontPopupMinWidth = 400;
static constexpr int kFontPopupMinHeight = 500;
static constexpr int kFontPopupItemHeight = 82;
static constexpr int kSelectToolId = 3;

class CurrentPageStackedWidget : public QStackedWidget {
public:
    using QStackedWidget::QStackedWidget;

    QSize sizeHint() const override
    {
        return currentWidget() ? currentWidget()->sizeHint() : QStackedWidget::sizeHint();
    }

    QSize minimumSizeHint() const override
    {
        return currentWidget() ? currentWidget()->minimumSizeHint() : QStackedWidget::minimumSizeHint();
    }
};

class FontPopupComboBox : public QFontComboBox {
public:
    using QFontComboBox::QFontComboBox;

protected:
    void showPopup() override
    {
        QFontComboBox::showPopup();

        auto* popupView = view();
        if (!popupView) return;

        popupView->setMinimumSize(kFontPopupMinWidth, kFontPopupMinHeight);
        if (auto* popupWindow = popupView->window()) {
            popupWindow->setMinimumSize(kFontPopupMinWidth, kFontPopupMinHeight);
            popupWindow->resize(qMax(popupWindow->width(), kFontPopupMinWidth),
                                qMax(popupWindow->height(), kFontPopupMinHeight));
        }
    }
};

static void configureOptionsLayout(QHBoxLayout* layout, const Theme* theme)
{
    if (!layout || !theme) return;
    layout->setContentsMargins(4, 0, 0, 0);
    layout->setSpacing(kToolOptionSpacing);
}

ToolOptionsBar::ToolOptionsBar(const QString& title, QWidget* parent)
    : QToolBar(title, parent)
{
    m_presetManager = new BrushPresetManager(this);
    m_presetManager->loadPresets();
    m_gradientPresetManager = new GradientPresetManager(this);
    m_gradientPresetManager->loadPresets();
    m_gradientDefinition = m_gradientPresetManager->currentGradient();

    m_optionsHost = new QWidget(this);
    m_optionsHost->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_optionsLayout = new QHBoxLayout(m_optionsHost);
    m_optionsLayout->setContentsMargins(0, 0, 0, 0);
    m_optionsLayout->setSpacing(kToolOptionSpacing);
    addWidget(m_optionsHost);

    m_stack = new CurrentPageStackedWidget(m_optionsHost);
    m_stack->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Preferred);
    m_optionsLayout->addWidget(m_stack);

    auto* empty = new QWidget(this);
    auto* emptyLay = new QHBoxLayout(empty);
    configureOptionsLayout(emptyLay, ThemeManager::instance()->current());
    emptyLay->addWidget(new QLabel(tr("Select a tool to see options")));
    emptyLay->addStretch();
    m_stack->addWidget(empty); // page 0

    m_stack->addWidget(createBrushPage()); // page 1 (Brush + Eraser)
    m_stack->addWidget(createSelectPage()); // page 2 (Select)
    m_stack->addWidget(createZoomPage());  // page 3 (Zoom)
    m_stack->addWidget(new QWidget(this)); // page 4 (Hand — no options)
    m_stack->addWidget(createTextPage());  // page 5 (Text)
    m_stack->addWidget(createMovePage());  // page 6 (Move)
    m_stack->addWidget(createCropPage());  // page 7 (Crop)
    m_stack->addWidget(createFillBucketPage());  // page 8 (Fill Bucket)
    m_stack->addWidget(createEyedropperPage());  // page 9 (Eyedropper)
    m_stack->addWidget(createShapePage());  // page 10 (Shape)
    m_stack->addWidget(createCloneStampPage());  // page 11 (Clone Stamp)
    m_stack->addWidget(createHealingBrushPage());  // page 12 (Healing Brush)
    m_stack->addWidget(createGradientPage());  // page 13 (Gradient)
    m_stack->addWidget(createSkewPage());  // page 14 (Skew — Distort/Perspective)
    m_stack->addWidget(createAiSelectPage());  // page 15 (AI Object Selection)
    m_stack->addWidget(createTransformPage());  // page 16 (Free Transform)

    m_stack->setCurrentIndex(0);
}

void ToolOptionsBar::setAuxiliaryOptionsWidget(QWidget* widget)
{
    if (m_auxiliaryOptionsWidget == widget)
        return;
    m_auxiliaryOptionsWidget = widget;
    if (!widget)
        return;

    widget->setParent(m_optionsHost);
    m_optionsLayout->addWidget(widget);
}

bool ToolOptionsBar::setAuxiliaryOptionsVisible(bool visible)
{
    if (!m_auxiliaryOptionsWidget)
        return false;

    const bool changed = m_auxiliaryOptionsWidget->isVisible() != visible;
    m_auxiliaryOptionsWidget->setVisible(visible);
    if (changed) {
        m_optionsHost->updateGeometry();
        updateGeometry();
    }
    return changed;
}

QWidget* ToolOptionsBar::createBrushPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_presetPicker = new BrushPresetPicker(this);
    m_presetPicker->setPresetManager(m_presetManager);
    m_presetPicker->setFixedWidth(60);
    // The brush picker is now just an entry point to the dedicated Brush Panel:
    // clicking it opens/focuses that panel instead of an internal grid.
    m_presetPicker->setOpensExternalPanel(true);
    connect(m_presetPicker, &BrushPresetPicker::panelRequested,
            this, &ToolOptionsBar::openBrushPanelRequested);
    connect(m_presetPicker, &BrushPresetPicker::importBrushesRequested,
            this, &ToolOptionsBar::importBrushesRequested);
    lay->addWidget(m_presetPicker);
    if (!m_presetManager->presets().empty())
        m_presetPicker->setCurrentPreset(m_presetManager->presets().front());

    lay->addWidget(new ToolBarSeparator(this));

    m_sizeSlider = new ScrubbableValueInput(tr("Size"), 1, 1000, 20, tr("px"), 1, this);
    lay->addWidget(m_sizeSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_hardnessSlider = new ScrubbableValueInput(tr("Hardness"), 0, 100, 80, tr("%"), 1, this);
    lay->addWidget(m_hardnessSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_opacitySlider = new ScrubbableValueInput(tr("Opacity"), 1, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_opacitySlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_flowSlider = new ScrubbableValueInput(tr("Flow"), 1, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_flowSlider);

    lay->addWidget(new ToolBarSeparator(this));

    // Pen-pressure quick toggle — a fast override of the active brush's pressure
    // dynamics. The fine controls (which axes, minimums, curve) live in the
    // Brush Settings panel; this just flips pressure.enabled. It stays available
    // even without a tablet because it belongs to the brush/preset, not the
    // device (mouse simply paints at constant pressure).
    m_brushPressureBtn = new QPushButton(this);
    m_brushPressureBtn->setCheckable(true);
    m_brushPressureBtn->setChecked(true);
    m_brushPressureBtn->setIcon(makeIcon(":/icons/brush-pressure.png"));
    m_brushPressureBtn->setIconSize(QSize(24, 24));
    m_brushPressureBtn->setFixedSize(26, 26);
    m_brushPressureBtn->setToolTip(tr("Use Pen Pressure"));
    lay->addWidget(m_brushPressureBtn);

    // Global minimum-pressure floor. Unlike Size/Opacity/Flow it is NOT reset when a
    // preset is selected (see the preset handler below) — it is a global override that
    // takes priority over the preset's pressure response.
    m_minPressureSlider = new ScrubbableValueInput(tr("Min Pressure"), 0, 100, 0, tr("%"), 1, this);
    m_minPressureSlider->setToolTip(tr("Minimum pen pressure (global override)"));
    lay->addWidget(m_minPressureSlider);

    // lay->addWidget(new ToolBarSeparator(this));

    // m_colorBtn = new QPushButton(this);
    // m_colorBtn->setFixedSize(24, 24);
    // m_colorBtn->setStyleSheet(colorBtnStyle(Qt::black));
    // lay->addWidget(m_colorBtn);

    lay->addWidget(new ToolBarSeparator(this));

    m_brushSettingsBtn = new QPushButton(this);
    m_brushSettingsBtn->setIcon(makeIcon(":/icons/brush-settings.png"));
    m_brushSettingsBtn->setIconSize(QSize(24, 24));
    m_brushSettingsBtn->setFixedSize(26, 26);
    bindShortcutTooltip(m_brushSettingsBtn,
                        QStringLiteral("panel.brush_settings.toggle"),
                        tr("Brush Settings"));
    lay->addWidget(m_brushSettingsBtn);

    lay->addStretch();

    connect(m_sizeSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushSizeChanged(scrubInt(v));
    });
    connect(m_hardnessSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushHardnessChanged(scrubInt(v));
    });
    connect(m_opacitySlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushOpacityChanged(scrubInt(v));
    });
    connect(m_flowSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushFlowChanged(scrubInt(v));
    });
    connect(m_minPressureSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushMinPressureChanged(scrubInt(v));
    });

    connect(m_brushPressureBtn, &QPushButton::toggled, this, [this](bool on) {
        emit brushPressureToggled(on);
    });

    // connect(m_colorBtn, &QPushButton::clicked, this, &ToolOptionsBar::pickColor);
    connect(m_brushSettingsBtn, &QPushButton::clicked, this, &ToolOptionsBar::brushSettingsRequested);

    // Preset picker connections
    connect(m_presetPicker, &BrushPresetPicker::presetSelected, this, [this](const BrushPreset& preset) {
        const auto& s = preset.settings;
        setBrushSize(static_cast<int>(s.size));
        setBrushHardness(static_cast<int>(s.hardness * 100.0f));
        setBrushOpacity(static_cast<int>(s.opacity * 100.0f));
        setBrushFlow(static_cast<int>(s.flow * 100.0f));
        emit presetSelected(preset);
    });
    connect(m_presetPicker, &BrushPresetPicker::savePresetRequested, this, [this]() {
        bool ok;
        QString name = QInputDialog::getText(this, tr("Save Brush Preset"),
            tr("Name:"), QLineEdit::Normal, QString(), &ok);
        if (!ok || name.isEmpty()) return;

        BrushPreset p;
        p.name = name;
        p.settings.size = static_cast<float>(scrubInt(m_sizeSlider->value()));
        p.settings.hardness = static_cast<float>(scrubInt(m_hardnessSlider->value())) / 100.0f;
        p.settings.opacity = static_cast<float>(scrubInt(m_opacitySlider->value())) / 100.0f;
        p.settings.flow = static_cast<float>(scrubInt(m_flowSlider->value())) / 100.0f;
        p.settings.color = Qt::black;
        p.settings.type = BrushType::Round;
        m_presetManager->addPreset(p);
        m_presetPicker->setCurrentPreset(p);
    });

    return w;
}

QWidget* ToolOptionsBar::createCloneStampPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_clonePresetPicker = new BrushPresetPicker(this);
    m_clonePresetPicker->setPresetManager(m_presetManager);
    m_clonePresetPicker->setFixedWidth(60);
    m_clonePresetPicker->setOpensExternalPanel(true);
    connect(m_clonePresetPicker, &BrushPresetPicker::panelRequested,
            this, &ToolOptionsBar::openBrushPanelRequested);
    connect(m_clonePresetPicker, &BrushPresetPicker::importBrushesRequested,
            this, &ToolOptionsBar::importBrushesRequested);
    lay->addWidget(m_clonePresetPicker);
    if (!m_presetManager->presets().empty())
        m_clonePresetPicker->setCurrentPreset(m_presetManager->presets().front());

    lay->addWidget(new ToolBarSeparator(this));

    m_cloneSizeSlider = new ScrubbableValueInput(tr("Size"), 1, 1000, 20, tr("px"), 1, this);
    lay->addWidget(m_cloneSizeSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_cloneHardnessSlider = new ScrubbableValueInput(tr("Hardness"), 0, 100, 80, tr("%"), 1, this);
    lay->addWidget(m_cloneHardnessSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_cloneOpacitySlider = new ScrubbableValueInput(tr("Opacity"), 1, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_cloneOpacitySlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_cloneFlowSlider = new ScrubbableValueInput(tr("Flow"), 1, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_cloneFlowSlider);

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(new QLabel(tr("Blend:")));
    m_cloneBlendModeCombo = new QComboBox(this);
    m_cloneBlendModeCombo->addItems({
        tr("Normal"), tr("Multiply"), tr("Screen"), tr("Overlay"),
        tr("Darken"), tr("Lighten"), tr("Color Dodge"), tr("Color Burn"),
        tr("Hard Light"), tr("Soft Light"), tr("Difference"), tr("Exclusion"),
        tr("Hue"), tr("Saturation"), tr("Color"), tr("Luminosity")
    });
    m_cloneBlendModeCombo->setFixedWidth(118);
    lay->addWidget(m_cloneBlendModeCombo);

    lay->addWidget(new ToolBarSeparator(this));

    m_cloneAlignedCb = new AppCheckBox(tr("Aligned"), this);
    m_cloneAlignedCb->setChecked(true);
    lay->addWidget(m_cloneAlignedCb);

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(new QLabel(tr("Sample:")));
    m_cloneSampleCombo = new QComboBox(this);
    m_cloneSampleCombo->addItems({tr("Current Layer"), tr("Current & Below"), tr("All Layers")});
    m_cloneSampleCombo->setCurrentIndex(1);
    m_cloneSampleCombo->setFixedWidth(132);
    lay->addWidget(m_cloneSampleCombo);

    auto emitPresetSettings = [this](const BrushPreset& preset) {
        const auto& s = preset.settings;
        if (m_cloneSizeSlider)
            m_cloneSizeSlider->setValue(static_cast<int>(s.size));
        if (m_cloneHardnessSlider)
            m_cloneHardnessSlider->setValue(static_cast<int>(s.hardness * 100.0f));
        if (m_cloneOpacitySlider)
            m_cloneOpacitySlider->setValue(static_cast<int>(s.opacity * 100.0f));
        if (m_cloneFlowSlider)
            m_cloneFlowSlider->setValue(static_cast<int>(s.flow * 100.0f));
        if (m_cloneBlendModeCombo)
            m_cloneBlendModeCombo->setCurrentIndex(static_cast<int>(s.blendMode));
        emit presetSelected(preset);
    };

    connect(m_clonePresetPicker, &BrushPresetPicker::presetSelected,
            this, emitPresetSettings);
    connect(m_cloneSizeSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushSizeChanged(scrubInt(v));
    });
    connect(m_cloneHardnessSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushHardnessChanged(scrubInt(v));
    });
    connect(m_cloneOpacitySlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushOpacityChanged(scrubInt(v));
    });
    connect(m_cloneFlowSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushFlowChanged(scrubInt(v));
    });
    connect(m_cloneBlendModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::cloneBlendModeChanged);
    connect(m_cloneAlignedCb, &QCheckBox::toggled,
            this, &ToolOptionsBar::cloneAlignedChanged);
    connect(m_cloneSampleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::cloneSampleModeChanged);

    lay->addStretch();
    return w;
}

QWidget* ToolOptionsBar::createHealingBrushPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_healingPresetPicker = new BrushPresetPicker(this);
    m_healingPresetPicker->setPresetManager(m_presetManager);
    m_healingPresetPicker->setFixedWidth(60);
    m_healingPresetPicker->setOpensExternalPanel(true);
    connect(m_healingPresetPicker, &BrushPresetPicker::panelRequested,
            this, &ToolOptionsBar::openBrushPanelRequested);
    connect(m_healingPresetPicker, &BrushPresetPicker::importBrushesRequested,
            this, &ToolOptionsBar::importBrushesRequested);
    lay->addWidget(m_healingPresetPicker);
    if (!m_presetManager->presets().empty())
        m_healingPresetPicker->setCurrentPreset(m_presetManager->presets().front());

    lay->addWidget(new ToolBarSeparator(this));

    m_healingSizeSlider = new ScrubbableValueInput(tr("Size"), 1, 1000, 20, tr("px"), 1, this);
    lay->addWidget(m_healingSizeSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_healingHardnessSlider = new ScrubbableValueInput(tr("Hardness"), 0, 100, 80, tr("%"), 1, this);
    lay->addWidget(m_healingHardnessSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_healingOpacitySlider = new ScrubbableValueInput(tr("Opacity"), 1, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_healingOpacitySlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_healingFlowSlider = new ScrubbableValueInput(tr("Flow"), 1, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_healingFlowSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_healingDiffusionSlider = new ScrubbableValueInput(tr("Diffusion"), 0, 100, 50, tr("%"), 1, this);
    lay->addWidget(m_healingDiffusionSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_healingAlignedCb = new AppCheckBox(tr("Aligned"), this);
    m_healingAlignedCb->setChecked(true);
    lay->addWidget(m_healingAlignedCb);

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(new QLabel(tr("Sample:")));
    m_healingSampleCombo = new QComboBox(this);
    m_healingSampleCombo->addItems({tr("Current Layer"), tr("Current & Below"), tr("All Layers")});
    m_healingSampleCombo->setCurrentIndex(1);
    m_healingSampleCombo->setFixedWidth(132);
    lay->addWidget(m_healingSampleCombo);

    auto emitPresetSettings = [this](const BrushPreset& preset) {
        const auto& s = preset.settings;
        if (m_healingSizeSlider)
            m_healingSizeSlider->setValue(static_cast<int>(s.size));
        if (m_healingHardnessSlider)
            m_healingHardnessSlider->setValue(static_cast<int>(s.hardness * 100.0f));
        if (m_healingOpacitySlider)
            m_healingOpacitySlider->setValue(static_cast<int>(s.opacity * 100.0f));
        if (m_healingFlowSlider)
            m_healingFlowSlider->setValue(static_cast<int>(s.flow * 100.0f));
        emit presetSelected(preset);
    };

    connect(m_healingPresetPicker, &BrushPresetPicker::presetSelected,
            this, emitPresetSettings);
    connect(m_healingSizeSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushSizeChanged(scrubInt(v));
    });
    connect(m_healingHardnessSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushHardnessChanged(scrubInt(v));
    });
    connect(m_healingOpacitySlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushOpacityChanged(scrubInt(v));
    });
    connect(m_healingFlowSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushFlowChanged(scrubInt(v));
    });
    connect(m_healingDiffusionSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit healingDiffusionChanged(scrubInt(v));
    });
    // Healing shares the Clone Stamp's aligned/sample-mode state on the canvas.
    connect(m_healingAlignedCb, &QCheckBox::toggled,
            this, &ToolOptionsBar::cloneAlignedChanged);
    connect(m_healingSampleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::cloneSampleModeChanged);

    lay->addStretch();
    return w;
}

void ToolOptionsBar::setTool(int tool)
{
    // tool: 0=Move, 1=Brush, 2=Eraser, 3=Select, 4=Zoom, 5=Hand, 6=Text,
    // 7=Crop, 8=FillBucket, 9=Eyedropper, 10=Shape, 11=CloneStamp, 12=Gradient,
    // 13=Skew
    // stack: 0=empty, 1=brush, 2=select, 3=zoom-empty, 4=hand-empty, 5=text,
    // 6=move, 7=crop, 8=fillbucket, 9=eyedropper, 10=shape, 11=clone,
    // 12=healing, 13=gradient, 14=skew
    static const int toolToPage[] = { 6, 1, 1, 2, 3, 4, 5, 7, 8, 9, 10, 11, 13 };
    int idx;
    if (tool == 11)  // Clone Stamp: page depends on the stamp sub-mode (clone/healing)
        idx = (m_stampMode == 1) ? 12 : 11;
    else if (tool == 13)  // Skew tool: Distort/Perspective page
        idx = 14;
    else if (tool == 14 || tool == 15) // AI Object Selection / AI Remove
        idx = 15;
    else
        idx = (tool >= 0 && tool < 13) ? toolToPage[tool] : 0;
    m_stack->setCurrentIndex(idx);
    m_stack->updateGeometry();
    m_optionsHost->updateGeometry();
    updateGeometry();
}

void ToolOptionsBar::setStampMode(int mode)
{
    m_stampMode = (mode == 1) ? 1 : 0;
    // If a Clone Stamp page is currently showing, switch to the matching one.
    const int cur = m_stack->currentIndex();
    if (cur == 11 || cur == 12) {
        m_stack->setCurrentIndex(m_stampMode == 1 ? 12 : 11);
        m_stack->updateGeometry();
        m_optionsHost->updateGeometry();
        updateGeometry();
    }
}

void ToolOptionsBar::setSubToolsForTool(int tool, const QVector<int>& subTools, int activeSubTool)
{
    if (tool != kSelectToolId)
        return;

    const bool showAll = subTools.isEmpty();
    for (auto it = m_selectTypeButtons.begin(); it != m_selectTypeButtons.end(); ++it) {
        if (it.value())
            it.value()->setVisible(showAll || subTools.contains(it.key()));
    }

    setActiveSubTool(tool, activeSubTool);
}

void ToolOptionsBar::setActiveSubTool(int tool, int subTool)
{
    if (tool != kSelectToolId || !m_selectTypeGroup)
        return;

    auto* btn = m_selectTypeButtons.value(subTool, nullptr);
    if (!btn)
        return;

    QSignalBlocker blocker(m_selectTypeGroup);
    btn->setChecked(true);

    if (m_selectToleranceSlider)
        m_selectToleranceSlider->setVisible(subTool == 3 || subTool == 4);
    if (m_selectSizeSlider)
        m_selectSizeSlider->setVisible(subTool == 4);
}

int ToolOptionsBar::brushSize() const
{
    return m_sizeSlider ? scrubInt(m_sizeSlider->value()) : 20;
}

int ToolOptionsBar::brushOpacity() const
{
    return m_opacitySlider ? scrubInt(m_opacitySlider->value()) : 100;
}

int ToolOptionsBar::brushHardness() const
{
    return m_hardnessSlider ? scrubInt(m_hardnessSlider->value()) : 80;
}

int ToolOptionsBar::brushFlow() const
{
    return m_flowSlider ? scrubInt(m_flowSlider->value()) : 100;
}

QColor ToolOptionsBar::brushColor() const
{
    return m_foreground;
}

void ToolOptionsBar::setBrushSize(int size)
{
    auto sync = [size](ScrubbableValueInput* input) {
        if (!input)
            return;
        input->blockSignals(true);
        input->setValue(size);
        input->blockSignals(false);
    };
    sync(m_sizeSlider);
    sync(m_cloneSizeSlider);
    sync(m_healingSizeSlider);
    sync(m_selectSizeSlider);
}

void ToolOptionsBar::setBrushHardness(int hardness)
{
    auto sync = [hardness](ScrubbableValueInput* input) {
        if (!input)
            return;
        input->blockSignals(true);
        input->setValue(hardness);
        input->blockSignals(false);
    };
    sync(m_hardnessSlider);
    sync(m_cloneHardnessSlider);
    sync(m_healingHardnessSlider);
}

void ToolOptionsBar::setBrushOpacity(int opacity)
{
    if (m_opacitySlider) {
        m_opacitySlider->blockSignals(true);
        m_opacitySlider->setValue(opacity);
        m_opacitySlider->blockSignals(false);
    }
}

void ToolOptionsBar::setBrushFlow(int flow)
{
    if (m_flowSlider) {
        m_flowSlider->blockSignals(true);
        m_flowSlider->setValue(flow);
        m_flowSlider->blockSignals(false);
    }
}

int ToolOptionsBar::brushMinPressure() const
{
    return m_minPressureSlider ? scrubInt(m_minPressureSlider->value()) : 0;
}

void ToolOptionsBar::setBrushMinPressure(int percent)
{
    if (m_minPressureSlider) {
        m_minPressureSlider->blockSignals(true);
        m_minPressureSlider->setValue(percent);
        m_minPressureSlider->blockSignals(false);
    }
}

void ToolOptionsBar::setBrushPressureEnabled(bool on)
{
    if (m_brushPressureBtn) {
        m_brushPressureBtn->blockSignals(true);
        m_brushPressureBtn->setChecked(on);
        m_brushPressureBtn->blockSignals(false);
    }
}

bool ToolOptionsBar::brushPressureEnabled() const
{
    return m_brushPressureBtn && m_brushPressureBtn->isChecked();
}

void ToolOptionsBar::setCurrentBrushPreset(const BrushPreset& preset)
{
    if (m_presetPicker)
        m_presetPicker->setCurrentPreset(preset);
    if (m_clonePresetPicker)
        m_clonePresetPicker->setCurrentPreset(preset);
    if (m_healingPresetPicker)
        m_healingPresetPicker->setCurrentPreset(preset);
}

QWidget* ToolOptionsBar::createZoomPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    auto* fitBtn = new QPushButton(w);
    fitBtn->setIcon(makeIcon(":/icons/zoom-fit.png"));
    fitBtn->setIconSize(QSize(24, 24));
    fitBtn->setFixedSize(26, 26);
    fitBtn->setToolTip(tr("Fit canvas to viewport"));
    lay->addWidget(fitBtn);

    lay->addWidget(new ToolBarSeparator(this));

    auto* originalBtn = new QPushButton(w);
    originalBtn->setIcon(makeIcon(":/icons/zoom-1-1.png"));
    originalBtn->setIconSize(QSize(24, 24));
    originalBtn->setFixedSize(26, 26);
    originalBtn->setToolTip(tr("Zoom to original size (100%)"));
    lay->addWidget(originalBtn);

    lay->addStretch();

    connect(fitBtn,      &QPushButton::clicked, this, &ToolOptionsBar::zoomFitClicked);
    connect(originalBtn, &QPushButton::clicked, this, &ToolOptionsBar::zoomOriginalClicked);

    return w;
}

QWidget* ToolOptionsBar::createSelectPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_selectTypeGroup = new QButtonGroup(this);
    m_selectTypeGroup->setExclusive(true);
    auto addTypeBtn = [&](const QString& iconPath, const QString& tooltip, int id,
                          const QString& shortcutId) {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(iconPath));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(26, 26);
        btn->setCheckable(true);
        btn->setChecked(id == 0);
        bindShortcutTooltip(btn, shortcutId, tooltip);
        m_selectTypeGroup->addButton(btn, id);
        m_selectTypeButtons.insert(id, btn);
        lay->addWidget(btn);
    };
    addTypeBtn(":/icons/select-rect.png",    tr("Rectangular Selection"), 0, QStringLiteral("tool.selection.marquee_rect"));
    addTypeBtn(":/icons/select-ellipse.png", tr("Elliptical Selection"), 1, QStringLiteral("tool.selection.marquee_ellipse"));
    addTypeBtn(":/icons/select-lasso.png",   tr("Lasso Selection"), 2, QStringLiteral("tool.selection.lasso"));
    addTypeBtn(":/icons/select-wand.png",    tr("Magic Wand"), 3, QStringLiteral("tool.selection.magic_wand"));
    addTypeBtn(":/icons/select-quick.png",   tr("Quick Selection"), 4, QStringLiteral("tool.selection.quick_selection"));
    addTypeBtn(":/icons/select-magnet.png",  tr("Magnetic Lasso"), 5, QStringLiteral("tool.selection.magnetic_lasso"));
    addTypeBtn(":/icons/select-polylasso.png",   tr("Polygonal Lasso"), 6, QStringLiteral("tool.selection.polygonal_lasso"));

    lay->addWidget(new ToolBarSeparator(this));

    // Quick Selection brush size (shares the global brush size via brushSizeChanged).
    m_selectSizeSlider = new ScrubbableValueInput(tr("Size"), 1, 1000, 20, tr("px"), 1, this);
    m_selectSizeSlider->setVisible(false);
    lay->addWidget(m_selectSizeSlider);
    connect(m_selectSizeSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit brushSizeChanged(scrubInt(v));
    });

    m_selectToleranceSlider = new ScrubbableValueInput(tr("Tolerance"), 1, 200, 32, QString(), 1, this);
    m_selectToleranceSlider->setVisible(false);
    lay->addWidget(m_selectToleranceSlider);
    connect(m_selectToleranceSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit toleranceChanged(scrubInt(v));
    });

    lay->addWidget(new ToolBarSeparator(this));

    auto* featherSlider = new ScrubbableValueInput(tr("Feather"), 0, 64, 3, tr("px"), 1, this);
    lay->addWidget(featherSlider);
    connect(featherSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit featherClicked(scrubInt(v));
    });

    lay->addWidget(new ToolBarSeparator(this));
    m_selectModeGroup = new QButtonGroup(this);
    m_selectModeGroup->setExclusive(true);
    auto addModeBtn = [&](const QString& iconPath, const QString& tooltip, int id) {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(iconPath));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(26, 26);
        btn->setCheckable(true);
        btn->setChecked(id == 0);
        btn->setToolTip(tooltip);
        m_selectModeGroup->addButton(btn, id);
        lay->addWidget(btn);
    };
    addModeBtn(":/icons/selection-new.png",       tr("New Selection"),        0);
    addModeBtn(":/icons/selection-add.png",        tr("Add to Selection"),     1);
    addModeBtn(":/icons/selection-subtract.png",   tr("Subtract from Selection"), 2);
    addModeBtn(":/icons/selection-intersect.png",  tr("Intersect with Selection"), 3);

    lay->addWidget(new ToolBarSeparator(this));
    m_selectAntiAliasCb = new AppCheckBox(tr("Anti-Alias"), this);
    m_selectAntiAliasCb->setChecked(false);
    lay->addWidget(m_selectAntiAliasCb);
    connect(m_selectAntiAliasCb, &QCheckBox::toggled, this, &ToolOptionsBar::selectAntiAliasChanged);

    lay->addWidget(new ToolBarSeparator(this));

    auto* invertBtn = new QPushButton(tr("Invert"), this);
    // invertBtn->setFixedHeight(24);
    bindShortcutTooltip(invertBtn, QStringLiteral("select.invert"), tr("Invert Selection"));
    connect(invertBtn, &QPushButton::clicked, this, &ToolOptionsBar::invertClicked);
    lay->addWidget(invertBtn);

    lay->addWidget(new ToolBarSeparator(this));

    auto* generativeFillBtn = new QPushButton(this);
    generativeFillBtn->setIcon(makeIcon(":/icons/generative-fill.png"));
    generativeFillBtn->setIconSize(QSize(33, 33));
    generativeFillBtn->setFixedSize(34, 34);
    bindShortcutTooltip(generativeFillBtn,
                        QStringLiteral("selection.generative_fill"),
                        tr("Generative Fill"));
    connect(generativeFillBtn, &QPushButton::clicked, this, &ToolOptionsBar::generativeFillClicked);
    lay->addWidget(generativeFillBtn);

    m_selectionOptionsBtn = new QPushButton(this);
    m_selectionOptionsBtn->setIcon(makeIcon(":/icons/select-settings.png"));
    m_selectionOptionsBtn->setIconSize(QSize(24, 24));
    m_selectionOptionsBtn->setFixedSize(34, 34);
    m_selectionOptionsBtn->setToolTip(tr("Options"));
    lay->addWidget(m_selectionOptionsBtn);

    m_selectionOptionsPanel = new PopupPanel(this);
    m_selectionOptionsPanel->setContentWidget(createSelectionOptionsContent());
    connect(m_selectionOptionsBtn, &QPushButton::pressed, this, [this]() {
        if (m_selectionOptionsPanel && m_selectionOptionsPanel->isVisible()) {
            m_selectionOptionsPanel->hide();
            m_selectionOptionsSuppressClick = true;
        }
    });
    connect(m_selectionOptionsBtn, &QPushButton::clicked, this, [this]() {
        if (m_selectionOptionsSuppressClick) {
            m_selectionOptionsSuppressClick = false;
            return;
        }
        if (m_selectionOptionsPanel)
            m_selectionOptionsPanel->toggleAnchoredTo(m_selectionOptionsBtn);
    });

    lay->addStretch();

    connect(m_selectTypeGroup, &QButtonGroup::idClicked, this, [this](int id) {
        // Magic Wand (3) and Quick Selection (4) use tolerance; Quick Selection
        // additionally exposes the brush size.
        m_selectToleranceSlider->setVisible(id == 3 || id == 4);
        if (m_selectSizeSlider)
            m_selectSizeSlider->setVisible(id == 4);
    });
    connect(m_selectTypeGroup, &QButtonGroup::idClicked, this, &ToolOptionsBar::selectTypeChanged);
    connect(m_selectModeGroup, &QButtonGroup::idClicked, this, &ToolOptionsBar::selectModeChanged);

    return w;
}

QWidget* ToolOptionsBar::createAiSelectPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    // Selection operation (New / Add / Subtract / Intersect) — same icons and
    // semantics as the other selection tools.
    m_aiOpRow = new QWidget(this);
    auto* opLay = new QHBoxLayout(m_aiOpRow);
    opLay->setContentsMargins(0, 0, 0, 0);
    opLay->setSpacing(kToolOptionSpacing);
    m_aiOpGroup = new QButtonGroup(this);
    m_aiOpGroup->setExclusive(true);
    auto addOpBtn = [&](const QString& iconPath, const QString& tooltip, int id) {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(iconPath));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(26, 26);
        btn->setCheckable(true);
        btn->setChecked(id == 0);
        btn->setToolTip(tooltip);
        m_aiOpGroup->addButton(btn, id);
        opLay->addWidget(btn);
    };
    addOpBtn(":/icons/selection-new.png",       tr("New Selection"), 0);
    addOpBtn(":/icons/selection-add.png",       tr("Add to Selection"), 1);
    addOpBtn(":/icons/selection-subtract.png",  tr("Subtract from Selection"), 2);
    addOpBtn(":/icons/selection-intersect.png", tr("Intersect with Selection"), 3);
    connect(m_aiOpGroup, &QButtonGroup::idClicked, this, [this](int id) {
        if (m_aiController)
            m_aiController->setOperation(static_cast<AiSelectionOperation>(id));
    });
    lay->addWidget(m_aiOpRow);

    // Separator between the Selection Mode controls and the model-settings button.
    lay->addWidget(new ToolBarSeparator(this));

    // The three model selectors (object-selection model, mask-refinement model and
    // the background-removal engine) live in a dedicated popup so the bar stays
    // uncluttered, mirroring the Refine Edges popup. The popup is built eagerly so
    // refreshAiModels()/bindAiController() can populate the lists before it is ever
    // opened.
    m_aiModelsBtn = new QPushButton(this);
    m_aiModelsBtn->setIcon(makeIcon(":/icons/ai-models.png"));
    m_aiModelsBtn->setIconSize(QSize(24, 24));
    m_aiModelsBtn->setFixedSize(34, 34);
    m_aiModelsBtn->setToolTip(tr("Model settings"));
    lay->addWidget(m_aiModelsBtn);

    m_aiModelsPanel = new PopupPanel(this);
    m_aiModelsPanel->setContentWidget(createAiModelsContent());
    connect(m_aiModelsBtn, &QPushButton::clicked, this, [this]() {
        if (m_aiModelsPanel)
            m_aiModelsPanel->toggleAnchoredTo(m_aiModelsBtn);
    });

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(new QLabel(tr("Sample:")));
    m_aiSampleCombo = new QComboBox(this);
    m_aiSampleCombo->addItem(tr("All Visible Layers"), int(AiSampleSource::AllVisible));
    m_aiSampleCombo->addItem(tr("Current Layer"), int(AiSampleSource::CurrentLayer));
    m_aiSampleCombo->setToolTip(tr("Which pixels the AI sees as input"));
    lay->addWidget(m_aiSampleCombo);
    connect(m_aiSampleCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_aiController)
            m_aiController->setSampleSource(
                static_cast<AiSampleSource>(m_aiSampleCombo->currentData().toInt()));
    });

    // Temporarily hidden: AI Remove Tool will be released later.
    // Do not remove the implementation. Only the user-facing UI is disabled.
    /*
    m_aiRemoveSampleCombo = new QComboBox(this);
    m_aiRemoveSampleCombo->addItem(tr("All Visible"), int(AiSnapshotSource::AllVisible));
    m_aiRemoveSampleCombo->addItem(tr("Current & Below"), int(AiSnapshotSource::CurrentAndBelow));
    m_aiRemoveSampleCombo->addItem(tr("Current Layer"), int(AiSnapshotSource::CurrentLayer));
    m_aiRemoveSampleCombo->setToolTip(tr("Which pixels AI Remove samples"));
    lay->addWidget(m_aiRemoveSampleCombo);
    connect(m_aiRemoveSampleCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { pushAiRemoveOptions(); });
    */

    lay->addWidget(new ToolBarSeparator(this));

    // Temporarily hidden: AI Remove Tool will be released later.
    // Do not remove the implementation. Only the user-facing UI is disabled.
    /*
    m_aiRemoveOutputLabel = new QLabel(tr("Output:"), this);
    lay->addWidget(m_aiRemoveOutputLabel);
    m_aiRemoveOutputCombo = new QComboBox(this);
    m_aiRemoveOutputCombo->addItem(tr("New Layer"), int(AiRemoveOutputMode::NewLayer));
    m_aiRemoveOutputCombo->addItem(tr("Active Layer"), int(AiRemoveOutputMode::ActiveLayer));
    lay->addWidget(m_aiRemoveOutputCombo);
    connect(m_aiRemoveOutputCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { pushAiRemoveOptions(); });

    m_aiRemoveOptionsBtn = new QPushButton(tr("Remove Options…"), this);
    connect(m_aiRemoveOptionsBtn, &QPushButton::clicked, this, [this]() {
        if (!m_aiRemovePanel) {
            m_aiRemovePanel = new PopupPanel(this);
            m_aiRemovePanel->setContentWidget(createAiRemoveContent());
        }
        m_aiRemovePanel->toggleAnchoredTo(m_aiRemoveOptionsBtn);
    });
    lay->addWidget(m_aiRemoveOptionsBtn);

    m_aiRemoveCancelBtn = new QPushButton(tr("Cancel"), this);
    connect(m_aiRemoveCancelBtn, &QPushButton::clicked, this, [this]() {
        if (m_aiRemoveController)
            m_aiRemoveController->cancel();
    });
    lay->addWidget(m_aiRemoveCancelBtn);
    */

    lay->addWidget(new ToolBarSeparator(this));

    m_aiAntiAliasCb = new AppCheckBox(tr("Anti-Alias"), this);
    m_aiAntiAliasCb->setChecked(true);
    m_aiAntiAliasCb->setToolTip(tr("Soften the mask edges when converting to a selection"));
    lay->addWidget(m_aiAntiAliasCb);
    connect(m_aiAntiAliasCb, &QCheckBox::toggled, this, [this](bool on) {
        if (m_aiController)
            m_aiController->setAntiAlias(on);
    });

    // Refine edges (Etapa 3 matting): a master toggle, the matting model to use,
    // and a popup with the edge/cleanup controls.
    m_aiRefineCb = new AppCheckBox(tr("Refine Edges"), this);
    m_aiRefineCb->setChecked(false);
    m_aiRefineCb->setToolTip(tr("Refine the mask edges with a matting model and/or edge cleanup"));
    lay->addWidget(m_aiRefineCb);
    connect(m_aiRefineCb, &QCheckBox::toggled, this, [this](bool on) {
        if (m_aiController) m_aiController->setRefineEnabled(on);
        // The Mask Refinement model selector stays usable regardless of the toggle.
        if (m_aiRefineOptionsBtn) m_aiRefineOptionsBtn->setEnabled(on);
    });

    m_aiRefineOptionsBtn = new QPushButton(this);
    m_aiRefineOptionsBtn->setIcon(makeIcon(":/icons/ui-settings.png"));
    m_aiRefineOptionsBtn->setIconSize(QSize(24, 24));
    m_aiRefineOptionsBtn->setFixedSize(34, 34);
    m_aiRefineOptionsBtn->setEnabled(false);
    m_aiRefineOptionsBtn->setToolTip(tr("Edge and cleanup options"));
    connect(m_aiRefineOptionsBtn, &QPushButton::clicked, this, [this]() {
        if (!m_aiRefinePanel) {
            m_aiRefinePanel = new PopupPanel(this);
            m_aiRefinePanel->setContentWidget(createAiRefineContent());
        }
        m_aiRefinePanel->toggleAnchoredTo(m_aiRefineOptionsBtn);
    });
    lay->addWidget(m_aiRefineOptionsBtn);

    lay->addWidget(new ToolBarSeparator(this));

    m_aiSelectSubjectBtn = new QPushButton(tr("Select Subject"), this);
    m_aiSelectSubjectBtn->setToolTip(tr("Automatically select the main subject"));
    connect(m_aiSelectSubjectBtn, &QPushButton::clicked, this, [this]() {
        if (m_aiController) m_aiController->selectSubject();
    });
    lay->addWidget(m_aiSelectSubjectBtn);

    m_aiRemoveBgBtn = new QPushButton(tr("Remove Background"), this);
    m_aiRemoveBgBtn->setToolTip(tr("Mask out the background of the active layer (non-destructive)"));
    connect(m_aiRemoveBgBtn, &QPushButton::clicked, this, [this]() {
        if (m_aiController) m_aiController->removeBackground();
    });
    lay->addWidget(m_aiRemoveBgBtn);

    lay->addStretch();

    // Inline "no model" warning + Open Settings shortcut (hidden when ready).
    m_aiWarningRow = new QWidget(this);
    auto* warnLay = new QHBoxLayout(m_aiWarningRow);
    warnLay->setContentsMargins(0, 0, 0, 0);
    warnLay->setSpacing(t->spaceSM);
    auto* warnLabel = new QLabel(tr("AI object selection requires a SAM model."), m_aiWarningRow);
    warnLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorWarning.name()));
    warnLay->addWidget(warnLabel);
    m_aiOpenSettingsBtn = new QPushButton(tr("Open Settings"), m_aiWarningRow);
    connect(m_aiOpenSettingsBtn, &QPushButton::clicked, this, &ToolOptionsBar::aiOpenSettingsRequested);
    warnLay->addWidget(m_aiOpenSettingsBtn);
    lay->addWidget(m_aiWarningRow);

    m_aiStatusLabel = new QLabel(tr("Ready"), this);
    m_aiStatusLabel->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorTextSecondary.name()));
    lay->addWidget(m_aiStatusLabel);

    refreshAiToolPageMode();
    return w;
}

QWidget* ToolOptionsBar::createAiModelsContent()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(m_aiModelsPanel);
    w->setMinimumWidth(300);
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    lay->setSpacing(t->spaceSM);

    auto addHeading = [&](const QString& text) {
        auto* l = new QLabel(text, w);
        l->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }")
                             .arg(t->colorTextBright.name()));
        lay->addWidget(l);
    };
    auto addDescription = [&](const QString& text) {
        auto* l = new QLabel(text, w);
        l->setWordWrap(true);
        l->setStyleSheet(QStringLiteral("QLabel { color: %1; }")
                             .arg(t->colorTextSecondary.name()));
        lay->addWidget(l);
    };
    auto addDivider = [&]() {
        auto* sep = new QFrame(w);
        sep->setFrameShape(QFrame::HLine);
        sep->setStyleSheet(QStringLiteral("color: %1; background: %1;").arg(t->colorBorder.name()));
        lay->addWidget(sep);
    };

    // 1. Object-selection model — detects/selects objects for the initial mask.
    addHeading(tr("Selection Model"));
    addDescription(tr("Detects and selects objects in the image. Used for the "
                      "initial AI object selection."));
    m_aiModelCombo = new QComboBox(w);
    m_aiModelCombo->setMinimumWidth(260);
    m_aiModelCombo->setToolTip(tr("SAM model used for AI object selection"));
    lay->addWidget(m_aiModelCombo);
    connect(m_aiModelCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_aiController)
            m_aiController->setRequestedModelId(m_aiModelCombo->currentData().toString());
    });

    addDivider();

    // 2. Mask-refinement (matting) model — runs after the initial selection.
    addHeading(tr("Mask Refinement Model"));
    addDescription(tr("Runs after the initial selection to improve the mask — "
                      "edges, hair, transparency and fine detail."));
    m_aiRefineModelCombo = new QComboBox(w);
    m_aiRefineModelCombo->setMinimumWidth(260);
    m_aiRefineModelCombo->setToolTip(tr("Matting model used to refine edges"));
    lay->addWidget(m_aiRefineModelCombo);
    connect(m_aiRefineModelCombo, qOverload<int>(&QComboBox::currentIndexChanged), this,
            [this](int) { pushAiRefineOptions(); });

    addDivider();

    // 3. Background-removal engine — used by the background-removal actions.
    addHeading(tr("Background Removal Engine"));
    addDescription(tr("Selects which mechanism removes the background in the "
                      "background-removal actions."));
    m_aiBgEngineCombo = new QComboBox(w);
    m_aiBgEngineCombo->setMinimumWidth(260);
    m_aiBgEngineCombo->addItem(tr("Auto"), int(AiBackgroundRemovalEngine::Auto));
    m_aiBgEngineCombo->addItem(tr("SAM"), int(AiBackgroundRemovalEngine::SamRefine));
    // RMBG / BiRefNet engine options temporarily hidden; backend support remains.
    // m_aiBgEngineCombo->addItem(tr("RMBG"), int(AiBackgroundRemovalEngine::Rmbg));
    // m_aiBgEngineCombo->addItem(tr("BiRefNet"), int(AiBackgroundRemovalEngine::BiRefNet));
    m_aiBgEngineCombo->setToolTip(tr("Which model removes the background"));
    lay->addWidget(m_aiBgEngineCombo);
    connect(m_aiBgEngineCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this](int) {
        if (m_aiController)
            m_aiController->setBackgroundRemovalEngine(
                static_cast<AiBackgroundRemovalEngine>(m_aiBgEngineCombo->currentData().toInt()));
    });

    // Temporarily hidden: inpainting model settings will be exposed later.
    // Backend/model support must remain implemented.
    /*
    addDivider();

    addHeading(tr("Inpainting Model"));
    addDescription(tr("Local ONNX model used by AI Remove Object."));
    m_aiInpaintModelCombo = new QComboBox(w);
    m_aiInpaintModelCombo->setMinimumWidth(260);
    m_aiInpaintModelCombo->addItem(tr("Auto"), QString());
    lay->addWidget(m_aiInpaintModelCombo);
    connect(m_aiInpaintModelCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { pushAiRemoveOptions(); });

    addHeading(tr("Remove Engine"));
    m_aiRemoveEngineCombo = new QComboBox(w);
    m_aiRemoveEngineCombo->setMinimumWidth(260);
    m_aiRemoveEngineCombo->addItem(tr("Auto"), int(AiRemoveEngine::Auto));
    m_aiRemoveEngineCombo->addItem(tr("Fast Remove"), int(AiRemoveEngine::DirectInpaint));
    m_aiRemoveEngineCombo->addItem(tr("Stable Diffusion Inpaint"), int(AiRemoveEngine::StableDiffusion));
    lay->addWidget(m_aiRemoveEngineCombo);
    connect(m_aiRemoveEngineCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, [this](int) { pushAiRemoveOptions(); });
    */

    return w;
}

QWidget* ToolOptionsBar::createAiRefineContent()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(m_aiRefinePanel);
    w->setMinimumWidth(240);
    w->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }")
                         .arg(t->colorTextBright.name()));
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    lay->setSpacing(t->spaceSM);

    lay->addWidget(new QLabel(tr("Edge"), w));
    auto addSlider = [&](ScrubbableValueInput* s) {
        s->setMinimumWidth(210);
        lay->addWidget(s);
        connect(s, &ScrubbableValueInput::valueChanged, this, [this](double) { pushAiRefineOptions(); });
    };
    m_aiSmoothSlider   = new ScrubbableValueInput(tr("Smooth"),   0, 100, 0, QString(), 1, w);
    m_aiFeatherSlider  = new ScrubbableValueInput(tr("Feather"),  0, 100, 0, tr("px"), 1, w);
    m_aiContrastSlider = new ScrubbableValueInput(tr("Contrast"), 0, 100, 0, QString(), 1, w);
    m_aiShiftSlider    = new ScrubbableValueInput(tr("Shift Edge"), -100, 100, 0, QString(), 1, w);
    addSlider(m_aiSmoothSlider);
    addSlider(m_aiFeatherSlider);
    addSlider(m_aiContrastSlider);
    addSlider(m_aiShiftSlider);

    auto* sep = new QFrame(w);
    sep->setFrameShape(QFrame::HLine);
    sep->setStyleSheet(QStringLiteral("color: %1; background: %1;").arg(t->colorBorder.name()));
    lay->addWidget(sep);

    lay->addWidget(new QLabel(tr("Cleanup"), w));
    m_aiRemoveIslandsCb = new AppCheckBox(tr("Remove small islands"), w);
    m_aiFillHolesCb     = new AppCheckBox(tr("Fill small holes"), w);
    m_aiPreserveSoftCb  = new AppCheckBox(tr("Preserve soft edges"), w);
    m_aiPreserveSoftCb->setChecked(true);
    for (AppCheckBox* cb : {m_aiRemoveIslandsCb, m_aiFillHolesCb, m_aiPreserveSoftCb}) {
        lay->addWidget(cb);
        connect(cb, &QCheckBox::toggled, this, [this](bool) { pushAiRefineOptions(); });
    }

    auto* sep2 = new QFrame(w);
    sep2->setFrameShape(QFrame::HLine);
    sep2->setStyleSheet(QStringLiteral("color: %1; background: %1;").arg(t->colorBorder.name()));
    lay->addWidget(sep2);

    m_aiRefineNowBtn = new QPushButton(tr("Refine Selection"), w);
    m_aiRefineNowBtn->setToolTip(tr("Refine the current selection's edges with a matting model"));
    connect(m_aiRefineNowBtn, &QPushButton::clicked, this, [this]() {
        if (m_aiController) m_aiController->refineSelection();
    });
    lay->addWidget(m_aiRefineNowBtn);

    return w;
}

QWidget* ToolOptionsBar::createAiRemoveContent()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(m_aiRemovePanel);
    w->setMinimumWidth(320);
    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    lay->setSpacing(t->spaceSM);

    auto* title = new QLabel(tr("Remove Object"), w);
    title->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }")
                             .arg(t->colorTextBright.name()));
    lay->addWidget(title);

    auto addSlider = [&](ScrubbableValueInput* input) {
        input->setMinimumWidth(260);
        lay->addWidget(input);
        connect(input, &ScrubbableValueInput::valueChanged, this,
                [this](double) { pushAiRemoveOptions(); });
    };

    m_aiRemoveGrowSlider = new ScrubbableValueInput(tr("Mask Grow"), 0, 128, 8, tr("px"), 1, w);
    m_aiRemoveFeatherSlider = new ScrubbableValueInput(tr("Mask Feather"), 0, 128, 4, tr("px"), 1, w);
    m_aiRemovePaddingSlider = new ScrubbableValueInput(tr("Padding"), 0, 512, 64, tr("px"), 1, w);
    addSlider(m_aiRemoveGrowSlider);
    addSlider(m_aiRemoveFeatherSlider);
    addSlider(m_aiRemovePaddingSlider);

    m_aiRemovePromptEdit = new QLineEdit(w);
    m_aiRemovePromptEdit->setPlaceholderText(tr("Prompt"));
    m_aiRemovePromptEdit->setText(QStringLiteral("background only, natural continuation, seamless fill"));
    lay->addWidget(m_aiRemovePromptEdit);
    connect(m_aiRemovePromptEdit, &QLineEdit::textChanged,
            this, [this](const QString&) { pushAiRemoveOptions(); });

    m_aiRemoveNegativePromptEdit = new QLineEdit(w);
    m_aiRemoveNegativePromptEdit->setPlaceholderText(tr("Negative Prompt"));
    m_aiRemoveNegativePromptEdit->setText(QStringLiteral("object, person, duplicate, artifact, blur, distortion, text, watermark"));
    lay->addWidget(m_aiRemoveNegativePromptEdit);
    connect(m_aiRemoveNegativePromptEdit, &QLineEdit::textChanged,
            this, [this](const QString&) { pushAiRemoveOptions(); });

    m_aiRemoveStepsSlider = new ScrubbableValueInput(tr("Steps"), 1, 150, 20, QString(), 1, w);
    m_aiRemoveStrengthSlider = new ScrubbableValueInput(tr("Strength"), 0, 100, 85, tr("%"), 1, w);
    m_aiRemoveGuidanceSlider = new ScrubbableValueInput(tr("Guidance"), 0, 30, 8, QString(), 1, w);
    m_aiRemoveSeedSlider = new ScrubbableValueInput(tr("Seed"), 0, 999999, 0, QString(), 1, w);
    addSlider(m_aiRemoveStepsSlider);
    addSlider(m_aiRemoveStrengthSlider);
    addSlider(m_aiRemoveGuidanceSlider);
    addSlider(m_aiRemoveSeedSlider);

    m_aiRemoveRandomSeedCb = new AppCheckBox(tr("Random Seed"), w);
    m_aiRemoveRandomSeedCb->setChecked(true);
    lay->addWidget(m_aiRemoveRandomSeedCb);
    connect(m_aiRemoveRandomSeedCb, &QCheckBox::toggled,
            this, [this](bool) { pushAiRemoveOptions(); });

    pushAiRemoveOptions();
    return w;
}

void ToolOptionsBar::pushAiRefineOptions()
{
    if (!m_aiController)
        return;
    AiRefineOptions opts = m_aiController->refineOptions();
    opts.enabled = m_aiRefineCb && m_aiRefineCb->isChecked();
    if (m_aiRefineModelCombo)
        opts.mattingModelId = m_aiRefineModelCombo->currentData().toString();
    if (m_aiSmoothSlider)   opts.smooth    = int(m_aiSmoothSlider->value());
    if (m_aiFeatherSlider)  opts.feather   = int(m_aiFeatherSlider->value());
    if (m_aiContrastSlider) opts.contrast  = int(m_aiContrastSlider->value());
    if (m_aiShiftSlider)    opts.shiftEdge = int(m_aiShiftSlider->value());
    if (m_aiRemoveIslandsCb) opts.removeSmallIslands = m_aiRemoveIslandsCb->isChecked();
    if (m_aiFillHolesCb)     opts.fillSmallHoles     = m_aiFillHolesCb->isChecked();
    if (m_aiPreserveSoftCb)  opts.preserveSoftEdges  = m_aiPreserveSoftCb->isChecked();
    m_aiController->setRefineOptions(opts);
}

void ToolOptionsBar::refreshAiModels()
{
    if (!m_aiModelCombo)
        return;
    QSignalBlocker blocker(m_aiModelCombo);
    m_aiModelCombo->clear();
    m_aiModelCombo->addItem(tr("Auto"), QString());
    if (m_aiController) {
        for (const QString& id : m_aiController->installedSamModels())
            m_aiModelCombo->addItem(m_aiController->modelDisplayName(id), id);
        const QString requested = m_aiController->requestedModelId();
        int idx = requested.isEmpty() ? 0 : m_aiModelCombo->findData(requested);
        m_aiModelCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }

    // Refine (matting) model list mirrors the same Auto + installed pattern.
    if (m_aiRefineModelCombo) {
        QSignalBlocker rb(m_aiRefineModelCombo);
        m_aiRefineModelCombo->clear();
        m_aiRefineModelCombo->addItem(tr("Auto"), QString());
        if (m_aiController) {
            for (const QString& id : m_aiController->installedMattingModels())
                m_aiRefineModelCombo->addItem(m_aiController->modelDisplayName(id), id);
            const QString req = m_aiController->refineOptions().mattingModelId;
            int idx = req.isEmpty() ? 0 : m_aiRefineModelCombo->findData(req);
            m_aiRefineModelCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
    }
}

void ToolOptionsBar::refreshAiRemoveModels()
{
    if (!m_aiInpaintModelCombo)
        return;
    QSignalBlocker blocker(m_aiInpaintModelCombo);
    m_aiInpaintModelCombo->clear();
    m_aiInpaintModelCombo->addItem(tr("Auto"), QString());
    if (m_aiRemoveController) {
        for (const QString& id : m_aiRemoveController->installedInpaintingModels())
            m_aiInpaintModelCombo->addItem(m_aiRemoveController->modelDisplayName(id), id);
        const QString requested = m_aiRemoveController->options().modelId;
        const int idx = requested.isEmpty() ? 0 : m_aiInpaintModelCombo->findData(requested);
        m_aiInpaintModelCombo->setCurrentIndex(idx < 0 ? 0 : idx);
    }
}

void ToolOptionsBar::pushAiRemoveOptions()
{
    if (!m_aiRemoveController)
        return;

    AiRemoveOptions opts = m_aiRemoveController->options();
    if (m_aiInpaintModelCombo)
        opts.modelId = m_aiInpaintModelCombo->currentData().toString();
    if (m_aiRemoveEngineCombo)
        opts.engine = static_cast<AiRemoveEngine>(m_aiRemoveEngineCombo->currentData().toInt());
    if (m_aiRemoveOutputCombo)
        opts.outputMode = static_cast<AiRemoveOutputMode>(m_aiRemoveOutputCombo->currentData().toInt());
    if (m_aiRemoveSampleCombo)
        opts.source = static_cast<AiSnapshotSource>(m_aiRemoveSampleCombo->currentData().toInt());
    if (m_aiRemoveGrowSlider) opts.maskGrowPx = scrubInt(m_aiRemoveGrowSlider->value());
    if (m_aiRemoveFeatherSlider) opts.maskFeatherPx = scrubInt(m_aiRemoveFeatherSlider->value());
    if (m_aiRemovePaddingSlider) opts.paddingPx = scrubInt(m_aiRemovePaddingSlider->value());
    if (m_aiRemovePromptEdit) opts.prompt = m_aiRemovePromptEdit->text();
    if (m_aiRemoveNegativePromptEdit) opts.negativePrompt = m_aiRemoveNegativePromptEdit->text();
    if (m_aiRemoveStepsSlider) opts.steps = scrubInt(m_aiRemoveStepsSlider->value());
    if (m_aiRemoveStrengthSlider) opts.strength = float(m_aiRemoveStrengthSlider->value() / 100.0);
    if (m_aiRemoveGuidanceSlider) opts.guidanceScale = float(m_aiRemoveGuidanceSlider->value());
    if (m_aiRemoveSeedSlider) opts.seed = quint64(qMax(0, scrubInt(m_aiRemoveSeedSlider->value())));
    if (m_aiRemoveRandomSeedCb) opts.randomSeed = m_aiRemoveRandomSeedCb->isChecked();

    m_aiRemoveController->setOptions(opts);
}

void ToolOptionsBar::refreshAiToolPageMode()
{
    const bool removeMode = m_aiToolPageMode == AiToolPageMode::RemoveObject;
    if (m_aiOpRow) m_aiOpRow->setVisible(!removeMode);
    if (m_aiSampleCombo) m_aiSampleCombo->setVisible(!removeMode);
    if (m_aiAntiAliasCb) m_aiAntiAliasCb->setVisible(!removeMode);
    if (m_aiRefineCb) m_aiRefineCb->setVisible(!removeMode);
    if (m_aiRefineOptionsBtn) m_aiRefineOptionsBtn->setVisible(!removeMode);
    if (m_aiSelectSubjectBtn) m_aiSelectSubjectBtn->setVisible(!removeMode);
    if (m_aiRemoveBgBtn) m_aiRemoveBgBtn->setVisible(!removeMode);

    if (m_aiRemoveSampleCombo) m_aiRemoveSampleCombo->setVisible(removeMode);
    if (m_aiRemoveOutputLabel) m_aiRemoveOutputLabel->setVisible(removeMode);
    if (m_aiRemoveOutputCombo) m_aiRemoveOutputCombo->setVisible(removeMode);
    if (m_aiRemoveOptionsBtn) m_aiRemoveOptionsBtn->setVisible(removeMode);
    if (m_aiRemoveCancelBtn) m_aiRemoveCancelBtn->setVisible(removeMode);
    refreshAiAvailability();
}

void ToolOptionsBar::setAiToolPageMode(AiToolPageMode mode)
{
    if (m_aiToolPageMode == mode) {
        refreshAiToolPageMode();
        return;
    }
    m_aiToolPageMode = mode;
    refreshAiToolPageMode();
}

void ToolOptionsBar::refreshAiAvailability()
{
    if (m_aiToolPageMode == AiToolPageMode::RemoveObject) {
        QString reason;
        const bool ready = m_aiRemoveController && m_aiRemoveController->isReady(&reason);
        if (m_aiWarningRow)
            m_aiWarningRow->setVisible(!ready);
        if (m_aiRemoveCancelBtn)
            m_aiRemoveCancelBtn->setEnabled(m_aiRemoveController != nullptr);
        if (m_aiStatusLabel)
            m_aiStatusLabel->setText(ready ? tr("Ready") : reason);
        return;
    }

    QString reason;
    const bool ready = m_aiController && m_aiController->isReady(&reason);
    // Remove Background / Refine can run with matting/bg models even without SAM.
    const bool bgReady = m_aiController && m_aiController->isRemoveBackgroundReady();
    const bool hasMatting = m_aiController && !m_aiController->installedMattingModels().isEmpty();

    if (m_aiWarningRow)
        m_aiWarningRow->setVisible(!ready && !bgReady);
    if (m_aiSelectSubjectBtn) m_aiSelectSubjectBtn->setEnabled(ready);
    if (m_aiRemoveBgBtn) m_aiRemoveBgBtn->setEnabled(bgReady);
    if (m_aiBgEngineCombo) m_aiBgEngineCombo->setEnabled(bgReady);

    // Refine controls need a matting model to be meaningful (edge-only cleanup is
    // still allowed, so keep the toggle enabled whenever the runtime is ready).
    const bool refineCapable = ready || bgReady;
    if (m_aiRefineCb) m_aiRefineCb->setEnabled(refineCapable);
    const bool refineOn = m_aiRefineCb && m_aiRefineCb->isChecked() && refineCapable;
    // The Mask Refinement model selector is not gated by the Refine Edges toggle —
    // it only needs the runtime ready and an installed matting model to choose from.
    if (m_aiRefineModelCombo) m_aiRefineModelCombo->setEnabled(refineCapable && hasMatting);
    if (m_aiRefineOptionsBtn) m_aiRefineOptionsBtn->setEnabled(refineOn);
    if (m_aiRefineNowBtn) m_aiRefineNowBtn->setEnabled(refineCapable);

    if (m_aiStatusLabel)
        m_aiStatusLabel->setText((ready || bgReady) ? tr("Ready") : reason);
}

void ToolOptionsBar::bindAiController(AiObjectSelectionController* controller)
{
    if (m_aiController == controller) {
        refreshAiModels();
        refreshAiAvailability();
        return;
    }
    // Drop status/availability links to the previous controller; the widget→
    // controller connections read m_aiController live, so they need no rebind.
    if (m_aiController)
        disconnect(m_aiController, nullptr, this, nullptr);

    m_aiController = controller;

    if (m_aiController) {
        // Guard against the per-canvas controller being destroyed (tab closed)
        // while still referenced here.
        connect(m_aiController, &QObject::destroyed, this, [this](QObject* o) {
            if (m_aiController == o) {
                m_aiController = nullptr;
                refreshAiAvailability();
            }
        });
        // Per-job progress is surfaced on the app status bar + loading dialog
        // (MainWindow), not here; the options-bar label only shows availability.
        connect(m_aiController, &AiObjectSelectionController::busyChanged, this,
                [this](bool busy) {
                    if (busy) {
                        for (QPushButton* b : {m_aiSelectSubjectBtn, m_aiRemoveBgBtn, m_aiRefineNowBtn})
                            if (b) b->setEnabled(false);
                    } else {
                        refreshAiAvailability();   // restore the correct enabled state
                    }
                });
        connect(m_aiController, &AiObjectSelectionController::availabilityChanged, this, [this]() {
            refreshAiModels();
            refreshAiAvailability();
        });

        // Sync the widgets to the controller's current option values.
        if (m_aiSampleCombo) {
            QSignalBlocker b(m_aiSampleCombo);
            const int idx = m_aiSampleCombo->findData(int(AiSampleSource::AllVisible));
            m_aiSampleCombo->setCurrentIndex(idx < 0 ? 0 : idx);
            m_aiController->setSampleSource(
                static_cast<AiSampleSource>(m_aiSampleCombo->currentData().toInt()));
        }
        if (m_aiOpGroup) {
            if (auto* b = m_aiOpGroup->button(int(m_aiController->operation())))
                b->setChecked(true);
        }
        if (m_aiAntiAliasCb)
            m_aiController->setAntiAlias(m_aiAntiAliasCb->isChecked());

        // Refine + background-removal engine reflect the controller's state.
        const AiRefineOptions ro = m_aiController->refineOptions();
        if (m_aiRefineCb) {
            QSignalBlocker b(m_aiRefineCb);
            m_aiRefineCb->setChecked(ro.enabled);
        }
        if (m_aiSmoothSlider)   m_aiSmoothSlider->setValue(ro.smooth);
        if (m_aiFeatherSlider)  m_aiFeatherSlider->setValue(ro.feather);
        if (m_aiContrastSlider) m_aiContrastSlider->setValue(ro.contrast);
        if (m_aiShiftSlider)    m_aiShiftSlider->setValue(ro.shiftEdge);
        if (m_aiRemoveIslandsCb) m_aiRemoveIslandsCb->setChecked(ro.removeSmallIslands);
        if (m_aiFillHolesCb)     m_aiFillHolesCb->setChecked(ro.fillSmallHoles);
        if (m_aiPreserveSoftCb)  m_aiPreserveSoftCb->setChecked(ro.preserveSoftEdges);
        if (m_aiBgEngineCombo) {
            QSignalBlocker b(m_aiBgEngineCombo);
            const int idx = m_aiBgEngineCombo->findData(int(m_aiController->backgroundRemovalEngine()));
            m_aiBgEngineCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
    }

    refreshAiModels();
    refreshAiAvailability();
}

void ToolOptionsBar::bindAiRemoveController(AiRemoveObjectController* controller)
{
    if (m_aiRemoveController == controller) {
        refreshAiRemoveModels();
        refreshAiAvailability();
        return;
    }
    if (m_aiRemoveController)
        disconnect(m_aiRemoveController, nullptr, this, nullptr);

    m_aiRemoveController = controller;
    if (m_aiRemoveController) {
        connect(m_aiRemoveController, &QObject::destroyed, this, [this](QObject* o) {
            if (m_aiRemoveController == o) {
                m_aiRemoveController = nullptr;
                refreshAiAvailability();
            }
        });
        connect(m_aiRemoveController, &AiRemoveObjectController::statusChanged, this,
                [this](const QString& s) { if (m_aiStatusLabel) m_aiStatusLabel->setText(s); });
        connect(m_aiRemoveController, &AiRemoveObjectController::runningChanged, this,
                [this](bool running) {
                    if (m_aiRemoveCancelBtn) m_aiRemoveCancelBtn->setEnabled(running);
                    if (!running) refreshAiAvailability();
                });
        connect(m_aiRemoveController, &AiRemoveObjectController::availabilityChanged, this, [this]() {
            refreshAiRemoveModels();
            refreshAiAvailability();
        });

        const AiRemoveOptions opts = m_aiRemoveController->options();
        if (m_aiRemoveEngineCombo) {
            QSignalBlocker b(m_aiRemoveEngineCombo);
            const int idx = m_aiRemoveEngineCombo->findData(int(opts.engine));
            m_aiRemoveEngineCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
        if (m_aiRemoveOutputCombo) {
            QSignalBlocker b(m_aiRemoveOutputCombo);
            const int idx = m_aiRemoveOutputCombo->findData(int(opts.outputMode));
            m_aiRemoveOutputCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
        if (m_aiRemoveSampleCombo) {
            QSignalBlocker b(m_aiRemoveSampleCombo);
            const int idx = m_aiRemoveSampleCombo->findData(int(opts.source));
            m_aiRemoveSampleCombo->setCurrentIndex(idx < 0 ? 0 : idx);
        }
    }

    refreshAiRemoveModels();
    pushAiRemoveOptions();
    refreshAiAvailability();
}

QWidget* ToolOptionsBar::createSelectionOptionsContent()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(m_selectionOptionsPanel);
    w->setMinimumWidth(260);
    w->setStyleSheet(QStringLiteral("QLabel { color: %1; font-weight: 600; }")
        .arg(t->colorTextBright.name()));

    auto* lay = new QVBoxLayout(w);
    lay->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, t->spaceMD);
    lay->setSpacing(t->spaceSM);

    auto* actionsLabel = new QLabel(tr("Actions"), w);
    lay->addWidget(actionsLabel);

    auto* actionRow1 = new QWidget(w);
    auto* actionRow1Lay = new QHBoxLayout(actionRow1);
    actionRow1Lay->setContentsMargins(0, 0, 0, 0);
    actionRow1Lay->setSpacing(t->spaceSM);

    auto* selectAllBtn = new QPushButton(tr("Select All"), actionRow1);
    // selectAllBtn->setFixedHeight(24);
    bindShortcutTooltip(selectAllBtn, QStringLiteral("select.all"), tr("Select All"));
    connect(selectAllBtn, &QPushButton::clicked, this, &ToolOptionsBar::selectAllClicked);
    actionRow1Lay->addWidget(selectAllBtn);

    auto* deselectBtn = new QPushButton(tr("Select None"), actionRow1);
    // deselectBtn->setFixedHeight(24);
    bindShortcutTooltip(deselectBtn, QStringLiteral("select.deselect"), tr("Select None"));
    connect(deselectBtn, &QPushButton::clicked, this, &ToolOptionsBar::deselectClicked);
    actionRow1Lay->addWidget(deselectBtn);
    lay->addWidget(actionRow1);

    auto* actionRow2 = new QWidget(w);
    auto* actionRow2Lay = new QHBoxLayout(actionRow2);
    actionRow2Lay->setContentsMargins(0, 0, 0, 0);
    actionRow2Lay->setSpacing(t->spaceSM);

    // auto* refineBtn = new QPushButton(tr("Refine Selection"), actionRow2);
    // // refineBtn->setFixedHeight(24);
    // bindShortcutTooltip(refineBtn, QStringLiteral("select.refine"), tr("Refine Selection"));
    // connect(refineBtn, &QPushButton::clicked, this, &ToolOptionsBar::refineEdgeClicked);
    // actionRow2Lay->addWidget(refineBtn);

    auto* cropBtn = new QPushButton(tr("Crop To Selection"), actionRow2);
    // cropBtn->setFixedHeight(24);
    bindShortcutTooltip(cropBtn,
                        QStringLiteral("selection.crop_to_selection"),
                        tr("Crop To Selection"));
    connect(cropBtn, &QPushButton::clicked, this, &ToolOptionsBar::cropToSelectionClicked);
    actionRow2Lay->addWidget(cropBtn);
    lay->addWidget(actionRow2);

    auto* separator = new QFrame(w);
    separator->setFrameShape(QFrame::HLine);
    separator->setStyleSheet(QStringLiteral("color: %1; background: %1;")
        .arg(t->colorBorder.name()));
    lay->addWidget(separator);

    auto* modifiersLabel = new QLabel(tr("Selection Modifiers"), w);
    lay->addWidget(modifiersLabel);

    auto addModifier = [&](ScrubbableValueInput* slider) {
        slider->setMinimumWidth(220);
        lay->addWidget(slider);
    };

    auto* growSlider = new ScrubbableValueInput(tr("Grow"), 1, 64, 2, tr("px"), 1, w);
    addModifier(growSlider);
    connect(growSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit growClicked(scrubInt(v));
    });

    auto* shrinkSlider = new ScrubbableValueInput(tr("Shrink"), 1, 64, 2, tr("px"), 1, w);
    addModifier(shrinkSlider);
    connect(shrinkSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit shrinkClicked(scrubInt(v));
    });

    auto* borderSlider = new ScrubbableValueInput(tr("Border"), 1, 64, 2, tr("px"), 1, w);
    addModifier(borderSlider);
    connect(borderSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit borderClicked(scrubInt(v));
    });

    auto* smoothSlider = new ScrubbableValueInput(tr("Smooth"), 1, 20, 2, tr("px"), 1, w);
    addModifier(smoothSlider);
    connect(smoothSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit smoothClicked(scrubInt(v));
    });

    return w;
}

QWidget* ToolOptionsBar::createTextPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_fontCombo = new FontPopupComboBox(this);
    m_fontCombo->setFixedWidth(140);
    m_fontCombo->setStyleSheet(
        QStringLiteral("QFontComboBox { color: white; }"));
    if (auto* fontPopup = m_fontCombo->view()) {
        fontPopup->setMinimumSize(kFontPopupMinWidth, kFontPopupMinHeight);
        fontPopup->setStyleSheet(
            QStringLiteral("QAbstractItemView { color: white; background: %1; padding: %2px; }"
                "QAbstractItemView::item { color: white; padding: 22px; min-height: 62px; }"
                "QAbstractItemView::item:selected { background: %3; }")
                .arg(t->colorBackgroundPrimary.name())
                .arg(t->spaceSM)
                .arg(t->colorSurfaceSelected.name()));
    }
    if (auto* fontModel = m_fontCombo->model()) {
        const QSize itemSize(kFontPopupMinWidth, kFontPopupItemHeight);
        for (int row = 0; row < fontModel->rowCount(); ++row)
            fontModel->setData(fontModel->index(row, 0), itemSize, Qt::SizeHintRole);
    }
    lay->addWidget(m_fontCombo);
    connect(m_fontCombo, &QFontComboBox::currentFontChanged, this, &ToolOptionsBar::textFontChanged);

    lay->addWidget(new ToolBarSeparator(this));

    m_fontSizeSlider = new ScrubbableValueInput(tr("Size"), 4, 1500, 32, tr("px"), 1, this);
    lay->addWidget(m_fontSizeSlider);
    connect(m_fontSizeSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit textSizeChanged(scrubInt(v));
    });

    lay->addWidget(new ToolBarSeparator(this));

    m_boldBtn = new QPushButton(makeIcon(":/icons/text-bold.png"), QString(), this);
    m_boldBtn->setCheckable(true);
    m_boldBtn->setFixedSize(26, 26);
    m_boldBtn->setIconSize(QSize(24, 24));
    bindShortcutTooltip(m_boldBtn, QStringLiteral("text.bold"), tr("Bold"));
    lay->addWidget(m_boldBtn);
    connect(m_boldBtn, &QPushButton::toggled, this, &ToolOptionsBar::textBoldChanged);

    m_italicBtn = new QPushButton(makeIcon(":/icons/text-italic.png"), QString(), this);
    m_italicBtn->setCheckable(true);
    m_italicBtn->setFixedSize(26, 26);
    m_italicBtn->setIconSize(QSize(24, 24));
    bindShortcutTooltip(m_italicBtn, QStringLiteral("text.italic"), tr("Italic"));
    lay->addWidget(m_italicBtn);
    connect(m_italicBtn, &QPushButton::toggled, this, &ToolOptionsBar::textItalicChanged);

    m_underlineBtn = new QPushButton(makeIcon(":/icons/text-underline.png"), QString(), this);
    m_underlineBtn->setCheckable(true);
    m_underlineBtn->setFixedSize(26, 26);
    m_underlineBtn->setIconSize(QSize(24, 24));
    bindShortcutTooltip(m_underlineBtn, QStringLiteral("text.underline"), tr("Underline"));
    lay->addWidget(m_underlineBtn);
    connect(m_underlineBtn, &QPushButton::toggled, this, &ToolOptionsBar::textUnderlineChanged);

    m_strikethroughBtn = new QPushButton(makeIcon(":/icons/text-strikethrough.png"), QString(), this);
    m_strikethroughBtn->setCheckable(true);
    m_strikethroughBtn->setFixedSize(26, 26);
    m_strikethroughBtn->setIconSize(QSize(24, 24));
    bindShortcutTooltip(m_strikethroughBtn, QStringLiteral("text.strikethrough"), tr("Strikethrough"));
    lay->addWidget(m_strikethroughBtn);
    connect(m_strikethroughBtn, &QPushButton::toggled, this, &ToolOptionsBar::textStrikethroughChanged);

    lay->addWidget(new ToolBarSeparator(this));

    m_textColorBtn = new QPushButton(this);
    m_textColorBtn->setFixedSize(24, 24);
    m_textColorBtn->setToolTip(tr("Text Color"));
    m_textColorBtn->setStyleSheet(colorBtnStyle(Qt::black));
    lay->addWidget(m_textColorBtn);
    connect(m_textColorBtn, &QPushButton::clicked, this, [this]() {
        if (m_textColorDlg) { m_textColorDlg->raise(); m_textColorDlg->activateWindow(); return; }
        auto* dlg = new ColorPickerDialog(m_foreground, ColorPickerMode::Foreground, this);
        m_textColorDlg = dlg;
        connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& c) {
            m_textColorBtn->setStyleSheet(colorBtnStyle(c));
            emit textColorChanged(c);
            emit foregroundColorChanged(c);
        });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    });

    lay->addWidget(new ToolBarSeparator(this));

    m_alignGroup = new QButtonGroup(this);
    m_alignGroup->setExclusive(true);
    struct { const char* icon; const char* tooltip; } alignDefs[3] = {
        {":/icons/format-align-left.png",   QT_TR_NOOP("Align Left")},
        {":/icons/format-align-center.png", QT_TR_NOOP("Align Center")},
        {":/icons/format-align-right.png",  QT_TR_NOOP("Align Right")},
    };
    for (int i = 0; i < 3; ++i) {
        auto* btn = new QPushButton(makeIcon(alignDefs[i].icon), QString(), this);
        btn->setCheckable(true);
        btn->setChecked(i == 0);
        btn->setFixedSize(28, 28);
        btn->setIconSize(QSize(24, 24));
        btn->setToolTip(tr(alignDefs[i].tooltip));
        m_alignGroup->addButton(btn, i);
        lay->addWidget(btn);
    }
    connect(m_alignGroup, &QButtonGroup::idClicked, this, &ToolOptionsBar::textAlignChanged);

    lay->addWidget(new ToolBarSeparator(this));

    m_trackingSlider = new ScrubbableValueInput(tr("Track"), -200, 400, 0, QString(), 10, this);
    lay->addWidget(m_trackingSlider);
    connect(m_trackingSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit textTrackingChanged(scrubInt(v) / 10.0);
    });

    lay->addWidget(new ToolBarSeparator(this));

    m_leadingSlider = new ScrubbableValueInput(tr("Lead"), 5, 50, 12, QString(), 1, this);
    lay->addWidget(m_leadingSlider);
    connect(m_leadingSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit textLeadingChanged(scrubInt(v) / 10.0);
    });

    lay->addStretch();
    return w;
}

QFont ToolOptionsBar::textFont() const
{
    return m_fontCombo ? m_fontCombo->currentFont() : QFont();
}

int ToolOptionsBar::textSize() const
{
    return m_fontSizeSlider ? scrubInt(m_fontSizeSlider->value()) : 32;
}

bool ToolOptionsBar::textBold() const
{
    return m_boldBtn ? m_boldBtn->isChecked() : false;
}

bool ToolOptionsBar::textItalic() const
{
    return m_italicBtn ? m_italicBtn->isChecked() : false;
}

bool ToolOptionsBar::textUnderline() const
{
    return m_underlineBtn ? m_underlineBtn->isChecked() : false;
}

bool ToolOptionsBar::textStrikethrough() const
{
    return m_strikethroughBtn ? m_strikethroughBtn->isChecked() : false;
}

QColor ToolOptionsBar::textColor() const
{
    return m_foreground;
}

int ToolOptionsBar::textAlign() const
{
    return m_alignGroup ? m_alignGroup->checkedId() : 0;
}

double ToolOptionsBar::textTracking() const
{
    return m_trackingSlider ? scrubInt(m_trackingSlider->value()) / 10.0 : 0.0;
}

double ToolOptionsBar::textLeading() const
{
    return m_leadingSlider ? scrubInt(m_leadingSlider->value()) / 10.0 : 1.2;
}

void ToolOptionsBar::setTextFont(const QFont& font)
{
    if (!m_fontCombo) return;
    m_fontCombo->blockSignals(true);
    m_fontCombo->setCurrentFont(font);
    m_fontCombo->blockSignals(false);
}

void ToolOptionsBar::setTextSize(int size)
{
    if (!m_fontSizeSlider) return;
    m_fontSizeSlider->blockSignals(true);
    m_fontSizeSlider->setValue(size);
    m_fontSizeSlider->blockSignals(false);
}

void ToolOptionsBar::setTextBold(bool bold)
{
    if (!m_boldBtn) return;
    m_boldBtn->blockSignals(true);
    m_boldBtn->setChecked(bold);
    m_boldBtn->blockSignals(false);
}

void ToolOptionsBar::setTextItalic(bool italic)
{
    if (!m_italicBtn) return;
    m_italicBtn->blockSignals(true);
    m_italicBtn->setChecked(italic);
    m_italicBtn->blockSignals(false);
}

void ToolOptionsBar::setTextUnderline(bool underline)
{
    if (!m_underlineBtn) return;
    m_underlineBtn->blockSignals(true);
    m_underlineBtn->setChecked(underline);
    m_underlineBtn->blockSignals(false);
}

void ToolOptionsBar::setTextStrikethrough(bool strikethrough)
{
    if (!m_strikethroughBtn) return;
    m_strikethroughBtn->blockSignals(true);
    m_strikethroughBtn->setChecked(strikethrough);
    m_strikethroughBtn->blockSignals(false);
}

void ToolOptionsBar::setTextColor(const QColor& color)
{
    if (!m_textColorBtn) return;
    m_textColorBtn->blockSignals(true);
    m_textColorBtn->setStyleSheet(colorBtnStyle(color));
    m_textColorBtn->blockSignals(false);
}

void ToolOptionsBar::setTextAlign(int align)
{
    if (!m_alignGroup) return;
    auto* btn = m_alignGroup->button(align);
    if (btn) {
        m_alignGroup->blockSignals(true);
        btn->setChecked(true);
        m_alignGroup->blockSignals(false);
    }
}

void ToolOptionsBar::setTextTracking(double tracking)
{
    if (!m_trackingSlider) return;
    int v = static_cast<int>(std::round(tracking * 10.0));
    v = qBound(-200, v, 400);
    m_trackingSlider->blockSignals(true);
    m_trackingSlider->setValue(v);
    m_trackingSlider->blockSignals(false);
}

void ToolOptionsBar::setTextLeading(double leading)
{
    if (!m_leadingSlider) return;
    int v = static_cast<int>(std::round(leading * 10.0));
    v = qBound(5, v, 50);
    m_leadingSlider->blockSignals(true);
    m_leadingSlider->setValue(v);
    m_leadingSlider->blockSignals(false);
}

QWidget* ToolOptionsBar::createMovePage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    w->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorTextBright.name()));
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    // Auto-Select checkbox
    m_moveAutoSelect = new AppCheckBox(tr("Auto-Select"));
    m_moveAutoSelect->setChecked(true);
    m_moveAutoSelect->setFixedHeight(24);
    lay->addWidget(m_moveAutoSelect);

    // Layer / Group combo (hidden)
    m_moveAutoSelectTarget = new QComboBox();
    m_moveAutoSelectTarget->addItem(tr("Layer"));
    m_moveAutoSelectTarget->addItem(tr("Group"));
    m_moveAutoSelectTarget->setFixedWidth(70);
    m_moveAutoSelectTarget->setEnabled(true);
    m_moveAutoSelectTarget->setVisible(false);
    lay->addWidget(m_moveAutoSelectTarget);

    // Show Transform Controls checkbox
    m_moveShowTransformControls = new AppCheckBox(tr("Show Transform Controls"));
    m_moveShowTransformControls->setChecked(true);
    m_moveShowTransformControls->setFixedHeight(24);
    lay->addWidget(m_moveShowTransformControls);

    lay->addStretch();

    // Connections
    connect(m_moveAutoSelect, &QCheckBox::toggled, this, [this](bool checked) {
        m_moveAutoSelectTarget->setEnabled(checked);
        emit moveAutoSelectChanged(checked);
    });

    connect(m_moveAutoSelectTarget, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::moveAutoSelectTargetChanged);

    connect(m_moveShowTransformControls, &QCheckBox::toggled,
            this, &ToolOptionsBar::moveShowTransformControlsChanged);

    return w;
}

QWidget* ToolOptionsBar::createTransformPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    w->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorTextBright.name()));
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_transformFields = new TransformFieldsWidget(TransformFieldsMode::OptionsBar, w);
    lay->addWidget(m_transformFields);

    lay->addSpacing(t->spaceSM);

    m_transformCancelBtn = new QPushButton(w);
    m_transformCancelBtn->setIcon(makeIcon(":/icons/ui-cancel.png"));
    m_transformCancelBtn->setIconSize(QSize(20, 20));
    m_transformCancelBtn->setFixedSize(26, 26);
    bindShortcutTooltip(m_transformCancelBtn,
                        QStringLiteral("canvas.cancel"),
                        tr("Cancel transform"));
    lay->addWidget(m_transformCancelBtn);

    m_transformApplyBtn = new QPushButton(w);
    m_transformApplyBtn->setIcon(makeIcon(":/icons/ui-apply.png"));
    m_transformApplyBtn->setIconSize(QSize(20, 20));
    m_transformApplyBtn->setFixedSize(26, 26);
    bindShortcutTooltip(m_transformApplyBtn,
                        QStringLiteral("canvas.commit"),
                        tr("Apply transform"));
    lay->addWidget(m_transformApplyBtn);

    lay->addStretch();

    connect(m_transformFields, &TransformFieldsWidget::fieldEdited,
            this, &ToolOptionsBar::transformFieldEdited);
    connect(m_transformCancelBtn, &QPushButton::clicked,
            this, &ToolOptionsBar::transformCancelClicked);
    connect(m_transformApplyBtn, &QPushButton::clicked,
            this, &ToolOptionsBar::transformApplyClicked);

    return w;
}

QWidget* ToolOptionsBar::createSkewPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    w->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorTextBright.name()));
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    // Distort / Perspective (enabled only when a raster layer is active). These
    // reuse the exact handlers/commands previously hosted on the Move page.
    m_distortBtn = new QPushButton(tr("Distort"));
    m_distortBtn->setCheckable(true);
    // m_distortBtn->setFixedHeight(24);
    m_distortBtn->setEnabled(false);
    m_distortBtn->setToolTip(tr("Distort: drag each corner freely"));
    lay->addWidget(m_distortBtn);

    m_perspectiveBtn = new QPushButton(tr("Perspective"));
    m_perspectiveBtn->setCheckable(true);
    // m_perspectiveBtn->setFixedHeight(24);
    m_perspectiveBtn->setEnabled(false);
    m_perspectiveBtn->setToolTip(tr("Perspective: coupled corner dragging"));
    lay->addWidget(m_perspectiveBtn);

    m_distortResetBtn = new QPushButton(this);
    m_distortResetBtn->setIcon(makeIcon(":/icons/transform-reset.png"));
    m_distortResetBtn->setIconSize(QSize(24, 24));
    m_distortResetBtn->setFixedSize(26, 26);
    m_distortResetBtn->setEnabled(false);
    m_distortResetBtn->setToolTip(tr("Reset distort/perspective to the original shape"));
    lay->addWidget(m_distortResetBtn);

    m_distortApplyBtn = new QPushButton(this);
    m_distortApplyBtn->setIcon(makeIcon(":/icons/skew-apply.png"));
    m_distortApplyBtn->setIconSize(QSize(24, 24));
    m_distortApplyBtn->setFixedSize(26, 26);
    m_distortApplyBtn->setEnabled(false);
    bindShortcutTooltip(m_distortApplyBtn,
                        QStringLiteral("canvas.commit"),
                        tr("Apply the transform and exit edit mode"));
    lay->addWidget(m_distortApplyBtn);

    lay->addStretch();

    connect(m_distortBtn, &QPushButton::clicked, this, &ToolOptionsBar::distortClicked);
    connect(m_perspectiveBtn, &QPushButton::clicked, this, &ToolOptionsBar::perspectiveClicked);
    connect(m_distortResetBtn, &QPushButton::clicked, this, &ToolOptionsBar::distortResetClicked);
    connect(m_distortApplyBtn, &QPushButton::clicked, this, &ToolOptionsBar::distortApplyClicked);

    return w;
}

void ToolOptionsBar::setDistortControlsEnabled(bool enabled)
{
    if (m_distortBtn) m_distortBtn->setEnabled(enabled);
    if (m_perspectiveBtn) m_perspectiveBtn->setEnabled(enabled);
    if (!enabled) {
        setDistortModeActive(-1);
        setDistortResetEnabled(false);
        setDistortApplyEnabled(false);
    }
}

void ToolOptionsBar::setDistortResetEnabled(bool enabled)
{
    if (m_distortResetBtn) m_distortResetBtn->setEnabled(enabled);
}

void ToolOptionsBar::setDistortApplyEnabled(bool enabled)
{
    if (m_distortApplyBtn) m_distortApplyBtn->setEnabled(enabled);
}

void ToolOptionsBar::setDistortModeActive(int mode)
{
    // mode matches TransformMode: 3 = Distort, 4 = Perspective, -1 = none.
    if (m_distortBtn) {
        QSignalBlocker b(m_distortBtn);
        m_distortBtn->setChecked(mode == 3);
    }
    if (m_perspectiveBtn) {
        QSignalBlocker b(m_perspectiveBtn);
        m_perspectiveBtn->setChecked(mode == 4);
    }
}

QWidget* ToolOptionsBar::createCropPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    w->setStyleSheet(QStringLiteral("QLabel { color: %1; }").arg(t->colorTextBright.name()));
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    // Aspect ratio combo
    lay->addWidget(new QLabel(tr("Ratio:")));
    m_cropRatioCombo = new QComboBox();
    m_cropRatioCombo->addItem(tr("Free"),   QSizeF(0, 0));
    m_cropRatioCombo->addItem(tr("Original"), QSizeF(-1, -1));
    m_cropRatioCombo->addItem(tr("1:1 Square"), QSizeF(1, 1));
    m_cropRatioCombo->addItem(tr("4:3"), QSizeF(4, 3));
    m_cropRatioCombo->addItem(tr("16:9"), QSizeF(16, 9));
    m_cropRatioCombo->addItem(tr("3:2"), QSizeF(3, 2));
    m_cropRatioCombo->addItem(tr("2:3"), QSizeF(2, 3));
    m_cropRatioCombo->addItem(tr("Custom"), QSizeF(-2, -2));
    m_cropRatioCombo->setFixedWidth(110);
    lay->addWidget(m_cropRatioCombo);

    // Custom width/height spinboxes
    m_cropWidthSpin = new QSpinBox();
    m_cropWidthSpin->setRange(1, 100000);
    m_cropWidthSpin->setValue(1920);
    m_cropWidthSpin->setFixedWidth(60);
    m_cropWidthSpin->setEnabled(false);
    lay->addWidget(m_cropWidthSpin);

    auto* xLabel = new QLabel(tr("×"));
    xLabel->setFixedWidth(12);
    xLabel->setAlignment(Qt::AlignCenter);
    lay->addWidget(xLabel);

    m_cropHeightSpin = new QSpinBox();
    m_cropHeightSpin->setRange(1, 100000);
    m_cropHeightSpin->setValue(1080);
    m_cropHeightSpin->setFixedWidth(60);
    m_cropHeightSpin->setEnabled(false);
    lay->addWidget(m_cropHeightSpin);

    // Separator

    lay->addWidget(new ToolBarSeparator(this));

    // Straighten slider
    m_cropStraightenSlider = new ScrubbableValueInput(tr("Straighten"), -450, 450, 0, tr("°"), 1, this);
    lay->addWidget(m_cropStraightenSlider);

    lay->addWidget(new ToolBarSeparator(this));

    // Guide overlay combo
    lay->addWidget(new QLabel(tr("Guides:")));
    m_cropGuideCombo = new QComboBox();
    m_cropGuideCombo->addItem(tr("None"), 0);
    m_cropGuideCombo->addItem(tr("Rule of Thirds"), 1);
    m_cropGuideCombo->addItem(tr("Golden Ratio"), 2);
    m_cropGuideCombo->addItem(tr("Grid"), 3);
    m_cropGuideCombo->setCurrentIndex(1);
    m_cropGuideCombo->setFixedWidth(120);
    lay->addWidget(m_cropGuideCombo);

    // Separator
    lay->addWidget(new ToolBarSeparator(this));

    // Overlay opacity
    m_cropOverlaySlider = new ScrubbableValueInput(tr("Overlay"), 0, 100, 40, tr("%"), 1, this);
    lay->addWidget(m_cropOverlaySlider);

    lay->addWidget(new ToolBarSeparator(this));

    // Reset button
    m_cropResetBtn = new QPushButton(this);
    m_cropResetBtn->setIcon(makeIcon(":/icons/crop-reset.png"));
    m_cropResetBtn->setIconSize(QSize(24, 24));
    m_cropResetBtn->setFixedSize(26, 26);
    bindShortcutTooltip(m_cropResetBtn, QStringLiteral("canvas.cancel"), tr("Reset Crop"));
    lay->addWidget(m_cropResetBtn);

    m_cropCommitBtn = new QPushButton(this);
    m_cropCommitBtn->setIcon(makeIcon(":/icons/crop-apply.png"));
    m_cropCommitBtn->setIconSize(QSize(24, 24));
    m_cropCommitBtn->setFixedSize(26, 26);
    bindShortcutTooltip(m_cropCommitBtn, QStringLiteral("canvas.commit"), tr("Apply Crop"));
    lay->addWidget(m_cropCommitBtn);

    lay->addStretch();

    // Connections
    connect(m_cropRatioCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
        QSizeF ratio = m_cropRatioCombo->itemData(idx).toSizeF();
        bool custom = (ratio.width() == -2);
        m_cropWidthSpin->setEnabled(custom);
        m_cropHeightSpin->setEnabled(custom);
        emit cropAspectRatioChanged(ratio);
        if (custom) {
            // Custom → immediately apply current W×H to crop rect
            emit cropCustomSizeChanged(m_cropWidthSpin->value(), m_cropHeightSpin->value());
        }
    });

    connect(m_cropWidthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
        if (!m_cropRatioCombo || m_cropRatioCombo->currentData().toSizeF().width() != -2)
            return;
        emit cropCustomSizeChanged(v, m_cropHeightSpin->value());
    });

    connect(m_cropHeightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this](int v) {
        if (!m_cropRatioCombo || m_cropRatioCombo->currentData().toSizeF().width() != -2)
            return;
        emit cropCustomSizeChanged(m_cropWidthSpin->value(), v);
    });

    connect(m_cropStraightenSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit cropStraightenChanged(static_cast<float>(scrubInt(v)) / 10.0f);
    });

    connect(m_cropGuideCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::cropGuideChanged);

    connect(m_cropOverlaySlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit cropOverlayOpacityChanged(static_cast<float>(scrubInt(v)) / 100.0f);
    });

    connect(m_cropResetBtn, &QPushButton::clicked,
            this, &ToolOptionsBar::cropResetClicked);

    connect(m_cropCommitBtn, &QPushButton::clicked,
            this, &ToolOptionsBar::cropCommitClicked);

    return w;
}

void ToolOptionsBar::cycleCropGuide()
{
    if (!m_cropGuideCombo) return;
    const int n = m_cropGuideCombo->count();
    if (n <= 0) return;
    // currentIndexChanged → cropGuideChanged is emitted by the existing connect.
    m_cropGuideCombo->setCurrentIndex((m_cropGuideCombo->currentIndex() + 1) % n);
}

void ToolOptionsBar::swapCropAspect()
{
    if (!m_cropRatioCombo) return;
    const QSizeF ratio = m_cropRatioCombo->currentData().toSizeF();
    // Free (0,0) and Original (-1,-1) have no orientation to swap.
    if (ratio.width() == 0 || ratio.width() == -1) return;

    if (ratio.width() == -2) {
        // Custom: swap the W/H spinboxes (block their valueChanged so the swap
        // applies as a single cropCustomSizeChanged, not two intermediate ones).
        if (!m_cropWidthSpin || !m_cropHeightSpin) return;
        const int w = m_cropWidthSpin->value();
        const int h = m_cropHeightSpin->value();
        m_cropWidthSpin->blockSignals(true);
        m_cropHeightSpin->blockSignals(true);
        m_cropWidthSpin->setValue(h);
        m_cropHeightSpin->setValue(w);
        m_cropWidthSpin->blockSignals(false);
        m_cropHeightSpin->blockSignals(false);
        emit cropCustomSizeChanged(h, w);
        return;
    }

    // Fixed ratio: apply the swapped orientation. Prefer selecting a combo item
    // that matches (e.g. 3:2 ↔ 2:3) so the label stays in sync; otherwise apply
    // the swapped ratio directly and leave the combo label as-is.
    const QSizeF swapped(ratio.height(), ratio.width());
    for (int i = 0; i < m_cropRatioCombo->count(); ++i) {
        if (m_cropRatioCombo->itemData(i).toSizeF() == swapped) {
            m_cropRatioCombo->setCurrentIndex(i);   // emits cropAspectRatioChanged
            return;
        }
    }
    emit cropAspectRatioChanged(swapped);
}

QWidget* ToolOptionsBar::createFillBucketPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_fillToleranceSlider = new ScrubbableValueInput(tr("Tolerance"), 0, 255, 32, QString(), 1, this);
    lay->addWidget(m_fillToleranceSlider);

    lay->addWidget(new ToolBarSeparator(this));

    m_fillColorBtn = new QPushButton(this);
    m_fillColorBtn->setFixedSize(24, 24);
    m_fillColorBtn->setStyleSheet(colorBtnStyle(Qt::black));
    lay->addWidget(m_fillColorBtn);

    lay->addStretch();

    connect(m_fillToleranceSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit fillBucketToleranceChanged(scrubInt(v));
    });

    connect(m_fillColorBtn, &QPushButton::clicked, this, [this]() {
        if (m_fillColorDlg) { m_fillColorDlg->raise(); m_fillColorDlg->activateWindow(); return; }
        auto* dlg = new ColorPickerDialog(m_foreground, ColorPickerMode::Foreground, this);
        m_fillColorDlg = dlg;
        connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& c) {
            m_fillColorBtn->setStyleSheet(colorBtnStyle(c));
            emit fillBucketColorChanged(c);
        });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    });

    return w;
}

QWidget* ToolOptionsBar::createGradientPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    m_gradientPreviewBtn = new QToolButton(this);
    m_gradientPreviewBtn->setToolButtonStyle(Qt::ToolButtonIconOnly);
    m_gradientPreviewBtn->setPopupMode(QToolButton::MenuButtonPopup);
    m_gradientPreviewBtn->setFixedSize(150, 24);
    m_gradientPreviewBtn->setIconSize(QSize(126, 18));
    m_gradientPreviewBtn->setMenu(new QMenu(m_gradientPreviewBtn));
    lay->addWidget(m_gradientPreviewBtn);

    m_gradientKindGroup = new QButtonGroup(this);
    m_gradientKindGroup->setExclusive(true);
    auto addKindButtonIcon = [&](const QString& iconPath, GradientKind kind, const QString& tip) {
        auto* btn = new QPushButton(this);
        btn->setCheckable(true);
        btn->setFixedSize(30, 30);
        btn->setIconSize(QSize(24, 24));
        btn->setIcon(QIcon(iconPath));
        btn->setToolTip(tip);
        const int id = static_cast<int>(kind);
        m_gradientKindGroup->addButton(btn, id);
        lay->addWidget(btn);
        if (m_gradientDefinition.kind == kind)
            btn->setChecked(true);
    };
    
    lay->addWidget(new ToolBarSeparator(this));

    addKindButtonIcon(QStringLiteral(":/icons/gradient-linear.png"), GradientKind::Linear, tr("Linear Gradient"));
    addKindButtonIcon(QStringLiteral(":/icons/gradient-radial.png"), GradientKind::Radial, tr("Radial Gradient"));
    addKindButtonIcon(QStringLiteral(":/icons/gradient-angle.png"), GradientKind::Angle, tr("Angle Gradient"));
    addKindButtonIcon(QStringLiteral(":/icons/gradient-reflected.png"), GradientKind::Reflected, tr("Reflected Gradient"));
    addKindButtonIcon(QStringLiteral(":/icons/gradient-diamond.png"), GradientKind::Diamond, tr("Diamond Gradient"));

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(new QLabel(tr("Mode:")));
    m_gradientBlendModeCombo = new QComboBox(this);
    m_gradientBlendModeCombo->addItems({
        tr("Normal"), tr("Multiply"), tr("Screen"), tr("Overlay"),
        tr("Darken"), tr("Lighten"), tr("Color Dodge"), tr("Color Burn"),
        tr("Hard Light"), tr("Soft Light"), tr("Difference"), tr("Exclusion"),
        tr("Hue"), tr("Saturation"), tr("Color"), tr("Luminosity")
    });
    m_gradientBlendModeCombo->setFixedWidth(118);
    lay->addWidget(m_gradientBlendModeCombo);

    lay->addWidget(new ToolBarSeparator(this));

    m_gradientOpacityInput = new ScrubbableValueInput(tr("Opacity"), 0, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_gradientOpacityInput);

    lay->addWidget(new ToolBarSeparator(this));

    m_gradientReverseCb = new AppCheckBox(tr("Reverse"), this);
    m_gradientDitherCb = new AppCheckBox(tr("Dither"), this);
    m_gradientTransparencyCb = new AppCheckBox(tr("Transparency"), this);
    lay->addWidget(m_gradientReverseCb);
    lay->addWidget(m_gradientDitherCb);
    lay->addWidget(m_gradientTransparencyCb);

    lay->addWidget(new ToolBarSeparator(this));

    lay->addWidget(new QLabel(tr("Method:")));
    m_gradientMethodCombo = new QComboBox(this);
    m_gradientMethodCombo->addItem(gradientInterpolationName(GradientInterpolationMethod::Perceptual),
                                   static_cast<int>(GradientInterpolationMethod::Perceptual));
    m_gradientMethodCombo->addItem(gradientInterpolationName(GradientInterpolationMethod::Linear),
                                   static_cast<int>(GradientInterpolationMethod::Linear));
    m_gradientMethodCombo->addItem(gradientInterpolationName(GradientInterpolationMethod::Classic),
                                   static_cast<int>(GradientInterpolationMethod::Classic));
    m_gradientMethodCombo->setFixedWidth(112);
    lay->addWidget(m_gradientMethodCombo);
    lay->addStretch();

    connect(m_gradientPreviewBtn, &QToolButton::clicked,
            this, &ToolOptionsBar::openGradientEditor);
    connect(m_gradientKindGroup, &QButtonGroup::idClicked, this, [this](int id) {
        m_gradientDefinition.kind = static_cast<GradientKind>(id);
        refreshGradientPreview();
        emit gradientDefinitionChanged(m_gradientDefinition);
    });
    connect(m_gradientBlendModeCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::gradientBlendModeChanged);
    connect(m_gradientOpacityInput, &ScrubbableValueInput::valueChanged, this, [this](double value) {
        emit gradientOpacityChanged(scrubInt(value));
    });
    connect(m_gradientReverseCb, &QCheckBox::toggled, this, [this](bool checked) {
        m_gradientDefinition.reverse = checked;
        refreshGradientPreview();
        emit gradientDefinitionChanged(m_gradientDefinition);
    });
    connect(m_gradientDitherCb, &QCheckBox::toggled, this, [this](bool checked) {
        m_gradientDefinition.dither = checked;
        emit gradientDefinitionChanged(m_gradientDefinition);
    });
    connect(m_gradientTransparencyCb, &QCheckBox::toggled, this, [this](bool checked) {
        m_gradientDefinition.transparency = checked;
        refreshGradientPreview();
        emit gradientDefinitionChanged(m_gradientDefinition);
    });
    connect(m_gradientMethodCombo, qOverload<int>(&QComboBox::currentIndexChanged), this, [this]() {
        m_gradientDefinition.interpolation = static_cast<GradientInterpolationMethod>(
            m_gradientMethodCombo->currentData().toInt());
        refreshGradientPreview();
        emit gradientDefinitionChanged(m_gradientDefinition);
    });
    if (m_gradientPresetManager) {
        connect(m_gradientPresetManager, &GradientPresetManager::presetsChanged,
                this, &ToolOptionsBar::refreshGradientPresetMenu);
        connect(m_gradientPresetManager, &GradientPresetManager::currentGradientChanged,
                this, &ToolOptionsBar::setGradientDefinition);
    }

    setGradientDefinition(m_gradientDefinition);
    return w;
}

bool ToolOptionsBar::moveAutoSelect() const
{
    return m_moveAutoSelect && m_moveAutoSelect->isChecked();
}

int ToolOptionsBar::moveAutoSelectTarget() const
{
    return m_moveAutoSelectTarget ? m_moveAutoSelectTarget->currentIndex() : 0;
}

bool ToolOptionsBar::showTransformControls() const
{
    return m_moveShowTransformControls && m_moveShowTransformControls->isChecked();
}

bool ToolOptionsBar::transformProportionsLocked() const
{
    return m_transformFields && m_transformFields->proportionsLocked();
}

void ToolOptionsBar::showTransformOptions(bool show)
{
    if (!m_stack) return;
    if (show)
        m_stack->setCurrentIndex(16);
}

void ToolOptionsBar::setTransformValues(double widthPx, double heightPx,
                                        double posX, double posY,
                                        double rotationDeg)
{
    if (m_transformFields)
        m_transformFields->setTransformValues(widthPx, heightPx, posX, posY, rotationDeg);
}

void ToolOptionsBar::setTransformFieldsEnabled(bool enabled)
{
    if (m_transformFields)
        m_transformFields->setFieldsEnabled(enabled);
    if (m_transformCancelBtn)
        m_transformCancelBtn->setEnabled(enabled);
    if (m_transformApplyBtn)
        m_transformApplyBtn->setEnabled(enabled);
}

void ToolOptionsBar::setMoveAutoSelect(bool enabled)
{
    if (!m_moveAutoSelect) return;
    m_moveAutoSelect->blockSignals(true);
    m_moveAutoSelect->setChecked(enabled);
    m_moveAutoSelectTarget->setEnabled(enabled);
    m_moveAutoSelect->blockSignals(false);
}

void ToolOptionsBar::setMoveAutoSelectTarget(int target)
{
    if (!m_moveAutoSelectTarget) return;
    m_moveAutoSelectTarget->blockSignals(true);
    m_moveAutoSelectTarget->setCurrentIndex(target);
    m_moveAutoSelectTarget->blockSignals(false);
}

void ToolOptionsBar::setShowTransformControls(bool show)
{
    if (!m_moveShowTransformControls) return;
    m_moveShowTransformControls->blockSignals(true);
    m_moveShowTransformControls->setChecked(show);
    m_moveShowTransformControls->blockSignals(false);
}

void ToolOptionsBar::pickColor()
{
    if (m_brushColorDlg) { m_brushColorDlg->raise(); m_brushColorDlg->activateWindow(); return; }
    auto* dlg = new ColorPickerDialog(m_foreground, ColorPickerMode::Foreground, this);
    m_brushColorDlg = dlg;
    connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& c) {
        // m_colorBtn->setStyleSheet(colorBtnStyle(c));
        emit brushColorChanged(c);
        emit foregroundColorChanged(c);
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

QWidget* ToolOptionsBar::createEyedropperPage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    // Sample Mode combo
    lay->addWidget(new QLabel(tr("Sample:")));
    auto* modeCombo = new QComboBox(this);
    modeCombo->addItem(tr("All Layers"), 0);     // Composite
    modeCombo->addItem(tr("Current Layer"), 1);  // CurrentLayer
    modeCombo->setCurrentIndex(0);
    modeCombo->setFixedWidth(120);
    lay->addWidget(modeCombo);

    lay->addWidget(new ToolBarSeparator(this));

    // Sample Size combo
    lay->addWidget(new QLabel(tr("Size:")));
    auto* sizeCombo = new QComboBox(this);
    sizeCombo->addItem(tr("Point Sample"), 0);
    sizeCombo->addItem(tr("3×3 Average"), 1);
    sizeCombo->addItem(tr("5×5 Average"), 2);
    sizeCombo->addItem(tr("11×11 Average"), 3);
    sizeCombo->setCurrentIndex(0);
    sizeCombo->setFixedWidth(130);
    lay->addWidget(sizeCombo);


    lay->addStretch();

    connect(modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::eyedropperSampleModeChanged);
    connect(sizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ToolOptionsBar::eyedropperSampleSizeChanged);

    return w;
}

void ToolOptionsBar::setForegroundColor(const QColor& color)
{
    m_foreground = color;
    // if (m_colorBtn) m_colorBtn->setStyleSheet(colorBtnStyle(color));
    if (m_textColorBtn) m_textColorBtn->setStyleSheet(colorBtnStyle(color));
    if (m_fillColorBtn) m_fillColorBtn->setStyleSheet(colorBtnStyle(color));
    m_shapeFillColor = color;
    if (m_shapeFillColorBtn) m_shapeFillColorBtn->setStyleSheet(colorBtnStyle(color));
    if (m_gradientPresetManager)
        m_gradientPresetManager->setSessionColors(m_foreground, m_background);
}

void ToolOptionsBar::setBackgroundColor(const QColor& color)
{
    m_background = color;
    m_shapeStrokeColor = color;
    if (m_shapeStrokeColorBtn) m_shapeStrokeColorBtn->setStyleSheet(colorBtnStyle(color));
    if (m_gradientPresetManager)
        m_gradientPresetManager->setSessionColors(m_foreground, m_background);
}

void ToolOptionsBar::setShapeType(int type)
{
    if (m_shapeTypeGroup) {
        QSignalBlocker blocker(m_shapeTypeGroup);
        if (auto* btn = m_shapeTypeGroup->button(type))
            btn->setChecked(true);
    }
    const bool isRect = (type == 0);
    const bool isPoly = (type == 3);
    const bool isStar = (type == 5);
    if (m_shapeCornerRadiusSlider)
        m_shapeCornerRadiusSlider->setVisible(isRect);
    if (m_shapeSidesInput)
        m_shapeSidesInput->setVisible(isPoly || isStar);
}

void ToolOptionsBar::setShapeFillColor(const QColor& color)
{
    m_shapeFillColor = color;
    if (m_shapeFillColorBtn)
        m_shapeFillColorBtn->setStyleSheet(colorBtnStyle(color));
}

void ToolOptionsBar::setShapeFillEnabled(bool enabled)
{
    if (!m_shapeFillCb)
        return;
    QSignalBlocker blocker(m_shapeFillCb);
    m_shapeFillCb->setChecked(enabled);
    if (m_shapeFillColorBtn)
        m_shapeFillColorBtn->setEnabled(enabled);
}

void ToolOptionsBar::setShapeStrokeColor(const QColor& color)
{
    m_shapeStrokeColor = color;
    if (m_shapeStrokeColorBtn)
        m_shapeStrokeColorBtn->setStyleSheet(colorBtnStyle(color));
}

void ToolOptionsBar::setShapeStrokeEnabled(bool enabled)
{
    if (!m_shapeStrokeCb)
        return;
    QSignalBlocker blocker(m_shapeStrokeCb);
    m_shapeStrokeCb->setChecked(enabled);
    if (m_shapeStrokeColorBtn)
        m_shapeStrokeColorBtn->setEnabled(enabled);
}

void ToolOptionsBar::setShapeStrokeWidth(int width)
{
    if (!m_shapeStrokeWidthSlider)
        return;
    QSignalBlocker blocker(m_shapeStrokeWidthSlider);
    m_shapeStrokeWidthSlider->setValue(width);
}

void ToolOptionsBar::setShapeOpacity(int opacity)
{
    if (!m_shapeOpacitySlider)
        return;
    QSignalBlocker blocker(m_shapeOpacitySlider);
    m_shapeOpacitySlider->setValue(opacity);
}

void ToolOptionsBar::setShapeAntiAlias(bool enabled)
{
    if (!m_shapeAntiAliasCb)
        return;
    QSignalBlocker blocker(m_shapeAntiAliasCb);
    m_shapeAntiAliasCb->setChecked(enabled);
}

void ToolOptionsBar::setShapeCornerRadius(int radius)
{
    if (!m_shapeCornerRadiusSlider)
        return;
    QSignalBlocker blocker(m_shapeCornerRadiusSlider);
    m_shapeCornerRadiusSlider->setValue(radius);
}

void ToolOptionsBar::setShapeSides(int sides)
{
    if (!m_shapeSidesInput)
        return;
    QSignalBlocker blocker(m_shapeSidesInput);
    m_shapeSidesInput->setValue(sides);
}

GradientDefinition ToolOptionsBar::gradientDefinition() const
{
    return m_gradientDefinition;
}

void ToolOptionsBar::setGradientDefinition(const GradientDefinition& definition)
{
    m_gradientDefinition = definition;
    m_gradientDefinition.normalize();

    if (m_gradientKindGroup) {
        QSignalBlocker blocker(m_gradientKindGroup);
        if (auto* btn = m_gradientKindGroup->button(static_cast<int>(m_gradientDefinition.kind)))
            btn->setChecked(true);
    }
    if (m_gradientReverseCb) {
        QSignalBlocker blocker(m_gradientReverseCb);
        m_gradientReverseCb->setChecked(m_gradientDefinition.reverse);
    }
    if (m_gradientDitherCb) {
        QSignalBlocker blocker(m_gradientDitherCb);
        m_gradientDitherCb->setChecked(m_gradientDefinition.dither);
    }
    if (m_gradientTransparencyCb) {
        QSignalBlocker blocker(m_gradientTransparencyCb);
        m_gradientTransparencyCb->setChecked(m_gradientDefinition.transparency);
    }
    if (m_gradientMethodCombo) {
        QSignalBlocker blocker(m_gradientMethodCombo);
        const int idx = m_gradientMethodCombo->findData(static_cast<int>(m_gradientDefinition.interpolation));
        m_gradientMethodCombo->setCurrentIndex(std::max(0, idx));
    }

    refreshGradientPreview();
    refreshGradientPresetMenu();
    emit gradientDefinitionChanged(m_gradientDefinition);
}

void ToolOptionsBar::refreshGradientPreview()
{
    if (!m_gradientPreviewBtn)
        return;

    const QImage preview = GradientRenderer::generateThumbnail(m_gradientDefinition, QSize(126, 18));
    m_gradientPreviewBtn->setIcon(QIcon(QPixmap::fromImage(preview)));
    m_gradientPreviewBtn->setToolTip(m_gradientDefinition.name.isEmpty()
        ? tr("Edit Gradient")
        : m_gradientDefinition.name);
}

void ToolOptionsBar::refreshGradientPresetMenu()
{
    if (!m_gradientPreviewBtn || !m_gradientPreviewBtn->menu() || !m_gradientPresetManager)
        return;

    auto* menu = m_gradientPreviewBtn->menu();
    menu->clear();
    for (const auto& preset : m_gradientPresetManager->presets()) {
        auto* action = menu->addAction(
            QIcon(QPixmap::fromImage(GradientRenderer::generateThumbnail(preset, QSize(96, 24)))),
            preset.name);
        connect(action, &QAction::triggered, this, [this, preset]() {
            if (m_gradientPresetManager)
                m_gradientPresetManager->setCurrentGradient(preset);
            else
                setGradientDefinition(preset);
        });
    }
}

void ToolOptionsBar::openGradientEditor()
{
    if (!m_gradientPresetManager)
        return;

    GradientEditorDialog dialog(m_gradientPresetManager, m_gradientDefinition, this);
    if (dialog.exec() == QDialog::Accepted)
        setGradientDefinition(dialog.gradient());
}

QWidget* ToolOptionsBar::createShapePage()
{
    auto* t = ThemeManager::instance()->current();
    auto* w = new QWidget(this);
    auto* lay = new QHBoxLayout(w);
    configureOptionsLayout(lay, t);

    // Shape Type buttons
    lay->addWidget(new QLabel(tr("Shape:")));
    m_shapeTypeGroup = new QButtonGroup(this);
    m_shapeTypeGroup->setExclusive(true);

    struct ShapeDef { QString icon; QString tip; int id; };
    const std::vector<ShapeDef> shapes = {
        {":/icons/shape-rect.png",    tr("Rectangle"), 0},
        {":/icons/shape-ellipse.png", tr("Ellipse"),   1},
        {":/icons/shape-line.png",    tr("Line"),      2},
        {":/icons/shape-polygon.png", tr("Polygon"),   3},
        {":/icons/shape-star.png", tr("Star"),      5},
        {":/icons/shape.png",         tr("Custom Shapes"), 6},
    };
    for (auto& s : shapes) {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(s.icon));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(26, 26);
        btn->setCheckable(true);
        btn->setToolTip(s.tip);
        if (s.id == 6)
            m_customShapesBtn = btn;
        m_shapeTypeGroup->addButton(btn, s.id);
        lay->addWidget(btn);
    }
    m_shapeTypeGroup->button(0)->setChecked(true);

    lay->addWidget(new ToolBarSeparator(this));

    // Fill color
    m_shapeFillCb = new AppCheckBox(tr("Fill"), this);
    m_shapeFillCb->setChecked(true);
    lay->addWidget(m_shapeFillCb);
    m_shapeFillColorBtn = new QPushButton(this);
    m_shapeFillColorBtn->setFixedSize(24, 24);
    m_shapeFillColorBtn->setStyleSheet(colorBtnStyle(m_shapeFillColor));
    lay->addWidget(m_shapeFillColorBtn);

    lay->addWidget(new ToolBarSeparator(this));

    // Stroke color
    m_shapeStrokeCb = new AppCheckBox(tr("Stroke"), this);
    m_shapeStrokeCb->setChecked(true);
    lay->addWidget(m_shapeStrokeCb);
    m_shapeStrokeColorBtn = new QPushButton(this);
    m_shapeStrokeColorBtn->setFixedSize(24, 24);
    m_shapeStrokeColorBtn->setStyleSheet(colorBtnStyle(m_shapeStrokeColor));
    lay->addWidget(m_shapeStrokeColorBtn);

    lay->addWidget(new ToolBarSeparator(this));

    // Stroke width
    m_shapeStrokeWidthSlider = new ScrubbableValueInput(tr("Width"), 0, 100, 1, tr("px"), 1, this);
    lay->addWidget(m_shapeStrokeWidthSlider);

    lay->addWidget(new ToolBarSeparator(this));

    // Opacity
    m_shapeOpacitySlider = new ScrubbableValueInput(tr("Opacity"), 1, 100, 100, tr("%"), 1, this);
    lay->addWidget(m_shapeOpacitySlider);

    lay->addWidget(new ToolBarSeparator(this));

    // Anti-alias
    m_shapeAntiAliasCb = new AppCheckBox(tr("AA"), this);
    m_shapeAntiAliasCb->setChecked(true);
    m_shapeAntiAliasCb->setToolTip(tr("Anti-alias"));
    lay->addWidget(m_shapeAntiAliasCb);

    lay->addWidget(new ToolBarSeparator(this));

    // Corner radius (Rectangle only)
    m_shapeCornerRadiusSlider = new ScrubbableValueInput(tr("Radius"), 0, 500, 0, tr("px"), 1, this);
    lay->addWidget(m_shapeCornerRadiusSlider);

    lay->addWidget(new ToolBarSeparator(this));

    // Sides (Polygon/Star only)
    m_shapeSidesInput = new ScrubbableValueInput(tr("Sides"), 3, 64, 6, QString(), 1, this);
    lay->addWidget(m_shapeSidesInput);

    lay->addStretch();

    // Initial visibility
    m_shapeCornerRadiusSlider->setVisible(true);
    m_shapeSidesInput->setVisible(false);

    // Signals
    connect(m_shapeTypeGroup, QOverload<int>::of(&QButtonGroup::idClicked),
            this, [this](int idx) {
        bool isRect = (idx == 0);
        bool isPoly = (idx == 3);
        bool isStar = (idx == 5);
        m_shapeCornerRadiusSlider->setVisible(isRect);
        m_shapeSidesInput->setVisible(isPoly || isStar);
        emit shapeTypeChanged(idx);
        if (idx == 6)
            emit openCustomShapesPanelRequested();
    });

    connect(m_shapeFillColorBtn, &QPushButton::clicked, this, [this]() {
        if (m_shapeFillCb && !m_shapeFillCb->isChecked()) return;
        if (m_shapeFillDlg) { m_shapeFillDlg->raise(); m_shapeFillDlg->activateWindow(); return; }
        auto* dlg = new ColorPickerDialog(m_shapeFillColor, ColorPickerMode::Foreground, this);
        m_shapeFillDlg = dlg;
        connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& c) {
            if (m_shapeFillCb && !m_shapeFillCb->isChecked()) return;
            m_shapeFillColor = c;
            m_shapeFillColorBtn->setStyleSheet(colorBtnStyle(c));
            emit shapeFillColorChanged(c);
        });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    });

    connect(m_shapeStrokeColorBtn, &QPushButton::clicked, this, [this]() {
        if (m_shapeStrokeCb && !m_shapeStrokeCb->isChecked()) return;
        if (m_shapeStrokeDlg) { m_shapeStrokeDlg->raise(); m_shapeStrokeDlg->activateWindow(); return; }
        auto* dlg = new ColorPickerDialog(m_shapeStrokeColor, ColorPickerMode::Foreground, this);
        m_shapeStrokeDlg = dlg;
        connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& c) {
            if (m_shapeStrokeCb && !m_shapeStrokeCb->isChecked()) return;
            m_shapeStrokeColor = c;
            m_shapeStrokeColorBtn->setStyleSheet(colorBtnStyle(c));
            emit shapeStrokeColorChanged(c);
        });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    });

    connect(m_shapeStrokeWidthSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit shapeStrokeWidthChanged(scrubInt(v));
    });
    connect(m_shapeFillCb, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_shapeFillColorBtn)
            m_shapeFillColorBtn->setEnabled(checked);
        emit shapeFillEnabledChanged(checked);
    });
    connect(m_shapeStrokeCb, &QCheckBox::toggled, this, [this](bool checked) {
        if (m_shapeStrokeColorBtn)
            m_shapeStrokeColorBtn->setEnabled(checked);
        emit shapeStrokeEnabledChanged(checked);
    });
    connect(m_shapeOpacitySlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit shapeOpacityChanged(scrubInt(v));
    });
    connect(m_shapeAntiAliasCb, &QCheckBox::toggled,
            this, &ToolOptionsBar::shapeAntiAliasChanged);
    connect(m_shapeCornerRadiusSlider, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit shapeCornerRadiusChanged(scrubInt(v));
    });
    connect(m_shapeSidesInput, &ScrubbableValueInput::valueChanged, this, [this](double v) {
        emit shapeSidesChanged(scrubInt(v));
    });

    return w;
}
