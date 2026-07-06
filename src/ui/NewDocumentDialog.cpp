#include "NewDocumentDialog.hpp"
#include "color/ColorProfileRepository.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFrame>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QPushButton>
#include "colorpicker/ColorPickerDialog.hpp"

NewDocumentDialog::NewDocumentDialog(QWidget* parent)
    : QDialog(parent)
{
    setUpdatesEnabled(false);

    setWindowTitle(tr("New | Edit"));
    setMinimumWidth(600);
    setMinimumHeight(480);

    auto* t = ThemeManager::instance()->current();
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t->spaceLG);
    mainLayout->setContentsMargins(t->spaceXL, t->spaceLG, t->spaceXL, t->spaceLG);

    // ── Title ──
    auto* title = new QLabel(tr("<h2>New | Edit</h2>"), this);
    title->setTextFormat(Qt::RichText);
    mainLayout->addWidget(title);

    // ── Body ──
    auto* bodyLayout = new QHBoxLayout;
    bodyLayout->setSpacing(t->spaceXXL);

    bodyLayout->addWidget(createFormSection(), 1);
    bodyLayout->addWidget(createButtonSection());

    mainLayout->addLayout(bodyLayout, 1);

    // ── Footer ──
    auto* footerLayout = new QHBoxLayout;
    footerLayout->addStretch();
    m_imageSizeLabel = new QLabel(this);
    m_imageSizeLabel->setObjectName("footerLabel");
    footerLayout->addWidget(m_imageSizeLabel);
    mainLayout->addLayout(footerLayout);

    setupPresets();
    updateImageSizeDisplay();

    connect(m_sizePresetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onSizePresetChanged);
    connect(m_widthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &NewDocumentDialog::onWidthChanged);
    connect(m_heightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &NewDocumentDialog::onHeightChanged);
    connect(m_widthUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onWidthUnitChanged);
    connect(m_heightUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onHeightUnitChanged);
    connect(m_resolutionSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &NewDocumentDialog::onResolutionChanged);
    connect(m_resolutionUnitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onResolutionUnitChanged);
    connect(m_backgroundCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &NewDocumentDialog::onBackgroundChanged);
    connect(m_bgColorBtn, &QPushButton::clicked,
            this, &NewDocumentDialog::pickBackgroundColor);

    setUpdatesEnabled(true);
}

void NewDocumentDialog::setupPresets()
{
    m_presets = {
        {"Landscape, 8 x 10 in",   10.0,   8.0,  "in"},
        {"Landscape, 16 x 20 in",  20.0,  16.0,  "in"},
        {"Portrait, 10 x 8 in",     8.0,  10.0,  "in"},
        {"Square, 12 x 12 in",     12.0,  12.0,  "in"},
        {"Square, 8 x 8 in",        8.0,   8.0,  "in"},
        {"A3",                     11.7,  16.5,  "in"},
        {"A4",                      8.27, 11.69, "in"},
        {"A5",                      5.83,  8.27, "in"},
        {"1920 x 1080 px",       1920.0, 1080.0, "px"},
        {"3840 x 2160 px",       3840.0, 2160.0, "px"},
        {"1024 x 768 px",        1024.0,  768.0, "px"},
    };

    m_sizePresetCombo->addItem(tr("Custom"));
    for (const auto& p : m_presets)
        m_sizePresetCombo->addItem(p.label);
}

QWidget* NewDocumentDialog::createFormSection()
{
    auto* t = ThemeManager::instance()->current();
    auto* container = new QWidget(this);
    auto* form = new QFormLayout(container);
    form->setLabelAlignment(Qt::AlignRight);
    form->setHorizontalSpacing(t->spaceLG);
    form->setVerticalSpacing(t->spaceMD);

    auto addField = [&](const QString& label, QWidget* field) {
        auto* lbl = new QLabel(label + ":", this);
        lbl->setFixedWidth(120);
        form->addRow(lbl, field);
    };

    // ── Name ──
    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setText(tr("My new document"));
    m_nameEdit->setMinimumWidth(200);
    addField(tr("Name"), m_nameEdit);

    // ── Document Type ──
    m_docTypeCombo = new QComboBox(this);
    m_docTypeCombo->addItems({tr("Photo"), tr("Illustration"), tr("Web"),
                              tr("Mobile"), tr("Film & Video")});
    addField(tr("Document Type"), m_docTypeCombo);

    // ── Size Preset ──
    m_sizePresetCombo = new QComboBox(this);
    m_sizePresetCombo->setMinimumWidth(200);
    addField(tr("Size"), m_sizePresetCombo);

    // ── Dimensions section ──
    auto* dimFrame = new QFrame(this);
    dimFrame->setObjectName("dimFrame");
    dimFrame->setFrameShape(QFrame::StyledPanel);
    auto* dimGrid = new QGridLayout(dimFrame);
    dimGrid->setSpacing(t->spaceSM);
    dimGrid->setContentsMargins(t->spaceMD, t->spaceSM, t->spaceMD, t->spaceSM);

    auto* widthLbl = new QLabel(tr("Width:"), this);
    m_widthSpin = new QDoubleSpinBox(this);
    m_widthSpin->setRange(0.01, 99999.0);
    m_widthSpin->setDecimals(2);
    m_widthSpin->setValue(10.0);
    m_widthSpin->setFixedWidth(90);
    m_widthUnitCombo = new QComboBox(this);
    m_widthUnitCombo->addItems({tr("Inches"), tr("cm"), tr("mm"), tr("px")});

    auto* heightLbl = new QLabel(tr("Height:"), this);
    m_heightSpin = new QDoubleSpinBox(this);
    m_heightSpin->setRange(0.01, 99999.0);
    m_heightSpin->setDecimals(2);
    m_heightSpin->setValue(8.0);
    m_heightSpin->setFixedWidth(90);
    m_heightUnitCombo = new QComboBox(this);
    m_heightUnitCombo->addItems({tr("Inches"), tr("cm"), tr("mm"), tr("px")});

    auto* resLbl = new QLabel(tr("Resolution:"), this);
    m_resolutionSpin = new QSpinBox(this);
    m_resolutionSpin->setRange(1, 9999);
    m_resolutionSpin->setValue(300);
    m_resolutionSpin->setFixedWidth(90);
    m_resolutionUnitCombo = new QComboBox(this);
    m_resolutionUnitCombo->addItems({tr("Pixels/Inch"), tr("Pixels/cm")});

    dimGrid->addWidget(widthLbl, 0, 0);
    dimGrid->addWidget(m_widthSpin, 0, 1);
    dimGrid->addWidget(m_widthUnitCombo, 0, 2);
    dimGrid->addWidget(heightLbl, 1, 0);
    dimGrid->addWidget(m_heightSpin, 1, 1);
    dimGrid->addWidget(m_heightUnitCombo, 1, 2);
    dimGrid->addWidget(resLbl, 2, 0);
    dimGrid->addWidget(m_resolutionSpin, 2, 1);
    dimGrid->addWidget(m_resolutionUnitCombo, 2, 2);

    // wrap in a vertical layout so the frame fills width
    auto* dimWrapper = new QWidget(this);
    auto* dimWrapperLayout = new QVBoxLayout(dimWrapper);
    dimWrapperLayout->setContentsMargins(0, 0, 0, 0);
    dimWrapperLayout->addWidget(dimFrame);
    form->addRow(dimWrapper);

    // ── Color Mode ──
    m_colorModeCombo = new QComboBox(this);
    m_colorModeCombo->addItems({
        tr("RGB Color, 8 bit"),
        tr("RGB Color, 16 bit"),
        tr("CMYK Color, 8 bit"),
        tr("CMYK Color, 16 bit"),
        tr("Grayscale, 8 bit"),
        tr("Grayscale, 16 bit"),
    });
    addField(tr("Color Mode"), m_colorModeCombo);

    // ── Background Contents ──
    auto* bgWidget = new QWidget(this);
    auto* bgLayout = new QHBoxLayout(bgWidget);
    bgLayout->setContentsMargins(0, 0, 0, 0);
    bgLayout->setSpacing(t->spaceSM);

    m_backgroundCombo = new QComboBox(this);
    m_backgroundCombo->addItems({
        tr("White"),
        tr("Black"),
        tr("Transparent"),
        tr("Background Color"),
        tr("Custom"),
    });
    m_backgroundCombo->setMinimumWidth(140);
    bgLayout->addWidget(m_backgroundCombo);

    m_bgColorBtn = new QPushButton(this);
    m_bgColorBtn->setObjectName("bgColorBtn");
    m_bgColorBtn->setFixedSize(24, 24);
    m_bgColorBtn->setCursor(Qt::PointingHandCursor);
    m_bgColorBtn->setStyleSheet(QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;").arg(QColor(Qt::white).name()).arg(t->colorBorder.name()).arg(t->radiusSM));
    bgLayout->addWidget(m_bgColorBtn);
    bgLayout->addStretch();

    addField(tr("Background Contents"), bgWidget);

    // ── Advanced Section ──
    auto* advGroup = new QGroupBox(tr("Advanced"), this);
    advGroup->setFlat(false);
    auto* advForm = new QFormLayout(advGroup);
    advForm->setHorizontalSpacing(10);

    m_colorProfileCombo = new QComboBox(this);
    {
        ColorProfileRepository repository;
        for (const ColorProfile& profile : repository.builtInRgbProfiles())
            m_colorProfileCombo->addItem(profile.displayName(),
                                         static_cast<int>(profile.kind()));
        // -1 sentinel = untagged (skip colour management for this document).
        m_colorProfileCombo->addItem(tr("Don't Color Manage this Document"), -1);
        const int sIdx = m_colorProfileCombo->findData(
            static_cast<int>(ColorProfileKind::SRgb));
        if (sIdx >= 0)
            m_colorProfileCombo->setCurrentIndex(sIdx);
    }
    advForm->addRow(tr("Color Profile:"), m_colorProfileCombo);

    auto* aspectLbl = new QLabel(tr("Square Pixels"), this);
    aspectLbl->setObjectName("infoLabel");
    advForm->addRow(tr("Pixel Aspect Ratio:"), aspectLbl);

    form->addRow(advGroup);

    return container;
}

QWidget* NewDocumentDialog::createButtonSection()
{
    auto* t = ThemeManager::instance()->current();
    auto* container = new QWidget(this);
    auto* layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(t->spaceMD);

    // Give it a minimum width for the buttons
    container->setFixedWidth(160);

    m_okBtn = new QPushButton(tr("OK"), this);
    m_okBtn->setObjectName("okBtn");
    m_okBtn->setDefault(true);
    m_okBtn->setMinimumHeight(32);
    layout->addWidget(m_okBtn);

    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setObjectName("cancelBtn");
    m_cancelBtn->setMinimumHeight(32);
    layout->addWidget(m_cancelBtn);

    layout->addSpacing(12);

    m_savePresetBtn = new QPushButton(tr("Save Preset..."), this);
    m_savePresetBtn->setObjectName("savePresetBtn");
    layout->addWidget(m_savePresetBtn);

    m_deletePresetBtn = new QPushButton(tr("Delete Preset..."), this);
    m_deletePresetBtn->setObjectName("deletePresetBtn");
    m_deletePresetBtn->setEnabled(false);
    layout->addWidget(m_deletePresetBtn);

    layout->addStretch();

    connect(m_okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    return container;
}

void NewDocumentDialog::setSettings(const DocumentSettings& s)
{
    m_nameEdit->setText(s.name);

    int dtIdx = m_docTypeCombo->findText(s.documentType);
    if (dtIdx >= 0) m_docTypeCombo->setCurrentIndex(dtIdx);

    m_widthSpin->setValue(s.width);
    m_heightSpin->setValue(s.height);

    int wuIdx = m_widthUnitCombo->findText(s.unit, Qt::MatchContains);
    if (wuIdx >= 0) m_widthUnitCombo->setCurrentIndex(wuIdx);
    int huIdx = m_heightUnitCombo->findText(s.unit, Qt::MatchContains);
    if (huIdx >= 0) m_heightUnitCombo->setCurrentIndex(huIdx);

    m_resolutionSpin->setValue(s.resolution);

    int ruIdx = m_resolutionUnitCombo->findText(s.resolutionUnit, Qt::MatchContains);
    if (ruIdx >= 0) m_resolutionUnitCombo->setCurrentIndex(ruIdx);

    int cmIdx = m_colorModeCombo->findText(s.colorMode, Qt::MatchContains);
    if (cmIdx >= 0) m_colorModeCombo->setCurrentIndex(cmIdx);

    if (m_colorProfileCombo) {
        const int want = s.colorManaged ? static_cast<int>(s.colorProfileKind) : -1;
        const int cpIdx = m_colorProfileCombo->findData(want);
        m_colorProfileCombo->setCurrentIndex(cpIdx >= 0 ? cpIdx : 0);
    }

    int bgIdx = m_backgroundCombo->findText(s.background, Qt::MatchContains);
    if (bgIdx >= 0) m_backgroundCombo->setCurrentIndex(bgIdx);

    auto* t = ThemeManager::instance()->current();
    m_bgColor = s.backgroundColor;
    m_bgColorBtn->setStyleSheet(
        QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;").arg(m_bgColor.name()).arg(t->colorBorder.name()).arg(t->radiusSM));

    updateImageSizeDisplay();
}

DocumentSettings NewDocumentDialog::settings() const
{
    DocumentSettings s;
    s.name = m_nameEdit->text();
    s.documentType = m_docTypeCombo->currentText();
    s.width = m_widthSpin->value();
    s.height = m_heightSpin->value();
    s.unit = m_widthUnitCombo->currentText();
    s.resolution = m_resolutionSpin->value();
    s.resolutionUnit = m_resolutionUnitCombo->currentText();
    s.colorMode = m_colorModeCombo->currentText();
    s.background = m_backgroundCombo->currentText();
    s.backgroundColor = m_bgColor;
    if (m_colorProfileCombo) {
        const int kindData = m_colorProfileCombo->currentData().toInt();
        s.colorManaged = (kindData >= 0);
        s.colorProfileKind = s.colorManaged
            ? static_cast<ColorProfileKind>(kindData)
            : ColorProfileKind::SRgb;
        s.colorProfile = m_colorProfileCombo->currentText();
    }
    s.pixelAspect = "Square Pixels";
    return s;
}

// ── Conversion helpers ──

double NewDocumentDialog::valueToInches(double v, const QString& unit) const
{
    if (unit.startsWith("Inch") || unit.startsWith("in"))
        return v;
    if (unit.startsWith("cm"))
        return v / 2.54;
    if (unit.startsWith("mm"))
        return v / 25.4;
    if (unit.startsWith("px"))
        return v / m_resolutionSpin->value();
    return v;
}

double NewDocumentDialog::inchesToValue(double inches, const QString& unit) const
{
    if (unit.startsWith("Inch") || unit.startsWith("in"))
        return inches;
    if (unit.startsWith("cm"))
        return inches * 2.54;
    if (unit.startsWith("mm"))
        return inches * 25.4;
    if (unit.startsWith("px"))
        return inches * m_resolutionSpin->value();
    return inches;
}

// ── Slots ──

void NewDocumentDialog::onSizePresetChanged(int index)
{
    if (index == 0) return; // "Custom"
    const auto& p = m_presets[index - 1];

    m_widthSpin->blockSignals(true);
    m_heightSpin->blockSignals(true);
    m_widthUnitCombo->blockSignals(true);
    m_heightUnitCombo->blockSignals(true);

    m_widthSpin->setValue(p.w);
    m_heightSpin->setValue(p.h);

    int unitIdx = m_widthUnitCombo->findText(p.unit, Qt::MatchContains);
    if (unitIdx >= 0) {
        m_widthUnitCombo->setCurrentIndex(unitIdx);
        m_heightUnitCombo->setCurrentIndex(unitIdx);
    }

    m_widthSpin->blockSignals(false);
    m_heightSpin->blockSignals(false);
    m_widthUnitCombo->blockSignals(false);
    m_heightUnitCombo->blockSignals(false);

    updateImageSizeDisplay();
}

void NewDocumentDialog::onWidthChanged(double)
{
    m_sizePresetCombo->blockSignals(true);
    m_sizePresetCombo->setCurrentIndex(0); // "Custom"
    m_sizePresetCombo->blockSignals(false);
    updateImageSizeDisplay();
}

void NewDocumentDialog::onHeightChanged(double)
{
    m_sizePresetCombo->blockSignals(true);
    m_sizePresetCombo->setCurrentIndex(0); // "Custom"
    m_sizePresetCombo->blockSignals(false);
    updateImageSizeDisplay();
}

void NewDocumentDialog::onWidthUnitChanged(int)
{
    updateImageSizeDisplay();
}

void NewDocumentDialog::onHeightUnitChanged(int)
{
    updateImageSizeDisplay();
}

void NewDocumentDialog::onResolutionChanged(int)
{
    updateImageSizeDisplay();
}

void NewDocumentDialog::onResolutionUnitChanged(int)
{
    updateImageSizeDisplay();
}

void NewDocumentDialog::onBackgroundChanged(int index)
{
    if (index == 4) { // "Custom"
        pickBackgroundColor();
    }
}

void NewDocumentDialog::pickBackgroundColor()
{
    auto* dlg = new ColorPickerDialog(m_bgColor, ColorPickerMode::Foreground, this);
    connect(dlg, &ColorPickerDialog::colorAccepted, this, [this](const QColor& c) {
        auto* t = ThemeManager::instance()->current();
        m_bgColor = c;
        m_bgColorBtn->setStyleSheet(
            QStringLiteral("background-color: %1; border: 1px solid %2; border-radius: %3px;").arg(m_bgColor.name()).arg(t->colorBorder.name()).arg(t->radiusSM));
    });
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->open();
}

void NewDocumentDialog::updateImageSizeDisplay()
{
    double w = m_widthSpin->value();
    double h = m_heightSpin->value();
    QString wUnit = m_widthUnitCombo->currentText();
    QString hUnit = m_heightUnitCombo->currentText();
    int res = m_resolutionSpin->value();
    QString resUnit = m_resolutionUnitCombo->currentText();

    double wInch = valueToInches(w, wUnit);
    double hInch = valueToInches(h, hUnit);

    double ppi = (resUnit.startsWith("Pixels/cm")) ? res * 2.54 : res;
    double wPx = wInch * ppi;
    double hPx = hInch * ppi;

    double bytesPerPixel = 4.0;
    double sizeBytes = wPx * hPx * bytesPerPixel;
    double sizeMB = sizeBytes / (1024.0 * 1024.0);

    m_imageSizeLabel->setText(
        tr("Image Size: %1M  (%2 x %3 px)")
            .arg(sizeMB, 0, 'f', 1)
            .arg(static_cast<int>(std::round(wPx)))
            .arg(static_cast<int>(std::round(hPx))));
}
