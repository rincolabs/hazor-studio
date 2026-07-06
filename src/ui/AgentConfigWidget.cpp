#include "AgentConfigWidget.hpp"
#include "AppSettingsMetrics.hpp"
#include "agent/LLMClient.hpp"
#include "agent/AgentPresetManager.hpp"
#include "tools/ToolCatalog.hpp"
#include "ui/IconUtils.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QTabWidget>
#include "ui/AppCheckBox.hpp"
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QClipboard>
#include <QApplication>
#include <QMessageBox>
#include <QDateTime>
#include <QPalette>

AgentConfigWidget::AgentConfigWidget(AgentPresetManager* manager, LLMClient* client,
                                     const QString& initialPreset, QWidget* parent)
    : QWidget(parent)
    , m_manager(manager)
    , m_client(client)
{
    auto* t = ThemeManager::instance()->current();

    setObjectName(QStringLiteral("agentConfigWidget"));
    setAttribute(Qt::WA_StyledBackground, true);
    auto surfacePalette = palette();
    surfacePalette.setColor(QPalette::Window, t->colorSurface);
    surfacePalette.setColor(QPalette::Base, t->colorSurface);
    setAutoFillBackground(true);
    setPalette(surfacePalette);

    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setSpacing(t->spaceMD);
    mainLayout->setContentsMargins(t->spaceXL, t->spaceLG, t->spaceXL, t->spaceLG);

    mainLayout->addWidget(createPresetBar());

    m_sectionsTabs = new QTabWidget(this);
    m_sectionsTabs->setObjectName(QStringLiteral("agentConfigTabs"));
    m_sectionsTabs->setTabPosition(QTabWidget::North);
    m_sectionsTabs->setUsesScrollButtons(true);
    m_sectionsTabs->setPalette(surfacePalette);

    auto makeTabPage = [this, t, surfacePalette](std::initializer_list<QWidget*> sections) {
        auto* page = new QWidget(this);
        page->setObjectName(QStringLiteral("agentConfigTabPage"));
        page->setAttribute(Qt::WA_StyledBackground, true);
        page->setAutoFillBackground(true);
        page->setPalette(surfacePalette);
        auto* layout = new QVBoxLayout(page);
        layout->setContentsMargins(t->spaceMD, t->spaceMD, t->spaceMD, 0);
        layout->setSpacing(t->spaceMD);
        for (auto* section : sections)
            layout->addWidget(section);
        layout->addStretch();
        return page;
    };

    m_sectionsTabs->addTab(makeTabPage({createHeaderSection()}), tr("Identification"));
    m_sectionsTabs->addTab(makeTabPage({createProviderSection(), createTestSection()}), tr("Model Provider"));
    m_modelSettingsSection = createModelSettingsSection();
    m_modelSettingsTabIndex = m_sectionsTabs->addTab(makeTabPage({m_modelSettingsSection}), tr("Model Settings"));
    m_behaviorSection = createBehaviorSection();
    m_behaviorTabIndex = m_sectionsTabs->addTab(makeTabPage({m_behaviorSection}), tr("Agent Behavior"));
    m_generativeSection = createGenerativeSection();
    m_generativeTabIndex = m_sectionsTabs->addTab(makeTabPage({m_generativeSection}), tr("Generative Settings"));
    m_sectionsTabs->addTab(makeTabPage({createAdvancedSection()}), tr("Advanced Settings"));
    mainLayout->addWidget(m_sectionsTabs, 1);

    m_validationLabel = new QLabel(this);
    m_validationLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
        .arg(ThemeManager::instance()->current()->colorDanger.name())
        .arg(ThemeManager::instance()->current()->fontSizeMD));
    m_validationLabel->hide();
    mainLayout->addWidget(m_validationLabel);

    refreshPresetCombo();

    if (!initialPreset.isEmpty()) {
        int idx = m_presetCombo->findData(initialPreset);
        if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
    }

    updateKindVisibility(currentKind());
    updateProviderFields();

    connect(m_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AgentConfigWidget::onKindChanged);
    connect(m_presetCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AgentConfigWidget::onPresetChanged);
    connect(m_newBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::onNewPreset);
    connect(m_deleteBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::onDeletePreset);
    connect(m_duplicateBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::onDuplicatePreset);
    connect(m_saveBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::onSavePreset);
    connect(m_dirtyBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::discardChanges);
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &AgentConfigWidget::onProviderChanged);
    connect(m_testConnBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::onTestConnection);
    connect(m_testPromptBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::onTestPrompt);
    connect(m_detectModelsBtn, &QPushButton::clicked,
            this, &AgentConfigWidget::onDetectModels);

    if (m_client) {
        connect(m_client, &LLMClient::connectionTestResult,
                this, &AgentConfigWidget::onConnectionResult);
        connect(m_client, &LLMClient::modelsFetched,
                this, &AgentConfigWidget::onModelsFetched);
    }

    connectDirtySignals();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &AgentConfigWidget::applyTheme);
    applyTheme();
    setDirty(false);
}

QString AgentConfigWidget::selectedPresetName() const
{
    return m_currentPreset.name;
}

void AgentConfigWidget::apply()
{
    saveChanges();
}

void AgentConfigWidget::load()
{
    refreshPresetCombo();
    if (m_manager && !m_manager->activePresetName().isEmpty()) {
        int idx = m_presetCombo->findData(m_manager->activePresetName());
        if (idx >= 0)
            m_presetCombo->setCurrentIndex(idx);
        else if (m_presetCombo->count() > 0)
            m_presetCombo->setCurrentIndex(0);
    }

    const QString name = m_presetCombo->currentData().toString();
    if (m_manager && m_manager->exists(name))
        loadPresetIntoForm(m_manager->preset(name));
}

bool AgentConfigWidget::hasUnsavedChanges() const
{
    return m_dirty;
}

bool AgentConfigWidget::confirmPendingChanges()
{
    if (!m_dirty)
        return true;

    switch (promptPendingChanges()) {
        case PendingChangesChoice::Save:
            return saveChanges();
        case PendingChangesChoice::Discard:
            discardChanges();
            return true;
        case PendingChangesChoice::Cancel:
            return false;
    }
    return false;
}

void AgentConfigWidget::applyTheme()
{
    auto* t = ThemeManager::instance()->current();

    QPalette surfacePalette = palette();
    surfacePalette.setColor(QPalette::Window, t->colorSurface);
    surfacePalette.setColor(QPalette::Base, t->colorSurface);
    setAutoFillBackground(true);
    setPalette(surfacePalette);
    setStyleSheet(QStringLiteral(
        "AgentConfigWidget#agentConfigWidget { background: %1; }"
        "QTabWidget#agentConfigTabs::pane { background: %1; border: 1px solid %2; border-top: none; border-bottom: none; }"
        "QWidget#agentConfigTabPage { background: %1; }")
        .arg(t->colorSurface.name(), t->colorBorder.name()));

    if (m_sectionsTabs) {
        m_sectionsTabs->setPalette(surfacePalette);
        for (int i = 0; i < m_sectionsTabs->count(); ++i) {
            if (auto* page = m_sectionsTabs->widget(i)) {
                page->setAttribute(Qt::WA_StyledBackground, true);
                page->setAutoFillBackground(true);
                page->setPalette(surfacePalette);
            }
        }
    }

    if (m_validationLabel) {
        m_validationLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
            .arg(t->colorDanger.name())
            .arg(t->fontSizeMD));
    }
    if (m_toolsCountLabel) {
        m_toolsCountLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
            .arg(t->colorTextSecondary.name())
            .arg(t->fontSizeMD));
    }
    if (m_testConnStatus) {
        QColor statusColor = t->colorTextSecondary;
        if (m_testConnStatus->isVisible()) {
            const QString statusText = m_testConnStatus->text();
            if (statusText.startsWith(tr("Testing")))
                statusColor = t->colorWarning;
            else if (statusText.startsWith(tr("Failed")))
                statusColor = t->colorDanger;
            else if (!statusText.isEmpty())
                statusColor = t->colorSuccess;
        }
        m_testConnStatus->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
            .arg(statusColor.name())
            .arg(t->fontSizeMD));
    }
}

// ── Preset bar ──

QWidget* AgentConfigWidget::createPresetBar()
{
    auto* container = new QWidget(this);
    auto* layout = new QHBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);

    auto makeIconButton = [this](const QString& iconPath, const QString& tooltip) -> QPushButton* {
        auto* btn = new QPushButton(this);
        btn->setIcon(makeIcon(iconPath));
        btn->setIconSize(QSize(24, 24));
        btn->setFixedSize(26, 26);
        btn->setToolTip(tooltip);
        return btn;
    };

    auto* label = new QLabel(tr("Agent:"), this);
    label->setStyleSheet("font-weight: bold;");
    layout->addWidget(label);

    m_presetCombo = new QComboBox(this);
    m_presetCombo->setMinimumWidth(200);
    layout->addWidget(m_presetCombo, 1);

    m_newBtn = makeIconButton(":/icons/ui-add.png", tr("New agent"));
    layout->addWidget(m_newBtn);

    m_deleteBtn = makeIconButton(":/icons/ui-delete.png", tr("Delete agent"));
    layout->addWidget(m_deleteBtn);

    m_duplicateBtn = makeIconButton(":/icons/ui-clone.png", tr("Duplicate agent"));
    layout->addWidget(m_duplicateBtn);

    m_saveBtn = makeIconButton(":/icons/ui-save.png", tr("Save agent changes"));
    layout->addWidget(m_saveBtn);

    m_dirtyBtn = makeIconButton(":/icons/ui-cancel.png", tr("Discard unsaved changes"));
    layout->addWidget(m_dirtyBtn);

    return container;
}

// ── Section builders ──

QWidget* AgentConfigWidget::createHeaderSection()
{
    auto* group = new QGroupBox(tr("Identification"), this);
    auto* form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignRight);

    m_nameEdit = new QLineEdit(this);
    m_nameEdit->setPlaceholderText(tr("e.g. Intelligent Editor"));
    form->addRow(tr("Name:"), m_nameEdit);

    m_descEdit = new QLineEdit(this);
    m_descEdit->setPlaceholderText(tr("e.g. Auto Color Grading Agent (optional)"));
    form->addRow(tr("Description:"), m_descEdit);

    m_kindCombo = new QComboBox(this);
    m_kindCombo->addItem(tr("Assistant (text / chat)"), static_cast<int>(Assistant));
    m_kindCombo->addItem(tr("Generative (image / inpaint)"), static_cast<int>(Generative));
    m_kindCombo->setToolTip(tr("Assistant presets drive the chat agent; "
                               "Generative presets drive Generative Fill."));
    form->addRow(tr("Type:"), m_kindCombo);

    return group;
}

AgentKind AgentConfigWidget::currentKind() const
{
    return m_kindCombo && m_kindCombo->currentData().toInt() == Generative
               ? Generative : Assistant;
}

QWidget* AgentConfigWidget::createProviderSection()
{
    auto* group = new QGroupBox(tr("Model Provider"), this);
    auto* layout = new QVBoxLayout(group);

    m_providerCombo = new QComboBox(this);
    repopulateProviderCombo(Assistant);
    layout->addWidget(m_providerCombo);

    auto* providerForm = new QWidget(this);
    auto* pform = new QFormLayout(providerForm);
    pform->setLabelAlignment(Qt::AlignRight);

    m_baseUrlEdit = new QLineEdit(this);
    m_baseUrlEdit->setPlaceholderText(tr("http://localhost:11434"));
    pform->addRow(tr("Base URL:"), m_baseUrlEdit);

    m_apiKeyEdit = new QLineEdit(this);
    m_apiKeyEdit->setPlaceholderText(tr("API Key (optional for local)"));
    m_apiKeyEdit->setEchoMode(QLineEdit::Password);
    pform->addRow(tr("API Key:"), m_apiKeyEdit);

    auto* modelRow = new QHBoxLayout;
    m_modelCombo = new QComboBox(this);
    m_modelCombo->setEditable(true);
    m_modelCombo->setInsertPolicy(QComboBox::NoInsert);
    m_modelCombo->lineEdit()->setPlaceholderText(tr("Model name (e.g. qwen2.5:3b)"));
    modelRow->addWidget(m_modelCombo, 1);

    m_detectModelsBtn = new QPushButton(tr("Detect"), this);
    m_detectModelsBtn->setToolTip(tr("Fetch available models from provider"));
    m_detectModelsBtn->setFixedWidth(80);
    modelRow->addWidget(m_detectModelsBtn);

    pform->addRow(tr("Model:"), modelRow);
    layout->addWidget(providerForm);

    return group;
}

// Populate the provider combo with the choices valid for a given kind. The
// ProviderConfig::Type enum is stored in each item's data (the visible order
// differs per kind, so index can't be used directly).
void AgentConfigWidget::repopulateProviderCombo(AgentKind kind)
{
    if (!m_providerCombo) return;
    m_providerCombo->blockSignals(true);
    m_providerCombo->clear();
    if (kind == Generative) {
        // Local = A1111 SD (:7860); Custom = SD with a custom base URL;
        // OpenAI = Images edits; Google = Gemini image.
        m_providerCombo->addItem(tr("Local SD (A1111 / Forge)"),
                                 static_cast<int>(ProviderConfig::Local));
        m_providerCombo->addItem(tr("OpenAI Images"),
                                 static_cast<int>(ProviderConfig::OpenAI));
        m_providerCombo->addItem(tr("Google (Gemini Image)"),
                                 static_cast<int>(ProviderConfig::Google));
        m_providerCombo->addItem(tr("Custom (SD / Stability / Replicate)"),
                                 static_cast<int>(ProviderConfig::Custom));
    } else {
        m_providerCombo->addItem(tr("Local (Ollama / llama.cpp)"),
                                 static_cast<int>(ProviderConfig::Local));
        m_providerCombo->addItem(tr("OpenAI"),
                                 static_cast<int>(ProviderConfig::OpenAI));
        m_providerCombo->addItem(tr("Anthropic (Claude)"),
                                 static_cast<int>(ProviderConfig::Anthropic));
        m_providerCombo->addItem(tr("Google (Gemini)"),
                                 static_cast<int>(ProviderConfig::Google));
        m_providerCombo->addItem(tr("Custom (OpenAI-compatible)"),
                                 static_cast<int>(ProviderConfig::Custom));
    }
    m_providerCombo->blockSignals(false);
}

QWidget* AgentConfigWidget::createModelSettingsSection()
{
    auto* group = new QGroupBox(tr("Model Settings"), this);
    auto* form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignRight);

    m_tempSpin = new QDoubleSpinBox(this);
    m_tempSpin->setRange(0.0, 1.0);
    m_tempSpin->setSingleStep(0.05);
    m_tempSpin->setDecimals(2);
    m_tempSpin->setValue(0.2);
    m_tempSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Temperature:"), m_tempSpin);

    m_maxTokensSpin = new QSpinBox(this);
    m_maxTokensSpin->setRange(1, 32768);
    m_maxTokensSpin->setValue(512);
    m_maxTokensSpin->setSingleStep(64);
    m_maxTokensSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Max Tokens:"), m_maxTokensSpin);

    m_topPSpin = new QDoubleSpinBox(this);
    m_topPSpin->setRange(0.0, 1.0);
    m_topPSpin->setSingleStep(0.05);
    m_topPSpin->setDecimals(2);
    m_topPSpin->setValue(0.9);
    m_topPSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Top P:"), m_topPSpin);

    m_freqPenaltySpin = new QDoubleSpinBox(this);
    m_freqPenaltySpin->setRange(-2.0, 2.0);
    m_freqPenaltySpin->setSingleStep(0.1);
    m_freqPenaltySpin->setDecimals(2);
    m_freqPenaltySpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Frequency Penalty:"), m_freqPenaltySpin);

    m_presencePenaltySpin = new QDoubleSpinBox(this);
    m_presencePenaltySpin->setRange(-2.0, 2.0);
    m_presencePenaltySpin->setSingleStep(0.1);
    m_presencePenaltySpin->setDecimals(2);
    m_presencePenaltySpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Presence Penalty:"), m_presencePenaltySpin);

    return group;
}

QWidget* AgentConfigWidget::createBehaviorSection()
{
    auto* group = new QGroupBox(tr("Agent Behavior"), this);
    auto* layout = new QVBoxLayout(group);

    auto* promptLabel = new QLabel(tr("System Prompt:"), this);
    layout->addWidget(promptLabel);

    m_systemPromptEdit = new QPlainTextEdit(this);
    m_systemPromptEdit->setPlaceholderText(tr("Leave empty to auto-generate from selected tools"));
    m_systemPromptEdit->setMinimumHeight(100);
    m_systemPromptEdit->setMaximumHeight(150);
    auto* pt = ThemeManager::instance()->current();
    // m_systemPromptEdit->setStyleSheet(
    //     QStringLiteral("QPlainTextEdit { background: %1; color: %2;"
    //         " border: 1px solid %3; border-radius: %4px;"
    //         " font: %5px monospace; }")
    //         .arg(pt->colorBackgroundPrimary.name())
    //         .arg(pt->colorTextPrimary.name())
    //         .arg(pt->colorBorder.name())
    //         .arg(pt->radiusMD)
    //         .arg(pt->fontSizeMD));
    layout->addWidget(m_systemPromptEdit);

    auto* toolsHeader = new QHBoxLayout;
    auto* toolsLabel = new QLabel(tr("Available Tools:"), this);
    toolsHeader->addWidget(toolsLabel);
    m_toolsCountLabel = new QLabel(this);
    m_toolsCountLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
        .arg(ThemeManager::instance()->current()->colorTextSecondary.name())
        .arg(ThemeManager::instance()->current()->fontSizeMD));
    toolsHeader->addWidget(m_toolsCountLabel);
    toolsHeader->addStretch();
    layout->addLayout(toolsHeader);

    m_toolsList = new QListWidget(this);
    m_toolsList->setMinimumHeight(120);
    m_toolsList->setMaximumHeight(200);

    const auto& allTools = ToolCatalog::allTools();
    for (const auto& t : allTools) {
        auto* item = new QListWidgetItem(
            QString("%1  (%2)").arg(QString::fromStdString(t.name),
                                     QString::fromStdString(t.category)));
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, QString::fromStdString(t.name));
        m_toolsList->addItem(item);
    }

    connect(m_toolsList, &QListWidget::itemChanged,
            this, &AgentConfigWidget::onToolsCheckChanged);

    layout->addWidget(m_toolsList);

    return group;
}

QWidget* AgentConfigWidget::createGenerativeSection()
{
    auto* group = new QGroupBox(tr("Generative Settings"), this);
    auto* form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignRight);

    m_stepsSpin = new QSpinBox(this);
    m_stepsSpin->setRange(1, 150);
    m_stepsSpin->setValue(20);
    m_stepsSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Steps:"), m_stepsSpin);

    m_strengthSpin = new QDoubleSpinBox(this);
    m_strengthSpin->setRange(0.0, 1.0);
    m_strengthSpin->setSingleStep(0.05);
    m_strengthSpin->setDecimals(2);
    m_strengthSpin->setValue(0.75);
    m_strengthSpin->setToolTip(tr("Denoising strength for img2img inpaint"));
    m_strengthSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Strength:"), m_strengthSpin);

    m_seedSpin = new QSpinBox(this);
    m_seedSpin->setRange(-1, 2147483647);
    m_seedSpin->setValue(-1);
    m_seedSpin->setToolTip(tr("-1 = random seed"));
    m_seedSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Seed:"), m_seedSpin);

    m_negativePromptEdit = new QLineEdit(this);
    m_negativePromptEdit->setPlaceholderText(tr("e.g. blurry, lowres, artifacts"));
    form->addRow(tr("Negative Prompt:"), m_negativePromptEdit);

    m_promptAssistCheck = new AppCheckBox(tr("Refine prompt with an assistant"), this);
    form->addRow(tr("Prompt Assist:"), m_promptAssistCheck);

    m_promptAssistCombo = new QComboBox(this);
    m_promptAssistCombo->setToolTip(tr("Assistant preset used to enrich the prompt"));
    if (m_manager) {
        for (const auto& n : m_manager->presetNames(Assistant))
            m_promptAssistCombo->addItem(n);
    }
    form->addRow(tr("Assist Preset:"), m_promptAssistCombo);

    return group;
}

QWidget* AgentConfigWidget::createTestSection()
{
    auto* group = new QGroupBox(tr("Test"), this);
    auto* layout = new QVBoxLayout(group);

    auto* connRow = new QHBoxLayout;
    m_testConnBtn = new QPushButton(tr("Test Connection"), this);
    m_testConnBtn->setObjectName("okBtn");
    connRow->addWidget(m_testConnBtn);

    m_testConnStatus = new QLabel(tr("Not tested"), this);
    m_testConnStatus->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
        .arg(ThemeManager::instance()->current()->colorTextSecondary.name())
        .arg(ThemeManager::instance()->current()->fontSizeMD));
    m_testConnStatus->clear();
    m_testConnStatus->setVisible(false);
    connRow->addWidget(m_testConnStatus);
    connRow->addStretch();
    layout->addLayout(connRow);

    auto* sep = new QLabel(tr("Test Prompt (debug):"), this);
    sep->setStyleSheet("font-weight: bold; margin-top: 8px;");
    layout->addWidget(sep);

    auto* testRow = new QHBoxLayout;
    m_testPromptInput = new QLineEdit(this);
    m_testPromptInput->setPlaceholderText(tr("e.g. increase contrast"));
    testRow->addWidget(m_testPromptInput, 1);

    m_testPromptBtn = new QPushButton(tr("Send"), this);
    m_testPromptBtn->setObjectName("okBtn");
    testRow->addWidget(m_testPromptBtn);
    layout->addLayout(testRow);

    m_testPromptOutput = new QPlainTextEdit(this);
    m_testPromptOutput->setReadOnly(true);
    m_testPromptOutput->setPlaceholderText(tr("Model response will appear here..."));
    m_testPromptOutput->setMaximumHeight(100);
    // m_testPromptOutput->setStyleSheet(
    //     QStringLiteral("QPlainTextEdit { background: %1; color: %2;"
    //         " border: 1px solid %3; border-radius: %4px;"
    //         " font: %5px monospace; }")
    //         .arg(ThemeManager::instance()->current()->colorBackgroundPrimary.name())
    //         .arg(ThemeManager::instance()->current()->colorTextPrimary.name())
    //         .arg(ThemeManager::instance()->current()->colorBorder.name())
    //         .arg(ThemeManager::instance()->current()->radiusMD)
    //         .arg(ThemeManager::instance()->current()->fontSizeMD));
    layout->addWidget(m_testPromptOutput);

    return group;
}

QWidget* AgentConfigWidget::createAdvancedSection()
{
    auto* group = new QGroupBox(tr("Advanced Settings"), this);
    group->setCheckable(true);
    group->setChecked(false);
    group->setFlat(false);

    auto* form = new QFormLayout(group);
    form->setLabelAlignment(Qt::AlignRight);

    m_timeoutSpin = new QSpinBox(this);
    m_timeoutSpin->setRange(1000, 300000);
    m_timeoutSpin->setValue(30000);
    m_timeoutSpin->setSingleStep(5000);
    m_timeoutSpin->setSuffix(tr(" ms"));
    m_timeoutSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Timeout:"), m_timeoutSpin);

    m_retriesSpin = new QSpinBox(this);
    m_retriesSpin->setRange(0, 10);
    m_retriesSpin->setValue(0);
    m_retriesSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    form->addRow(tr("Retries:"), m_retriesSpin);

    m_streamCheck = new AppCheckBox(tr("Enable streaming"), this);
    form->addRow(tr("Stream:"), m_streamCheck);

    m_logLevelCombo = new QComboBox(this);
    m_logLevelCombo->addItems({tr("debug"), tr("info"), tr("warn"), tr("error")});
    m_logLevelCombo->setCurrentText(tr("info"));
    form->addRow(tr("Log Level:"), m_logLevelCombo);

    m_cacheCheck = new AppCheckBox(tr("Enable response caching"), this);
    form->addRow(tr("Cache:"), m_cacheCheck);

    return group;
}

// ── Preset management ──

void AgentConfigWidget::refreshPresetCombo()
{
    if (!m_manager) return;

    // The real preset name is stored in item data; the label carries a kind
    // badge. Lookups must go through item data, never the display text.
    QString current = m_presetCombo->currentData().toString();
    m_presetCombo->blockSignals(true);
    m_presetCombo->clear();
    for (const auto& name : m_manager->presetNames()) {
        const AgentKind k = m_manager->preset(name).kind;
        QString badge = (k == Generative) ? tr("Generative") : tr("Assistant");
        m_presetCombo->addItem(QStringLiteral("%1  ·  %2").arg(name, badge), name);
    }
    int idx = m_presetCombo->findData(current);
    if (idx >= 0)
        m_presetCombo->setCurrentIndex(idx);
    else if (m_presetCombo->count() > 0)
        m_presetCombo->setCurrentIndex(0);
    m_presetCombo->blockSignals(false);

    m_deleteBtn->setEnabled(m_manager->presetNames().size() > 1);
}

void AgentConfigWidget::connectDirtySignals()
{
    auto mark = [this]() { markDirty(); };

    connect(m_nameEdit, &QLineEdit::textChanged, this, mark);
    connect(m_descEdit, &QLineEdit::textChanged, this, mark);
    connect(m_kindCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, mark);
    connect(m_providerCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, mark);
    connect(m_baseUrlEdit, &QLineEdit::textChanged, this, mark);
    connect(m_apiKeyEdit, &QLineEdit::textChanged, this, mark);
    connect(m_modelCombo, &QComboBox::currentTextChanged, this, mark);
    connect(m_tempSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, mark);
    connect(m_maxTokensSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, mark);
    connect(m_topPSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, mark);
    connect(m_freqPenaltySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, mark);
    connect(m_presencePenaltySpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, mark);
    connect(m_systemPromptEdit, &QPlainTextEdit::textChanged, this, mark);
    connect(m_toolsList, &QListWidget::itemChanged, this, mark);
    connect(m_stepsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, mark);
    connect(m_strengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, mark);
    connect(m_seedSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, mark);
    connect(m_negativePromptEdit, &QLineEdit::textChanged, this, mark);
    connect(m_promptAssistCheck, &QCheckBox::toggled, this, mark);
    connect(m_promptAssistCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, mark);
    connect(m_timeoutSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, mark);
    connect(m_retriesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, mark);
    connect(m_streamCheck, &QCheckBox::toggled, this, mark);
    connect(m_logLevelCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, mark);
    connect(m_cacheCheck, &QCheckBox::toggled, this, mark);
}

void AgentConfigWidget::markDirty()
{
    if (m_loadingPreset || m_switchingPreset)
        return;
    setDirty(true);
}

void AgentConfigWidget::setDirty(bool dirty)
{
    if (m_dirty != dirty)
        m_dirty = dirty;
    updateDirtyUi();
}

void AgentConfigWidget::updateDirtyUi()
{
    if (m_saveBtn)
        m_saveBtn->setEnabled(m_dirty);
    if (m_dirtyBtn)
        m_dirtyBtn->setVisible(m_dirty);
}

void AgentConfigWidget::resetTestConnectionStatus()
{
    if (!m_testConnStatus)
        return;

    m_testConnStatus->clear();
    m_testConnStatus->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
        .arg(ThemeManager::instance()->current()->colorTextSecondary.name())
        .arg(ThemeManager::instance()->current()->fontSizeMD));
    m_testConnStatus->setVisible(false);
}

bool AgentConfigWidget::saveChanges()
{
    if (!m_manager)
        return false;

    validateInputs();
    const QString previousName = m_loadedPresetName;
    saveFormToPreset(m_currentPreset);
    m_currentPreset.name = m_nameEdit->text().trimmed();
    if (m_currentPreset.name.isEmpty())
        return false;

    if (!previousName.isEmpty() &&
        previousName != m_currentPreset.name &&
        m_manager->exists(previousName)) {
        AgentConfig presetAtPreviousName = m_currentPreset;
        presetAtPreviousName.name = previousName;
        m_manager->save(presetAtPreviousName);
        m_manager->rename(previousName, m_currentPreset.name);
    } else {
        m_manager->save(m_currentPreset);
    }
    m_manager->setActivePreset(m_currentPreset.name);
    m_loadedPresetName = m_currentPreset.name;

    refreshPresetCombo();
    setPresetComboByName(m_loadedPresetName);
    setDirty(false);
    return true;
}

void AgentConfigWidget::discardChanges()
{
    if (!m_manager)
        return;

    QString name = m_loadedPresetName;
    if (!m_manager->exists(name))
        name = m_presetCombo->currentData().toString();
    if (!m_manager->exists(name))
        return;

    setPresetComboByName(name);
    loadPresetIntoForm(m_manager->preset(name));
}

AgentConfigWidget::PendingChangesChoice AgentConfigWidget::promptPendingChanges()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(tr("Unsaved Changes"));
    box.setText(tr("There are unsaved changes. Do you want to save before continuing?"));

    auto* saveBtn = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    auto* discardBtn = box.addButton(tr("Discard Changes"), QMessageBox::DestructiveRole);
    auto* cancelBtn = box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(qobject_cast<QPushButton*>(saveBtn));
    box.exec();

    if (box.clickedButton() == saveBtn)
        return PendingChangesChoice::Save;
    if (box.clickedButton() == discardBtn)
        return PendingChangesChoice::Discard;
    if (box.clickedButton() == cancelBtn)
        return PendingChangesChoice::Cancel;
    return PendingChangesChoice::Cancel;
}

void AgentConfigWidget::setPresetComboByName(const QString& name)
{
    if (!m_presetCombo)
        return;

    const int idx = m_presetCombo->findData(name);
    if (idx < 0)
        return;

    m_switchingPreset = true;
    m_presetCombo->setCurrentIndex(idx);
    m_switchingPreset = false;
}

void AgentConfigWidget::onPresetChanged(int index)
{
    if (index < 0 || m_loadingPreset || m_switchingPreset || !m_manager) return;

    const QString targetName = m_presetCombo->itemData(index).toString();
    const QString previousName = m_loadedPresetName;

    if (m_dirty) {
        const PendingChangesChoice choice = promptPendingChanges();
        if (choice == PendingChangesChoice::Cancel) {
            setPresetComboByName(previousName);
            return;
        }
        if (choice == PendingChangesChoice::Save && !saveChanges()) {
            setPresetComboByName(previousName);
            return;
        }
        if (choice == PendingChangesChoice::Save)
            setPresetComboByName(targetName);
    }

    if (m_manager->exists(targetName)) {
        loadPresetIntoForm(m_manager->preset(targetName));
    }
}

void AgentConfigWidget::onNewPreset()
{
    if (!m_manager) return;
    if (!confirmPendingChanges()) return;

    AgentConfig def;
    def.name = "New Agent";
    int n = 1;
    while (m_manager->exists(def.name))
        def.name = QString("New Agent %1").arg(++n);

    m_manager->save(def);
    refreshPresetCombo();

    int idx = m_presetCombo->findData(def.name);
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

void AgentConfigWidget::onDeletePreset()
{
    if (!m_manager) return;
    if (!confirmPendingChanges()) return;

    QString name = m_presetCombo->currentData().toString();
    if (m_manager->presetNames().size() <= 1) return;

    auto result = QMessageBox::question(this, tr("Delete Agent"),
        tr("Delete agent \"%1\"?").arg(name),
        QMessageBox::Yes | QMessageBox::No);
    if (result != QMessageBox::Yes) return;

    m_manager->remove(name);
    refreshPresetCombo();

    if (m_presetCombo->count() > 0)
        loadPresetIntoForm(m_manager->preset(m_presetCombo->currentData().toString()));
}

void AgentConfigWidget::onDuplicatePreset()
{
    if (!m_manager) return;
    if (!confirmPendingChanges()) return;

    QString name = m_presetCombo->currentData().toString();
    QString newName = m_manager->duplicate(name);
    if (newName.isEmpty()) return;

    refreshPresetCombo();
    int idx = m_presetCombo->findData(newName);
    if (idx >= 0) m_presetCombo->setCurrentIndex(idx);
}

// ── Slots ──

void AgentConfigWidget::onKindChanged(int /*index*/)
{
    if (m_loadingPreset) return;
    AgentKind kind = currentKind();
    repopulateProviderCombo(kind);
    updateKindVisibility(kind);
    updateProviderFields();
    markDirty();
}

void AgentConfigWidget::updateKindVisibility(AgentKind kind)
{
    const bool gen = (kind == Generative);
    setSectionTabVisible(m_modelSettingsTabIndex, !gen);
    setSectionTabVisible(m_behaviorTabIndex, !gen);
    setSectionTabVisible(m_generativeTabIndex, gen);
    ensureCurrentTabVisible();
}

void AgentConfigWidget::setSectionTabVisible(int index, bool visible)
{
    if (m_sectionsTabs && index >= 0 && index < m_sectionsTabs->count())
        m_sectionsTabs->setTabVisible(index, visible);
}

void AgentConfigWidget::ensureCurrentTabVisible()
{
    if (!m_sectionsTabs)
        return;

    const int current = m_sectionsTabs->currentIndex();
    if (current >= 0 && m_sectionsTabs->isTabVisible(current))
        return;

    for (int i = 0; i < m_sectionsTabs->count(); ++i) {
        if (m_sectionsTabs->isTabVisible(i)) {
            m_sectionsTabs->setCurrentIndex(i);
            return;
        }
    }
}

void AgentConfigWidget::onProviderChanged(int /*index*/)
{
    if (m_loadingPreset) return;
    updateProviderFields();
    markDirty();
}

void AgentConfigWidget::updateProviderFields()
{
    if (!m_providerCombo || m_providerCombo->currentIndex() < 0) return;
    ProviderConfig::Type type =
        static_cast<ProviderConfig::Type>(m_providerCombo->currentData().toInt());
    const AgentKind kind = currentKind();

    // Local-SD generative default differs from Ollama's local chat default.
    if (kind == Generative && type == ProviderConfig::Local)
        m_baseUrlEdit->setText(QStringLiteral("http://localhost:7860"));
    else
        m_baseUrlEdit->setText(ProviderConfig::defaultBaseUrl(type));

    m_apiKeyEdit->setText(type == ProviderConfig::Local ? "ollama" : "");

    if (type == ProviderConfig::Local)
        m_apiKeyEdit->setPlaceholderText(kind == Generative
            ? tr("API Key (not needed for local SD)")
            : tr("API Key (default: ollama)"));
    else if (type == ProviderConfig::OpenAI)
        m_apiKeyEdit->setPlaceholderText(tr("sk-... (required)"));
    else
        m_apiKeyEdit->setPlaceholderText(tr("API Key (optional)"));
}

void AgentConfigWidget::onTestConnection()
{
    saveFormToPreset(m_currentPreset);

    m_testConnStatus->setText(tr("Testing..."));
    m_testConnStatus->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
        .arg(ThemeManager::instance()->current()->colorWarning.name())
        .arg(ThemeManager::instance()->current()->fontSizeMD));
    m_testConnStatus->setVisible(true);

    if (m_client) {
        m_client->applyConfig(m_currentPreset);
        m_client->testConnection();
    }
}

void AgentConfigWidget::onConnectionResult(bool ok, const QString& details, const QStringList& models)
{
    if (ok) {
        m_testConnStatus->setText(details);
        m_testConnStatus->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
            .arg(ThemeManager::instance()->current()->colorSuccess.name())
            .arg(ThemeManager::instance()->current()->fontSizeMD));

        if (!models.isEmpty()) {
            m_loadingPreset = true;
            m_modelCombo->clear();
            for (const auto& m : models)
                m_modelCombo->addItem(m);
            m_modelCombo->setCurrentText(m_currentPreset.provider.model);
            m_loadingPreset = false;
        }
    } else {
        m_testConnStatus->setText(QString("Failed: %1").arg(details));
        m_testConnStatus->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
            .arg(ThemeManager::instance()->current()->colorDanger.name())
            .arg(ThemeManager::instance()->current()->fontSizeMD));
    }
    m_testConnStatus->setVisible(true);
}

void AgentConfigWidget::onModelsFetched(const QStringList& models)
{
    if (models.isEmpty()) return;

    QString current = m_modelCombo->currentText();
    m_loadingPreset = true;
    m_modelCombo->clear();
    for (const auto& m : models)
        m_modelCombo->addItem(m);

    if (!current.isEmpty())
        m_modelCombo->setCurrentText(current);
    m_loadingPreset = false;
}

void AgentConfigWidget::onDetectModels()
{
    saveFormToPreset(m_currentPreset);

    m_detectModelsBtn->setEnabled(false);
    m_detectModelsBtn->setText(tr("Loading..."));

    if (m_client) {
        m_client->applyConfig(m_currentPreset);
        m_client->fetchModels();
    }

    m_detectModelsBtn->setEnabled(true);
    m_detectModelsBtn->setText(tr("Detect"));
}

void AgentConfigWidget::onSavePreset()
{
    saveChanges();
}

void AgentConfigWidget::onTestPrompt()
{
    QString input = m_testPromptInput->text().trimmed();
    if (input.isEmpty() || !m_client) return;

    saveFormToPreset(m_currentPreset);
    m_client->applyConfig(m_currentPreset);

    m_testPromptBtn->setEnabled(false);
    m_testPromptOutput->setPlainText(tr("Processing..."));

    QString prompt = m_systemPromptEdit->toPlainText().trimmed();
    if (prompt.isEmpty()) {
        std::vector<std::string> enabled;
        for (int i = 0; i < m_toolsList->count(); ++i) {
            auto* item = m_toolsList->item(i);
            if (item->checkState() == Qt::Checked)
                enabled.push_back(item->data(Qt::UserRole).toString().toStdString());
        }
        prompt = QString::fromStdString(ToolCatalog::systemPrompt(enabled));
    }

    auto conn = std::make_shared<QMetaObject::Connection>();
    *conn = connect(m_client, &LLMClient::responseReceived, this,
        [this, conn](const QString& resp) {
            m_testPromptOutput->setPlainText(resp);
            m_testPromptBtn->setEnabled(true);
            disconnect(*conn);
        });

    auto errConn = std::make_shared<QMetaObject::Connection>();
    *errConn = connect(m_client, &LLMClient::errorOccurred, this,
        [this, errConn](const QString& err) {
            m_testPromptOutput->setPlainText(QString("Error: %1").arg(err));
            m_testPromptBtn->setEnabled(true);
            disconnect(*errConn);
        });

    m_client->sendAsync(prompt, input);
}

void AgentConfigWidget::onToolsCheckChanged()
{
    int total = m_toolsList->count();
    int checked = 0;
    for (int i = 0; i < total; ++i) {
        if (m_toolsList->item(i)->checkState() == Qt::Checked)
            checked++;
    }
    m_toolsCountLabel->setText(QString("(%1/%2 enabled)").arg(checked).arg(total));
}

// ── Form <-> Preset ──

void AgentConfigWidget::loadPresetIntoForm(const AgentConfig& preset)
{
    m_loadingPreset = true;
    m_currentPreset = preset;
    m_loadedPresetName = preset.name;

    m_nameEdit->setText(preset.name);
    m_descEdit->setText(preset.description);

    // Kind first — it governs the provider list and which sections show.
    {
        int ki = m_kindCombo->findData(static_cast<int>(preset.kind));
        m_kindCombo->setCurrentIndex(ki >= 0 ? ki : 0);
    }
    repopulateProviderCombo(preset.kind);
    updateKindVisibility(preset.kind);

    {
        int pi = m_providerCombo->findData(static_cast<int>(preset.provider.type));
        m_providerCombo->setCurrentIndex(pi >= 0 ? pi : 0);
    }
    m_baseUrlEdit->setText(preset.provider.baseUrl);
    m_apiKeyEdit->setText(preset.provider.apiKey);

    {
        QString model = preset.provider.model;
        int mi = m_modelCombo->findText(model);
        if (mi >= 0)
            m_modelCombo->setCurrentIndex(mi);
        else
            m_modelCombo->setCurrentText(model);
    }

    m_tempSpin->setValue(preset.modelSettings.temperature);
    m_maxTokensSpin->setValue(preset.modelSettings.maxTokens);
    m_topPSpin->setValue(preset.modelSettings.topP);
    m_freqPenaltySpin->setValue(preset.modelSettings.frequencyPenalty);
    m_presencePenaltySpin->setValue(preset.modelSettings.presencePenalty);

    m_systemPromptEdit->setPlainText(preset.behavior.systemPrompt);

    QStringList enabled = preset.behavior.enabledTools;
    for (int i = 0; i < m_toolsList->count(); ++i) {
        auto* item = m_toolsList->item(i);
        QString name = item->data(Qt::UserRole).toString();
        item->setCheckState(enabled.isEmpty() || enabled.contains(name)
                            ? Qt::Checked : Qt::Unchecked);
    }
    onToolsCheckChanged();

    m_timeoutSpin->setValue(preset.timeoutMs);
    m_retriesSpin->setValue(preset.retries);
    m_streamCheck->setChecked(preset.stream);

    int llIdx = m_logLevelCombo->findText(preset.logLevel, Qt::MatchFixedString);
    if (llIdx >= 0) m_logLevelCombo->setCurrentIndex(llIdx);
    m_cacheCheck->setChecked(preset.cache);

    // Generative settings
    m_stepsSpin->setValue(preset.generative.steps);
    m_strengthSpin->setValue(preset.generative.strength);
    m_seedSpin->setValue(preset.generative.seed);
    m_negativePromptEdit->setText(preset.generative.negativePrompt);
    m_promptAssistCheck->setChecked(preset.generative.promptAssistEnabled);
    {
        int ai = m_promptAssistCombo->findText(preset.generative.promptAssistPreset);
        if (ai >= 0) m_promptAssistCombo->setCurrentIndex(ai);
    }

    resetTestConnectionStatus();
    m_loadingPreset = false;
    setDirty(false);
}

void AgentConfigWidget::saveFormToPreset(AgentConfig& preset) const
{
    preset.name = m_nameEdit->text().trimmed();
    preset.description = m_descEdit->text().trimmed();
    preset.kind = currentKind();

    preset.provider.type = static_cast<ProviderConfig::Type>(
        m_providerCombo->currentData().toInt());
    preset.provider.baseUrl = m_baseUrlEdit->text().trimmed();
    preset.provider.apiKey = m_apiKeyEdit->text().trimmed();
    preset.provider.model = m_modelCombo->currentText().trimmed();

    preset.modelSettings.temperature = m_tempSpin->value();
    preset.modelSettings.maxTokens = m_maxTokensSpin->value();
    preset.modelSettings.topP = m_topPSpin->value();
    preset.modelSettings.frequencyPenalty = m_freqPenaltySpin->value();
    preset.modelSettings.presencePenalty = m_presencePenaltySpin->value();

    preset.behavior.systemPrompt = m_systemPromptEdit->toPlainText();

    QStringList enabled;
    for (int i = 0; i < m_toolsList->count(); ++i) {
        auto* item = m_toolsList->item(i);
        if (item->checkState() == Qt::Checked)
            enabled.append(item->data(Qt::UserRole).toString());
    }
    preset.behavior.enabledTools = enabled;
    preset.behavior.toolMode = AgentBehavior::Auto;
    preset.outputMode = AgentConfig::ToolCalls;

    preset.timeoutMs = m_timeoutSpin->value();
    preset.retries = m_retriesSpin->value();
    preset.stream = m_streamCheck->isChecked();
    preset.logLevel = m_logLevelCombo->currentText();
    preset.cache = m_cacheCheck->isChecked();

    preset.generative.steps = m_stepsSpin->value();
    preset.generative.strength = m_strengthSpin->value();
    preset.generative.seed = m_seedSpin->value();
    preset.generative.negativePrompt = m_negativePromptEdit->text().trimmed();
    preset.generative.promptAssistEnabled = m_promptAssistCheck->isChecked();
    preset.generative.promptAssistPreset = m_promptAssistCombo->currentText();
}

void AgentConfigWidget::validateInputs()
{
    QStringList errors;

    if (m_nameEdit->text().trimmed().isEmpty())
        errors << tr("Agent name is required");

    QString url = m_baseUrlEdit->text().trimmed();
    ProviderConfig::Type type =
        static_cast<ProviderConfig::Type>(m_providerCombo->currentData().toInt());

    if (url.isEmpty())
        errors << tr("Base URL is required");

    if (type == ProviderConfig::OpenAI && m_apiKeyEdit->text().trimmed().isEmpty())
        errors << tr("API Key is required for this provider");

    if (m_modelCombo->currentText().trimmed().isEmpty())
        errors << tr("Model name is required");

    if (!errors.isEmpty()) {
        m_validationLabel->setText(errors.join("\n"));
        m_validationLabel->show();
    } else {
        m_validationLabel->hide();
    }
}
