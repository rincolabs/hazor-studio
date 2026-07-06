#include "ColorPickerDialog.hpp"
#include "SaturationBrightnessArea.hpp"
#include "ColorHueSlider.hpp"
#include "ColorPreviewWidget.hpp"
#include "ColorFieldsWidget.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>

ColorPickerDialog::ColorPickerDialog(const QColor& initialColor,
                                     ColorPickerMode mode,
                                     QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_initialColor(initialColor)
    , m_selectedColor(initialColor)
{
    QString modeText = (mode == ColorPickerMode::Foreground)
        ? tr("Foreground Color") : tr("Background Color");
    setWindowTitle(tr("Color Picker (%1)").arg(modeText));
    setMinimumSize(560, 400);

    auto* t = ThemeManager::instance()->current();

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(t->spaceLG, t->spaceLG, t->spaceLG, t->spaceLG);
    mainLayout->setSpacing(t->spaceMD);

    auto* topLayout = new QHBoxLayout;
    topLayout->setSpacing(t->spaceMD);

    // Left: saturation/value area
    m_satBrightArea = new SaturationBrightnessArea(this);
    m_satBrightArea->setMinimumSize(200, 200);
    topLayout->addWidget(m_satBrightArea, 1);

    // Vertical hue slider
    m_hueSlider = new ColorHueSlider(this);
    m_hueSlider->setMinimumSize(24, 200);
    m_hueSlider->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    topLayout->addWidget(m_hueSlider, 0);

    // Right column: preview + action buttons on top, numeric fields below.
    auto* rightLayout = new QVBoxLayout;
    rightLayout->setSpacing(t->spaceMD);

    auto* previewButtonsRow = new QHBoxLayout;
    previewButtonsRow->setSpacing(t->spaceMD);

    m_previewWidget = new ColorPreviewWidget(this);
    m_previewWidget->setCurrentColor(m_initialColor);
    m_previewWidget->setNewColor(m_selectedColor);
    previewButtonsRow->addWidget(m_previewWidget, 0, Qt::AlignTop);

    auto* buttonCol = new QVBoxLayout;
    buttonCol->setSpacing(t->spaceSM);

    auto* okBtn = new QPushButton(tr("OK"), this);
    okBtn->setObjectName("okBtn");
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton(tr("Cancel"), this);
    cancelBtn->setObjectName("cancelBtn");
    auto* swatchesBtn = new QPushButton(tr("Add to Swatches"), this);
    auto* librariesBtn = new QPushButton(tr("Color Libraries"), this);

    buttonCol->addWidget(okBtn);
    buttonCol->addWidget(cancelBtn);
    buttonCol->addWidget(swatchesBtn);
    buttonCol->addWidget(librariesBtn);
    buttonCol->addStretch();
    previewButtonsRow->addLayout(buttonCol);

    rightLayout->addLayout(previewButtonsRow);

    m_fieldsWidget = new ColorFieldsWidget(this);
    rightLayout->addWidget(m_fieldsWidget);
    rightLayout->addStretch();

    topLayout->addLayout(rightLayout, 0);

    mainLayout->addLayout(topLayout, 1);

    setStyleSheet(t->globalStyleSheet());
    setAttribute(Qt::WA_StyledBackground);

    connect(m_satBrightArea, &SaturationBrightnessArea::colorChanged,
            this, &ColorPickerDialog::onVisualColorChanged);
    connect(m_hueSlider, &ColorHueSlider::hueChanged,
            this, &ColorPickerDialog::onHueSliderChanged);
    connect(m_fieldsWidget, &ColorFieldsWidget::colorChanged,
            this, &ColorPickerDialog::onFieldsColorChanged);

    connect(okBtn, &QPushButton::clicked, this, [this]() {
        emit colorAccepted(m_selectedColor);
        accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, [this]() {
        emit colorPreviewChanged(m_initialColor);
        reject();
    });
    connect(swatchesBtn, &QPushButton::clicked, this, [this]() {
        emit swatchAddRequested(m_selectedColor);
    });
    connect(librariesBtn, &QPushButton::clicked, this, [this]() {
        emit colorLibrariesRequested();
    });

    syncAllWidgets();
}

void ColorPickerDialog::onVisualColorChanged(const QColor& color)
{
    if (m_updatingUI) return;
    m_selectedColor = color;
    emit colorPreviewChanged(m_selectedColor);
    syncAllWidgets();
}

void ColorPickerDialog::onHueSliderChanged(double hue)
{
    if (m_updatingUI) return;
    double s = m_selectedColor.saturationF();
    double v = m_selectedColor.valueF();
    if (s < 0.0) s = 1.0;
    if (v < 0.0) v = 1.0;
    m_selectedColor = QColor::fromHsvF(hue / 360.0, s, v);
    emit colorPreviewChanged(m_selectedColor);
    syncAllWidgets();
}

void ColorPickerDialog::onFieldsColorChanged(const QColor& color)
{
    if (m_updatingUI) return;
    m_selectedColor = color;
    emit colorPreviewChanged(m_selectedColor);
    syncAllWidgets();
}

void ColorPickerDialog::syncAllWidgets()
{
    m_updatingUI = true;

    m_satBrightArea->setColor(m_selectedColor);
    double h = m_selectedColor.hueF();
    if (h < 0.0) h = 0.0;
    m_hueSlider->setHue(h * 360.0);
    m_previewWidget->setNewColor(m_selectedColor);
    m_fieldsWidget->setColor(m_selectedColor);

    m_updatingUI = false;
}
