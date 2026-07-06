#include "AgentPanel.hpp"
#include "AgentController.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QHBoxLayout>
#include <QDateTime>
#include <QScrollBar>
#include <QDebug>

AgentPanel::AgentPanel(AgentController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    auto* t = ThemeManager::instance()->current();
    setObjectName(QStringLiteral("agentPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    setAutoFillBackground(true);
    auto* pal = new QPalette();
    pal->setColor(QPalette::Window, t->colorSurface);
    pal->setColor(QPalette::Base, t->colorSurface);
    setPalette(*pal);
    delete pal;
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(t->spaceSM, t->spaceSM, t->spaceSM, t->spaceSM);
    layout->setSpacing(t->spaceSM);

    auto* agentRow = new QHBoxLayout;
    auto* agentLabel = new QLabel(tr("Agent:"), this);
    agentLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px;")
        .arg(t->colorTextSecondary.name())
        .arg(t->fontSizeMD));
    agentRow->addWidget(agentLabel);

    m_agentCombo = new QComboBox(this);
    m_agentCombo->setMinimumWidth(120);
    m_agentCombo->setObjectName("agentCombo");
    agentRow->addWidget(m_agentCombo);

    m_statusLabel = new QLabel(tr("AI Agent — ready"));
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px; padding: %3px;")
        .arg(t->colorTextSecondary.name())
        .arg(t->fontSizeMD)
        .arg(t->spaceXS));
    agentRow->addWidget(m_statusLabel);
    agentRow->addStretch();

    m_configBtn = new QPushButton(QString::fromUtf8("\xe2\x9a\x99"), this);
    m_configBtn->setObjectName("agentConfigBtn");
    m_configBtn->setFixedSize(24, 24);
    m_configBtn->setToolTip(tr("Configure Agent"));
    connect(m_configBtn, &QPushButton::clicked, this, &AgentPanel::configureClicked);
    agentRow->addWidget(m_configBtn);

    layout->addLayout(agentRow);


    m_modelInfo = new QLabel(tr("Model"));
    m_modelInfo->setStyleSheet(QStringLiteral("color: %1; font-size: %2px; padding: %3px %4px;")
        .arg(t->colorTextDisabled.name())
        .arg(t->fontSizeXS)
        .arg(t->spaceXS)
        .arg(t->spaceSM));
    layout->addWidget(m_modelInfo);


    m_logView = new QTextEdit(this);
    m_logView->setReadOnly(true);
    m_logView->setObjectName("agentLogView");

    auto* inputRow = new QHBoxLayout();

    m_input = new QLineEdit(this);
    m_input->setPlaceholderText(tr("Ask AI to edit the image..."));
    m_input->setObjectName("agentInput");

    m_submitBtn = new QPushButton(tr("Send"), this);
    m_submitBtn->setObjectName("agentSendBtn");

    inputRow->addWidget(m_input, 1);
    inputRow->addWidget(m_submitBtn);

    layout->addWidget(m_logView, 1);
    layout->addLayout(inputRow);

    setStyleSheet(t->agentPanelStyleSheet());

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        auto* th = ThemeManager::instance()->current();
        {
            QPalette p;
            p.setColor(QPalette::Window, th->colorSurface);
            p.setColor(QPalette::Base, th->colorSurface);
            setPalette(p);
        }
        setStyleSheet(th->agentPanelStyleSheet());
        m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: %2px; padding: %3px;")
            .arg(th->colorTextSecondary.name())
            .arg(th->fontSizeMD)
            .arg(th->spaceXS));
        m_modelInfo->setStyleSheet(QStringLiteral("color: %1; font-size: %2px; padding: %3px %4px;")
            .arg(th->colorTextDisabled.name())
            .arg(th->fontSizeSM)
            .arg(th->spaceXS)
            .arg(th->spaceSM));
    });

    connect(m_submitBtn, &QPushButton::clicked, this, &AgentPanel::onSubmit);
    connect(m_input, &QLineEdit::returnPressed, this, &AgentPanel::onSubmit);
    connect(m_agentCombo, &QComboBox::activated,
            this, &AgentPanel::onAgentComboChanged);

    if (m_controller) {
        connect(m_controller, &AgentController::processingStarted,
                this, &AgentPanel::onProcessingStarted);
        connect(m_controller, &AgentController::processingFinished,
                this, &AgentPanel::onProcessingFinished);
        connect(m_controller, &AgentController::assistantResponse,
                this, [this](const QString& text) {
                    m_assistantTextShown = true;
                    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
                    m_logView->append(
                        QString("[%1] Assistant: %2")
                            .arg(timestamp, text));
                    QScrollBar* sb = m_logView->verticalScrollBar();
                    sb->setValue(sb->maximum());
                });
        connect(m_controller, &AgentController::toolsGenerated,
                this, [this](const std::vector<ToolCall>& tools) {
                    // If the model replied with only tool-calls and no prose,
                    // surface a placeholder so the turn isn't visually silent.
                    if (m_assistantTextShown || tools.empty())
                        return;
                    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
                    m_logView->append(
                        QString("[%1] Assistant: %2")
                            .arg(timestamp,
                                 tr("Running %n tool(s)...", "", static_cast<int>(tools.size()))));
                    QScrollBar* sb = m_logView->verticalScrollBar();
                    sb->setValue(sb->maximum());
                });
        connect(m_controller, &AgentController::toolExecuted,
                this, [this](const ToolCall& tool, bool success) {
                    onToolExecuted(QString::fromStdString(tool.name), success);
                });
        connect(m_controller, &AgentController::errorMessage,
                this, &AgentPanel::onError);
    }
}

void AgentPanel::setAgentList(const QStringList& names)
{
    QString current = m_agentCombo->currentText();
    m_agentCombo->clear();
    m_agentCombo->addItems(names);
    int idx = names.indexOf(current);
    if (idx >= 0)
        m_agentCombo->setCurrentIndex(idx);
}

void AgentPanel::setActiveAgent(const QString& name)
{
    int idx = m_agentCombo->findText(name);
    if (idx >= 0)
        m_agentCombo->setCurrentIndex(idx);
}

void AgentPanel::setModelInfo(const QString& info)
{
    m_modelInfo->setText(info);
}

void AgentPanel::onAgentComboChanged(int index)
{
    if (index < 0) return;
    QString name = m_agentCombo->currentText();
    emit agentSelected(name);
}

void AgentPanel::onSubmit()
{
    QString text = m_input->text().trimmed();
    if (text.isEmpty() || !m_controller) return;

    m_input->clear();
    m_assistantTextShown = false;
    emit inputSubmitted(text);

    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logView->append(
        QString("[%1] User: %2").arg(timestamp, text));

    m_controller->processNaturalLanguage(text);
}

void AgentPanel::onProcessingStarted()
{
    m_submitBtn->setEnabled(false);
    m_input->setEnabled(false);
    m_statusLabel->setText(tr("AI Agent — processing..."));
}

void AgentPanel::onProcessingFinished()
{
    m_submitBtn->setEnabled(true);
    m_input->setEnabled(true);
    m_statusLabel->setText(tr("AI Agent — ready"));
}

void AgentPanel::onToolExecuted(const QString& name, bool success)
{
    auto* th = ThemeManager::instance()->current();
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    QString icon = success ? "\xe2\x9c\x93" : "\xe2\x9c\x97";
    QString color = success ? th->colorSuccess.name() : th->colorDanger.name();
    m_logView->append(
        QStringLiteral("<span style='color:%1;'>[%2]    %3 %4</span>")
            .arg(color, timestamp, icon, name));

    QScrollBar* sb = m_logView->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void AgentPanel::onError(const QString& msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logView->append(
        QString("[%1] Error: %2").arg(timestamp, msg));
}
