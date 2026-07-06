#include "GenerativeFillDialog.hpp"

#include "controller/ImageController.hpp"
#include "agent/AgentPresetManager.hpp"
#include "agent/AgentConfig.hpp"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QDoubleSpinBox>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QPushButton>
#include <QDialogButtonBox>
#include <QSignalBlocker>

GenerativeFillDialog::GenerativeFillDialog(ImageController* controller,
                                           AgentPresetManager* presets,
                                           QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
    , m_presets(presets)
{
    setWindowTitle(tr("Generative Fill"));
    setMinimumWidth(420);

    auto* layout = new QVBoxLayout(this);
    auto* form = new QFormLayout;

    m_presetCombo = new QComboBox(this);
    form->addRow(tr("Preset:"), m_presetCombo);

    m_promptEdit = new QPlainTextEdit(this);
    m_promptEdit->setPlaceholderText(tr("Describe what to generate in the selection..."));
    m_promptEdit->setMaximumHeight(80);
    form->addRow(tr("Prompt:"), m_promptEdit);

    m_negativeEdit = new QLineEdit(this);
    m_negativeEdit->setPlaceholderText(tr("Optional (overrides preset)"));
    form->addRow(tr("Negative:"), m_negativeEdit);

    m_modeCombo = new QComboBox(this);
    m_modeCombo->addItem(tr("Fill selection (active layer)"),
                         static_cast<int>(ImageController::GenFillMode::FillSelection));
    m_modeCombo->addItem(tr("Create as new layer"),
                         static_cast<int>(ImageController::GenFillMode::FillAsNewLayer));
    form->addRow(tr("Mode:"), m_modeCombo);

    m_strengthSpin = new QDoubleSpinBox(this);
    m_strengthSpin->setRange(0.0, 1.0);
    m_strengthSpin->setSingleStep(0.05);
    m_strengthSpin->setValue(0.75);
    form->addRow(tr("Strength:"), m_strengthSpin);

    m_stepsSpin = new QSpinBox(this);
    m_stepsSpin->setRange(1, 150);
    m_stepsSpin->setValue(20);
    form->addRow(tr("Steps:"), m_stepsSpin);

    m_seedSpin = new QSpinBox(this);
    m_seedSpin->setRange(-1, 2147483647);
    m_seedSpin->setValue(-1);
    form->addRow(tr("Seed:"), m_seedSpin);

    layout->addLayout(form);

    m_cloudWarning = new QLabel(
        tr("⚠ This provider sends your image to an external service."), this);
    m_cloudWarning->setWordWrap(true);
    m_cloudWarning->setStyleSheet("color: #d08770;");
    layout->addWidget(m_cloudWarning);

    m_cloudOptIn = new QCheckBox(tr("I understand and want to continue"), this);
    layout->addWidget(m_cloudOptIn);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
    m_runBtn = buttons->addButton(tr("Generate"), QDialogButtonBox::AcceptRole);
    layout->addWidget(buttons);

    connect(m_presetCombo, &QComboBox::currentTextChanged, this, [this]() {
        applyCurrentPresetSettings();
        onPresetChanged();
    });
    connect(buttons, &QDialogButtonBox::rejected, this, &GenerativeFillDialog::closeRequested);
    connect(m_runBtn, &QPushButton::clicked, this, &GenerativeFillDialog::onAccept);
    connect(m_cloudOptIn, &QCheckBox::toggled, this, [this]() { onPresetChanged(); });

    reloadPresets();
    onPresetChanged();
}

void GenerativeFillDialog::setController(ImageController* controller)
{
    m_controller = controller;
}

void GenerativeFillDialog::reloadPresets()
{
    const QString current = m_presetCombo->currentText();
    const QString active = m_presets ? m_presets->activeGenerativeName() : QString();

    QSignalBlocker blocker(m_presetCombo);
    m_presetCombo->clear();
    if (m_presets) {
        for (const auto& name : m_presets->presetNames(Generative))
            m_presetCombo->addItem(name);
    }

    int idx = current.isEmpty() ? -1 : m_presetCombo->findText(current);
    if (idx < 0 && !active.isEmpty())
        idx = m_presetCombo->findText(active);
    if (idx < 0 && m_presetCombo->count() > 0)
        idx = 0;
    const bool presetChanged = idx >= 0 && m_presetCombo->itemText(idx) != current;
    if (idx >= 0)
        m_presetCombo->setCurrentIndex(idx);

    blocker.unblock();
    if (presetChanged)
        applyCurrentPresetSettings();
    onPresetChanged();
}

bool GenerativeFillDialog::selectedIsCloud() const
{
    if (!m_presets || m_presetCombo->currentText().isEmpty()) return false;
    AgentConfig cfg = m_presets->preset(m_presetCombo->currentText());
    const QString url = cfg.provider.baseUrl;
    // Local SD (A1111) keeps everything on the machine; everything else is cloud.
    if (cfg.provider.type == ProviderConfig::Local) return false;
    if (cfg.provider.type == ProviderConfig::Custom &&
        (url.contains("localhost") || url.contains("127.0.0.1")))
        return false;
    return true;
}

void GenerativeFillDialog::applyCurrentPresetSettings()
{
    if (m_presets && !m_presetCombo->currentText().isEmpty()) {
        AgentConfig cfg = m_presets->preset(m_presetCombo->currentText());
        m_strengthSpin->setValue(cfg.generative.strength);
        m_stepsSpin->setValue(cfg.generative.steps);
        m_seedSpin->setValue(cfg.generative.seed);
        m_negativeEdit->setText(cfg.generative.negativePrompt);
    }
}

void GenerativeFillDialog::onPresetChanged()
{
    const bool cloud = selectedIsCloud();
    const bool noPreset = m_presetCombo->count() == 0;
    m_cloudWarning->setVisible(cloud);
    m_cloudOptIn->setVisible(cloud);
    m_runBtn->setEnabled(!noPreset && (!cloud || m_cloudOptIn->isChecked()));
}

void GenerativeFillDialog::onAccept()
{
    if (!m_controller || !m_presets) return;
    if (m_presetCombo->currentText().isEmpty()) return;

    AgentConfig cfg = m_presets->preset(m_presetCombo->currentText());
    m_controller->setGenerativePreset(cfg);

    auto mode = static_cast<ImageController::GenFillMode>(
        m_modeCombo->currentData().toInt());

    m_controller->generativeFill(
        m_promptEdit->toPlainText().trimmed(), mode,
        m_negativeEdit->text().trimmed(),
        m_strengthSpin->value(), m_stepsSpin->value(), m_seedSpin->value());
}
