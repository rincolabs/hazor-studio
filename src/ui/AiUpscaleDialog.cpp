#include "ui/AiUpscaleDialog.hpp"

#include "ai/upscale/RealEsrganProcessBackend.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AppCheckBox.hpp"
#include "ui/AppComboBox.hpp"

#include <QButtonGroup>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QSettings>
#include <QVBoxLayout>

AiUpscaleDialog::AiUpscaleDialog(const UpscaleBackendStatus& backendStatus,
                                 UpscaleTarget initialTarget,
                                 QWidget* parent)
    : QDialog(parent)
    , m_backendStatus(backendStatus)
{
    setWindowTitle(tr("AI Upscale"));
    setModal(true);
    setMinimumWidth(420);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 14, 14, 14);
    root->setSpacing(10);

    auto* targetBox = new QWidget(this);
    auto* targetLayout = new QVBoxLayout(targetBox);
    targetLayout->setContentsMargins(0, 0, 0, 0);
    targetLayout->setSpacing(4);
    targetLayout->addWidget(new QLabel(tr("Target"), targetBox));
    m_layerTarget = new QRadioButton(tr("Current Layer"), targetBox);
    m_documentTarget = new QRadioButton(tr("Entire Document"), targetBox);
    targetLayout->addWidget(m_layerTarget);
    targetLayout->addWidget(m_documentTarget);
    root->addWidget(targetBox);

    auto* outputBox = new QWidget(this);
    auto* outputLayout = new QVBoxLayout(outputBox);
    outputLayout->setContentsMargins(0, 0, 0, 0);
    outputLayout->setSpacing(4);
    outputLayout->addWidget(new QLabel(tr("Output"), outputBox));
    m_newLayer = new QRadioButton(tr("Create new upscaled layer"), outputBox);
    m_replaceLayer = new QRadioButton(tr("Replace current layer"), outputBox);
    m_newDocument = new QRadioButton(tr("Create new document"), outputBox);
    m_replaceDocument = new QRadioButton(tr("Replace current document / flattened"), outputBox);
    outputLayout->addWidget(m_newLayer);
    outputLayout->addWidget(m_replaceLayer);
    outputLayout->addWidget(m_newDocument);
    outputLayout->addWidget(m_replaceDocument);
    root->addWidget(outputBox);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 0, 0, 0);
    form->setSpacing(8);

    m_modelCombo = new AppComboBox(this);
    populateModels();
    form->addRow(tr("Model:"), m_modelCombo);

    m_scaleCombo = new AppComboBox(this);
    m_scaleCombo->addItem(tr("4x"), 4);
    m_scaleCombo->setCurrentIndex(0);
    form->addRow(tr("Scale:"), m_scaleCombo);

    m_tileCombo = new AppComboBox(this);
    m_tileCombo->addItem(tr("Auto"), 0);
    m_tileCombo->addItem(QStringLiteral("256"), 256);
    m_tileCombo->addItem(QStringLiteral("512"), 512);
    m_tileCombo->addItem(QStringLiteral("768"), 768);
    form->addRow(tr("Tile Size:"), m_tileCombo);
    root->addLayout(form);

    auto* optionsBox = new QWidget(this);
    auto* optionsLayout = new QVBoxLayout(optionsBox);
    optionsLayout->setContentsMargins(0, 0, 0, 0);
    optionsLayout->setSpacing(4);
    optionsLayout->addWidget(new QLabel(tr("Options"), optionsBox));
    m_preserveAlpha = new AppCheckBox();
    m_preserveAlpha->setText(tr("Preserve alpha"));
    m_preserveAlpha->setChecked(true);
    m_preserveMask = new AppCheckBox();
    m_preserveMask->setText(tr("Preserve layer mask"));
    m_preserveMask->setChecked(true);
    m_preserveProfile = new AppCheckBox();
    m_preserveProfile->setText(tr("Preserve color profile"));
    m_preserveProfile->setChecked(true);
    optionsLayout->addWidget(m_preserveAlpha);
    optionsLayout->addWidget(m_preserveMask);
    optionsLayout->addWidget(m_preserveProfile);
    root->addWidget(optionsBox);

    auto* backendBox = new QWidget(this);
    auto* backendLayout = new QVBoxLayout(backendBox);
    backendLayout->setContentsMargins(0, 0, 0, 0);
    backendLayout->setSpacing(4);
    backendLayout->addWidget(new QLabel(tr("Backend"), backendBox));
    m_backendName = new QLabel(tr("Real-ESRGAN ncnn Vulkan"), backendBox);
    m_backendStatusLabel = new QLabel(backendStatus.userMessage, backendBox);
    m_detailLabel = new QLabel(backendStatus.technicalDetails, backendBox);
    m_detailLabel->setWordWrap(true);
    backendLayout->addWidget(m_backendName);
    backendLayout->addWidget(m_backendStatusLabel);
    backendLayout->addWidget(m_detailLabel);

    auto* backendActions = new QHBoxLayout;
    m_openModelsButton = new QPushButton(tr("Open AI Model Manager"), backendBox);
    m_locateBackendButton = new QPushButton(tr("Locate Backend"), backendBox);
    backendActions->addWidget(m_openModelsButton);
    backendActions->addWidget(m_locateBackendButton);
    backendLayout->addLayout(backendActions);
    root->addWidget(backendBox);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Cancel, this);
    m_upscaleButton = buttons->addButton(tr("Upscale"), QDialogButtonBox::AcceptRole);
    root->addWidget(buttons);

    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect(m_upscaleButton, &QPushButton::clicked, this, &QDialog::accept);
    connect(m_openModelsButton, &QPushButton::clicked,
            this, &AiUpscaleDialog::openModelManagerRequested);
    connect(m_locateBackendButton, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Locate Real-ESRGAN Backend"));
        if (path.isEmpty())
            return;
        QSettings().setValue(QStringLiteral("ai/upscale/realesrganExecutable"), path);
        RealEsrganProcessBackend backend;
        m_backendStatus = backend.probe();
        m_backendStatusLabel->setText(m_backendStatus.userMessage);
        m_detailLabel->setText(m_backendStatus.technicalDetails);
        updateUiState();
        emit backendPathChanged();
    });

    connect(m_layerTarget, &QRadioButton::toggled, this, &AiUpscaleDialog::updateUiState);
    connect(m_documentTarget, &QRadioButton::toggled, this, &AiUpscaleDialog::updateUiState);
    connect(m_replaceLayer, &QRadioButton::toggled, this, &AiUpscaleDialog::updateUiState);
    connect(m_modelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AiUpscaleDialog::updateUiState);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &AiUpscaleDialog::applyTheme);

    if (initialTarget == UpscaleTarget::CurrentLayer)
        m_layerTarget->setChecked(true);
    else
        m_documentTarget->setChecked(true);
    m_newLayer->setChecked(initialTarget == UpscaleTarget::CurrentLayer);
    m_newDocument->setChecked(initialTarget == UpscaleTarget::CurrentDocument);

    updateUiState();
    applyTheme();
}

void AiUpscaleDialog::populateModels()
{
    m_modelCombo->clear();
    const QList<AiModelDescriptor> models =
        AiModelRegistry::instance()->installedModelsForTask(QStringLiteral("upscale"));
    for (const AiModelDescriptor& model : models)
        m_modelCombo->addItem(model.displayName.isEmpty() ? model.id : model.displayName, model.id);

    const QString packagedDir = RealEsrganProcessBackend::packagedModelsDirectory();
    const auto addPackaged = [this, packagedDir](const QString& id, const QString& name) {
        if (m_modelCombo->findData(id) >= 0)
            return;
        for (const QString& file : RealEsrganProcessBackend::expectedModelFiles(id)) {
            if (!QFileInfo::exists(QDir(packagedDir).filePath(file)))
                return;
        }
        m_modelCombo->addItem(name, id);
    };
    addPackaged(QStringLiteral("realesrgan-x4plus"), tr("Real-ESRGAN x4 Plus"));
    addPackaged(QStringLiteral("realesrgan-x4plus-anime"), tr("Real-ESRGAN x4 Plus Anime"));
    addPackaged(QStringLiteral("realesr-animevideov3"), tr("Real-ESR AnimeVideo v3"));

    if (m_modelCombo->count() == 0)
        m_modelCombo->addItem(tr("No installed upscale models"), QString());
}

UpscaleOptions AiUpscaleDialog::options() const
{
    UpscaleOptions opts;
    opts.modelId = m_modelCombo->currentData().toString();
    opts.scale = m_scaleCombo->currentData().toInt();
    opts.tileSize = m_tileCombo->currentData().toInt();
    opts.preserveAlpha = m_preserveAlpha->isChecked();
    opts.preserveLayerMask = m_preserveMask->isChecked();
    opts.preserveColorProfile = m_preserveProfile->isChecked();
    opts.target = m_layerTarget->isChecked()
        ? UpscaleTarget::CurrentLayer : UpscaleTarget::CurrentDocument;
    if (opts.target == UpscaleTarget::CurrentLayer)
        opts.output = m_replaceLayer->isChecked()
            ? UpscaleOutputMode::ReplaceLayer : UpscaleOutputMode::NewLayer;
    else
        opts.output = m_replaceDocument->isChecked()
            ? UpscaleOutputMode::ReplaceDocument : UpscaleOutputMode::NewDocument;
    return opts;
}

void AiUpscaleDialog::updateUiState()
{
    const bool layer = m_layerTarget->isChecked();
    m_newLayer->setEnabled(layer);
    m_replaceLayer->setEnabled(layer);
    m_newDocument->setEnabled(!layer);
    m_replaceDocument->setEnabled(false);
    m_replaceDocument->setToolTip(tr("Replace Document is disabled until full document snapshots are available."));
    if (layer && !m_newLayer->isChecked() && !m_replaceLayer->isChecked())
        m_newLayer->setChecked(true);
    if (!layer && !m_newDocument->isChecked())
        m_newDocument->setChecked(true);

    const bool hasModel = !m_modelCombo->currentData().toString().isEmpty();
    const bool available = m_backendStatus.available();
    m_upscaleButton->setEnabled(available && hasModel);
    m_openModelsButton->setVisible(!hasModel);
    m_locateBackendButton->setVisible(!available);
    m_preserveMask->setEnabled(layer);
}

void AiUpscaleDialog::applyTheme()
{
    const auto* t = ThemeManager::instance()->current();
    if (!t)
        return;
    setStyleSheet(QStringLiteral(R"(
        QDialog {
            background: %1;
            color: %2;
            font-family: "%3";
            font-size: %4px;
        }
        QLabel {
            color: %2;
        }
        QLabel:first-child {
            color: %5;
        }
        QPushButton {
            background: %6;
            color: %2;
            border: 1px solid %7;
            border-radius: %8px;
            padding: 5px 10px;
        }
        QPushButton:hover {
            background: %9;
        }
        QPushButton:disabled {
            color: %10;
            background: %6;
        }
    )")
        .arg(t->colorSurface.name())
        .arg(t->colorTextPrimary.name())
        .arg(t->fontFamily)
        .arg(t->fontSizeMD)
        .arg(t->colorTextSecondary.name())
        .arg(t->colorSurface.name())
        .arg(t->colorBorder.name())
        .arg(t->radiusSM)
        .arg(t->colorSurfaceHover.name())
        .arg(t->colorTextDisabled.name()));
}
