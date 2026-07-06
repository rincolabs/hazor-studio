#include "RulerGuideSettingsPage.hpp"

#include "AppSettingsMetrics.hpp"
#include "colorpicker/ColorPickerDialog.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include "ui/AppCheckBox.hpp"
#include <QComboBox>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QPushButton>
#include <QScrollArea>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QSettings>
#include <cmath>

namespace {

void applySurfacePalette(QWidget* widget, const Theme* th)
{
    if (!widget || !th)
        return;
    widget->setAutoFillBackground(true);
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Window, th->colorSurface);
    palette.setColor(QPalette::Base, th->colorSurface);
    widget->setPalette(palette);
}

} // namespace

RulerGuideSettingsPage::RulerGuideSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* th = ThemeManager::instance()->current();
    setObjectName(QStringLiteral("rulerGuideSettingsPage"));
    applyStyleSheet();

    auto addCheckRow = [](QFormLayout* form, const QString& label, QCheckBox* checkbox) {
        checkbox->setText(QString());
        checkbox->setFixedWidth(28);
        checkbox->setToolTip(label);
        form->addRow(label + QStringLiteral(":"), checkbox);
    };

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setObjectName("rulerGuideScroll");
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    applySurfacePalette(scroll, th);
    applySurfacePalette(scroll->viewport(), th);

    auto* content = new QWidget(scroll);
    content->setObjectName("rulerGuideContent");
    applySurfacePalette(content, th);
    auto* mainLay = new QVBoxLayout(content);
    mainLay->setContentsMargins(24, 16, 24, 16);
    mainLay->setSpacing(th->spaceMD);

    auto* generalGroup = new QGroupBox(tr("General"), content);
    auto* generalForm = new QFormLayout(generalGroup);
    generalForm->setSpacing(th->spaceSM);
    generalForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    auto* opacityRow = new QWidget(generalGroup);
    auto* opacityLay = new QHBoxLayout(opacityRow);
    opacityLay->setContentsMargins(0, 0, 0, 0);
    opacityLay->setSpacing(th->spaceSM);
    m_iconOpacitySlider = new QSlider(Qt::Horizontal, generalGroup);
    m_iconOpacitySlider->setRange(50, 100);
    m_iconOpacitySlider->setFixedWidth(160);
    m_opacityLabel = new QLabel(generalGroup);
    m_opacityLabel->setFixedWidth(32);
    connect(m_iconOpacitySlider, &QSlider::valueChanged, this, [this](int v) {
        m_opacityLabel->setText(QStringLiteral("%1%").arg(v));
        auto* th = ThemeManager::instance()->current();
        th->iconOpacity = v / 100.0f;
    });
    opacityLay->addWidget(m_iconOpacitySlider);
    opacityLay->addWidget(m_opacityLabel);
    opacityLay->addStretch();
    generalForm->addRow(tr("Icon Opacity:"), opacityRow);

    m_showColorPaletteCb = new AppCheckBox(generalGroup);
    addCheckRow(generalForm, tr("Show color palette"), m_showColorPaletteCb);
    mainLay->addWidget(generalGroup);

    auto* rulersGroup = new QGroupBox(tr("Rulers"), content);
    auto* rulersForm = new QFormLayout(rulersGroup);
    rulersForm->setSpacing(th->spaceSM);
    rulersForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_showRulersCb = new AppCheckBox(rulersGroup);
    addCheckRow(rulersForm, tr("Show rulers"), m_showRulersCb);

    m_unitCombo = new QComboBox(rulersGroup);
    m_unitCombo->addItem(tr("Pixels"), QVariant::fromValue(QStringLiteral("px")));
    m_unitCombo->addItem(tr("Percent"), QVariant::fromValue(QStringLiteral("percent")));
    m_unitCombo->addItem(tr("Inches"), QVariant::fromValue(QStringLiteral("in")));
    m_unitCombo->addItem(tr("Centimeters"), QVariant::fromValue(QStringLiteral("cm")));
    m_unitCombo->addItem(tr("Millimeters"), QVariant::fromValue(QStringLiteral("mm")));
    rulersForm->addRow(tr("Unit:"), m_unitCombo);

    m_majorTickSpin = new QSpinBox(rulersGroup);
    m_majorTickSpin->setRange(1, 2000);
    m_majorTickSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_majorTickSpin->setSuffix(tr(" units"));
    rulersForm->addRow(tr("Major tick spacing:"), m_majorTickSpin);

    m_minorDivSpin = new QSpinBox(rulersGroup);
    m_minorDivSpin->setRange(1, 20);
    m_minorDivSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    rulersForm->addRow(tr("Minor tick divisions:"), m_minorDivSpin);

    m_rulerSizeSpin = new QSpinBox(rulersGroup);
    m_rulerSizeSpin->setRange(16, 64);
    m_rulerSizeSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_rulerSizeSpin->setSuffix(tr(" px"));
    rulersForm->addRow(tr("Ruler size:"), m_rulerSizeSpin);

    m_mouseIndicatorCb = new AppCheckBox(rulersGroup);
    addCheckRow(rulersForm, tr("Show mouse indicator"), m_mouseIndicatorCb);
    mainLay->addWidget(rulersGroup);

    auto* guidesGroup = new QGroupBox(tr("Guides"), content);
    auto* guidesForm = new QFormLayout(guidesGroup);
    guidesForm->setSpacing(th->spaceSM);
    guidesForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_showGuidesCb = new AppCheckBox(guidesGroup);
    addCheckRow(guidesForm, tr("Show guides"), m_showGuidesCb);
    m_lockGuidesCb = new AppCheckBox(guidesGroup);
    addCheckRow(guidesForm, tr("Lock guides"), m_lockGuidesCb);
    m_guidesSnapCb = new AppCheckBox(guidesGroup);
    addCheckRow(guidesForm, tr("Snap to guides"), m_guidesSnapCb);

    m_guideColorBtn = new QPushButton(guidesGroup);
    m_guideColorBtn->setFixedWidth(96);
    guidesForm->addRow(tr("Guide color:"), m_guideColorBtn);
    connect(m_guideColorBtn, &QPushButton::clicked, this, [this]() {
        auto* dlg = new ColorPickerDialog(m_guideColor, ColorPickerMode::Foreground, this);
        connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& color) {
            m_guideColor = color;
            updateColorButton();
        });
        dlg->setAttribute(Qt::WA_DeleteOnClose);
        dlg->open();
    });

    guidesForm->addRow(tr("Guide opacity:"), makeOpacityRow());
    mainLay->addWidget(guidesGroup);

    auto* snapGroup = new QGroupBox(tr("Snap"), content);
    auto* snapForm = new QFormLayout(snapGroup);
    snapForm->setSpacing(th->spaceSM);
    snapForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_snapEnabledCb = new AppCheckBox(snapGroup);
    addCheckRow(snapForm, tr("Enable snap"), m_snapEnabledCb);
    m_snapCanvasBoundsCb = new AppCheckBox(snapGroup);
    addCheckRow(snapForm, tr("Snap to canvas bounds"), m_snapCanvasBoundsCb);
    m_snapCanvasCenterCb = new AppCheckBox(snapGroup);
    addCheckRow(snapForm, tr("Snap to canvas center"), m_snapCanvasCenterCb);
    m_snapGuidesCb = new AppCheckBox(snapGroup);
    addCheckRow(snapForm, tr("Snap to guides"), m_snapGuidesCb);

    m_snapToleranceSpin = new QSpinBox(snapGroup);
    m_snapToleranceSpin->setRange(1, 64);
    m_snapToleranceSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_snapToleranceSpin->setSuffix(tr(" px"));
    snapForm->addRow(tr("Snap tolerance:"), m_snapToleranceSpin);

    m_snapIndicatorsCb = new AppCheckBox(snapGroup);
    addCheckRow(snapForm, tr("Show snap indicators"), m_snapIndicatorsCb);
    mainLay->addWidget(snapGroup);

    mainLay->addStretch();
    scroll->setWidget(content);
    root->addWidget(scroll);

    loadSettings();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &RulerGuideSettingsPage::applyStyleSheet);
    applyStyleSheet();
}

QWidget* RulerGuideSettingsPage::makeOpacityRow()
{
    auto* row = new QWidget(this);
    auto* lay = new QHBoxLayout(row);
    lay->setContentsMargins(0, 0, 0, 0);
    m_guideOpacitySlider = new QSlider(Qt::Horizontal, row);
    m_guideOpacitySlider->setRange(5, 100);
    m_guideOpacitySlider->setFixedWidth(160);
    lay->addWidget(m_guideOpacitySlider);
    lay->addStretch();
    return row;
}

void RulerGuideSettingsPage::applyStyleSheet()
{
    auto* th = ThemeManager::instance()->current();
    const QString surface = th->colorSurface.name();
    const QString text = th->colorTextPrimary.name();

    setStyleSheet(th->globalStyleSheet() + QStringLiteral(
        "QWidget#rulerGuideSettingsPage,"
        "QScrollArea#rulerGuideScroll,"
        "QWidget#rulerGuideContent { background: %1; color: %2; }"
        "QScrollArea#rulerGuideScroll { border: none; }")
        .arg(surface, text));

    applySurfacePalette(this, th);
    if (auto* scroll = findChild<QScrollArea*>(QStringLiteral("rulerGuideScroll"))) {
        applySurfacePalette(scroll, th);
        applySurfacePalette(scroll->viewport(), th);
    }
    if (auto* content = findChild<QWidget*>(QStringLiteral("rulerGuideContent")))
        applySurfacePalette(content, th);
}

void RulerGuideSettingsPage::updateColorButton()
{
    if (!m_guideColorBtn)
        return;
    m_guideColorBtn->setText(m_guideColor.name(QColor::HexRgb).toUpper());
    m_guideColorBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; }")
        .arg(m_guideColor.name(QColor::HexRgb),
             m_guideColor.lightness() > 128 ? QStringLiteral("#111315") : QStringLiteral("#FFFFFF")));
}

void RulerGuideSettingsPage::loadSettings()
{
    QSettings qs;
    const int savedOpacity = qs.contains(QStringLiteral("iconOpacity"))
                                 ? qs.value(QStringLiteral("iconOpacity")).toInt() : -1;
    auto* th = ThemeManager::instance()->current();
    const int opacityVal = savedOpacity >= 0 ? savedOpacity : static_cast<int>(th->iconOpacity * 100.0f);
    m_iconOpacitySlider->setValue(opacityVal);
    m_opacityLabel->setText(QStringLiteral("%1%").arg(opacityVal));
    th->iconOpacity = opacityVal / 100.0f;

    m_showColorPaletteCb->setChecked(qs.value(QStringLiteral("View/colorPaletteVisible"), true).toBool());

    const RulerGuideSettings settings = RulerGuideSettings::load();
    m_showRulersCb->setChecked(settings.rulers.showRulers);
    const QString unitValue = rulerUnitToSettingsValue(settings.rulers.unit);
    const int unitIndex = m_unitCombo->findData(unitValue);
    m_unitCombo->setCurrentIndex(unitIndex >= 0 ? unitIndex : 0);
    m_majorTickSpin->setValue(static_cast<int>(std::round(settings.rulers.majorTickSpacing)));
    m_minorDivSpin->setValue(settings.rulers.minorTickDivisions);
    m_rulerSizeSpin->setValue(settings.rulers.rulerSize);
    m_mouseIndicatorCb->setChecked(settings.rulers.showMouseIndicator);

    m_showGuidesCb->setChecked(settings.guides.showGuides);
    m_lockGuidesCb->setChecked(settings.guides.lockGuides);
    m_guidesSnapCb->setChecked(settings.guides.snapToGuides);
    m_guideColor = settings.guides.guideColor;
    m_guideOpacitySlider->setValue(static_cast<int>(settings.guides.guideOpacity * 100.0));
    updateColorButton();

    m_snapEnabledCb->setChecked(settings.snap.enabled);
    m_snapCanvasBoundsCb->setChecked(settings.snap.snapToCanvasBounds);
    m_snapCanvasCenterCb->setChecked(settings.snap.snapToCanvasCenter);
    m_snapGuidesCb->setChecked(settings.snap.snapToGuides);
    m_snapToleranceSpin->setValue(static_cast<int>(std::round(settings.snap.toleranceScreenPx)));
    m_snapIndicatorsCb->setChecked(settings.snap.showIndicators);
}

void RulerGuideSettingsPage::saveSettings()
{
    QSettings qs;
    qs.setValue(QStringLiteral("iconOpacity"), static_cast<qlonglong>(m_iconOpacitySlider->value()));
    qs.setValue(QStringLiteral("View/colorPaletteVisible"), m_showColorPaletteCb->isChecked());

    RulerGuideSettings settings;
    settings.rulers.showRulers = m_showRulersCb->isChecked();
    settings.rulers.unit = rulerUnitFromSettingsValue(m_unitCombo->currentData().toString());
    settings.rulers.majorTickSpacing = m_majorTickSpin->value();
    settings.rulers.minorTickDivisions = m_minorDivSpin->value();
    settings.rulers.rulerSize = m_rulerSizeSpin->value();
    settings.rulers.showMouseIndicator = m_mouseIndicatorCb->isChecked();

    settings.guides.showGuides = m_showGuidesCb->isChecked();
    settings.guides.lockGuides = m_lockGuidesCb->isChecked();
    settings.guides.snapToGuides = m_guidesSnapCb->isChecked();
    settings.guides.guideColor = m_guideColor;
    settings.guides.guideOpacity = m_guideOpacitySlider->value() / 100.0;

    settings.snap.enabled = m_snapEnabledCb->isChecked();
    settings.snap.snapToCanvasBounds = m_snapCanvasBoundsCb->isChecked();
    settings.snap.snapToCanvasCenter = m_snapCanvasCenterCb->isChecked();
    settings.snap.snapToGuides = m_snapGuidesCb->isChecked();
    settings.snap.toleranceScreenPx = m_snapToleranceSpin->value();
    settings.snap.showIndicators = m_snapIndicatorsCb->isChecked();

    RulerGuideSettings::save(settings);
}
