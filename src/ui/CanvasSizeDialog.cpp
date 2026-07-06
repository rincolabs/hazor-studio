#include "CanvasSizeDialog.hpp"
#include "colorpicker/ColorPickerDialog.hpp"

#include <QButtonGroup>
#include "ui/AppCheckBox.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

CanvasSizeDialog::CanvasSizeDialog(const QSize& currentSize,
                                   double resolutionDpi,
                                   const QColor& backgroundColor,
                                   QWidget* parent)
    : QDialog(parent)
    , m_currentSize(currentSize)
    , m_resolutionDpi(std::max(1.0, resolutionDpi))
    , m_backgroundColor(backgroundColor)
    , m_customColor(backgroundColor)
{
    setWindowTitle(tr("Canvas Size"));
    setModal(true);
    resize(420, 420);

    auto* root = new QVBoxLayout(this);

    auto* currentGroup = new QGroupBox(tr("Current Size"), this);
    auto* currentLayout = new QGridLayout(currentGroup);
    m_currentWidthLabel = new QLabel(tr("%1 px").arg(m_currentSize.width()), this);
    m_currentHeightLabel = new QLabel(tr("%1 px").arg(m_currentSize.height()), this);
    currentLayout->addWidget(new QLabel(tr("Width:"), this), 0, 0);
    currentLayout->addWidget(m_currentWidthLabel, 0, 1);
    currentLayout->addWidget(new QLabel(tr("Height:"), this), 1, 0);
    currentLayout->addWidget(m_currentHeightLabel, 1, 1);

    auto* newGroup = new QGroupBox(tr("New Size"), this);
    auto* newLayout = new QGridLayout(newGroup);

    m_widthSpin = new QDoubleSpinBox(this);
    m_heightSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(1.0, 200000.0);
    m_heightSpin->setRange(1.0, 200000.0);
    m_widthSpin->setValue(m_currentSize.width());
    m_heightSpin->setValue(m_currentSize.height());

    m_unitCombo = new QComboBox(this);
    m_unitCombo->addItem(tr("Pixels"));
    m_unitCombo->addItem(tr("Percent"));
    m_unitCombo->addItem(tr("Inches"));
    m_unitCombo->addItem(tr("Centimeters"));
    m_unitCombo->addItem(tr("Millimeters"));
    m_unitCombo->addItem(tr("Points"));
    m_unitCombo->addItem(tr("Picas"));

    m_relativeCheck = new AppCheckBox(tr("Relative"), this);

    newLayout->addWidget(new QLabel(tr("Width:"), this), 0, 0);
    newLayout->addWidget(m_widthSpin, 0, 1);
    newLayout->addWidget(m_unitCombo, 0, 2);
    newLayout->addWidget(new QLabel(tr("Height:"), this), 1, 0);
    newLayout->addWidget(m_heightSpin, 1, 1);
    newLayout->addWidget(m_relativeCheck, 2, 0, 1, 2);

    auto* anchorGroup = new QGroupBox(tr("Anchor"), this);
    auto* anchorLayout = new QGridLayout(anchorGroup);
    m_anchorButtons = new QButtonGroup(this);
    m_anchorButtons->setExclusive(true);

    struct AnchorEntry {
        const char* label;
        CanvasAnchor anchor;
        int row;
        int col;
    };

    const AnchorEntry anchors[] = {
        {"TL", CanvasAnchor::TopLeft, 0, 0},
        {"TC", CanvasAnchor::TopCenter, 0, 1},
        {"TR", CanvasAnchor::TopRight, 0, 2},
        {"ML", CanvasAnchor::MiddleLeft, 1, 0},
        {"C",  CanvasAnchor::Center, 1, 1},
        {"MR", CanvasAnchor::MiddleRight, 1, 2},
        {"BL", CanvasAnchor::BottomLeft, 2, 0},
        {"BC", CanvasAnchor::BottomCenter, 2, 1},
        {"BR", CanvasAnchor::BottomRight, 2, 2},
    };

    for (const AnchorEntry& entry : anchors) {
        auto* btn = new QPushButton(QString::fromLatin1(entry.label), this);
        btn->setCheckable(true);
        btn->setMinimumWidth(40);
        anchorLayout->addWidget(btn, entry.row, entry.col);
        m_anchorButtons->addButton(btn, static_cast<int>(entry.anchor));
    }
    if (auto* center = m_anchorButtons->button(static_cast<int>(CanvasAnchor::Center)))
        center->setChecked(true);

    auto* extensionGroup = new QGroupBox(tr("Canvas Extension Color"), this);
    auto* extensionLayout = new QHBoxLayout(extensionGroup);
    m_extensionModeCombo = new QComboBox(this);
    m_extensionModeCombo->addItem(tr("Transparent"));
    m_extensionModeCombo->addItem(tr("Background"));
    m_extensionModeCombo->addItem(tr("Custom"));
    m_colorButton = new QPushButton(tr("Pick Color"), this);
    m_colorButton->setEnabled(false);
    extensionLayout->addWidget(m_extensionModeCombo);
    extensionLayout->addWidget(m_colorButton);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    root->addWidget(currentGroup);
    root->addWidget(newGroup);
    root->addWidget(anchorGroup);
    root->addWidget(extensionGroup);
    root->addStretch();
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_unitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanvasSizeDialog::onUnitChanged);
    connect(m_relativeCheck, &QCheckBox::toggled,
            this, &CanvasSizeDialog::onRelativeToggled);
    connect(m_extensionModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CanvasSizeDialog::onExtensionModeChanged);
    connect(m_colorButton, &QPushButton::clicked,
            this, &CanvasSizeDialog::pickCustomColor);

    refreshSpinPresentation();
}

CanvasSizeSettings CanvasSizeDialog::settings() const
{
    CanvasSizeSettings out;
    const SizeUnit unit = static_cast<SizeUnit>(m_unitCombo->currentIndex());

    int widthPx = static_cast<int>(std::round(unitToPixels(
        m_widthSpin->value(), unit, m_currentSize.width())));
    int heightPx = static_cast<int>(std::round(unitToPixels(
        m_heightSpin->value(), unit, m_currentSize.height())));

    if (m_relativeCheck->isChecked()) {
        widthPx = m_currentSize.width() + widthPx;
        heightPx = m_currentSize.height() + heightPx;
    }

    out.targetSize = QSize(std::max(1, widthPx), std::max(1, heightPx));
    out.anchor = selectedAnchor();

    const int mode = m_extensionModeCombo->currentIndex();
    out.fillExtension = (mode != 0);
    if (mode == 1)
        out.extensionColor = m_backgroundColor;
    else if (mode == 2)
        out.extensionColor = m_customColor;
    else
        out.extensionColor = Qt::transparent;

    return out;
}

void CanvasSizeDialog::onUnitChanged(int)
{
    refreshSpinPresentation();

    const SizeUnit unit = static_cast<SizeUnit>(m_unitCombo->currentIndex());
    if (m_relativeCheck->isChecked()) {
        m_widthSpin->setValue(0.0);
        m_heightSpin->setValue(0.0);
        return;
    }

    m_widthSpin->setValue(pixelsToUnit(m_currentSize.width(), unit, m_currentSize.width()));
    m_heightSpin->setValue(pixelsToUnit(m_currentSize.height(), unit, m_currentSize.height()));
}

void CanvasSizeDialog::onRelativeToggled(bool)
{
    refreshSpinPresentation();

    const SizeUnit unit = static_cast<SizeUnit>(m_unitCombo->currentIndex());
    if (m_relativeCheck->isChecked()) {
        m_widthSpin->setValue(0.0);
        m_heightSpin->setValue(0.0);
        return;
    }

    m_widthSpin->setValue(pixelsToUnit(m_currentSize.width(), unit, m_currentSize.width()));
    m_heightSpin->setValue(pixelsToUnit(m_currentSize.height(), unit, m_currentSize.height()));
}

void CanvasSizeDialog::onExtensionModeChanged(int index)
{
    m_colorButton->setEnabled(index == 2);
}

void CanvasSizeDialog::pickCustomColor()
{
    ColorPickerDialog dlg(m_customColor, ColorPickerMode::Foreground, this);
    if (dlg.exec() == QDialog::Accepted) {
        QColor chosen = dlg.selectedColor();
        if (chosen.isValid())
            m_customColor = chosen;
    }
}

double CanvasSizeDialog::unitToPixels(double value, SizeUnit unit, int basePixels) const
{
    switch (unit) {
    case SizeUnit::Pixels:
        return value;
    case SizeUnit::Percent:
        return static_cast<double>(basePixels) * value / 100.0;
    case SizeUnit::Inches:
        return value * m_resolutionDpi;
    case SizeUnit::Centimeters:
        return value * (m_resolutionDpi / 2.54);
    case SizeUnit::Millimeters:
        return value * (m_resolutionDpi / 25.4);
    case SizeUnit::Points:
        return value * (m_resolutionDpi / 72.0);
    case SizeUnit::Picas:
        return value * (m_resolutionDpi / 6.0);
    }
    return value;
}

double CanvasSizeDialog::pixelsToUnit(double pixels, SizeUnit unit, int basePixels) const
{
    switch (unit) {
    case SizeUnit::Pixels:
        return pixels;
    case SizeUnit::Percent:
        return 100.0 * pixels / std::max(1, basePixels);
    case SizeUnit::Inches:
        return pixels / m_resolutionDpi;
    case SizeUnit::Centimeters:
        return pixels / (m_resolutionDpi / 2.54);
    case SizeUnit::Millimeters:
        return pixels / (m_resolutionDpi / 25.4);
    case SizeUnit::Points:
        return pixels / (m_resolutionDpi / 72.0);
    case SizeUnit::Picas:
        return pixels / (m_resolutionDpi / 6.0);
    }
    return pixels;
}

void CanvasSizeDialog::refreshSpinPresentation()
{
    const SizeUnit unit = static_cast<SizeUnit>(m_unitCombo->currentIndex());
    const bool relative = m_relativeCheck->isChecked();

    if (unit == SizeUnit::Pixels) {
        m_widthSpin->setDecimals(0);
        m_heightSpin->setDecimals(0);
    } else if (unit == SizeUnit::Percent) {
        m_widthSpin->setDecimals(2);
        m_heightSpin->setDecimals(2);
    } else {
        m_widthSpin->setDecimals(4);
        m_heightSpin->setDecimals(4);
    }

    if (relative) {
        m_widthSpin->setRange(-200000.0, 200000.0);
        m_heightSpin->setRange(-200000.0, 200000.0);
    } else {
        m_widthSpin->setRange(1.0, 200000.0);
        m_heightSpin->setRange(1.0, 200000.0);
    }
}

CanvasAnchor CanvasSizeDialog::selectedAnchor() const
{
    const int id = m_anchorButtons->checkedId();
    switch (id) {
    case static_cast<int>(CanvasAnchor::TopLeft):
        return CanvasAnchor::TopLeft;
    case static_cast<int>(CanvasAnchor::TopCenter):
        return CanvasAnchor::TopCenter;
    case static_cast<int>(CanvasAnchor::TopRight):
        return CanvasAnchor::TopRight;
    case static_cast<int>(CanvasAnchor::MiddleLeft):
        return CanvasAnchor::MiddleLeft;
    case static_cast<int>(CanvasAnchor::MiddleRight):
        return CanvasAnchor::MiddleRight;
    case static_cast<int>(CanvasAnchor::BottomLeft):
        return CanvasAnchor::BottomLeft;
    case static_cast<int>(CanvasAnchor::BottomCenter):
        return CanvasAnchor::BottomCenter;
    case static_cast<int>(CanvasAnchor::BottomRight):
        return CanvasAnchor::BottomRight;
    default:
        return CanvasAnchor::Center;
    }
}
