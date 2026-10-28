#include "AgentWebBridge.hpp"

#include "AgentController.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/WebThemeVariables.hpp"

#include <QDateTime>
#include <QDesktopServices>
#include <QUrl>

AgentWebBridge::AgentWebBridge(AgentController* controller, QObject* parent)
    : QObject(parent)
    , m_controller(controller)
{
    if (m_controller) {
        connect(m_controller, &AgentController::processingStarted, this, [this]() {
            m_assistantTextShown = false;
            emit processingChanged(true);
        });
        connect(m_controller, &AgentController::processingFinished, this, [this]() {
            emit processingChanged(false);
        });
        connect(m_controller, &AgentController::assistantResponse, this,
                [this](const QString& text) {
            if (text.isEmpty())
                return;
            m_assistantTextShown = true;
            emit assistantMessageStarted(timestamp());
            // The current backend delivers a complete response. Keeping this
            // as a separate chunk event lets the DOM update incrementally when
            // the backend gains token streaming, without replacing messages.
            emit assistantMessageChunk(text);
            emit assistantMessageFinished();
        });
        connect(m_controller, &AgentController::toolsGenerated, this,
                [this](const std::vector<ToolCall>& tools) {
            if (m_assistantTextShown || tools.empty())
                return;
            emit statusMessageAdded(
                tr("Running %n tool(s)...", "", static_cast<int>(tools.size())),
                timestamp());
        });
        connect(m_controller, &AgentController::toolExecuted, this,
                [this](const ToolCall& tool, bool success) {
            emit toolMessageAdded(QString::fromStdString(tool.name), success, timestamp());
        });
        connect(m_controller, &AgentController::errorMessage, this,
                [this](const QString& error) {
            emit errorAdded(error, timestamp());
        });
    }

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        emit themeChanged(webtheme::variables());
    });
}

void AgentWebBridge::setAgentList(const QStringList& names)
{
    m_agentNames = names;
    if (!m_activeAgent.isEmpty() && !m_agentNames.contains(m_activeAgent))
        m_activeAgent.clear();
    emit agentStateChanged(m_agentNames, m_activeAgent);
}

void AgentWebBridge::setActiveAgent(const QString& name)
{
    m_activeAgent = name;
    emit agentStateChanged(m_agentNames, m_activeAgent);
}

void AgentWebBridge::setModelInfo(const QString& info)
{
    m_modelInfo = info;
    emit modelInfoChanged(m_modelInfo);
}

QVariantMap AgentWebBridge::initialState() const
{
    QVariantMap state;
    state.insert(QStringLiteral("agents"), m_agentNames);
    state.insert(QStringLiteral("activeAgent"), m_activeAgent);
    state.insert(QStringLiteral("modelInfo"), m_modelInfo);
    state.insert(QStringLiteral("processing"), m_controller && m_controller->isProcessing());
    state.insert(QStringLiteral("theme"), webtheme::variables());
    return state;
}

void AgentWebBridge::sendMessage(const QString& text)
{
    const QString prompt = text.trimmed();
    if (prompt.isEmpty())
        return;

    if (!m_controller) {
        emit errorAdded(tr("Agent backend is unavailable."), timestamp());
        return;
    }
    if (m_controller->isProcessing()) {
        emit errorAdded(tr("The agent is already processing a request."), timestamp());
        return;
    }

    m_assistantTextShown = false;
    emit userMessageAdded(prompt, timestamp());
    emit messageSubmitted(prompt);
    m_controller->processNaturalLanguage(prompt);
}

void AgentWebBridge::selectAgent(const QString& name)
{
    if (!m_agentNames.contains(name) || name == m_activeAgent)
        return;
    emit agentSelected(name);
}

void AgentWebBridge::requestConfiguration()
{
    emit configurationRequested();
}

void AgentWebBridge::openExternalLink(const QString& url)
{
    const QUrl target(url);
    if (!target.isValid() || (target.scheme() != QStringLiteral("https") &&
                              target.scheme() != QStringLiteral("http"))) {
        emit errorAdded(tr("Only HTTP and HTTPS links can be opened."), timestamp());
        return;
    }
    QDesktopServices::openUrl(target);
}

QString AgentWebBridge::timestamp()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss"));
}
