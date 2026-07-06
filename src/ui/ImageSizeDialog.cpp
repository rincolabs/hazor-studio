#include "ImageSizeDialog.hpp"

#include "ui/AppCheckBox.hpp"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

ImageSizeDialog::ImageSizeDialog(const QSize& currentSize,
                                 double currentResolutionDpi,
                                 QWidget* parent)
    : QDialog(parent)
    , m_originalSize(currentSize)
    , m_originalResolutionDpi(std::max(1.0, currentResolutionDpi))
    , m_targetWidthPx(std::max(1, currentSize.width()))
    , m_targetHeightPx(std::max(1, currentSize.height()))
{
    setWindowTitle(tr("Image Size"));
    setModal(true);
    resize(460, 420);

    auto* root = new QVBoxLayout(this);

    auto* pixelGroup = new QGroupBox(tr("Pixel Dimensions"), this);
    auto* pixelLayout = new QGridLayout(pixelGroup);

    m_pixelWidthSpin = new QDoubleSpinBox(this);
    m_pixelWidthSpin->setRange(1.0, 200000.0);
    m_pixelHeightSpin = new QDoubleSpinBox(this);
    m_pixelHeightSpin->setRange(1.0, 200000.0);

    m_pixelUnitCombo = new QComboBox(this);
    m_pixelUnitCombo->addItem(tr("Pixels"));
    m_pixelUnitCombo->addItem(tr("Percent"));

    pixelLayout->addWidget(new QLabel(tr("Width:"), this), 0, 0);
    pixelLayout->addWidget(m_pixelWidthSpin, 0, 1);
    pixelLayout->addWidget(m_pixelUnitCombo, 0, 2);
    pixelLayout->addWidget(new QLabel(tr("Height:"), this), 1, 0);
    pixelLayout->addWidget(m_pixelHeightSpin, 1, 1);

    auto* docGroup = new QGroupBox(tr("Document Size"), this);
    auto* docLayout = new QGridLayout(docGroup);

    m_docWidthSpin = new QDoubleSpinBox(this);
    m_docWidthSpin->setRange(0.001, 100000.0);
    m_docWidthSpin->setDecimals(4);
    m_docHeightSpin = new QDoubleSpinBox(this);
    m_docHeightSpin->setRange(0.001, 100000.0);
    m_docHeightSpin->setDecimals(4);

    m_docUnitCombo = new QComboBox(this);
    m_docUnitCombo->addItem(tr("Inches"));
    m_docUnitCombo->addItem(tr("Centimeters"));
    m_docUnitCombo->addItem(tr("Millimeters"));
    m_docUnitCombo->addItem(tr("Points"));
    m_docUnitCombo->addItem(tr("Picas"));

    m_resolutionSpin = new QDoubleSpinBox(this);
    m_resolutionSpin->setRange(1.0, 9600.0);
    m_resolutionSpin->setDecimals(3);
    m_resolutionUnitCombo = new QComboBox(this);
    m_resolutionUnitCombo->addItem(tr("Pixels/Inch"));
    m_resolutionUnitCombo->addItem(tr("Pixels/Centimeter"));

    docLayout->addWidget(new QLabel(tr("Width:"), this), 0, 0);
    docLayout->addWidget(m_docWidthSpin, 0, 1);
    docLayout->addWidget(m_docUnitCombo, 0, 2);
    docLayout->addWidget(new QLabel(tr("Height:"), this), 1, 0);
    docLayout->addWidget(m_docHeightSpin, 1, 1);
    docLayout->addWidget(new QLabel(tr("Resolution:"), this), 2, 0);
    docLayout->addWidget(m_resolutionSpin, 2, 1);
    docLayout->addWidget(m_resolutionUnitCombo, 2, 2);

    m_scaleStylesCheck = new AppCheckBox(tr("Scale Styles"), this);
    m_scaleStylesCheck->setChecked(true);

    m_constrainCheck = new AppCheckBox(tr("Constrain Proportions"), this);
    m_constrainCheck->setChecked(true);

    m_resampleCheck = new AppCheckBox(tr("Resample Image"), this);
    m_resampleCheck->setChecked(true);

    auto* interpolationRow = new QHBoxLayout;
    interpolationRow->addWidget(new QLabel(tr("Interpolation:"), this));
    m_interpolationCombo = new QComboBox(this);
    m_interpolationCombo->addItem(tr("Nearest"), static_cast<int>(ResizeInterpolation::Nearest));
    m_interpolationCombo->addItem(tr("Bilinear"), static_cast<int>(ResizeInterpolation::Bilinear));
    m_interpolationCombo->addItem(tr("Bicubic"), static_cast<int>(ResizeInterpolation::Bicubic));
    m_interpolationCombo->addItem(tr("Bicubic Automatic"), static_cast<int>(ResizeInterpolation::BicubicAutomatic));
    m_interpolationCombo->addItem(tr("Lanczos"), static_cast<int>(ResizeInterpolation::Lanczos));
    m_interpolationCombo->setCurrentIndex(3);
    interpolationRow->addWidget(m_interpolationCombo, 1);

    m_buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);

    root->addWidget(pixelGroup);
    root->addWidget(docGroup);
    root->addWidget(m_scaleStylesCheck);
    root->addWidget(m_constrainCheck);
    root->addWidget(m_resampleCheck);
    root->addLayout(interpolationRow);
    root->addStretch();
    root->addWidget(m_buttons);

    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    connect(m_pixelWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ImageSizeDialog::onPixelWidthChanged);
    connect(m_pixelHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ImageSizeDialog::onPixelHeightChanged);
    connect(m_pixelUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageSizeDialog::onPixelUnitChanged);
    connect(m_docWidthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ImageSizeDialog::onDocWidthChanged);
    connect(m_docHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ImageSizeDialog::onDocHeightChanged);
    connect(m_docUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageSizeDialog::onDocUnitChanged);
    connect(m_resolutionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ImageSizeDialog::onResolutionChanged);
    connect(m_resolutionUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ImageSizeDialog::onResolutionUnitChanged);
    connect(m_resampleCheck, &QCheckBox::toggled,
            this, &ImageSizeDialog::onResampleToggled);

    m_resolutionSpin->setValue(m_originalResolutionDpi);
    updatePixelSpinPresentation();
    updatePixelSpinsFromTarget();
    updateDocumentFieldsFromTarget();
}

ImageSizeSettings ImageSizeDialog::settings() const
{
    ImageSizeSettings out;
    out.pixelSize = QSize(std::max(1, m_targetWidthPx), std::max(1, m_targetHeightPx));
    out.resolutionDpi = currentResolutionDpi();
    out.scaleStyles = m_scaleStylesCheck->isChecked();
    out.constrainProportions = m_constrainCheck->isChecked();
    out.resampleImage = m_resampleCheck->isChecked();
    out.interpolation = static_cast<ResizeInterpolation>(
        m_interpolationCombo->currentData().toInt());
    return out;
}

void ImageSizeDialog::onPixelWidthChanged(double value)
{
    if (m_updating || !m_resampleCheck->isChecked())
        return;

    const bool percent = (m_pixelUnitCombo->currentIndex() == 1);
    if (percent) {
        const double pct = std::max(1.0, value);
        m_targetWidthPx = std::max(1, static_cast<int>(std::round(m_originalSize.width() * pct / 100.0)));
    } else {
        m_targetWidthPx = std::max(1, static_cast<int>(std::round(value)));
    }

    if (m_constrainCheck->isChecked() && m_originalSize.width() > 0) {
        const double ratio = static_cast<double>(m_originalSize.height())
                           / static_cast<double>(m_originalSize.width());
        m_targetHeightPx = std::max(1, static_cast<int>(std::round(m_targetWidthPx * ratio)));
    }

    updatePixelSpinsFromTarget();
    updateDocumentFieldsFromTarget();
}

void ImageSizeDialog::onPixelHeightChanged(double value)
{
    if (m_updating || !m_resampleCheck->isChecked())
        return;

    const bool percent = (m_pixelUnitCombo->currentIndex() == 1);
    if (percent) {
        const double pct = std::max(1.0, value);
        m_targetHeightPx = std::max(1, static_cast<int>(std::round(m_originalSize.height() * pct / 100.0)));
    } else {
        m_targetHeightPx = std::max(1, static_cast<int>(std::round(value)));
    }

    if (m_constrainCheck->isChecked() && m_originalSize.height() > 0) {
        const double ratio = static_cast<double>(m_originalSize.width())
                           / static_cast<double>(m_originalSize.height());
        m_targetWidthPx = std::max(1, static_cast<int>(std::round(m_targetHeightPx * ratio)));
    }

    updatePixelSpinsFromTarget();
    updateDocumentFieldsFromTarget();
}

void ImageSizeDialog::onPixelUnitChanged(int)
{
    updatePixelSpinPresentation();
    updatePixelSpinsFromTarget();
}

void ImageSizeDialog::onDocWidthChanged(double value)
{
    if (m_updating)
        return;
    updateTargetFromDocument(value, true);
}

void ImageSizeDialog::onDocHeightChanged(double value)
{
    if (m_updating)
        return;
    updateTargetFromDocument(value, false);
}

void ImageSizeDialog::onDocUnitChanged(int)
{
    updateDocumentFieldsFromTarget();
}

void ImageSizeDialog::onResolutionChanged(double)
{
    if (m_updating)
        return;
    updateDocumentFieldsFromTarget();
}

void ImageSizeDialog::onResolutionUnitChanged(int)
{
    if (m_updating)
        return;
    updateDocumentFieldsFromTarget();
}

void ImageSizeDialog::onResampleToggled(bool checked)
{
    m_pixelWidthSpin->setEnabled(checked);
    m_pixelHeightSpin->setEnabled(checked);
    m_pixelUnitCombo->setEnabled(checked);
    m_interpolationCombo->setEnabled(checked);
    m_scaleStylesCheck->setEnabled(checked);

    if (!checked) {
        m_targetWidthPx = std::max(1, m_originalSize.width());
        m_targetHeightPx = std::max(1, m_originalSize.height());
    }

    updatePixelSpinsFromTarget();
    updateDocumentFieldsFromTarget();
}

double ImageSizeDialog::unitToInchesFactor(DocUnit unit) const
{
    switch (unit) {
    case DocUnit::Inches:
        return 1.0;
    case DocUnit::Centimeters:
        return 1.0 / 2.54;
    case DocUnit::Millimeters:
        return 1.0 / 25.4;
    case DocUnit::Points:
        return 1.0 / 72.0;
    case DocUnit::Picas:
        return 1.0 / 6.0;
    }
    return 1.0;
}

double ImageSizeDialog::currentResolutionDpi() const
{
    const double value = std::max(1.0, m_resolutionSpin->value());
    if (m_resolutionUnitCombo->currentIndex() == 1)
        return value * 2.54;
    return value;
}

void ImageSizeDialog::updatePixelSpinPresentation()
{
    m_updating = true;
    if (m_pixelUnitCombo->currentIndex() == 1) {
        m_pixelWidthSpin->setDecimals(2);
        m_pixelHeightSpin->setDecimals(2);
        m_pixelWidthSpin->setRange(1.0, 10000.0);
        m_pixelHeightSpin->setRange(1.0, 10000.0);
    } else {
        m_pixelWidthSpin->setDecimals(0);
        m_pixelHeightSpin->setDecimals(0);
        m_pixelWidthSpin->setRange(1.0, 200000.0);
        m_pixelHeightSpin->setRange(1.0, 200000.0);
    }
    m_updating = false;
}

void ImageSizeDialog::updatePixelSpinsFromTarget()
{
    m_updating = true;
    if (m_pixelUnitCombo->currentIndex() == 1) {
        const double wPct = 100.0 * static_cast<double>(m_targetWidthPx)
                          / std::max(1, m_originalSize.width());
        const double hPct = 100.0 * static_cast<double>(m_targetHeightPx)
                          / std::max(1, m_originalSize.height());
        m_pixelWidthSpin->setValue(wPct);
        m_pixelHeightSpin->setValue(hPct);
    } else {
        m_pixelWidthSpin->setValue(m_targetWidthPx);
        m_pixelHeightSpin->setValue(m_targetHeightPx);
    }
    m_updating = false;
}

void ImageSizeDialog::updateDocumentFieldsFromTarget()
{
    m_updating = true;
    const double dpi = currentResolutionDpi();
    const double unitFactor = unitToInchesFactor(static_cast<DocUnit>(m_docUnitCombo->currentIndex()));

    const double widthInches = static_cast<double>(m_targetWidthPx) / std::max(1.0, dpi);
    const double heightInches = static_cast<double>(m_targetHeightPx) / std::max(1.0, dpi);

    m_docWidthSpin->setValue(widthInches / unitFactor);
    m_docHeightSpin->setValue(heightInches / unitFactor);
    m_updating = false;
}

void ImageSizeDialog::updateTargetFromDocument(double documentValue, bool widthChanged)
{
    const double dpi = currentResolutionDpi();
    const double unitFactor = unitToInchesFactor(static_cast<DocUnit>(m_docUnitCombo->currentIndex()));
    const double inches = std::max(0.0001, documentValue) * unitFactor;
    const int pixels = std::max(1, static_cast<int>(std::round(inches * dpi)));

    if (!m_resampleCheck->isChecked()) {
        const double desiredInches = std::max(0.0001, inches);
        const double fixedPixels = widthChanged
            ? static_cast<double>(m_targetWidthPx)
            : static_cast<double>(m_targetHeightPx);
        const double newDpi = std::max(1.0, fixedPixels / desiredInches);

        m_updating = true;
        if (m_resolutionUnitCombo->currentIndex() == 1)
            m_resolutionSpin->setValue(newDpi / 2.54);
        else
            m_resolutionSpin->setValue(newDpi);
        m_updating = false;

        updateDocumentFieldsFromTarget();
        return;
    }

    if (widthChanged)
        m_targetWidthPx = pixels;
    else
        m_targetHeightPx = pixels;

    if (m_constrainCheck->isChecked()) {
        if (widthChanged && m_originalSize.width() > 0) {
            const double ratio = static_cast<double>(m_originalSize.height())
                               / static_cast<double>(m_originalSize.width());
            m_targetHeightPx = std::max(1, static_cast<int>(std::round(m_targetWidthPx * ratio)));
        } else if (!widthChanged && m_originalSize.height() > 0) {
            const double ratio = static_cast<double>(m_originalSize.width())
                               / static_cast<double>(m_originalSize.height());
            m_targetWidthPx = std::max(1, static_cast<int>(std::round(m_targetHeightPx * ratio)));
        }
    }

    updatePixelSpinsFromTarget();
    updateDocumentFieldsFromTarget();
}
