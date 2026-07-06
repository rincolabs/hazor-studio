#include "AiSettingsPage.hpp"
#include "AppSettingsMetrics.hpp"
#include "AppCheckBox.hpp"
#include "AppComboBox.hpp"

#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/runtime/AiExecutionProvider.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/models/AiModelDescriptor.hpp"
#include "ai/models/AiModelDownloadManager.hpp"
#include "ai/compat/AiCompatibilityManager.hpp"
#include "ai/AiJobRunner.hpp"

#include "core/AppPaths.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QLabel>
#include <QSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QProgressBar>
#include <QFrame>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QSignalBlocker>
#include <QByteArray>

namespace {

constexpr const char* kThemeColorRole = "themeColorRole";
constexpr const char* kThemeRoleSecondary = "secondary";
constexpr const char* kThemeRoleWarning = "warning";

QString labelStyleForRole(const Theme* th, const QByteArray& role)
{
    QColor color = th->colorTextSecondary;
    if (role == kThemeRoleWarning)
        color = th->colorWarning;
    return QStringLiteral("color: %1;").arg(color.name());
}

void markThemeLabel(QLabel* label, const char* role)
{
    if (!label)
        return;
    label->setProperty(kThemeColorRole, role);
    label->setStyleSheet(labelStyleForRole(ThemeManager::instance()->current(), QByteArray(role)));
}

void applyMarkedLabelTheme(QWidget* root, const Theme* th)
{
    for (auto* label : root->findChildren<QLabel*>()) {
        const QByteArray role = label->property(kThemeColorRole).toByteArray();
        if (!role.isEmpty())
            label->setStyleSheet(labelStyleForRole(th, role));
    }
}

QString humanSize(qint64 bytes)
{
    if (bytes <= 0) return QStringLiteral("—");
    return QLocale().formattedDataSize(bytes, 1, QLocale::DataSizeTraditionalFormat);
}

QString qualityLabel(const QString& quality)
{
    const QString q = quality.trimmed().toLower();
    if (q == QLatin1String("fast") || q == QLatin1String("compatible"))
        return QObject::tr("Fast");
    if (q == QLatin1String("balanced") || q == QLatin1String("recommended"))
        return QObject::tr("Balanced");
    if (q == QLatin1String("high"))
        return QObject::tr("High");
    if (q == QLatin1String("professional"))
        return QObject::tr("Professional");
    if (q == QLatin1String("portrait"))
        return QObject::tr("Portrait");
    return quality;
}

} // namespace

AiSettingsPage::AiSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral(
        "AiSettingsPage { background: %1; }"
        "QScrollArea { background: %1; border: none; }"
        "QScrollArea > QWidget > QWidget { background: %1; }"
    ).arg(th->colorSurface.name()));

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto* scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    outer->addWidget(scroll);

    auto* content = new QWidget(scroll);
    auto* lay = new QVBoxLayout(content);
    lay->setContentsMargins(24, 16, 24, 16);
    lay->setSpacing(th->spaceMD);

    lay->addWidget(buildCompatibilityGroup());
    lay->addWidget(buildRuntimeGroup());
    lay->addWidget(buildProvidersGroup());
    lay->addWidget(buildPerformanceGroup());
    lay->addWidget(buildModelsGroup());
    lay->addWidget(buildDiagnosticsGroup());
    lay->addStretch();

    scroll->setWidget(content);

    // React to background state changes (downloads, registry, runtime status).
    auto* dl = AiModelDownloadManager::instance();
    connect(dl, &AiModelDownloadManager::started, this, &AiSettingsPage::onDownloadStarted);
    connect(dl, &AiModelDownloadManager::progress, this, &AiSettingsPage::onDownloadProgress);
    connect(dl, &AiModelDownloadManager::statusText, this, &AiSettingsPage::onDownloadStatus);
    connect(dl, &AiModelDownloadManager::finished, this, &AiSettingsPage::onDownloadFinished);
    connect(AiModelRegistry::instance(), &AiModelRegistry::installedModelsChanged,
            this, &AiSettingsPage::refreshModelList);
    connect(AiRuntimeManager::instance(), &AiRuntimeManager::runtimeStatusChanged,
            this, &AiSettingsPage::refreshDiagnostics);
    connect(AiCompatibilityManager::instance(), &AiCompatibilityManager::compatibilityChanged,
            this, &AiSettingsPage::refreshCompatibility);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &AiSettingsPage::applyTheme);

    loadSettings();
}

void AiSettingsPage::applyTheme()
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral(
        "AiSettingsPage { background: %1; }"
        "QScrollArea { background: %1; border: none; }"
        "QScrollArea > QWidget > QWidget { background: %1; }"
    ).arg(th->colorSurface.name()));
    applyMarkedLabelTheme(this, th);
    refreshProviderStatuses();
    refreshCompatibility();
    refreshModelList();
    refreshDiagnostics();
}

QWidget* AiSettingsPage::buildRuntimeGroup()
{
    auto* th = ThemeManager::instance()->current();
    auto* group = new QGroupBox(tr("AI Runtime"), this);
    auto* form = new QFormLayout(group);
    form->setSpacing(th->spaceSM);
    form->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_runtimeBanner = new QLabel(group);
    m_runtimeBanner->setWordWrap(true);
    markThemeLabel(m_runtimeBanner, kThemeRoleWarning);
    m_runtimeBanner->setVisible(false);
    form->addRow(m_runtimeBanner);

    m_enableCb = new AppCheckBox(tr("Enable AI features"), group);
    form->addRow(QString(), m_enableCb);

    m_providerCombo = new AppComboBox(group);
    // Populated dynamically with only the providers that are actually selectable
    // on this platform/runtime (spec §9) — see populateExecutionProviderCombo().
    populateExecutionProviderCombo();
    form->addRow(tr("Execution Provider:"), m_providerCombo);

    m_providerStatusLabel = new QLabel(group);
    m_providerStatusLabel->setTextFormat(Qt::RichText);
    markThemeLabel(m_providerStatusLabel, kThemeRoleSecondary);
    form->addRow(tr("Available providers:"), m_providerStatusLabel);

    auto* realesrganRow = new QWidget(group);
    auto* realesrganLay = new QHBoxLayout(realesrganRow);
    realesrganLay->setContentsMargins(0, 0, 0, 0);
    realesrganLay->setSpacing(th->spaceSM);
    m_realesrganPathLabel = new QLabel(realesrganRow);
    m_realesrganPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    markThemeLabel(m_realesrganPathLabel, kThemeRoleSecondary);
    realesrganLay->addWidget(m_realesrganPathLabel, 1);
    auto* locateRealEsrganBtn = new QPushButton(tr("Locate..."), realesrganRow);
    auto* clearRealEsrganBtn = new QPushButton(tr("Clear"), realesrganRow);
    realesrganLay->addWidget(locateRealEsrganBtn);
    realesrganLay->addWidget(clearRealEsrganBtn);
    form->addRow(tr("Real-ESRGAN executable:"), realesrganRow);
    connect(locateRealEsrganBtn, &QPushButton::clicked, this, [this]() {
        const QString path = QFileDialog::getOpenFileName(
            this, tr("Locate Real-ESRGAN Backend"), m_realesrganExePath);
        if (path.isEmpty())
            return;
        m_realesrganExePath = path;
        if (m_realesrganPathLabel)
            m_realesrganPathLabel->setText(path);
    });
    connect(clearRealEsrganBtn, &QPushButton::clicked, this, [this]() {
        m_realesrganExePath.clear();
        if (m_realesrganPathLabel)
            m_realesrganPathLabel->setText(tr("Automatic packaged backend"));
    });

    m_cpuFallbackCb = new AppCheckBox(tr("Allow CPU fallback when the selected provider fails"), group);
    m_fp16Cb        = new AppCheckBox(tr("Use FP16 when supported"), group);
    m_graphOptCb    = new AppCheckBox(tr("Enable ONNX graph optimizations"), group);
    m_memArenaCb    = new AppCheckBox(tr("Enable memory arena"), group);
    m_memPatternCb  = new AppCheckBox(tr("Enable memory pattern"), group);
    m_preloadCb     = new AppCheckBox(tr("Preload installed models on startup"), group);
    for (AppCheckBox* cb : {m_cpuFallbackCb, m_fp16Cb, m_graphOptCb, m_memArenaCb, m_memPatternCb, m_preloadCb})
        form->addRow(QString(), cb);

    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this]() { refreshProviderStatuses(); });
    connect(m_enableCb, &QCheckBox::toggled, this, [this]() { setControlsEnabledForRuntime(); });

    return group;
}

QWidget* AiSettingsPage::buildPerformanceGroup()
{
    auto* th = ThemeManager::instance()->current();
    auto* group = new QGroupBox(tr("Performance"), this);
    auto* form = new QFormLayout(group);
    form->setSpacing(th->spaceSM);
    form->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_cpuThreadsSpin = new QSpinBox(group);
    m_cpuThreadsSpin->setRange(0, 256);
    m_cpuThreadsSpin->setSpecialValueText(tr("Auto"));
    m_cpuThreadsSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("CPU threads:"), m_cpuThreadsSpin);

    m_concurrentSpin = new QSpinBox(group);
    m_concurrentSpin->setRange(1, 8);
    m_concurrentSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Max concurrent AI jobs:"), m_concurrentSpin);

    m_maxLoadedModelsSpin = new QSpinBox(group);
    m_maxLoadedModelsSpin->setRange(1, 8);
    m_maxLoadedModelsSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_maxLoadedModelsSpin->setToolTip(tr("How many ONNX models stay loaded in memory at once. "
                                         "The least-recently-used model is unloaded past this limit."));
    form->addRow(tr("Max loaded models:"), m_maxLoadedModelsSpin);

    m_gpuBudgetSpin = new QSpinBox(group);
    m_gpuBudgetSpin->setRange(0, 131072);
    m_gpuBudgetSpin->setSingleStep(256);
    m_gpuBudgetSpin->setSuffix(tr(" MB"));
    m_gpuBudgetSpin->setSpecialValueText(tr("Auto"));
    m_gpuBudgetSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("GPU memory budget:"), m_gpuBudgetSpin);

    m_cpuBudgetSpin = new QSpinBox(group);
    m_cpuBudgetSpin->setRange(0, 131072);
    m_cpuBudgetSpin->setSingleStep(256);
    m_cpuBudgetSpin->setSuffix(tr(" MB"));
    m_cpuBudgetSpin->setSpecialValueText(tr("Auto"));
    m_cpuBudgetSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("CPU memory budget:"), m_cpuBudgetSpin);

    m_embedCacheCb = new AppCheckBox(tr("Cache image embeddings per document"), group);
    form->addRow(tr("Embedding cache:"), m_embedCacheCb);

    m_maxDocsSpin = new QSpinBox(group);
    m_maxDocsSpin->setRange(0, 32);
    m_maxDocsSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Max cached documents:"), m_maxDocsSpin);

    m_maxEmbedMbSpin = new QSpinBox(group);
    m_maxEmbedMbSpin->setRange(0, 131072);
    m_maxEmbedMbSpin->setSingleStep(128);
    m_maxEmbedMbSpin->setSuffix(tr(" MB"));
    m_maxEmbedMbSpin->setSpecialValueText(tr("Auto"));
    m_maxEmbedMbSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Max cached embeddings memory:"), m_maxEmbedMbSpin);

    m_workingResCombo = new AppComboBox(group);
    m_workingResCombo->addItem(tr("Auto"), static_cast<int>(AiWorkingResolution::Auto));
    m_workingResCombo->addItem(tr("Preserve document resolution"), static_cast<int>(AiWorkingResolution::PreserveDocument));
    m_workingResCombo->addItem(tr("Limit longest side"), static_cast<int>(AiWorkingResolution::LimitLongestSide));
    form->addRow(tr("Working resolution:"), m_workingResCombo);

    m_workingResSideCombo = new AppComboBox(group);
    for (int side : {1024, 1536, 2048, 3072})
        m_workingResSideCombo->addItem(QStringLiteral("%1 px").arg(side), side);
    form->addRow(tr("Longest side limit:"), m_workingResSideCombo);

    auto* hint = new QLabel(tr("Limiting resolution improves performance and memory use, but can reduce precision on fine detail."), group);
    hint->setWordWrap(true);
    markThemeLabel(hint, kThemeRoleSecondary);
    form->addRow(QString(), hint);

    connect(m_workingResCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this]() {
        const bool limit = m_workingResCombo->currentData().toInt() == static_cast<int>(AiWorkingResolution::LimitLongestSide);
        m_workingResSideCombo->setEnabled(limit);
    });
    connect(m_embedCacheCb, &QCheckBox::toggled, this, [this](bool on) {
        m_maxDocsSpin->setEnabled(on);
        m_maxEmbedMbSpin->setEnabled(on);
    });

    return group;
}

QWidget* AiSettingsPage::buildModelsGroup()
{
    auto* th = ThemeManager::instance()->current();
    auto* group = new QGroupBox(tr("Models"), this);
    auto* lay = new QVBoxLayout(group);
    lay->setSpacing(th->spaceSM);
    lay->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    // Models directory / custom-folder row. Hidden for the first release: the app
    // ships and runs with the bundled models only. The widgets are still created
    // (kept valid for the directory label updates) but never shown.
    auto* dirRow = new QWidget(group);
    auto* dirLay = new QHBoxLayout(dirRow);
    dirLay->setContentsMargins(0, 0, 0, 0);
    dirLay->setSpacing(th->spaceSM);
    dirLay->addWidget(new QLabel(tr("Models directory:"), dirRow));
    m_modelsDirLabel = new QLabel(dirRow);
    markThemeLabel(m_modelsDirLabel, kThemeRoleSecondary);
    m_modelsDirLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    dirLay->addWidget(m_modelsDirLabel, 1);
    auto* changeBtn = new QPushButton(tr("Change…"), dirRow);
    auto* openBtn = new QPushButton(tr("Open Folder"), dirRow);
    dirLay->addWidget(changeBtn);
    dirLay->addWidget(openBtn);
    lay->addWidget(dirRow);
    connect(changeBtn, &QPushButton::clicked, this, &AiSettingsPage::onChangeModelsDir);
    connect(openBtn, &QPushButton::clicked, this, &AiSettingsPage::onOpenModelsFolder);
    dirRow->setVisible(false);

    auto* note = new QLabel(tr("Models are grouped by function. Object Selection / Segmenters (SAM) detect objects; "
                               "Matting / Refine Edge (BiRefNet, MODNet) improve hair, transparency and soft edges; "
                               "Background Removal (RMBG) creates a foreground mask directly."), group);
    note->setWordWrap(true);
    markThemeLabel(note, kThemeRoleSecondary);
    lay->addWidget(note);

    // Dynamic container: rebuilt on every refreshModelList() because discovery,
    // downloads and the user dropping a custom model can change the set at runtime.
    m_modelsContainer = new QWidget(group);
    m_modelsLayout = new QVBoxLayout(m_modelsContainer);
    m_modelsLayout->setContentsMargins(0, 0, 0, 0);
    m_modelsLayout->setSpacing(th->spaceSM);
    lay->addWidget(m_modelsContainer);

    return group;
}

void AiSettingsPage::refreshModelList()
{
    if (!m_modelsLayout)
        return;
    auto* th = ThemeManager::instance()->current();
    auto* registry = AiModelRegistry::instance();

    // Tear down the previous cards (and their live download-row pointers).
    m_rows.clear();
    QLayoutItem* item = nullptr;
    while ((item = m_modelsLayout->takeAt(0)) != nullptr) {
        if (QWidget* w = item->widget())
            w->deleteLater();
        delete item;
    }

    auto addSectionHeader = [&](const QString& text) {
        auto* hdr = new QLabel(text, m_modelsContainer);
        hdr->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; border: none; margin-top: %2px;")
                               .arg(th->colorTextBright.name()).arg(th->spaceSM));
        m_modelsLayout->addWidget(hdr);
    };

    // One section per function/task, listing only the models actually found on
    // disk. Manifest-based download listing is disabled for the first release:
    // no downloadable entries are shown and no download action is offered.
    for (const QString& task : AiModelTaxonomy::allTasks()) {
        // Temporarily hidden: inpainting model settings will be exposed later.
        // Backend/model support must remain implemented.
        if (task == QLatin1String("inpainting"))
            continue;

        QList<AiModelDescriptor> discovered;
        for (const AiModelDescriptor& d : registry->modelsByTask(task))
            discovered << d;

        if (discovered.isEmpty())
            continue;

        addSectionHeader(AiModelTaxonomy::taskDisplayName(task));
        for (const AiModelDescriptor& d : discovered)
            m_modelsLayout->addWidget(buildDiscoveredCard(d));
    }

    // Diagnostics: invalid models never break the app, but surface here.
    const QList<AiModelDescriptor> invalid = registry->invalidModels();
    if (!invalid.isEmpty()) {
        addSectionHeader(tr("Diagnostics — ignored / invalid models"));
        for (const AiModelDescriptor& d : invalid)
            m_modelsLayout->addWidget(buildInvalidCard(d));
    }
}

QWidget* AiSettingsPage::buildDiagnosticsGroup()
{
    auto* th = ThemeManager::instance()->current();
    auto* group = new QGroupBox(tr("Runtime diagnostics"), this);
    auto* lay = new QVBoxLayout(group);
    lay->setSpacing(th->spaceSM);
    lay->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_diagLabel = new QLabel(group);
    m_diagLabel->setTextFormat(Qt::RichText);
    m_diagLabel->setWordWrap(true);
    m_diagLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    markThemeLabel(m_diagLabel, kThemeRoleSecondary);
    lay->addWidget(m_diagLabel);

    auto* btnRow = new QWidget(group);
    auto* btnLay = new QHBoxLayout(btnRow);
    btnLay->setContentsMargins(0, 0, 0, 0);
    m_clearCacheBtn = new QPushButton(tr("Clear AI Cache"), btnRow);
    btnLay->addWidget(m_clearCacheBtn);
    btnLay->addStretch();
    lay->addWidget(btnRow);
    connect(m_clearCacheBtn, &QPushButton::clicked, this, &AiSettingsPage::onClearCache);

    return group;
}

void AiSettingsPage::loadSettings()
{
    const AiRuntimeSettings s = AiRuntimeManager::instance()->settings();

    m_enableCb->setChecked(s.enabled);

    int pIdx = m_providerCombo->findData(s.executionProvider);
    m_providerCombo->setCurrentIndex(pIdx >= 0 ? pIdx : 0);

    m_cpuFallbackCb->setChecked(s.allowCpuFallback);
    m_fp16Cb->setChecked(s.useFp16WhenAvailable);
    m_graphOptCb->setChecked(s.enableGraphOptimization);
    m_memArenaCb->setChecked(s.enableMemoryArena);
    m_memPatternCb->setChecked(s.enableMemoryPattern);
    m_preloadCb->setChecked(s.preloadModelsOnStartup);

    m_cpuThreadsSpin->setValue(s.cpuThreadCount);
    m_concurrentSpin->setValue(s.maxConcurrentAiJobs);
    m_maxLoadedModelsSpin->setValue(s.maxLoadedModels);
    m_gpuBudgetSpin->setValue(s.maxGpuMemoryMB);
    m_cpuBudgetSpin->setValue(s.maxCpuMemoryMB);
    m_embedCacheCb->setChecked(s.embeddingCacheEnabled);
    m_maxDocsSpin->setValue(s.maxCachedDocuments);
    m_maxEmbedMbSpin->setValue(s.maxCachedEmbeddingsMB);

    int wrIdx = m_workingResCombo->findData(static_cast<int>(s.workingResolution));
    m_workingResCombo->setCurrentIndex(wrIdx >= 0 ? wrIdx : 0);
    int sideIdx = m_workingResSideCombo->findData(s.workingResolutionLongestSide);
    m_workingResSideCombo->setCurrentIndex(sideIdx >= 0 ? sideIdx : 0);
    m_workingResSideCombo->setEnabled(s.workingResolution == AiWorkingResolution::LimitLongestSide);
    m_maxDocsSpin->setEnabled(s.embeddingCacheEnabled);
    m_maxEmbedMbSpin->setEnabled(s.embeddingCacheEnabled);

    m_modelsDir = s.resolvedModelsDirectory();
    m_modelsDirLabel->setText(m_modelsDir);
    QSettings settings;
    m_realesrganExePath = settings.value(QStringLiteral("ai/upscale/realesrganExecutable")).toString();
    if (m_realesrganPathLabel) {
        m_realesrganPathLabel->setText(m_realesrganExePath.isEmpty()
            ? tr("Automatic packaged backend") : m_realesrganExePath);
    }

    setControlsEnabledForRuntime();
    refreshProviderStatuses();
    refreshModelList();
    refreshDiagnostics();
    refreshCompatibility();
}

void AiSettingsPage::saveSettings()
{
    AiRuntimeSettings s = AiRuntimeManager::instance()->settings();

    s.enabled = m_enableCb->isChecked();
    s.executionProvider = m_providerCombo->currentData().toString();
    s.allowCpuFallback = m_cpuFallbackCb->isChecked();
    s.useFp16WhenAvailable = m_fp16Cb->isChecked();
    s.enableGraphOptimization = m_graphOptCb->isChecked();
    s.enableMemoryArena = m_memArenaCb->isChecked();
    s.enableMemoryPattern = m_memPatternCb->isChecked();
    s.preloadModelsOnStartup = m_preloadCb->isChecked();

    s.cpuThreadCount = m_cpuThreadsSpin->value();
    s.maxConcurrentAiJobs = m_concurrentSpin->value();
    s.maxLoadedModels = m_maxLoadedModelsSpin->value();
    s.maxGpuMemoryMB = m_gpuBudgetSpin->value();
    s.maxCpuMemoryMB = m_cpuBudgetSpin->value();
    s.embeddingCacheEnabled = m_embedCacheCb->isChecked();
    s.maxCachedDocuments = m_maxDocsSpin->value();
    s.maxCachedEmbeddingsMB = m_maxEmbedMbSpin->value();

    s.workingResolution = static_cast<AiWorkingResolution>(m_workingResCombo->currentData().toInt());
    s.workingResolutionLongestSide = m_workingResSideCombo->currentData().toInt();

    // Default models directory is stored as empty (so it follows the app data
    // location); a user-chosen directory is stored verbatim.
    s.modelsDirectory = (m_modelsDir == AiRuntimeSettings().resolvedModelsDirectory()) ? QString() : m_modelsDir;

    AiRuntimeManager::instance()->setSettings(s);

    QSettings settings;
    if (m_realesrganExePath.trimmed().isEmpty())
        settings.remove(QStringLiteral("ai/upscale/realesrganExecutable"));
    else
        settings.setValue(QStringLiteral("ai/upscale/realesrganExecutable"), m_realesrganExePath.trimmed());
}

void AiSettingsPage::setControlsEnabledForRuntime()
{
    const bool compiled = AiRuntimeManager::instance()->isRuntimeAvailable();
    if (m_runtimeBanner) {
        if (!compiled) {
            m_runtimeBanner->setText(tr("ONNX Runtime is not available in this build. AI inference features are disabled."));
            m_runtimeBanner->setVisible(true);
        } else {
            m_runtimeBanner->setVisible(false);
        }
    }
    const bool runtimeControls = compiled && m_enableCb->isChecked();
    const QList<QWidget*> runtimeWidgets = {
        m_providerCombo, m_cpuFallbackCb, m_fp16Cb, m_graphOptCb,
        m_memArenaCb, m_memPatternCb, m_preloadCb
    };
    for (QWidget* w : runtimeWidgets)
        if (w) w->setEnabled(runtimeControls);
    m_enableCb->setEnabled(compiled);
}

void AiSettingsPage::refreshProviderStatuses()
{
    if (!m_providerStatusLabel)
        return;
    auto* th = ThemeManager::instance()->current();
    const auto providers = AiRuntimeManager::instance()->availableProviders();

    QStringList lines;
    for (const AiExecutionProviderInfo& p : providers) {
        QColor c = th->colorTextSecondary;
        if (p.status == QLatin1String("Available") || p.status == QLatin1String("Active"))
            c = th->colorSuccess;
        else if (p.status.startsWith(QLatin1String("Not")))
            c = th->colorTextDisabled;
        lines << QStringLiteral("%1: <span style='color:%2'>%3</span>")
                     .arg(p.displayName, c.name(), p.status);
    }
    m_providerStatusLabel->setText(lines.join(QStringLiteral("<br>")));
}

QWidget* AiSettingsPage::buildCompatibilityGroup()
{
    auto* th = ThemeManager::instance()->current();
    auto* group = new QGroupBox(tr("Compatibility"), this);
    auto* lay = new QVBoxLayout(group);
    lay->setSpacing(th->spaceSM);
    lay->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_compatLabel = new QLabel(group);
    m_compatLabel->setTextFormat(Qt::RichText);
    m_compatLabel->setWordWrap(true);
    m_compatLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    markThemeLabel(m_compatLabel, kThemeRoleSecondary);
    lay->addWidget(m_compatLabel);

    return group;
}

QWidget* AiSettingsPage::buildProvidersGroup()
{
    auto* th = ThemeManager::instance()->current();
    auto* group = new QGroupBox(tr("Execution Providers"), this);
    auto* lay = new QVBoxLayout(group);
    lay->setSpacing(th->spaceSM);
    lay->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    auto* note = new QLabel(tr("Only providers compatible with this platform and runtime can be "
                               "selected above. Others are listed here for diagnostics."), group);
    note->setWordWrap(true);
    markThemeLabel(note, kThemeRoleSecondary);
    lay->addWidget(note);

    m_providersContainer = new QWidget(group);
    m_providersLayout = new QVBoxLayout(m_providersContainer);
    m_providersLayout->setContentsMargins(0, 0, 0, 0);
    m_providersLayout->setSpacing(th->spaceSM);
    lay->addWidget(m_providersContainer);

    return group;
}

void AiSettingsPage::populateExecutionProviderCombo()
{
    if (!m_providerCombo)
        return;
    const QString current = m_providerCombo->currentData().toString();
    QSignalBlocker block(m_providerCombo);
    m_providerCombo->clear();
    m_providerCombo->addItem(tr("Auto"), QString::fromLatin1(AiProvider::kAuto));
    for (const AiProviderCompatibility& p : AiCompatibilityManager::instance()->compatibleProviders())
        m_providerCombo->addItem(p.displayName, p.id);
    // Safety net: if nothing is selectable (e.g. runtime missing) still offer CPU.
    if (m_providerCombo->count() == 1)
        m_providerCombo->addItem(AiProvider::displayName(QString::fromLatin1(AiProvider::kCpu)),
                                 QString::fromLatin1(AiProvider::kCpu));
    const int idx = m_providerCombo->findData(current);
    m_providerCombo->setCurrentIndex(idx >= 0 ? idx : 0);
}

void AiSettingsPage::refreshCompatibility()
{
    auto* th = ThemeManager::instance()->current();
    populateExecutionProviderCombo();

    const AiCompatibilityReport rep = AiCompatibilityManager::instance()->buildReport();

    auto colored = [](const QString& text, const QColor& c) {
        return QStringLiteral("<span style='color:%1'>%2</span>").arg(c.name(), text.toHtmlEscaped());
    };
    auto statusColor = [&](AiCompatibilityStatus s) {
        switch (s) {
        case AiCompatibilityStatus::Compatible:          return th->colorSuccess;
        case AiCompatibilityStatus::PartiallyCompatible: return th->colorWarning;
        case AiCompatibilityStatus::Disabled:            return th->colorTextDisabled;
        case AiCompatibilityStatus::Unknown:             return th->colorTextSecondary;
        default:                                         return th->colorDanger;
        }
    };

    // ── Compatibility summary ──
    if (m_compatLabel) {
        QStringList lines;
        const QString platformName = rep.platform.osName.isEmpty()
            ? tr("Unknown") : rep.platform.osName;
        QString platformVal = platformName;
        if (!rep.platform.osVersion.isEmpty() && !platformName.contains(rep.platform.osVersion))
            platformVal += QStringLiteral(" %1").arg(rep.platform.osVersion);
        if (!rep.platform.architecture.isEmpty())
            platformVal += QStringLiteral(" (%1)").arg(rep.platform.architecture);
        lines << QStringLiteral("<b>%1:</b> %2 — %3").arg(tr("Platform"), platformVal.toHtmlEscaped(),
                       colored(aiCompatibilityStatusLabel(rep.platform.status), statusColor(rep.platform.status)));

        const QString rtVal = rep.runtime.onnxRuntimeAvailable
            ? tr("%1 — Loaded").arg(rep.runtime.onnxRuntimeVersion.isEmpty()
                                        ? tr("Available") : rep.runtime.onnxRuntimeVersion)
            : tr("Not available in this build");
        lines << QStringLiteral("<b>%1:</b> %2").arg(tr("ONNX Runtime"),
                       colored(rtVal, statusColor(rep.runtime.status)));
        if (!rep.runtime.runtimePath.isEmpty())
            lines << QStringLiteral("<b>%1:</b> %2").arg(tr("Runtime path"), rep.runtime.runtimePath.toHtmlEscaped());

        QString modelsVal = tr("%1 built-in, %2 downloaded, %3 custom")
            .arg(rep.models.bundledModels).arg(rep.models.downloadedModels).arg(rep.models.customModels);
        if (rep.models.invalidModels > 0)
            modelsVal += QStringLiteral(" — %1").arg(tr("%1 invalid").arg(rep.models.invalidModels));
        lines << QStringLiteral("<b>%1:</b> %2").arg(tr("Models"), modelsVal.toHtmlEscaped());

        lines << QStringLiteral("<b>%1:</b> %2").arg(tr("AI status"),
                       colored(aiCompatibilityStatusLabel(rep.globalStatus), statusColor(rep.globalStatus)));

        // Surface platform/model warnings beneath the summary.
        for (const auto* list : { &rep.platform.messages, &rep.models.messages }) {
            for (const AiCompatibilityMessage& m : *list) {
                if (m.severity == AiCompatibilitySeverity::Ok || m.severity == AiCompatibilitySeverity::Info)
                    continue;
                lines << colored(m.message, th->colorWarning);
            }
        }
        m_compatLabel->setText(lines.join(QStringLiteral("<br>")));
    }

    // ── Per-provider cards ──
    if (m_providersLayout) {
        QLayoutItem* item = nullptr;
        while ((item = m_providersLayout->takeAt(0)) != nullptr) {
            if (QWidget* w = item->widget())
                w->deleteLater();
            delete item;
        }
        for (const AiProviderCompatibility& p : rep.providers) {
            auto* card = new QFrame(m_providersContainer);
            card->setStyleSheet(QStringLiteral("QFrame { background: %1; border: 1px solid %2; border-radius: %3px; }")
                                    .arg(th->colorBackgroundTertiary.name(), th->colorBorder.name())
                                    .arg(th->radiusMD));
            auto* cl = new QVBoxLayout(card);
            cl->setContentsMargins(th->spaceMD, th->spaceSM, th->spaceMD, th->spaceSM);
            cl->setSpacing(th->spaceXS);

            QString title = p.displayName;
            if (p.isSelectable())
                title += QStringLiteral("  •  ") + tr("Selectable");
            auto* name = new QLabel(title, card);
            name->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; border: none;").arg(th->colorTextBright.name()));
            cl->addWidget(name);

            const QString statusText = p.reason.isEmpty()
                ? aiCompatibilityStatusLabel(p.status) : p.reason;
            auto* status = new QLabel(QStringLiteral("Status: <span style='color:%1'>%2</span>")
                                          .arg(statusColor(p.status).name(), statusText.toHtmlEscaped()), card);
            status->setTextFormat(Qt::RichText);
            status->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorTextSecondary.name()));
            cl->addWidget(status);

            if (!p.details.isEmpty()) {
                auto* det = new QLabel(p.details, card);
                det->setWordWrap(true);
                det->setStyleSheet(QStringLiteral("color: %1; border: none; font-size: 11px;").arg(th->colorTextSecondary.name()));
                cl->addWidget(det);
            }
            m_providersLayout->addWidget(card);
        }
    }
}

QWidget* AiSettingsPage::buildDiscoveredCard(const AiModelDescriptor& model)
{
    auto* th = ThemeManager::instance()->current();
    auto* registry = AiModelRegistry::instance();

    auto* card = new QFrame(m_modelsContainer);
    card->setStyleSheet(QStringLiteral("QFrame { background: %1; border: 1px solid %2; border-radius: %3px; }")
                            .arg(th->colorBackgroundTertiary.name(), th->colorBorder.name())
                            .arg(th->radiusMD));
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(th->spaceMD, th->spaceSM, th->spaceMD, th->spaceSM);
    cardLay->setSpacing(th->spaceXS);

    const bool isUserDefault = registry->userDefaultModelId(model.task) == model.id;
    QString title = model.displayName.isEmpty() ? model.id : model.displayName;
    if (isUserDefault || model.isDefault)
        title += QStringLiteral("  •  ") + tr("Default");
    auto* nameLabel = new QLabel(title, card);
    nameLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; border: none;").arg(th->colorTextBright.name()));
    cardLay->addWidget(nameLabel);

    // Origin • Status • Quality badges.
    QColor statusColor = (model.status == AiModelStatus::Valid) ? th->colorSuccess : th->colorWarning;
    QString badges = QStringLiteral("Origin: <b>%1</b>").arg(AiModelTaxonomy::originToString(model.origin));
    badges += QStringLiteral("&nbsp;&nbsp;•&nbsp;&nbsp;Status: <span style='color:%1'>%2</span>")
                  .arg(statusColor.name(), AiModelTaxonomy::statusToString(model.status));
    if (!model.quality.isEmpty())
        badges += QStringLiteral("&nbsp;&nbsp;•&nbsp;&nbsp;Quality: %1").arg(qualityLabel(model.quality));
    // Bundled models are the built-in default fallback and are never removable.
    if (model.origin == AiModelOrigin::Bundled)
        badges += QStringLiteral("&nbsp;&nbsp;•&nbsp;&nbsp;%1").arg(tr("Default fallback: Yes"));
    auto* badgeLabel = new QLabel(badges, card);
    badgeLabel->setTextFormat(Qt::RichText);
    badgeLabel->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorTextSecondary.name()));
    cardLay->addWidget(badgeLabel);

    if (!model.description.isEmpty()) {
        auto* desc = new QLabel(model.description, card);
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorTextSecondary.name()));
        cardLay->addWidget(desc);
    }

    // Files + license + source.
    const QStringList fileNames = model.files.allFiles();
    QStringList meta;
    if (!fileNames.isEmpty())
        meta << tr("Files: %1").arg(fileNames.join(QStringLiteral(", ")));
    if (!model.licenseName.isEmpty())
        meta << tr("License: %1").arg(model.licenseName);
    if (!model.sourceName.isEmpty())
        meta << tr("Source: %1").arg(model.sourceName);
    if (!meta.isEmpty()) {
        auto* metaLabel = new QLabel(meta.join(QStringLiteral("\n")), card);
        metaLabel->setWordWrap(true);
        metaLabel->setStyleSheet(QStringLiteral("color: %1; border: none; font-size: 11px;").arg(th->colorTextSecondary.name()));
        cardLay->addWidget(metaLabel);
    }

    // Action row.
    auto* actionRow = new QWidget(card);
    auto* actionLay = new QHBoxLayout(actionRow);
    actionLay->setContentsMargins(0, 0, 0, 0);
    actionLay->setSpacing(th->spaceSM);
    actionLay->addStretch();

    const QString id = model.id;
    const QString task = model.task;
    const QString dir = model.rootDir;
    const QString licenseFile = model.licenseFile;
    const QString fallbackUrl = model.sourceUrl;

    auto* defaultBtn = new QPushButton(tr("Use as default"), actionRow);
    defaultBtn->setEnabled(model.status == AiModelStatus::Valid && !isUserDefault);
    connect(defaultBtn, &QPushButton::clicked, this, [this, task, id]() { onUseAsDefault(task, id); });
    actionLay->addWidget(defaultBtn);

    // Downloaded models can be removed; bundled/custom cannot. Redownload is
    // intentionally not offered — manifest downloads are disabled for now.
    if (model.origin == AiModelOrigin::Downloaded) {
        auto* removeBtn = new QPushButton(tr("Remove"), actionRow);
        connect(removeBtn, &QPushButton::clicked, this, [this, id]() { onRemoveModel(id); });
        actionLay->addWidget(removeBtn);
    }

    auto* showBtn = new QPushButton(tr("Show files"), actionRow);
    connect(showBtn, &QPushButton::clicked, this, [this, dir]() { onShowFiles(dir); });
    actionLay->addWidget(showBtn);

    if (!licenseFile.isEmpty() || !fallbackUrl.isEmpty()) {
        auto* licBtn = new QPushButton(tr("Open license"), actionRow);
        connect(licBtn, &QPushButton::clicked, this, [this, dir, licenseFile, fallbackUrl]() {
            onOpenLicense(dir, licenseFile, fallbackUrl);
        });
        actionLay->addWidget(licBtn);
    }

    cardLay->addWidget(actionRow);
    return card;
}

QWidget* AiSettingsPage::buildDownloadCard(const AiManifestEntry& entry)
{
    auto* th = ThemeManager::instance()->current();
    auto* dl = AiModelDownloadManager::instance();

    auto* card = new QFrame(m_modelsContainer);
    card->setStyleSheet(QStringLiteral("QFrame { background: %1; border: 1px solid %2; border-radius: %3px; }")
                            .arg(th->colorBackgroundTertiary.name(), th->colorBorder.name())
                            .arg(th->radiusMD));
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(th->spaceMD, th->spaceSM, th->spaceMD, th->spaceSM);
    cardLay->setSpacing(th->spaceXS);

    QString title = entry.name.isEmpty() ? entry.id : entry.name;
    if (entry.recommended)
        title += QStringLiteral("  •  ") + tr("Recommended");
    auto* nameLabel = new QLabel(title, card);
    nameLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; border: none;").arg(th->colorTextBright.name()));
    cardLay->addWidget(nameLabel);

    QString badges = QStringLiteral("Origin: <b>%1</b>").arg(tr("Download manifest"));
    badges += QStringLiteral("&nbsp;&nbsp;•&nbsp;&nbsp;Status: %1").arg(tr("Not installed"));
    if (!entry.quality.isEmpty())
        badges += QStringLiteral("&nbsp;&nbsp;•&nbsp;&nbsp;Quality: %1").arg(qualityLabel(entry.quality));
    auto* badgeLabel = new QLabel(badges, card);
    badgeLabel->setTextFormat(Qt::RichText);
    badgeLabel->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorTextSecondary.name()));
    cardLay->addWidget(badgeLabel);

    if (!entry.description.isEmpty()) {
        auto* desc = new QLabel(entry.description, card);
        desc->setWordWrap(true);
        desc->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorTextSecondary.name()));
        cardLay->addWidget(desc);
    }

    QStringList fileNames;
    for (const AiModelFile& f : entry.files)
        if (!f.filename.isEmpty()) fileNames << f.filename;
    QStringList meta;
    if (!fileNames.isEmpty())
        meta << tr("Files: %1").arg(fileNames.join(QStringLiteral(", ")));
    if (!entry.license.isEmpty())
        meta << tr("License: %1").arg(entry.license);
    const QString source = entry.sourceName.isEmpty() ? entry.sourceUrl : entry.sourceName;
    if (!source.isEmpty())
        meta << tr("Source: %1").arg(source);
    if (!meta.isEmpty()) {
        auto* metaLabel = new QLabel(meta.join(QStringLiteral("\n")), card);
        metaLabel->setWordWrap(true);
        metaLabel->setStyleSheet(QStringLiteral("color: %1; border: none; font-size: 11px;").arg(th->colorTextSecondary.name()));
        cardLay->addWidget(metaLabel);
    }

    auto* actionRow = new QWidget(card);
    auto* actionLay = new QHBoxLayout(actionRow);
    actionLay->setContentsMargins(0, 0, 0, 0);
    actionLay->setSpacing(th->spaceSM);

    ModelRow row;
    row.status = new QLabel(actionRow);
    row.status->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorTextSecondary.name()));
    actionLay->addWidget(row.status);

    row.progress = new QProgressBar(actionRow);
    row.progress->setFixedHeight(8);
    row.progress->setTextVisible(false);
    row.progress->setVisible(false);
    actionLay->addWidget(row.progress, 1);
    actionLay->addStretch();

    row.actionBtn = new QPushButton(actionRow);
    const QString id = entry.id;
    actionLay->addWidget(row.actionBtn);
    cardLay->addWidget(actionRow);

    const bool downloadingThis = dl->isDownloading(id);
    if (downloadingThis) {
        row.status->setText(tr("Downloading…"));
        row.progress->setVisible(true);
        row.progress->setRange(0, 0);
        row.actionBtn->setText(tr("Cancel"));
        connect(row.actionBtn, &QPushButton::clicked, this, [id]() {
            AiModelDownloadManager::instance()->cancel(id);
        });
    } else {
        row.status->setText(tr("Not installed"));
        row.actionBtn->setText(tr("Download"));
        row.actionBtn->setEnabled(!dl->isBusy());
        connect(row.actionBtn, &QPushButton::clicked, this, [this, id]() { onModelAction(id); });
    }

    m_rows.insert(id, row);
    return card;
}

QWidget* AiSettingsPage::buildInvalidCard(const AiModelDescriptor& model)
{
    auto* th = ThemeManager::instance()->current();
    auto* card = new QFrame(m_modelsContainer);
    card->setStyleSheet(QStringLiteral("QFrame { background: %1; border: 1px solid %2; border-radius: %3px; }")
                            .arg(th->colorBackgroundTertiary.name(), th->colorWarning.name())
                            .arg(th->radiusMD));
    auto* cardLay = new QVBoxLayout(card);
    cardLay->setContentsMargins(th->spaceMD, th->spaceSM, th->spaceMD, th->spaceSM);
    cardLay->setSpacing(th->spaceXS);

    const QString idText = model.id.isEmpty() ? tr("(unknown id)") : model.id;
    auto* nameLabel = new QLabel(QStringLiteral("%1  •  %2  •  %3")
                                     .arg(idText, AiModelTaxonomy::originToString(model.origin),
                                          AiModelTaxonomy::statusToString(model.status)), card);
    nameLabel->setStyleSheet(QStringLiteral("color: %1; font-weight: 600; border: none;").arg(th->colorWarning.name()));
    cardLay->addWidget(nameLabel);

    QString detail;
    if (!model.statusReason.isEmpty())
        detail = model.statusReason;
    if (!model.rootDir.isEmpty())
        detail += (detail.isEmpty() ? QString() : QStringLiteral("\n")) + model.rootDir;
    if (!detail.isEmpty()) {
        auto* detailLabel = new QLabel(detail, card);
        detailLabel->setWordWrap(true);
        detailLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        detailLabel->setStyleSheet(QStringLiteral("color: %1; border: none; font-size: 11px;").arg(th->colorTextSecondary.name()));
        cardLay->addWidget(detailLabel);
    }
    return card;
}

void AiSettingsPage::refreshDiagnostics()
{
    if (!m_diagLabel)
        return;
    const AiRuntimeDiagnostic d = AiRuntimeManager::instance()->diagnostic();

    auto rowText = [](const QString& k, const QString& v) {
        return QStringLiteral("<b>%1:</b> %2").arg(k, v.isEmpty() ? QStringLiteral("—") : v.toHtmlEscaped());
    };

    QStringList lines;
    lines << rowText(tr("ONNX Runtime"), d.runtimeInstalled ? tr("Installed") : tr("Not available in this build"));
    lines << rowText(tr("Status"), aiRuntimeStatusLabel(d.status));
    if (d.runtimeInstalled) {
        lines << rowText(tr("Runtime version"), d.runtimeVersion);
        lines << rowText(tr("Loaded from"), d.loadedFrom);
        lines << rowText(tr("Available providers"),
                         AiRuntimeManager::instance()->deviceInfo().availableProviders.join(QStringLiteral(", ")));
    }
    lines << rowText(tr("Selected provider"), d.selectedProvider);
    lines << rowText(tr("Active provider"), d.activeProvider);
    lines << rowText(tr("Last fallback"), d.lastFallback);
    lines << rowText(tr("Loaded models"), QString::number(d.loadedSessions));
    {
        auto* runner = AiJobRunner::instance();
        const int embeds = runner->cachedEmbeddingCount();
        const qint64 embedBytes = runner->cachedEmbeddingBytes();
        lines << rowText(tr("Cached embeddings"),
                         QStringLiteral("%1 (%2)").arg(QString::number(embeds), humanSize(embedBytes)));
    }
    if (!d.lastError.isEmpty())
        lines << rowText(tr("Last error"), d.lastError);

    m_diagLabel->setText(lines.join(QStringLiteral("<br>")));
}

void AiSettingsPage::onChangeModelsDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this, tr("Choose Models Directory"), m_modelsDir);
    if (dir.isEmpty())
        return;
    QFileInfo fi(dir);
    if (!fi.isWritable()) {
        m_modelsDirLabel->setText(tr("%1  (not writable)").arg(dir));
        return;
    }
    m_modelsDir = dir;
    m_modelsDirLabel->setText(dir);
    // Point the registry at the new directory now so downloads target it and the
    // installed list reflects it; the preference is persisted on dialog accept.
    AiModelRegistry::instance()->setModelsDirectory(dir);
    refreshModelList();
}

void AiSettingsPage::onOpenModelsFolder()
{
    QDir().mkpath(m_modelsDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_modelsDir));
}

void AiSettingsPage::onClearCache()
{
    // Drop loaded ONNX sessions (runtime) and the embedding cache + loaded
    // segmenter/matting models (job pipeline).
    AiRuntimeManager::instance()->clearCache();
    AiJobRunner::instance()->resetPipeline();

    // Clear the on-disk AI cache (embeddings/previews/temp/downloads). Installed
    // models live in the data dir and are never touched here.
    QDir aiCache(AppPaths::aiCacheDir());
    const QFileInfoList entries = aiCache.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : entries) {
        QDir sub(fi.absoluteFilePath());
        const QFileInfoList contents = sub.entryInfoList(QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo& c : contents) {
            if (c.isDir()) QDir(c.absoluteFilePath()).removeRecursively();
            else           QFile::remove(c.absoluteFilePath());
        }
    }
    refreshDiagnostics();
}

void AiSettingsPage::onModelAction(const QString& modelId)
{
    const AiManifestEntry* entry = AiModelRegistry::instance()->manifest().model(modelId);
    if (!entry)
        return;

    // Placeholder manifest entries carry no URL yet — make that explicit rather
    // than failing silently mid-download.
    bool hasUrls = !entry->files.isEmpty();
    for (const AiModelFile& f : entry->files)
        if (f.url.trimmed().isEmpty()) hasUrls = false;
    if (!hasUrls) {
        if (m_rows.contains(modelId)) {
            auto* th = ThemeManager::instance()->current();
            m_rows[modelId].status->setText(tr("No download URL configured yet for this model."));
            m_rows[modelId].status->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorWarning.name()));
        }
        return;
    }

    AiModelDownloadManager::instance()->download(*entry);
}

void AiSettingsPage::onUseAsDefault(const QString& task, const QString& modelId)
{
    // Persists the per-task default; emits installedModelsChanged → refreshModelList().
    AiModelRegistry::instance()->setDefaultModelForTask(task, modelId);
}

void AiSettingsPage::onRemoveModel(const QString& modelId)
{
    const auto ret = QMessageBox::question(
        this, tr("Remove model"),
        tr("Remove the downloaded model '%1' and its files? You can download it again later.").arg(modelId),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (ret != QMessageBox::Yes)
        return;
    QString err;
    if (!AiModelRegistry::instance()->removeInstalled(modelId, &err))
        QMessageBox::warning(this, tr("Remove model"), err);
    // removeInstalled refreshes the registry → refreshModelList() runs.
}

void AiSettingsPage::onRedownload(const QString& modelId)
{
    onModelAction(modelId); // reinstalls into downloaded/, replacing atomically
}

void AiSettingsPage::onShowFiles(const QString& dir)
{
    if (dir.isEmpty())
        return;
    QDir().mkpath(dir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void AiSettingsPage::onOpenLicense(const QString& dir, const QString& licenseFile, const QString& fallbackUrl)
{
    if (!licenseFile.isEmpty()) {
        const QString path = QDir(dir).filePath(licenseFile);
        if (QFile::exists(path)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            return;
        }
    }
    if (!fallbackUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(fallbackUrl));
}

void AiSettingsPage::onDownloadStarted(const QString& /*modelId*/)
{
    // Rebuild so the active card shows Cancel + progress and others disable.
    refreshModelList();
}

void AiSettingsPage::onDownloadProgress(const QString& modelId, qint64 received, qint64 total)
{
    if (!m_rows.contains(modelId))
        return;
    QProgressBar* bar = m_rows[modelId].progress;
    if (total > 0) {
        bar->setRange(0, 100);
        bar->setValue(static_cast<int>(received * 100 / total));
    } else {
        bar->setRange(0, 0);
    }
}

void AiSettingsPage::onDownloadStatus(const QString& modelId, const QString& text)
{
    if (m_rows.contains(modelId))
        m_rows[modelId].status->setText(text);
}

void AiSettingsPage::onDownloadFinished(const QString& modelId, bool success, const QString& error)
{
    // Rebuild first (an installed model becomes a discovered card; a failed one
    // stays a download card), then surface any error on the rebuilt row.
    refreshModelList();
    if (!success && !error.isEmpty() && m_rows.contains(modelId)) {
        auto* th = ThemeManager::instance()->current();
        ModelRow& row = m_rows[modelId];
        row.status->setText(error);
        row.status->setStyleSheet(QStringLiteral("color: %1; border: none;").arg(th->colorDanger.name()));
        row.progress->setVisible(false);
    }
    refreshDiagnostics();
}
