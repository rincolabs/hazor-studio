#include "ExportImageDialog.hpp"
#include "io/ImageIO.hpp"
#include "core/Document.hpp"
#include "color/ColorProfileRepository.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QComboBox>
#include <QStackedWidget>
#include <QSlider>
#include <QLabel>
#include "ui/AppCheckBox.hpp"
#include "ui/IconUtils.hpp"
#include <QSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QBuffer>
#include <QImageWriter>
#include <QPainter>
#include <QResizeEvent>
#include <QtConcurrentRun>
#include <QFutureWatcher>
#include <QInputDialog>
#include <QMessageBox>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <limits>

// ── Preset serialization ─────────────────────────────────────

static const char* kSettingsKey = "presets";

QVariantMap ExportPreset::toVariant() const
{
    return {
        {"name", name},
        {"format", format},
        {"quality", quality},
        {"compression", compression},
        {"progressive", progressive},
        {"transparency", transparency},
        {"resizeEnabled", resizeEnabled},
        {"resizeW", resizeW},
        {"resizeH", resizeH},
        {"resampleMode", resampleMode}
    };
}

ExportPreset ExportPreset::fromVariant(const QVariantMap& v)
{
    ExportPreset p;
    p.name = v.value("name").toString();
    p.format = normalizeImageExtension(v.value("format", "png").toString());
    p.quality = v.value("quality", 92).toInt();
    p.compression = v.value("compression", 6).toInt();
    p.progressive = v.value("progressive", false).toBool();
    p.transparency = v.value("transparency", true).toBool();
    p.resizeEnabled = v.value("resizeEnabled", false).toBool();
    p.resizeW = v.value("resizeW", 0).toInt();
    p.resizeH = v.value("resizeH", 0).toInt();
    p.resampleMode = v.value("resampleMode", 1).toInt();
    return p;
}

// ── Dialog ───────────────────────────────────────────────────

ExportImageDialog::ExportImageDialog(Document* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    setWindowTitle(tr("Export As"));
    setMinimumSize(720, 520);

    m_fullImage = ::compositeImage(m_doc);
    if (m_fullImage.isNull())
        m_fullImage = QImage(1, 1, QImage::Format_RGBA8888);

    auto* t = ThemeManager::instance()->current();
    setStyleSheet(t->exportDialogStyleSheet());

    m_previewTimer = new QTimer(this);
    m_previewTimer->setSingleShot(true);
    m_previewTimer->setInterval(300);
    connect(m_previewTimer, &QTimer::timeout, this, &ExportImageDialog::updatePreview);

    m_previewWatcher = new QFutureWatcher<ExportPreviewResult>(this);
    connect(m_previewWatcher, &QFutureWatcher<ExportPreviewResult>::finished,
            this, &ExportImageDialog::onPreviewComputed);

    buildUi();
    loadPresets();
    populatePresetCombo();
    updatePreview();
}

ExportImageDialog::Options ExportImageDialog::options() const
{
    return m_opts;
}

// ── Layout ───────────────────────────────────────────────────

void ExportImageDialog::buildUi()
{
    auto* t = ThemeManager::instance()->current();
    auto* mainLayout = new QVBoxLayout(this);

    // Two columns: a large preview on the left, all options on the right.
    auto* topRow = new QHBoxLayout;

    // ── Preview panel (left column, the larger one) ──
    auto* previewGroup = new QGroupBox(tr("Preview"));
    auto* previewLayout = new QVBoxLayout(previewGroup);

    m_previewLabel = new QLabel;
    m_previewLabel->setMinimumSize(320, 240);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    m_previewLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    m_previewLabel->setStyleSheet(QStringLiteral("background: %1; border: 1px solid %2;").arg(t->colorBackgroundPrimary.name()).arg(t->colorBorder.name()));
    previewLayout->addWidget(m_previewLabel, 1);

    m_fileSizeLabel = new QLabel;
    m_fileSizeLabel->setObjectName("infoLabel");
    m_fileSizeLabel->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_fileSizeLabel);

    m_dimensionsLabel = new QLabel;
    m_dimensionsLabel->setObjectName("infoLabel");
    m_dimensionsLabel->setAlignment(Qt::AlignCenter);
    previewLayout->addWidget(m_dimensionsLabel);

    topRow->addWidget(previewGroup, 3);

    // ── Options column (right) ──
    auto* optionsColumn = new QVBoxLayout;

    // ── Settings panel ──
    auto* settingsGroup = new QGroupBox(tr("Export Settings"));
    auto* settingsLayout = new QVBoxLayout(settingsGroup);

    auto* formLayout = new QFormLayout;

    // Format
    m_formatCombo = new QComboBox;
    QStringList writableFormats = imageCodecRegistry().allWritableFormats();
    if (writableFormats.isEmpty())
        writableFormats << QStringLiteral("png");
    for (const QString& format : writableFormats) {
        const QString normalized = normalizeImageExtension(format);
        m_formatCombo->addItem(normalized.toUpper(), normalized);
    }
    const int pngIndex = m_formatCombo->findData(QStringLiteral("png"));
    if (pngIndex >= 0)
        m_formatCombo->setCurrentIndex(pngIndex);
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportImageDialog::onFormatChanged);
    formLayout->addRow(tr("Format:"), m_formatCombo);

    // Preset
    auto* presetRow = new QHBoxLayout;
    m_presetCombo = new QComboBox;
    m_presetCombo->setMinimumWidth(140);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportImageDialog::onPresetSelected);
    presetRow->addWidget(m_presetCombo);

    auto* savePresetBtn = new QPushButton(tr("Save..."));
    savePresetBtn->setObjectName("savePresetBtn");
    connect(savePresetBtn, &QPushButton::clicked, this, &ExportImageDialog::onSavePreset);
    presetRow->addWidget(savePresetBtn);

    m_deletePresetBtn = new QPushButton(tr("Delete"));
    m_deletePresetBtn->setObjectName("deletePresetBtn");
    connect(m_deletePresetBtn, &QPushButton::clicked, this, &ExportImageDialog::onDeletePreset);
    presetRow->addWidget(m_deletePresetBtn);

    formLayout->addRow(tr("Preset:"), presetRow);
    settingsLayout->addLayout(formLayout);

    // Options stacked widget
    m_optionsStack = new QStackedWidget;
    m_optionsStack->addWidget(createPngPage());
    m_optionsStack->addWidget(createJpegPage());
    m_optionsStack->addWidget(createGenericPage());
    settingsLayout->addWidget(m_optionsStack);

    optionsColumn->addWidget(settingsGroup);

    // ── Image Size group ──
    auto* sizeGroup = new QGroupBox(tr("Image Size"));
    auto* sizeLayout = new QVBoxLayout(sizeGroup);

    m_resizeCheck = new AppCheckBox(tr("Resize"));
    connect(m_resizeCheck, &QCheckBox::toggled, this, &ExportImageDialog::onResizeToggled);
    sizeLayout->addWidget(m_resizeCheck);

    auto* sizeRow = new QHBoxLayout;
    sizeRow->addWidget(new QLabel(tr("Width:")));
    m_widthSpin = new QSpinBox;
    m_widthSpin->setRange(1, 65535);
    m_widthSpin->setValue(m_fullImage.width());
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ExportImageDialog::onWidthChanged);
    sizeRow->addWidget(m_widthSpin);
    sizeRow->addWidget(new QLabel(tr("px")));

    m_constrainCheck = new QPushButton;
    m_constrainCheck->setIcon(makeIcon(QStringLiteral(":/icons/constrain-proportions.png")));
    m_constrainCheck->setIconSize(QSize(20, 20));
    m_constrainCheck->setFixedSize(28, 24);
    m_constrainCheck->setCheckable(true);
    m_constrainCheck->setChecked(true);
    m_constrainCheck->setToolTip(tr("Constrain proportions"));
    connect(m_constrainCheck, &QPushButton::toggled, this, &ExportImageDialog::onConstrainToggled);
    sizeRow->addWidget(m_constrainCheck);

    sizeRow->addWidget(new QLabel(tr("Height:")));
    m_heightSpin = new QSpinBox;
    m_heightSpin->setRange(1, 65535);
    m_heightSpin->setValue(m_fullImage.height());
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ExportImageDialog::onHeightChanged);
    sizeRow->addWidget(m_heightSpin);
    sizeRow->addWidget(new QLabel(tr("px")));

    sizeLayout->addLayout(sizeRow);

    auto* resampleRow = new QHBoxLayout;
    resampleRow->addWidget(new QLabel(tr("Resample:")));
    m_resampleCombo = new QComboBox;
    m_resampleCombo->addItem(tr("Nearest Neighbor"), 0);
    m_resampleCombo->addItem(tr("Bilinear"), 1);
    m_resampleCombo->addItem(tr("Bicubic"), 2);
    m_resampleCombo->setCurrentIndex(1);
    connect(m_resampleCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportImageDialog::onResampleChanged);
    resampleRow->addWidget(m_resampleCombo, 1);
    sizeLayout->addLayout(resampleRow);

    optionsColumn->addWidget(sizeGroup);

    // ── Color group ──
    auto* colorGroup = new QGroupBox(tr("Color"));
    auto* colorLayout = new QVBoxLayout(colorGroup);

    auto* colorModeRow = new QHBoxLayout;
    colorModeRow->addWidget(new QLabel(tr("Profile:")));
    m_colorModeCombo = new QComboBox;
    m_colorModeCombo->addItem(tr("Convert to sRGB (embed)"),
                              static_cast<int>(ExportColorMode::ConvertToSRgbAndEmbed));
    const QString docProfileName = (m_doc && m_doc->colorProfile().isValid())
        ? m_doc->colorProfile().displayName() : tr("Untagged");
    m_colorModeCombo->addItem(tr("Keep document profile (%1)").arg(docProfileName),
                              static_cast<int>(ExportColorMode::UseDocumentProfileAndEmbed));
    m_colorModeCombo->addItem(tr("Convert to profile\xE2\x80\xA6"),
                              static_cast<int>(ExportColorMode::ConvertToSelectedProfileAndEmbed));
    m_colorModeCombo->addItem(tr("Don't embed profile"),
                              static_cast<int>(ExportColorMode::DoNotEmbedProfile));
    connect(m_colorModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportImageDialog::onColorModeChanged);
    colorModeRow->addWidget(m_colorModeCombo, 1);
    colorLayout->addLayout(colorModeRow);

    auto* colorProfileRow = new QHBoxLayout;
    colorProfileRow->addWidget(new QLabel(tr("Target:")));
    m_colorProfileCombo = new QComboBox;
    {
        ColorProfileRepository repo;
        for (const ColorProfile& profile : repo.builtInRgbProfiles())
            m_colorProfileCombo->addItem(profile.displayName(),
                                         static_cast<int>(profile.kind()));
        const int sIdx = m_colorProfileCombo->findData(
            static_cast<int>(ColorProfileKind::SRgb));
        if (sIdx >= 0)
            m_colorProfileCombo->setCurrentIndex(sIdx);
    }
    m_colorProfileCombo->setEnabled(false);
    connect(m_colorProfileCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportImageDialog::onColorProfileChanged);
    colorProfileRow->addWidget(m_colorProfileCombo, 1);
    colorLayout->addLayout(colorProfileRow);

    m_colorWarningLabel = new QLabel;
    m_colorWarningLabel->setWordWrap(true);
    m_colorWarningLabel->setStyleSheet(QStringLiteral("color:#e0a030;"));
    m_colorWarningLabel->hide();
    colorLayout->addWidget(m_colorWarningLabel);

    optionsColumn->addWidget(colorGroup);
    optionsColumn->addStretch();

    // Assemble the two columns into the dialog.
    topRow->addLayout(optionsColumn, 2);
    mainLayout->addLayout(topRow, 1);

    // Seed the selected target profile + warning from the initial state.
    onColorProfileChanged(m_colorProfileCombo->currentIndex());
    updateColorWarning();

    // ── Bottom buttons ──
    auto* btnLayout = new QHBoxLayout;
    btnLayout->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnLayout->addWidget(cancelBtn);

    auto* exportBtn = new QPushButton(tr("Export"));
    exportBtn->setObjectName("okBtn");
    exportBtn->setDefault(true);
    connect(exportBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnLayout->addWidget(exportBtn);

    mainLayout->addLayout(btnLayout);

    // Initial state
    m_resizeCheck->setChecked(false);
    m_widthSpin->setEnabled(false);
    m_heightSpin->setEnabled(false);
    m_resampleCombo->setEnabled(false);
    m_constrainCheck->setEnabled(false);
    onFormatChanged(0);
}

QWidget* ExportImageDialog::createPngPage()
{
    auto* page = new QWidget;
    auto* layout = new QFormLayout(page);

    auto* compressionRow = new QHBoxLayout;
    m_compressionSlider = new QSlider(Qt::Horizontal);
    m_compressionSlider->setRange(0, 9);
    m_compressionSlider->setValue(6);
    connect(m_compressionSlider, &QSlider::valueChanged,
            this, &ExportImageDialog::onCompressionChanged);
    compressionRow->addWidget(m_compressionSlider, 1);

    m_compressionValueLabel = new QLabel("6");
    compressionRow->addWidget(m_compressionValueLabel);
    layout->addRow(tr("Compression:"), compressionRow);

    auto* ticksRow = new QHBoxLayout;
    auto addTick = [&](int val, const QString& label) {
        auto* l = new QLabel(label);
        l->setObjectName("infoLabel");
        l->setAlignment(Qt::AlignHCenter);
        ticksRow->addWidget(l);
    };
    addTick(0, "None");
    addTick(3, "Fast");
    addTick(6, "Small");
    addTick(9, "Best");
    layout->addRow(QString(), ticksRow);

    m_transparencyCheck = new AppCheckBox(tr("Transparency (alpha channel)"));
    m_transparencyCheck->setChecked(true);
    connect(m_transparencyCheck, &QCheckBox::toggled,
            this, &ExportImageDialog::onTransparencyToggled);
    layout->addRow(QString(), m_transparencyCheck);

    return page;
}

QWidget* ExportImageDialog::createJpegPage()
{
    auto* page = new QWidget;
    auto* layout = new QFormLayout(page);

    auto* qualityRow = new QHBoxLayout;
    m_qualitySlider = new QSlider(Qt::Horizontal);
    m_qualitySlider->setRange(0, 100);
    m_qualitySlider->setValue(92);
    connect(m_qualitySlider, &QSlider::valueChanged,
            this, &ExportImageDialog::onQualityChanged);
    qualityRow->addWidget(m_qualitySlider, 1);

    m_qualityValueLabel = new QLabel("92");
    qualityRow->addWidget(m_qualityValueLabel);
    layout->addRow(tr("Quality:"), qualityRow);

    auto* ticksRow = new QHBoxLayout;
    auto addTick = [&](int val, const QString& label) {
        auto* l = new QLabel(label);
        l->setObjectName("infoLabel");
        l->setAlignment(Qt::AlignHCenter);
        ticksRow->addWidget(l);
    };
    addTick(0, "Low");
    addTick(25, "Medium");
    addTick(50, "High");
    addTick(75, "Maximum");
    addTick(100, "Best");
    layout->addRow(QString(), ticksRow);

    m_progressiveCheck = new AppCheckBox(tr("Progressive scan"));
    connect(m_progressiveCheck, &QCheckBox::toggled,
            this, &ExportImageDialog::onProgressiveToggled);
    layout->addRow(QString(), m_progressiveCheck);

    return page;
}

QWidget* ExportImageDialog::createGenericPage()
{
    auto* page = new QWidget;
    auto* layout = new QVBoxLayout(page);
    auto* label = new QLabel(tr("No advanced options for this format."));
    label->setObjectName("infoLabel");
    label->setWordWrap(true);
    layout->addWidget(label);
    layout->addStretch();
    return page;
}

// ── Slots: format options ────────────────────────────────────

void ExportImageDialog::onFormatChanged(int index)
{
    Q_UNUSED(index)
    const QString format = normalizeImageExtension(m_formatCombo->currentData().toString());
    const bool isJpeg = (format == QLatin1String("jpg") || format == QLatin1String("jpeg"));
    const bool isWebp = (format == QLatin1String("webp"));
    if (m_optionsStack) {
        if (format == QLatin1String("png"))
            m_optionsStack->setCurrentIndex(0);
        else if (isJpeg || isWebp)
            m_optionsStack->setCurrentIndex(1); // WebP reuses the quality page
        else
            m_optionsStack->setCurrentIndex(2);
    }
    // Progressive scan is a JPEG-only feature; WebP shares the quality slider
    // but not the progressive option.
    if (m_progressiveCheck)
        m_progressiveCheck->setVisible(isJpeg);
    m_opts.format = format;
    updateColorWarning();
    updatePreview();
}

void ExportImageDialog::onQualityChanged(int value)
{
    m_qualityValueLabel->setText(QString::number(value));
    m_opts.quality = value;

    if (m_presetCombo->currentIndex() >= 0)
        m_presetCombo->setCurrentIndex(0);

    if (!m_updating)
        m_previewTimer->start();
}

void ExportImageDialog::onCompressionChanged(int value)
{
    m_compressionValueLabel->setText(QString::number(value));
    m_opts.compression = value;

    if (m_presetCombo->currentIndex() >= 0)
        m_presetCombo->setCurrentIndex(0);

    if (!m_updating)
        m_previewTimer->start();
}

void ExportImageDialog::onProgressiveToggled(bool checked)
{
    m_opts.progressive = checked;
    if (!m_updating)
        m_previewTimer->start();
}

void ExportImageDialog::onTransparencyToggled(bool checked)
{
    m_opts.transparency = checked;
    if (!m_updating)
        m_previewTimer->start();
}

void ExportImageDialog::onResizeToggled(bool checked)
{
    m_opts.resizeEnabled = checked;
    m_widthSpin->setEnabled(checked);
    m_heightSpin->setEnabled(checked);
    m_resampleCombo->setEnabled(checked);
    m_constrainCheck->setEnabled(checked);
    if (!m_updating)
        m_previewTimer->start();
}

void ExportImageDialog::onWidthChanged(int value)
{
    Q_UNUSED(value)
    if (m_updating) return;
    if (m_constrainCheck->isChecked()) {
        m_updating = true;
        double ratio = static_cast<double>(m_fullImage.height()) / m_fullImage.width();
        m_heightSpin->setValue(static_cast<int>(std::round(m_widthSpin->value() * ratio)));
        m_updating = false;
    }
    m_opts.targetSize.setWidth(m_widthSpin->value());
    m_opts.targetSize.setHeight(m_heightSpin->value());
    m_previewTimer->start();
}

void ExportImageDialog::onHeightChanged(int value)
{
    Q_UNUSED(value)
    if (m_updating) return;
    if (m_constrainCheck->isChecked()) {
        m_updating = true;
        double ratio = static_cast<double>(m_fullImage.width()) / m_fullImage.height();
        m_widthSpin->setValue(static_cast<int>(std::round(m_heightSpin->value() * ratio)));
        m_updating = false;
    }
    m_opts.targetSize.setWidth(m_widthSpin->value());
    m_opts.targetSize.setHeight(m_heightSpin->value());
    m_previewTimer->start();
}

void ExportImageDialog::onConstrainToggled(bool checked)
{
    Q_UNUSED(checked)
}

void ExportImageDialog::onResampleChanged(int index)
{
    m_opts.resampleMode = (index == 0)
        ? Qt::FastTransformation
        : Qt::SmoothTransformation;
    if (!m_updating)
        m_previewTimer->start();
}

void ExportImageDialog::onColorModeChanged(int index)
{
    if (!m_colorModeCombo) return;
    const auto mode = static_cast<ExportColorMode>(
        m_colorModeCombo->itemData(index).toInt());
    m_opts.colorMode = mode;
    if (m_colorProfileCombo)
        m_colorProfileCombo->setEnabled(
            mode == ExportColorMode::ConvertToSelectedProfileAndEmbed);
    onColorProfileChanged(m_colorProfileCombo ? m_colorProfileCombo->currentIndex() : -1);
    updateColorWarning();
}

void ExportImageDialog::onColorProfileChanged(int index)
{
    Q_UNUSED(index)
    if (!m_colorProfileCombo) return;
    const auto kind = static_cast<ColorProfileKind>(
        m_colorProfileCombo->currentData().toInt());
    m_opts.colorSelectedProfile = ColorProfile::fromKind(kind);
}

void ExportImageDialog::updateColorWarning()
{
    if (!m_colorWarningLabel) return;
    // Formats whose current writer preserves an embedded ICC profile. Others
    // (gif, bmp, …) silently drop it, so warn before the user relies on it.
    static const QStringList kIccFormats = {
        QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
        QStringLiteral("tif"), QStringLiteral("tiff"), QStringLiteral("webp")
    };
    const QString fmt = m_opts.format.toLower();
    const bool embeds = m_opts.colorMode != ExportColorMode::DoNotEmbedProfile;
    if (embeds && !fmt.isEmpty() && !kIccFormats.contains(fmt)) {
        m_colorWarningLabel->setText(
            tr("The %1 format may not embed an ICC profile; colors could be "
               "interpreted differently in other applications.").arg(fmt.toUpper()));
        m_colorWarningLabel->show();
    } else {
        m_colorWarningLabel->hide();
    }
}

void ExportImageDialog::syncSizeLink()
{
    m_opts.targetSize.setWidth(m_widthSpin->value());
    m_opts.targetSize.setHeight(m_heightSpin->value());
}

// ── Preview ──────────────────────────────────────────────────

namespace {

// Runs off the GUI thread: encodes the image (for the size estimate) and, for
// lossy formats, decodes it back so the preview reflects the chosen quality.
ExportPreviewResult computeExportPreview(quint64 generation,
                                         QImage image,
                                         QString extension,
                                         ImageSaveOptions options,
                                         bool wantRoundTrip)
{
    ExportPreviewResult result;
    result.generation = generation;

    QImage roundTrip;
    result.byteSize = estimateEncodedSize(image, extension, options,
                                          wantRoundTrip ? &roundTrip : nullptr);
    if (wantRoundTrip && !roundTrip.isNull()) {
        result.image = roundTrip;
        result.hasImage = true;
    }
    return result;
}

} // namespace

void ExportImageDialog::updatePreview()
{
    m_previewTimer->stop();

    // Build the image to export
    QImage exportImg = m_fullImage;
    QSize originalSize = m_fullImage.size();

    if (m_opts.resizeEnabled) {
        QSize target(m_widthSpin->value(), m_heightSpin->value());
        exportImg = m_fullImage.scaled(target, Qt::IgnoreAspectRatio, m_opts.resampleMode);
    } else {
        m_opts.targetSize = originalSize;
    }

    // Update dimensions label
    m_dimensionsLabel->setText(tr("%1 x %2 px")
        .arg(exportImg.width())
        .arg(exportImg.height()));

    // Show the uncompressed image immediately so the UI stays responsive while
    // the (possibly heavy) encode + round-trip runs in the background. Keep the
    // source around so it can be re-scaled when the dialog is resized.
    m_previewSourceImage = exportImg;
    rescalePreviewPixmap();
    m_fileSizeLabel->setText(tr("Calculating\xE2\x80\xA6"));

    const QString normalizedFormat = normalizeImageExtension(m_opts.format);

    ImageSaveOptions estimateOptions;
    estimateOptions.quality = m_opts.quality;
    estimateOptions.compressionLevel = m_opts.compression;
    estimateOptions.progressive = m_opts.progressive;
    estimateOptions.preserveAlpha = m_opts.transparency;

    // Only lossy formats change visually with their quality setting; for the
    // others the round-trip would be identical to the source, so skip it.
    const bool isLossy = (normalizedFormat == QLatin1String("jpg")
        || normalizedFormat == QLatin1String("jpeg")
        || normalizedFormat == QLatin1String("webp"));

    // Encode off-thread. Each call bumps the generation and replaces the watched
    // future, so a fast slider drag (debounced by m_previewTimer) only ever
    // applies the most recent result and never blocks the UI.
    const quint64 generation = ++m_previewGeneration;
    m_previewWatcher->setFuture(QtConcurrent::run(
        computeExportPreview, generation, exportImg, normalizedFormat,
        estimateOptions, isLossy));
}

void ExportImageDialog::onPreviewComputed()
{
    if (m_previewWatcher->future().resultCount() == 0)
        return;

    const ExportPreviewResult result = m_previewWatcher->result();
    if (result.generation != m_previewGeneration)
        return; // A newer request superseded this one.

    if (result.byteSize >= 0)
        m_fileSizeLabel->setText(tr("~ %1").arg(formatFileSize(result.byteSize)));
    else
        m_fileSizeLabel->setText(tr("(size unavailable)"));

    if (result.hasImage && !result.image.isNull()) {
        m_previewSourceImage = result.image;
        rescalePreviewPixmap();
    }
}

void ExportImageDialog::rescalePreviewPixmap()
{
    if (!m_previewLabel || m_previewSourceImage.isNull())
        return;

    // Fit the thumbnail inside the label's current area, keeping aspect ratio.
    const QSize area = m_previewLabel->contentsRect().size() - QSize(4, 4);
    if (area.width() <= 0 || area.height() <= 0)
        return;

    QImage scaled = m_previewSourceImage.scaled(
        area, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_previewLabel->setPixmap(QPixmap::fromImage(scaled));
}

void ExportImageDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    rescalePreviewPixmap();
}

QString ExportImageDialog::formatFileSize(qint64 bytes) const
{
    if (bytes < 1024)
        return tr("%1 B").arg(bytes);
    if (bytes < 1024 * 1024)
        return tr("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    return tr("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 2);
}

// ── Image composite (delegates to ImageIO) ─────────────────────

// ── Presets ──────────────────────────────────────────────────

void ExportImageDialog::loadPresets()
{
    m_presets.clear();
    QSettings s;
    int count = s.beginReadArray(kSettingsKey);
    for (int i = 0; i < count; ++i) {
        s.setArrayIndex(i);
        ExportPreset p = ExportPreset::fromVariant(
            QJsonDocument::fromJson(s.value("data").toString().toUtf8()).object().toVariantMap());
        m_presets.append(p);
    }
    s.endArray();
}

void ExportImageDialog::savePresets()
{
    QSettings s;
    s.beginWriteArray(kSettingsKey);
    for (int i = 0; i < m_presets.size(); ++i) {
        s.setArrayIndex(i);
        s.setValue("data", QString::fromUtf8(
            QJsonDocument(QJsonObject::fromVariantMap(m_presets[i].toVariant())).toJson(
                QJsonDocument::Compact)));
    }
    s.endArray();
}

void ExportImageDialog::populatePresetCombo()
{
    m_updating = true;
    int prev = m_presetCombo->currentIndex();
    m_presetCombo->clear();
    m_presetCombo->addItem(tr("(Custom)"));
    for (auto& p : m_presets)
        m_presetCombo->addItem(p.name);
    if (prev > 0 && prev < m_presetCombo->count())
        m_presetCombo->setCurrentIndex(prev);
    m_deletePresetBtn->setEnabled(m_presets.size() > 0);
    m_updating = false;
}

void ExportImageDialog::applyCurrentAsPresetValue()
{
    m_opts.format = normalizeImageExtension(m_formatCombo->currentData().toString());
    m_opts.quality = m_qualitySlider ? m_qualitySlider->value() : 92;
    m_opts.compression = m_compressionSlider ? m_compressionSlider->value() : 6;
    m_opts.progressive = m_progressiveCheck ? m_progressiveCheck->isChecked() : false;
    m_opts.transparency = m_transparencyCheck ? m_transparencyCheck->isChecked() : true;
    m_opts.resizeEnabled = m_resizeCheck->isChecked();
    m_opts.targetSize = QSize(m_widthSpin->value(), m_heightSpin->value());
    m_opts.resampleMode = (m_resampleCombo->currentIndex() == 0)
        ? Qt::FastTransformation : Qt::SmoothTransformation;
}

void ExportImageDialog::onPresetSelected(int index)
{
    if (m_updating) return;
    if (index <= 0) return; // Custom selected

    const auto& preset = m_presets[index - 1];
    m_updating = true;

    int fmtIdx = m_formatCombo->findData(normalizeImageExtension(preset.format));
    if (fmtIdx >= 0) m_formatCombo->setCurrentIndex(fmtIdx);

    if (m_qualitySlider) {
        m_qualitySlider->setValue(preset.quality);
        m_opts.quality = preset.quality;
    }
    if (m_compressionSlider) {
        m_compressionSlider->setValue(preset.compression);
        m_opts.compression = preset.compression;
    }
    if (m_progressiveCheck) {
        m_progressiveCheck->setChecked(preset.progressive);
        m_opts.progressive = preset.progressive;
    }
    if (m_transparencyCheck) {
        m_transparencyCheck->setChecked(preset.transparency);
        m_opts.transparency = preset.transparency;
    }
    m_resizeCheck->setChecked(preset.resizeEnabled);
    m_widthSpin->setValue(preset.resizeW > 0 ? preset.resizeW : m_fullImage.width());
    m_heightSpin->setValue(preset.resizeH > 0 ? preset.resizeH : m_fullImage.height());
    m_resampleCombo->setCurrentIndex(preset.resampleMode);

    m_updating = false;
    updatePreview();
}

void ExportImageDialog::onSavePreset()
{
    bool ok = false;
    QString name = QInputDialog::getText(this, tr("Save Preset"),
        tr("Preset name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty()) return;

    applyCurrentAsPresetValue();

    ExportPreset preset;
    preset.name = name.trimmed();
    preset.format = normalizeImageExtension(m_opts.format);
    preset.quality = m_opts.quality;
    preset.compression = m_opts.compression;
    preset.progressive = m_opts.progressive;
    preset.transparency = m_opts.transparency;
    preset.resizeEnabled = m_opts.resizeEnabled;
    preset.resizeW = m_opts.targetSize.width();
    preset.resizeH = m_opts.targetSize.height();
    preset.resampleMode = m_resampleCombo->currentIndex();

    // Replace if exists
    for (int i = 0; i < m_presets.size(); ++i) {
        if (m_presets[i].name == preset.name) {
            m_presets[i] = preset;
            savePresets();
            populatePresetCombo();
            m_presetCombo->setCurrentIndex(i + 1);
            return;
        }
    }

    m_presets.append(preset);
    savePresets();
    populatePresetCombo();
    m_presetCombo->setCurrentIndex(m_presets.size());
}

void ExportImageDialog::onDeletePreset()
{
    int idx = m_presetCombo->currentIndex();
    if (idx <= 0) return;

    const auto& preset = m_presets[idx - 1];
    auto ret = QMessageBox::question(this, tr("Delete Preset"),
        tr("Delete preset \"%1\"?").arg(preset.name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes) return;

    m_presets.removeAt(idx - 1);
    savePresets();
    populatePresetCombo();
}
