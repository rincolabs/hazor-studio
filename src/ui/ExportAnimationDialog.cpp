#include "ExportAnimationDialog.hpp"

#include "core/Document.hpp"
#include "io/ImageIO.hpp"
#include "io/ImageCodec.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"
#include "ui/CanvasPresets.hpp"
#include "ui/IconUtils.hpp"

#include <QColorDialog>
#include <QComboBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPainter>
#include <QPalette>
#include <QPaintEvent>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QSplitter>
#include <QTabWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

// ── Preset serialization ─────────────────────────────────────────────────────

namespace {
const char* kCustomPresetsKey = "animExport/customPresets";
const char* kLastDirKey = "animExport/lastDirectory";

// Theme warning colour, so the labels track the active theme rather than a
// baked-in amber.
QString warningStyle()
{
    if (auto* t = ThemeManager::instance()->current())
        return QStringLiteral("color:%1;").arg(t->colorWarning.name());
    return QStringLiteral("color:#e0a030;");
}

// Paint the tab page itself without changing its palette or stylesheet. Both
// would be inherited by child controls and interfere with their theme rules.
class ExportTabPage final : public QWidget {
protected:
    void paintEvent(QPaintEvent* event) override
    {
        QPainter painter(this);
        const QColor background = ThemeManager::instance()->current()
            ? ThemeManager::instance()->current()->colorSurface
            : palette().color(QPalette::Window);
        painter.fillRect(event->rect(), background);
    }
};

QString formatToString(anim::ExportFormat f)
{
    switch (f) {
        case anim::ExportFormat::Mp4:          return QStringLiteral("mp4");
        case anim::ExportFormat::Gif:          return QStringLiteral("gif");
        case anim::ExportFormat::PngSequence:  return QStringLiteral("png-seq");
        case anim::ExportFormat::JpegSequence: return QStringLiteral("jpg-seq");
        case anim::ExportFormat::WebpSequence: return QStringLiteral("webp-seq");
    }
    return QStringLiteral("mp4");
}

anim::ExportFormat formatFromString(const QString& s)
{
    if (s == QLatin1String("gif")) return anim::ExportFormat::Gif;
    if (s == QLatin1String("png-seq")) return anim::ExportFormat::PngSequence;
    if (s == QLatin1String("jpg-seq")) return anim::ExportFormat::JpegSequence;
    if (s == QLatin1String("webp-seq")) return anim::ExportFormat::WebpSequence;
    return anim::ExportFormat::Mp4;
}

// Put a dense tab page in a vertical scroll area so its group boxes always get
// their full preferred height. Without this, the QTabWidget hands the page a
// fixed (smaller) height and the stylesheet padding-top on each QGroupBox — not
// reflected in its minimum-size hint — lets the inner form rows overlap.
QWidget* wrapInScroll(QWidget* content)
{
    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setWidget(content);
    return scroll;
}

QString humanFileSize(qint64 bytes)
{
    if (bytes < 0) return QStringLiteral("—");
    if (bytes < 1024) return QStringLiteral("%1 B").arg(bytes);
    if (bytes < 1024 * 1024) return QStringLiteral("%1 KB").arg(bytes / 1024.0, 0, 'f', 1);
    if (bytes < 1024LL * 1024 * 1024)
        return QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
    return QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024.0 * 1024.0), 0, 'f', 2);
}
} // namespace

QVariantMap AnimExportPreset::toVariant() const
{
    return {
        {"name", name},
        {"category", category},
        {"builtin", builtin},
        {"format", formatToString(format)},
        {"width", width},
        {"height", height},
        {"crf", crf},
        {"gifColors", gifColors},
    };
}

AnimExportPreset AnimExportPreset::fromVariant(const QVariantMap& v)
{
    AnimExportPreset p;
    p.name = v.value("name").toString();
    p.category = v.value("category", QStringLiteral("Custom")).toString();
    p.builtin = v.value("builtin", false).toBool();
    p.format = formatFromString(v.value("format", QStringLiteral("mp4")).toString());
    p.width = v.value("width", 0).toInt();
    p.height = v.value("height", 0).toInt();
    p.crf = v.value("crf", 20).toInt();
    p.gifColors = v.value("gifColors", 256).toInt();
    return p;
}

// ── Construction ─────────────────────────────────────────────────────────────

ExportAnimationDialog::ExportAnimationDialog(Document* doc, QWidget* parent)
    : QDialog(parent)
    , m_doc(doc)
{
    setWindowTitle(tr("Export Animation As"));
    setMinimumSize(880, 640);
    resize(920, 680);

    if (auto* t = ThemeManager::instance()->current()) {
        // Same base as the "Export As" dialog, extended so QDoubleSpinBox matches
        // QSpinBox and the tab pane blends into the dialog surface (Export As has
        // no tabs, so its whole body is one uniform colorSurface).
        QString qss = t->exportDialogStyleSheet();
        qss += QStringLiteral(
                   "QDoubleSpinBox { min-height: 20px; padding: %1px; }\n"
                   "QTabWidget::pane { background: %2; }\n"
                   "QSplitter { background: %2; }\n"
                   "QTreeWidget#presetTree { background: %3; }\n")
                   .arg(t->spaceSM)
                   .arg(t->colorSurface.name())
                   .arg(t->colorBackgroundTertiary.name());
        setStyleSheet(qss);
    }

    m_ffmpegAvailable = anim::AnimationExportService::isFfmpegAvailable();

    m_canvasFrame = ::compositeImage(m_doc);
    if (m_canvasFrame.isNull())
        m_canvasFrame = QImage(1, 1, QImage::Format_RGBA8888);

    loadCustomPresets();
    buildUi();

    m_updating = true;
    seedFromDocument();
    populatePresetTree();
    m_updating = false;

    updateFormatVisibility();
    updateRangeInfo();
    updateSummary();
    updatePreview();
}

QSize ExportAnimationDialog::canvasSize() const
{
    return m_doc && !m_doc->size.isEmpty() ? m_doc->size : QSize(1, 1);
}

anim::ExportFormat ExportAnimationDialog::currentFormat() const
{
    if (!m_formatCombo)
        return anim::ExportFormat::Mp4;
    return static_cast<anim::ExportFormat>(m_formatCombo->currentData().toInt());
}

// ── Layout ───────────────────────────────────────────────────────────────────

void ExportAnimationDialog::buildUi()
{
    auto* rootLayout = new QVBoxLayout(this);

    auto* splitter = new QSplitter(Qt::Horizontal);
    splitter->addWidget(buildLeftPanel());

    // ── Right panel ──
    auto* right = new QWidget;
    auto* rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    // Format + output.
    auto* headerForm = new QFormLayout;
    m_formatCombo = new QComboBox;
    m_formatCombo->addItem(tr("MP4 Video"), static_cast<int>(anim::ExportFormat::Mp4));
    m_formatCombo->addItem(tr("Animated GIF"), static_cast<int>(anim::ExportFormat::Gif));
    m_formatCombo->addItem(tr("PNG Sequence"), static_cast<int>(anim::ExportFormat::PngSequence));
    m_formatCombo->addItem(tr("JPEG Sequence"), static_cast<int>(anim::ExportFormat::JpegSequence));
    if (imageCodecRegistry().findWriter(QStringLiteral("webp")))
        m_formatCombo->addItem(tr("WebP Sequence"),
                               static_cast<int>(anim::ExportFormat::WebpSequence));
    connect(m_formatCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportAnimationDialog::onFormatChanged);
    headerForm->addRow(tr("Format:"), m_formatCombo);

    auto* outputRow = new QHBoxLayout;
    m_outputEdit = new QLineEdit;
    m_outputEdit->setPlaceholderText(tr("Choose a destination…"));
    connect(m_outputEdit, &QLineEdit::textChanged, this, [this]() {
        if (!m_updating) updateSummary();
    });
    outputRow->addWidget(m_outputEdit, 1);
    m_browseBtn = new QPushButton(tr("Browse…"));
    connect(m_browseBtn, &QPushButton::clicked, this, &ExportAnimationDialog::onBrowseOutput);
    outputRow->addWidget(m_browseBtn);
    m_outputLabel = new QLabel(tr("Output file:"));
    headerForm->addRow(m_outputLabel, outputRow);
    rightLayout->addLayout(headerForm);

    m_ffmpegWarning = new QLabel(
        tr("FFmpeg was not found. Install it (or set HAZOR_FFMPEG) to export MP4 or GIF."));
    m_ffmpegWarning->setWordWrap(true);
    m_ffmpegWarning->setStyleSheet(warningStyle());
    m_ffmpegWarning->hide();
    rightLayout->addWidget(m_ffmpegWarning);

    // Tabs.
    m_tabs = new QTabWidget;
    m_tabs->addTab(wrapInScroll(buildOutputTab()), tr("Output"));       // 0
    m_tabs->addTab(wrapInScroll(buildVideoTab()), tr("Video / GIF"));   // 1
    m_tabs->addTab(wrapInScroll(buildSequenceTab()), tr("Image Sequence")); // 2
    m_tabs->addTab(wrapInScroll(buildAdvancedTab()), tr("Advanced"));   // 3
    rightLayout->addWidget(m_tabs, 1);

    // Summary + preview.
    auto* summaryGroup = new QGroupBox(tr("Summary"));
    auto* summaryLayout = new QHBoxLayout(summaryGroup);
    m_summaryLabel = new QLabel;
    m_summaryLabel->setTextFormat(Qt::RichText);
    m_summaryLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_summaryLabel->setWordWrap(true);
    summaryLayout->addWidget(m_summaryLabel, 1);

    m_previewLabel = new QLabel;
    m_previewLabel->setFixedSize(200, 130);
    m_previewLabel->setAlignment(Qt::AlignCenter);
    if (auto* t = ThemeManager::instance()->current())
        m_previewLabel->setStyleSheet(
            QStringLiteral("background:%1; border:1px solid %2;")
                .arg(t->colorBackgroundPrimary.name(), t->colorBorder.name()));
    summaryLayout->addWidget(m_previewLabel);
    rightLayout->addWidget(summaryGroup);

    splitter->addWidget(right);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    splitter->setSizes({230, 620});
    rootLayout->addWidget(splitter, 1);

    // ── Bottom buttons ──
    auto* btnRow = new QHBoxLayout;
    auto* resetBtn = new QPushButton(tr("Reset"));
    connect(resetBtn, &QPushButton::clicked, this, &ExportAnimationDialog::onResetClicked);
    btnRow->addWidget(resetBtn);
    btnRow->addStretch();
    auto* cancelBtn = new QPushButton(tr("Cancel"));
    cancelBtn->setObjectName("cancelBtn");
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    auto* exportBtn = new QPushButton(tr("Export"));
    exportBtn->setObjectName("okBtn");
    exportBtn->setDefault(true);
    connect(exportBtn, &QPushButton::clicked, this, &ExportAnimationDialog::accept);
    btnRow->addWidget(exportBtn);
    rootLayout->addLayout(btnRow);
}

QWidget* ExportAnimationDialog::buildLeftPanel()
{
    auto* panel = new QWidget;
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(0, 0, 0, 0);

    auto* title = new QLabel(tr("Presets"));
    title->setAlignment(Qt::AlignHCenter);
    layout->addWidget(title);

    m_searchEdit = new QLineEdit;
    m_searchEdit->setPlaceholderText(tr("Search"));
    m_searchEdit->setClearButtonEnabled(true);
    connect(m_searchEdit, &QLineEdit::textChanged,
            this, &ExportAnimationDialog::onSearchChanged);
    layout->addWidget(m_searchEdit);

    m_presetTree = new QTreeWidget;
    m_presetTree->setObjectName(QStringLiteral("presetTree"));
    m_presetTree->setHeaderHidden(true);
    m_presetTree->setColumnCount(1);
    connect(m_presetTree, &QTreeWidget::itemSelectionChanged,
            this, &ExportAnimationDialog::onPresetActivated);
    layout->addWidget(m_presetTree, 1);

    auto* toolRow = new QHBoxLayout;
    m_addPresetBtn = new QPushButton(QStringLiteral("+"));
    m_addPresetBtn->setObjectName("savePresetBtn");
    m_addPresetBtn->setFixedWidth(38);
    m_addPresetBtn->setToolTip(tr("Save current settings as a custom preset"));
    connect(m_addPresetBtn, &QPushButton::clicked, this, &ExportAnimationDialog::onAddPreset);
    toolRow->addWidget(m_addPresetBtn);
    m_removePresetBtn = new QPushButton(QStringLiteral("−"));
    m_removePresetBtn->setObjectName("deletePresetBtn");
    m_removePresetBtn->setFixedWidth(38);
    m_removePresetBtn->setToolTip(tr("Delete the selected custom preset"));
    m_removePresetBtn->setEnabled(false);
    connect(m_removePresetBtn, &QPushButton::clicked, this,
            &ExportAnimationDialog::onRemovePreset);
    toolRow->addWidget(m_removePresetBtn);
    toolRow->addStretch();
    layout->addLayout(toolRow);

    return panel;
}

QWidget* ExportAnimationDialog::buildOutputTab()
{
    auto* tab = new ExportTabPage;
    auto* layout = new QVBoxLayout(tab);

    // ── Range ──
    auto* rangeGroup = new QGroupBox(tr("Frame Range"));
    auto* rangeLayout = new QFormLayout(rangeGroup);
    m_rangeModeCombo = new QComboBox;
    m_rangeModeCombo->addItem(tr("Entire Animation"), 0);
    m_rangeModeCombo->addItem(tr("Playback Range"), 1);
    m_rangeModeCombo->addItem(tr("Custom Range"), 2);
    connect(m_rangeModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportAnimationDialog::onRangeModeChanged);
    rangeLayout->addRow(tr("Range:"), m_rangeModeCombo);

    auto* frameRow = new QHBoxLayout;
    m_startSpin = new QSpinBox;
    m_startSpin->setRange(-1000000, 1000000);
    m_endSpin = new QSpinBox;
    m_endSpin->setRange(-1000000, 1000000);
    connect(m_startSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ExportAnimationDialog::onRangeSpinChanged);
    connect(m_endSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ExportAnimationDialog::onRangeSpinChanged);
    frameRow->addWidget(new QLabel(tr("Start:")));
    frameRow->addWidget(m_startSpin);
    frameRow->addSpacing(8);
    frameRow->addWidget(new QLabel(tr("End:")));
    frameRow->addWidget(m_endSpin);
    frameRow->addStretch();
    rangeLayout->addRow(QString(), frameRow);

    m_totalFramesLabel = new QLabel;
    m_totalFramesLabel->setObjectName("infoLabel");
    rangeLayout->addRow(tr("Total:"), m_totalFramesLabel);
    m_durationLabel = new QLabel;
    m_durationLabel->setObjectName("infoLabel");
    rangeLayout->addRow(tr("Duration:"), m_durationLabel);
    layout->addWidget(rangeGroup);

    // ── FPS ──
    auto* fpsGroup = new QGroupBox(tr("Frame Rate"));
    auto* fpsLayout = new QFormLayout(fpsGroup);
    auto* fpsRow = new QHBoxLayout;
    m_fpsCombo = new QComboBox;
    for (double v : {12.0, 15.0, 23.976, 24.0, 25.0, 29.97, 30.0, 50.0, 59.94, 60.0})
        m_fpsCombo->addItem(QString::number(v, 'g', 6), v);
    m_fpsCombo->addItem(tr("Custom"), -1.0);
    connect(m_fpsCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportAnimationDialog::onFpsChanged);
    fpsRow->addWidget(m_fpsCombo);
    m_fpsCustomSpin = new QDoubleSpinBox;
    m_fpsCustomSpin->setRange(0.1, 480.0);
    m_fpsCustomSpin->setDecimals(3);
    m_fpsCustomSpin->setValue(24.0);
    connect(m_fpsCustomSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this]() { if (!m_updating) { updateRangeInfo(); updateSummary(); } });
    fpsRow->addWidget(m_fpsCustomSpin);
    fpsRow->addStretch();
    fpsLayout->addRow(tr("FPS:"), fpsRow);
    m_fpsMismatchLabel = new QLabel;
    m_fpsMismatchLabel->setObjectName("infoLabel");
    m_fpsMismatchLabel->setWordWrap(true);
    m_fpsMismatchLabel->setStyleSheet(warningStyle());
    m_fpsMismatchLabel->hide();
    fpsLayout->addRow(QString(), m_fpsMismatchLabel);
    layout->addWidget(fpsGroup);

    // ── Size ──
    auto* sizeGroup = new QGroupBox(tr("Size"));
    auto* sizeLayout = new QVBoxLayout(sizeGroup);

    m_originalSizeCheck = new AppCheckBox(tr("Use original canvas size"));
    connect(m_originalSizeCheck, &QCheckBox::toggled,
            this, &ExportAnimationDialog::onOriginalSizeToggled);
    sizeLayout->addWidget(m_originalSizeCheck);

    auto* dimRow = new QHBoxLayout;
    dimRow->addWidget(new QLabel(tr("Width:")));
    m_widthSpin = new QSpinBox;
    m_widthSpin->setRange(1, 16384);
    connect(m_widthSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ExportAnimationDialog::onWidthChanged);
    dimRow->addWidget(m_widthSpin);
    m_constrainBtn = new QPushButton;
    m_constrainBtn->setIcon(makeIcon(QStringLiteral(":/icons/constrain-proportions.png")));
    m_constrainBtn->setIconSize(QSize(18, 18));
    m_constrainBtn->setFixedSize(28, 24);
    m_constrainBtn->setCheckable(true);
    m_constrainBtn->setChecked(true);
    m_constrainBtn->setToolTip(tr("Preserve aspect ratio"));
    dimRow->addWidget(m_constrainBtn);
    dimRow->addWidget(new QLabel(tr("Height:")));
    m_heightSpin = new QSpinBox;
    m_heightSpin->setRange(1, 16384);
    connect(m_heightSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &ExportAnimationDialog::onHeightChanged);
    dimRow->addWidget(m_heightSpin);
    dimRow->addStretch();
    sizeLayout->addLayout(dimRow);

    auto* extraRow = new QHBoxLayout;
    extraRow->addWidget(new QLabel(tr("Scale:")));
    m_scaleSpin = new QDoubleSpinBox;
    m_scaleSpin->setRange(1.0, 1000.0);
    m_scaleSpin->setValue(100.0);
    m_scaleSpin->setSuffix(QStringLiteral(" %"));
    connect(m_scaleSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ExportAnimationDialog::onScaleChanged);
    extraRow->addWidget(m_scaleSpin);
    extraRow->addSpacing(8);
    extraRow->addWidget(new QLabel(tr("Aspect:")));
    m_aspectCombo = new QComboBox;
    m_aspectCombo->addItem(tr("Original"), 0.0);
    m_aspectCombo->addItem(QStringLiteral("1:1"), 1.0);
    m_aspectCombo->addItem(QStringLiteral("4:5"), 4.0 / 5.0);
    m_aspectCombo->addItem(QStringLiteral("16:9"), 16.0 / 9.0);
    m_aspectCombo->addItem(QStringLiteral("9:16"), 9.0 / 16.0);
    m_aspectCombo->addItem(QStringLiteral("4:3"), 4.0 / 3.0);
    m_aspectCombo->addItem(QStringLiteral("3:2"), 3.0 / 2.0);
    m_aspectCombo->addItem(tr("Custom"), -1.0);
    connect(m_aspectCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportAnimationDialog::onAspectChanged);
    extraRow->addWidget(m_aspectCombo);
    extraRow->addStretch();
    sizeLayout->addLayout(extraRow);

    auto* fitRow = new QHBoxLayout;
    fitRow->addWidget(new QLabel(tr("When aspect differs:")));
    m_fitCombo = new QComboBox;
    m_fitCombo->addItem(tr("Fit (letterbox)"), static_cast<int>(anim::FitMode::Fit));
    m_fitCombo->addItem(tr("Fill (crop)"), static_cast<int>(anim::FitMode::Fill));
    m_fitCombo->addItem(tr("Stretch"), static_cast<int>(anim::FitMode::Stretch));
    connect(m_fitCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { if (!m_updating) { updateSummary(); updatePreview(); } });
    fitRow->addWidget(m_fitCombo);
    fitRow->addStretch();
    sizeLayout->addLayout(fitRow);

    auto* bgRow = new QHBoxLayout;
    m_transparencyCheck = new AppCheckBox(tr("Transparency"));
    m_transparencyCheck->setChecked(true);
    connect(m_transparencyCheck, &QCheckBox::toggled, this, [this]() {
        if (!m_updating) { updateSummary(); updatePreview(); }
    });
    bgRow->addWidget(m_transparencyCheck);
    bgRow->addSpacing(12);
    bgRow->addWidget(new QLabel(tr("Background:")));
    m_bgColorBtn = new QPushButton;
    m_bgColorBtn->setFixedSize(40, 22);
    connect(m_bgColorBtn, &QPushButton::clicked, this, &ExportAnimationDialog::onPickBackground);
    bgRow->addWidget(m_bgColorBtn);
    bgRow->addStretch();
    sizeLayout->addLayout(bgRow);

    m_aspectWarnLabel = new QLabel;
    m_aspectWarnLabel->setObjectName("infoLabel");
    m_aspectWarnLabel->setWordWrap(true);
    m_aspectWarnLabel->setStyleSheet(warningStyle());
    m_aspectWarnLabel->hide();
    sizeLayout->addWidget(m_aspectWarnLabel);

    layout->addWidget(sizeGroup);
    layout->addStretch();
    return tab;
}

QWidget* ExportAnimationDialog::buildVideoTab()
{
    auto* tab = new ExportTabPage;
    auto* layout = new QVBoxLayout(tab);

    // ── MP4 ──
    m_mp4Group = new QGroupBox(tr("MP4 Video"));
    auto* mp4Layout = new QFormLayout(m_mp4Group);
    m_mp4QualityCombo = new QComboBox;
    m_mp4QualityCombo->addItem(tr("Low"), 30);
    m_mp4QualityCombo->addItem(tr("Medium"), 26);
    m_mp4QualityCombo->addItem(tr("High"), 22);
    m_mp4QualityCombo->addItem(tr("Very High"), 18);
    m_mp4QualityCombo->addItem(tr("Custom"), -1);
    m_mp4QualityCombo->setCurrentIndex(2);
    connect(m_mp4QualityCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportAnimationDialog::onMp4QualityChanged);
    mp4Layout->addRow(tr("Quality:"), m_mp4QualityCombo);

    m_codecCombo = new QComboBox;
    m_codecCombo->addItem(tr("H.264"), QStringLiteral("libx264"));
    m_codecCombo->addItem(tr("H.265 / HEVC"), QStringLiteral("libx265"));
    connect(m_codecCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { if (!m_updating) updateSummary(); });
    mp4Layout->addRow(tr("Codec:"), m_codecCombo);

    m_crfSpin = new QSpinBox;
    m_crfSpin->setRange(0, 51);
    m_crfSpin->setValue(22);
    m_crfSpin->setToolTip(tr("Constant Rate Factor — lower is higher quality."));
    m_crfSpin->setEnabled(false);
    connect(m_crfSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]() { if (!m_updating) updateSummary(); });
    mp4Layout->addRow(tr("CRF:"), m_crfSpin);

    m_encoderPresetCombo = new QComboBox;
    for (const QString& p : {QStringLiteral("ultrafast"), QStringLiteral("veryfast"),
                             QStringLiteral("fast"), QStringLiteral("medium"),
                             QStringLiteral("slow"), QStringLiteral("veryslow")})
        m_encoderPresetCombo->addItem(p, p);
    m_encoderPresetCombo->setCurrentText(QStringLiteral("medium"));
    mp4Layout->addRow(tr("Encoder speed:"), m_encoderPresetCombo);

    m_pixFmtCombo = new QComboBox;
    m_pixFmtCombo->addItem(QStringLiteral("yuv420p"), QStringLiteral("yuv420p"));
    m_pixFmtCombo->addItem(QStringLiteral("yuv444p"), QStringLiteral("yuv444p"));
    mp4Layout->addRow(tr("Pixel format:"), m_pixFmtCombo);

    m_hwCheck = new AppCheckBox(tr("Use hardware encoder (h264_nvenc, if available)"));
    mp4Layout->addRow(QString(), m_hwCheck);
    layout->addWidget(m_mp4Group);

    // ── GIF ──
    m_gifGroup = new QGroupBox(tr("Animated GIF"));
    auto* gifLayout = new QFormLayout(m_gifGroup);
    m_gifColorsSpin = new QSpinBox;
    m_gifColorsSpin->setRange(2, 256);
    m_gifColorsSpin->setValue(256);
    connect(m_gifColorsSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]() { if (!m_updating) updateSummary(); });
    gifLayout->addRow(tr("Colors:"), m_gifColorsSpin);

    m_gifDitherCheck = new AppCheckBox(tr("Dithering"));
    m_gifDitherCheck->setChecked(true);
    gifLayout->addRow(QString(), m_gifDitherCheck);

    auto* loopRow = new QHBoxLayout;
    m_gifLoopCombo = new QComboBox;
    m_gifLoopCombo->addItem(tr("Forever"), 0);
    m_gifLoopCombo->addItem(tr("Once"), -1);
    m_gifLoopCombo->addItem(tr("Custom"), 1);
    connect(m_gifLoopCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ExportAnimationDialog::onGifLoopChanged);
    loopRow->addWidget(m_gifLoopCombo);
    m_gifLoopSpin = new QSpinBox;
    m_gifLoopSpin->setRange(1, 10000);
    m_gifLoopSpin->setValue(3);
    m_gifLoopSpin->setSuffix(tr(" times"));
    m_gifLoopSpin->hide();
    loopRow->addWidget(m_gifLoopSpin);
    loopRow->addStretch();
    gifLayout->addRow(tr("Loop:"), loopRow);
    layout->addWidget(m_gifGroup);

    layout->addStretch();
    return tab;
}

QWidget* ExportAnimationDialog::buildSequenceTab()
{
    auto* tab = new ExportTabPage;
    auto* layout = new QVBoxLayout(tab);

    auto* nameGroup = new QGroupBox(tr("File Naming"));
    auto* nameLayout = new QFormLayout(nameGroup);
    m_prefixEdit = new QLineEdit(QStringLiteral("frame"));
    connect(m_prefixEdit, &QLineEdit::textChanged, this, [this]() {
        if (!m_updating) { updateNamePreview(); updateSummary(); }
    });
    nameLayout->addRow(tr("Prefix:"), m_prefixEdit);

    m_paddingSpin = new QSpinBox;
    m_paddingSpin->setRange(1, 8);
    m_paddingSpin->setValue(4);
    connect(m_paddingSpin, QOverload<int>::of(&QSpinBox::valueChanged),
            this, [this]() { if (!m_updating) updateNamePreview(); });
    nameLayout->addRow(tr("Number digits:"), m_paddingSpin);

    m_namePreviewLabel = new QLabel;
    m_namePreviewLabel->setObjectName("infoLabel");
    m_namePreviewLabel->setTextFormat(Qt::PlainText);
    nameLayout->addRow(tr("Preview:"), m_namePreviewLabel);
    layout->addWidget(nameGroup);

    auto* qualityGroup = new QGroupBox(tr("Image Options"));
    auto* qualityLayout = new QVBoxLayout(qualityGroup);

    m_pngRow = new QWidget;
    auto* pngLayout = new QHBoxLayout(m_pngRow);
    pngLayout->setContentsMargins(0, 0, 0, 0);
    pngLayout->addWidget(new QLabel(tr("PNG compression:")));
    m_pngCompressionSlider = new QSlider(Qt::Horizontal);
    m_pngCompressionSlider->setRange(0, 9);
    m_pngCompressionSlider->setValue(6);
    pngLayout->addWidget(m_pngCompressionSlider, 1);
    qualityLayout->addWidget(m_pngRow);

    m_jpegRow = new QWidget;
    auto* jpegLayout = new QHBoxLayout(m_jpegRow);
    jpegLayout->setContentsMargins(0, 0, 0, 0);
    jpegLayout->addWidget(new QLabel(tr("Quality:")));
    m_jpegQualitySlider = new QSlider(Qt::Horizontal);
    m_jpegQualitySlider->setRange(1, 100);
    m_jpegQualitySlider->setValue(92);
    connect(m_jpegQualitySlider, &QSlider::valueChanged,
            this, [this]() { if (!m_updating) updateSummary(); });
    jpegLayout->addWidget(m_jpegQualitySlider, 1);
    qualityLayout->addWidget(m_jpegRow);

    auto* seqNote = new QLabel(
        tr("Transparency and background are set in the Output tab."));
    seqNote->setObjectName("infoLabel");
    seqNote->setWordWrap(true);
    qualityLayout->addWidget(seqNote);

    layout->addWidget(qualityGroup);
    layout->addStretch();
    return tab;
}

QWidget* ExportAnimationDialog::buildAdvancedTab()
{
    auto* tab = new ExportTabPage;
    auto* layout = new QFormLayout(tab);

    m_interpCombo = new QComboBox;
    m_interpCombo->addItem(tr("Nearest Neighbor"), 0);
    m_interpCombo->addItem(tr("Bilinear"), 1);
    m_interpCombo->addItem(tr("Bicubic"), 2);
    m_interpCombo->addItem(tr("Lanczos"), 3);
    m_interpCombo->setCurrentIndex(1);
    connect(m_interpCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { if (!m_updating) updatePreview(); });
    layout->addRow(tr("Resize interpolation:"), m_interpCombo);

    auto* note = new QLabel(
        tr("Resizing is skipped when the output resolution matches the canvas. "
           "The export frame rate resamples the timeline frames directly."));
    note->setObjectName("infoLabel");
    note->setWordWrap(true);
    layout->addRow(QString(), note);
    return tab;
}

// ── Presets ──────────────────────────────────────────────────────────────────

QVector<AnimExportPreset> ExportAnimationDialog::builtinPresets() const
{
    using F = anim::ExportFormat;
    auto mk = [](const QString& cat, const QString& name, F fmt,
                 int w, int h, int crf = 20) {
        AnimExportPreset p;
        p.category = cat; p.name = name; p.builtin = true;
        p.format = fmt; p.width = w; p.height = h; p.crf = crf;
        return p;
    };
    QVector<AnimExportPreset> list;
    list << mk("General", tr("MP4 — Original Size"), F::Mp4, 0, 0, 20)
         << mk("General", tr("MP4 — High Quality"), F::Mp4, 0, 0, 16)
         << mk("General", tr("GIF — Original Size"), F::Gif, 0, 0)
         << mk("General", tr("Image Sequence"), F::PngSequence, 0, 0);

    // The platform size presets are shared with the New Document dialog so the
    // two dialogs stay in sync from a single catalogue.
    for (const canvaspresets::CanvasPreset& cp : canvaspresets::builtinPresets()) {
        if (cp.kind != canvaspresets::DocumentKind::Animation)
            continue;
        const QSize px = canvaspresets::presetPixelSize(cp);
        list << mk(cp.group, cp.name, F::Mp4, px.width(), px.height());
    }
    return list;
}

void ExportAnimationDialog::populatePresetTree()
{
    m_presetTree->clear();
    QHash<QString, QTreeWidgetItem*> categories;
    auto ensureCat = [&](const QString& cat) -> QTreeWidgetItem* {
        auto it = categories.constFind(cat);
        if (it != categories.constEnd())
            return it.value();
        auto* node = new QTreeWidgetItem(m_presetTree, {cat});
        node->setFlags(Qt::ItemIsEnabled);
        categories.insert(cat, node);
        return node;
    };

    ensureCat(tr("Custom"));
    for (const AnimExportPreset& p : builtinPresets()) {
        auto* leaf = new QTreeWidgetItem(ensureCat(p.category), {p.name});
        leaf->setData(0, Qt::UserRole, p.toVariant());
    }
    for (const AnimExportPreset& p : m_customPresets) {
        auto* leaf = new QTreeWidgetItem(ensureCat(tr("Custom")), {p.name});
        leaf->setData(0, Qt::UserRole, p.toVariant());
    }
    m_presetTree->expandAll();
}

void ExportAnimationDialog::onPresetActivated()
{
    auto* item = m_presetTree->currentItem();
    if (!item) {
        m_removePresetBtn->setEnabled(false);
        return;
    }
    const QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid()) {
        m_removePresetBtn->setEnabled(false);
        return;
    }
    const AnimExportPreset preset = AnimExportPreset::fromVariant(data.toMap());
    m_removePresetBtn->setEnabled(!preset.builtin);
    if (!m_updating)
        applyPreset(preset);
}

void ExportAnimationDialog::applyPreset(const AnimExportPreset& preset)
{
    m_updating = true;
    const int fmtIdx = m_formatCombo->findData(static_cast<int>(preset.format));
    if (fmtIdx >= 0)
        m_formatCombo->setCurrentIndex(fmtIdx);

    if (preset.width <= 0 || preset.height <= 0) {
        m_originalSizeCheck->setChecked(true);
        onOriginalSizeToggled(true);
    } else {
        m_originalSizeCheck->setChecked(false);
        onOriginalSizeToggled(false);
        setSize(preset.width, preset.height);
    }

    // MP4 quality → Custom + explicit CRF so presets can fine-tune it.
    const int qIdx = m_mp4QualityCombo->findData(-1);
    if (qIdx >= 0)
        m_mp4QualityCombo->setCurrentIndex(qIdx);
    m_crfSpin->setEnabled(true);
    m_crfSpin->setValue(preset.crf);
    m_gifColorsSpin->setValue(preset.gifColors);
    m_updating = false;

    updateFormatVisibility();
    updateRangeInfo();
    updateSummary();
    updatePreview();
}

void ExportAnimationDialog::onAddPreset()
{
    bool ok = false;
    const QString name = QInputDialog::getText(this, tr("Save Preset"),
        tr("Preset name:"), QLineEdit::Normal, QString(), &ok);
    if (!ok || name.trimmed().isEmpty())
        return;

    AnimExportPreset p;
    p.name = name.trimmed();
    p.category = tr("Custom");
    p.builtin = false;
    p.format = currentFormat();
    if (!m_originalSizeCheck->isChecked()) {
        p.width = m_widthSpin->value();
        p.height = m_heightSpin->value();
    }
    p.crf = m_crfSpin->value();
    p.gifColors = m_gifColorsSpin->value();

    for (auto& existing : m_customPresets) {
        if (existing.name == p.name) {
            existing = p;
            saveCustomPresets();
            populatePresetTree();
            return;
        }
    }
    m_customPresets.push_back(p);
    saveCustomPresets();
    populatePresetTree();
}

void ExportAnimationDialog::onRemovePreset()
{
    auto* item = m_presetTree->currentItem();
    if (!item)
        return;
    const QVariant data = item->data(0, Qt::UserRole);
    if (!data.isValid())
        return;
    const AnimExportPreset preset = AnimExportPreset::fromVariant(data.toMap());
    if (preset.builtin)
        return;
    for (int i = 0; i < m_customPresets.size(); ++i) {
        if (m_customPresets[i].name == preset.name) {
            m_customPresets.remove(i);
            break;
        }
    }
    saveCustomPresets();
    populatePresetTree();
}

void ExportAnimationDialog::onSearchChanged(const QString& text)
{
    const QString needle = text.trimmed();
    for (int i = 0; i < m_presetTree->topLevelItemCount(); ++i) {
        QTreeWidgetItem* cat = m_presetTree->topLevelItem(i);
        int visibleChildren = 0;
        for (int j = 0; j < cat->childCount(); ++j) {
            QTreeWidgetItem* leaf = cat->child(j);
            const bool match = needle.isEmpty()
                || leaf->text(0).contains(needle, Qt::CaseInsensitive);
            leaf->setHidden(!match);
            if (match)
                ++visibleChildren;
        }
        cat->setHidden(cat->childCount() > 0 && visibleChildren == 0);
    }
}

void ExportAnimationDialog::loadCustomPresets()
{
    m_customPresets.clear();
    QSettings s;
    const QJsonDocument doc = QJsonDocument::fromJson(
        s.value(kCustomPresetsKey).toString().toUtf8());
    for (const QJsonValue& v : doc.array())
        m_customPresets.push_back(AnimExportPreset::fromVariant(v.toObject().toVariantMap()));
}

void ExportAnimationDialog::saveCustomPresets()
{
    QJsonArray arr;
    for (const AnimExportPreset& p : m_customPresets)
        arr.append(QJsonObject::fromVariantMap(p.toVariant()));
    QSettings s;
    s.setValue(kCustomPresetsKey,
               QString::fromUtf8(QJsonDocument(arr).toJson(QJsonDocument::Compact)));
}

// ── Seeding / helpers ────────────────────────────────────────────────────────

void ExportAnimationDialog::seedFromDocument()
{
    const QSize canvas = canvasSize();
    m_widthSpin->setValue(canvas.width());
    m_heightSpin->setValue(canvas.height());
    m_originalSizeCheck->setChecked(true);
    m_widthSpin->setEnabled(false);
    m_heightSpin->setEnabled(false);
    m_scaleSpin->setEnabled(false);
    m_aspectCombo->setEnabled(false);
    m_constrainBtn->setEnabled(false);

    m_backgroundColor = QColor(0, 0, 0);
    updateBackgroundSwatch();

    const anim::AnimationModel& model = m_doc->animation;
    m_startSpin->setValue(model.startFrame());
    m_endSpin->setValue(std::max(model.startFrame(), model.endFrame()));
    m_rangeModeCombo->setCurrentIndex(0);
    m_startSpin->setEnabled(false);
    m_endSpin->setEnabled(false);

    updateFpsControls();

    QString prefix = m_doc->name.trimmed();
    prefix.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9_-]")), QStringLiteral("_"));
    if (prefix.isEmpty())
        prefix = QStringLiteral("frame");
    m_prefixEdit->setText(prefix);
    updateNamePreview();

    onMp4QualityChanged();
}

void ExportAnimationDialog::updateFpsControls()
{
    const double docFps = m_doc->animation.fps();
    int matchIdx = -1;
    for (int i = 0; i < m_fpsCombo->count(); ++i) {
        const double v = m_fpsCombo->itemData(i).toDouble();
        if (v > 0 && std::abs(v - docFps) < 0.005) {
            matchIdx = i;
            break;
        }
    }
    if (matchIdx >= 0) {
        m_fpsCombo->setCurrentIndex(matchIdx);
        m_fpsCustomSpin->setVisible(false);
    } else {
        const int customIdx = m_fpsCombo->findData(-1.0);
        m_fpsCombo->setCurrentIndex(customIdx);
        m_fpsCustomSpin->setValue(docFps);
        m_fpsCustomSpin->setVisible(true);
    }
}

double ExportAnimationDialog::currentFps() const
{
    const double data = m_fpsCombo->currentData().toDouble();
    if (data > 0)
        return data;
    return m_fpsCustomSpin->value();
}

QSize ExportAnimationDialog::currentTargetSize() const
{
    if (m_originalSizeCheck->isChecked())
        return canvasSize();
    return QSize(m_widthSpin->value(), m_heightSpin->value());
}

// ── Slots: format / output ───────────────────────────────────────────────────

void ExportAnimationDialog::onFormatChanged()
{
    if (m_updating)
        return;
    updateFormatVisibility();
    updateNamePreview();
    updateSummary();
    updatePreview();
}

void ExportAnimationDialog::updateFormatVisibility()
{
    const anim::ExportFormat fmt = currentFormat();
    const bool seq = anim::isSequenceFormat(fmt);
    const bool alpha = anim::formatSupportsAlpha(fmt);
    const bool needsFfmpeg = (fmt == anim::ExportFormat::Mp4
                              || fmt == anim::ExportFormat::Gif);

    m_tabs->setTabVisible(1, !seq);   // Video / GIF — hidden for sequences
    m_tabs->setTabVisible(2, seq);    // Image Sequence — hidden for video/GIF
    if ((seq && m_tabs->currentIndex() == 1)
        || (!seq && m_tabs->currentIndex() == 2))
        m_tabs->setCurrentIndex(0);

    if (m_mp4Group)
        m_mp4Group->setVisible(fmt == anim::ExportFormat::Mp4);
    if (m_gifGroup)
        m_gifGroup->setVisible(fmt == anim::ExportFormat::Gif);
    if (m_pngRow)
        m_pngRow->setVisible(fmt == anim::ExportFormat::PngSequence);
    if (m_jpegRow)
        m_jpegRow->setVisible(fmt == anim::ExportFormat::JpegSequence
                              || fmt == anim::ExportFormat::WebpSequence);

    m_transparencyCheck->setEnabled(alpha);
    if (!alpha)
        m_transparencyCheck->setChecked(false);

    m_outputLabel->setText(seq ? tr("Output folder:") : tr("Output file:"));
    m_ffmpegWarning->setVisible(needsFfmpeg && !m_ffmpegAvailable);
}

void ExportAnimationDialog::onBrowseOutput()
{
    const anim::ExportFormat fmt = currentFormat();
    QSettings s;
    const QString lastDir = s.value(kLastDirKey, QDir::homePath()).toString();

    if (anim::isSequenceFormat(fmt)) {
        const QString dir = QFileDialog::getExistingDirectory(
            this, tr("Choose Output Folder"), lastDir);
        if (dir.isEmpty())
            return;
        m_outputEdit->setText(dir);
    } else {
        const QString ext = anim::extensionForFormat(fmt);
        const QString filter = (fmt == anim::ExportFormat::Mp4)
            ? tr("MP4 Video (*.mp4)") : tr("GIF Image (*.gif)");
        QString path = QFileDialog::getSaveFileName(
            this, tr("Export Animation"), lastDir, filter, nullptr,
            QFileDialog::DontConfirmOverwrite);
        if (path.isEmpty())
            return;
        if (QFileInfo(path).suffix().compare(ext, Qt::CaseInsensitive) != 0)
            path += QStringLiteral(".%1").arg(ext);
        m_outputEdit->setText(path);
    }
    updateSummary();
}

// ── Slots: range ─────────────────────────────────────────────────────────────

void ExportAnimationDialog::onRangeModeChanged()
{
    const int mode = m_rangeModeCombo->currentData().toInt();
    const anim::AnimationModel& model = m_doc->animation;
    const int docStart = model.startFrame();
    const int docEnd = std::max(model.startFrame(), model.endFrame());

    const bool prev = m_updating;
    m_updating = true;
    const bool custom = (mode == 2);
    m_startSpin->setEnabled(custom);
    m_endSpin->setEnabled(custom);
    if (mode == 0) {
        m_startSpin->setValue(docStart);
        m_endSpin->setValue(docEnd);
    } else if (mode == 1) {
        m_startSpin->setValue(std::clamp(model.playbackStart(), docStart, docEnd));
        m_endSpin->setValue(std::clamp(model.playbackEnd(), docStart, docEnd));
    }
    m_updating = prev;
    updateRangeInfo();
    updateSummary();
}

void ExportAnimationDialog::onRangeSpinChanged()
{
    if (m_updating)
        return;
    m_updating = true;
    if (m_startSpin->value() > m_endSpin->value()) {
        if (sender() == m_startSpin)
            m_endSpin->setValue(m_startSpin->value());
        else
            m_startSpin->setValue(m_endSpin->value());
    }
    m_updating = false;
    updateRangeInfo();
    updateSummary();
}

void ExportAnimationDialog::updateRangeInfo()
{
    const int total = m_endSpin->value() - m_startSpin->value() + 1;
    const double fps = currentFps();
    m_totalFramesLabel->setText(tr("%n frame(s)", "", std::max(0, total)));
    if (fps > 0 && total > 0)
        m_durationLabel->setText(tr("%1 s").arg(total / fps, 0, 'f', 2));
    else
        m_durationLabel->setText(QStringLiteral("—"));
}

void ExportAnimationDialog::onFpsChanged()
{
    const bool custom = (m_fpsCombo->currentData().toDouble() < 0);
    m_fpsCustomSpin->setVisible(custom);
    if (m_updating)
        return;
    const double docFps = m_doc->animation.fps();
    const bool mismatch = std::abs(currentFps() - docFps) > 0.005;
    if (mismatch) {
        m_fpsMismatchLabel->setText(
            tr("Export FPS (%1) differs from the timeline FPS (%2); the frames "
               "are resampled to the new rate.")
                .arg(currentFps(), 0, 'g', 6).arg(docFps, 0, 'g', 6));
        m_fpsMismatchLabel->show();
    } else {
        m_fpsMismatchLabel->hide();
    }
    updateRangeInfo();
    updateSummary();
}

// ── Slots: size ──────────────────────────────────────────────────────────────

void ExportAnimationDialog::onOriginalSizeToggled(bool on)
{
    m_widthSpin->setEnabled(!on);
    m_heightSpin->setEnabled(!on);
    m_scaleSpin->setEnabled(!on);
    m_aspectCombo->setEnabled(!on);
    m_constrainBtn->setEnabled(!on);
    if (on) {
        const bool prev = m_updating;
        m_updating = true;
        const QSize canvas = canvasSize();
        m_widthSpin->setValue(canvas.width());
        m_heightSpin->setValue(canvas.height());
        m_scaleSpin->setValue(100.0);
        m_aspectCombo->setCurrentIndex(0);
        m_updating = prev;
    }
    if (!m_updating) {
        updateSummary();
        updatePreview();
    }
}

void ExportAnimationDialog::setSize(int w, int h)
{
    const bool prev = m_updating;
    m_updating = true;
    m_widthSpin->setValue(std::clamp(w, 1, 16384));
    m_heightSpin->setValue(std::clamp(h, 1, 16384));
    const QSize canvas = canvasSize();
    if (canvas.width() > 0)
        m_scaleSpin->setValue(m_widthSpin->value() * 100.0 / canvas.width());
    m_updating = prev;
}

void ExportAnimationDialog::onWidthChanged()
{
    if (m_updating)
        return;
    if (m_constrainBtn->isChecked()) {
        const double r = [this]() {
            const double d = m_aspectCombo->currentData().toDouble();
            if (d > 0) return d;
            const QSize c = canvasSize();
            return c.height() > 0 ? double(c.width()) / c.height() : 1.0;
        }();
        m_updating = true;
        m_heightSpin->setValue(std::max(1, int(std::round(m_widthSpin->value() / r))));
        m_updating = false;
    }
    const QSize canvas = canvasSize();
    if (canvas.width() > 0) {
        m_updating = true;
        m_scaleSpin->setValue(m_widthSpin->value() * 100.0 / canvas.width());
        m_updating = false;
    }
    updateSummary();
    updatePreview();
}

void ExportAnimationDialog::onHeightChanged()
{
    if (m_updating)
        return;
    if (m_constrainBtn->isChecked()) {
        const double r = [this]() {
            const double d = m_aspectCombo->currentData().toDouble();
            if (d > 0) return d;
            const QSize c = canvasSize();
            return c.height() > 0 ? double(c.width()) / c.height() : 1.0;
        }();
        m_updating = true;
        m_widthSpin->setValue(std::max(1, int(std::round(m_heightSpin->value() * r))));
        m_updating = false;
    }
    updateSummary();
    updatePreview();
}

void ExportAnimationDialog::onScaleChanged()
{
    if (m_updating)
        return;
    const QSize canvas = canvasSize();
    const double s = m_scaleSpin->value() / 100.0;
    m_updating = true;
    m_widthSpin->setValue(std::max(1, int(std::round(canvas.width() * s))));
    m_heightSpin->setValue(std::max(1, int(std::round(canvas.height() * s))));
    m_updating = false;
    updateSummary();
    updatePreview();
}

void ExportAnimationDialog::onAspectChanged()
{
    if (m_updating)
        return;
    const double r = m_aspectCombo->currentData().toDouble();
    if (r > 0) {
        m_constrainBtn->setChecked(true);
        m_updating = true;
        m_heightSpin->setValue(std::max(1, int(std::round(m_widthSpin->value() / r))));
        m_updating = false;
    } else if (r == 0.0) {
        // Original: restore the canvas ratio from the current width.
        m_constrainBtn->setChecked(true);
        const QSize c = canvasSize();
        if (c.width() > 0) {
            m_updating = true;
            m_heightSpin->setValue(std::max(1,
                int(std::round(m_widthSpin->value() * double(c.height()) / c.width()))));
            m_updating = false;
        }
    }
    updateSummary();
    updatePreview();
}

void ExportAnimationDialog::onPickBackground()
{
    const QColor picked = QColorDialog::getColor(
        m_backgroundColor, this, tr("Background Color"));
    if (!picked.isValid())
        return;
    m_backgroundColor = picked;
    updateBackgroundSwatch();
    updateSummary();
    updatePreview();
}

void ExportAnimationDialog::updateBackgroundSwatch()
{
    QString border = QStringLiteral("#000000");
    int radius = 2;
    if (auto* t = ThemeManager::instance()->current()) {
        border = t->colorBorder.name();
        radius = t->radiusSM;
    }
    // A themed colour swatch, mirroring the theme's QPushButton#bgColorBtn look.
    m_bgColorBtn->setStyleSheet(
        QStringLiteral("background:%1; border:1px solid %2; border-radius:%3px;")
            .arg(m_backgroundColor.name(), border)
            .arg(radius));
}

// ── Slots: video / gif ───────────────────────────────────────────────────────

void ExportAnimationDialog::onMp4QualityChanged()
{
    const int crf = m_mp4QualityCombo->currentData().toInt();
    const bool custom = (crf < 0);
    m_crfSpin->setEnabled(custom);
    if (!custom) {
        const bool prev = m_updating;
        m_updating = true;
        m_crfSpin->setValue(crf);
        m_updating = prev;
    }
    if (!m_updating)
        updateSummary();
}

void ExportAnimationDialog::onGifLoopChanged()
{
    const int mode = m_gifLoopCombo->currentData().toInt();
    m_gifLoopSpin->setVisible(mode > 0);
    if (!m_updating)
        updateSummary();
}

// ── Naming / summary / preview ───────────────────────────────────────────────

void ExportAnimationDialog::updateNamePreview()
{
    if (!anim::isSequenceFormat(currentFormat())) {
        m_namePreviewLabel->clear();
        return;
    }
    const QString ext = anim::extensionForFormat(currentFormat());
    const QString prefix = m_prefixEdit->text().isEmpty()
        ? QStringLiteral("frame") : m_prefixEdit->text();
    const int pad = m_paddingSpin->value();
    const int start = m_startSpin->value();
    QStringList lines;
    for (int i = 0; i < 3; ++i) {
        const QString num = QString::number(start + i).rightJustified(pad, QLatin1Char('0'));
        lines << QStringLiteral("%1_%2.%3").arg(prefix, num, ext);
    }
    m_namePreviewLabel->setText(lines.join(QLatin1Char('\n')));
}

void ExportAnimationDialog::updateSummary()
{
    if (m_updating)
        return;

    const anim::ExportFormat fmt = currentFormat();
    const QSize size = currentTargetSize();
    const double fps = currentFps();
    const int total = m_endSpin->value() - m_startSpin->value() + 1;
    const bool alpha = anim::formatSupportsAlpha(fmt) && m_transparencyCheck->isChecked();

    const QSize canvas = canvasSize();
    const double outAr = size.height() > 0 ? double(size.width()) / size.height() : 0;
    const double canvasAr = canvas.height() > 0 ? double(canvas.width()) / canvas.height() : 0;
    const bool arMismatch = std::abs(outAr - canvasAr) > 0.01;
    m_aspectWarnLabel->setVisible(arMismatch && !m_originalSizeCheck->isChecked());
    if (m_aspectWarnLabel->isVisible())
        m_aspectWarnLabel->setText(
            tr("Output aspect ratio differs from the canvas — %1 will be applied.")
                .arg(m_fitCombo->currentText()));

    QString estSize = QStringLiteral("—");
    if (anim::isSequenceFormat(fmt) && total > 0 && !m_canvasFrame.isNull()) {
        ImageSaveOptions so;
        so.quality = m_jpegQualitySlider->value();
        so.compressionLevel = m_pngCompressionSlider->value();
        so.preserveAlpha = alpha;
        const QImage frame = m_canvasFrame.scaled(size, Qt::IgnoreAspectRatio,
                                                  Qt::SmoothTransformation);
        const qint64 per = estimateEncodedSize(frame, anim::extensionForFormat(fmt), so);
        if (per > 0)
            estSize = humanFileSize(per * total);
    }

    auto row = [](const QString& k, const QString& v) {
        return QStringLiteral("<b>%1</b> %2<br>").arg(k, v);
    };
    QString html;
    html += row(tr("Format:"), m_formatCombo->currentText());
    html += row(tr("Resolution:"), tr("%1 × %2").arg(size.width()).arg(size.height()));
    html += row(tr("Aspect:"), outAr > 0 ? QString::number(outAr, 'f', 3) : QStringLiteral("—"));
    html += row(tr("FPS:"), QString::number(fps, 'g', 6));
    html += row(tr("Range:"), tr("%1 – %2").arg(m_startSpin->value()).arg(m_endSpin->value()));
    html += row(tr("Frames:"), QString::number(std::max(0, total)));
    html += row(tr("Duration:"),
                fps > 0 ? tr("%1 s").arg(total / fps, 0, 'f', 2) : QStringLiteral("—"));
    html += row(tr("Transparency:"), alpha ? tr("Yes") : tr("No"));
    html += row(tr("Est. size:"), estSize);
    m_summaryLabel->setText(html);
}

void ExportAnimationDialog::updatePreview()
{
    if (m_updating || !m_previewLabel)
        return;
    const QSize labelSize = m_previewLabel->contentsRect().size() - QSize(6, 6);
    if (labelSize.width() <= 0 || labelSize.height() <= 0)
        return;

    const anim::ExportFormat fmt = currentFormat();
    const QSize target = currentTargetSize();
    if (target.isEmpty())
        return;
    const bool alpha = anim::formatSupportsAlpha(fmt) && m_transparencyCheck->isChecked();

    // A small canvas at the OUTPUT aspect ratio, so crop / letterbox shows.
    const QSize preview = target.scaled(labelSize, Qt::KeepAspectRatio);
    QImage img(preview, QImage::Format_RGBA8888);
    img.fill(alpha ? QColor(Qt::transparent) : m_backgroundColor);

    if (!m_canvasFrame.isNull()) {
        const QSize canvas = m_canvasFrame.size();
        const auto fit = static_cast<anim::FitMode>(m_fitCombo->currentData().toInt());
        QRectF dst;
        switch (fit) {
            case anim::FitMode::Stretch:
                dst = QRectF(0, 0, preview.width(), preview.height());
                break;
            case anim::FitMode::Fit: {
                const QSize s = canvas.scaled(preview, Qt::KeepAspectRatio);
                dst = QRectF((preview.width() - s.width()) / 2.0,
                             (preview.height() - s.height()) / 2.0, s.width(), s.height());
                break;
            }
            case anim::FitMode::Fill: {
                const QSize s = canvas.scaled(preview, Qt::KeepAspectRatioByExpanding);
                dst = QRectF((preview.width() - s.width()) / 2.0,
                             (preview.height() - s.height()) / 2.0, s.width(), s.height());
                break;
            }
        }
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        p.drawImage(dst, m_canvasFrame);
        p.end();
    }
    m_previewLabel->setPixmap(QPixmap::fromImage(img));
}

// ── Request / accept ─────────────────────────────────────────────────────────

void ExportAnimationDialog::buildRequest()
{
    const anim::ExportFormat fmt = currentFormat();
    anim::AnimationExportRequest r;
    r.format = fmt;
    r.startFrame = m_startSpin->value();
    r.endFrame = m_endSpin->value();
    r.fps = currentFps();
    r.targetSize = currentTargetSize();
    r.fitMode = static_cast<anim::FitMode>(m_fitCombo->currentData().toInt());
    r.resampleMode = (m_interpCombo->currentData().toInt() == 0)
        ? Qt::FastTransformation : Qt::SmoothTransformation;
    r.backgroundColor = m_backgroundColor;
    r.transparency = anim::formatSupportsAlpha(fmt) && m_transparencyCheck->isChecked();

    if (anim::isSequenceFormat(fmt)) {
        r.outputDirectory = m_outputEdit->text().trimmed();
        r.fileNamePrefix = m_prefixEdit->text().trimmed().isEmpty()
            ? QStringLiteral("frame") : m_prefixEdit->text().trimmed();
        r.framePadding = m_paddingSpin->value();
        r.imageOptions.quality = m_jpegQualitySlider->value();
        r.imageOptions.compression = m_pngCompressionSlider->value();
        r.imageOptions.preserveAlpha = r.transparency;
    } else {
        r.outputFile = m_outputEdit->text().trimmed();
    }

    if (fmt == anim::ExportFormat::Mp4) {
        r.videoCodec = m_codecCombo->currentData().toString();
        r.crf = m_crfSpin->value();
        r.encoderPreset = m_encoderPresetCombo->currentData().toString();
        r.pixelFormat = m_pixFmtCombo->currentData().toString();
        r.hardwareEncoder = m_hwCheck->isChecked();
    } else if (fmt == anim::ExportFormat::Gif) {
        r.gifColors = m_gifColorsSpin->value();
        r.gifDither = m_gifDitherCheck->isChecked();
        const int loopMode = m_gifLoopCombo->currentData().toInt();
        r.gifLoop = (loopMode > 0) ? m_gifLoopSpin->value() : loopMode;
    }
    m_request = r;
}

void ExportAnimationDialog::accept()
{
    buildRequest();
    const anim::ExportFormat fmt = currentFormat();
    const bool seq = anim::isSequenceFormat(fmt);

    const QString destination = seq ? m_request.outputDirectory : m_request.outputFile;
    if (destination.isEmpty()) {
        QMessageBox::warning(this, tr("Export Animation"),
            seq ? tr("Choose an output folder first.")
                : tr("Choose an output file first."));
        return;
    }
    if ((fmt == anim::ExportFormat::Mp4 || fmt == anim::ExportFormat::Gif)
        && !m_ffmpegAvailable) {
        QMessageBox::warning(this, tr("Export Animation"),
            tr("FFmpeg is required to export MP4 or GIF, but it was not found. "
               "Install FFmpeg and make sure it is on your PATH (or set the "
               "HAZOR_FFMPEG environment variable)."));
        return;
    }

    // Overwrite confirmation for single-file formats.
    if (!seq && QFile::exists(destination)) {
        const auto ret = QMessageBox::question(this, tr("Confirm Overwrite"),
            tr("%1 already exists.\nDo you want to replace it?")
                .arg(QFileInfo(destination).fileName()),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (ret != QMessageBox::Yes)
            return;
        m_request.overwriteExisting = true;
    }

    QSettings s;
    s.setValue(kLastDirKey, seq
        ? m_request.outputDirectory
        : QFileInfo(m_request.outputFile).absolutePath());

    QDialog::accept();
}

void ExportAnimationDialog::onResetClicked()
{
    m_updating = true;
    m_presetTree->clearSelection();
    m_formatCombo->setCurrentIndex(0);
    m_rangeModeCombo->setCurrentIndex(0);
    m_fitCombo->setCurrentIndex(0);
    m_interpCombo->setCurrentIndex(1);
    m_transparencyCheck->setChecked(true);
    m_mp4QualityCombo->setCurrentIndex(2);
    m_gifColorsSpin->setValue(256);
    m_gifDitherCheck->setChecked(true);
    m_gifLoopCombo->setCurrentIndex(0);
    seedFromDocument();
    onRangeModeChanged();
    m_updating = false;

    onMp4QualityChanged();
    updateFormatVisibility();
    updateRangeInfo();
    updateSummary();
    updatePreview();
}
