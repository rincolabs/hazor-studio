#include "AgentController.hpp"
#include "tools/ToolExecutor.hpp"
#include "tools/ToolCatalog.hpp"

#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDateTime>

AgentController::AgentController(ToolExecutor* executor, QObject* parent)
    : QObject(parent)
    , m_executor(executor)
{
    regenerateSystemPrompt();

    connect(&m_client, &LLMClient::textResponse,
            this, &AgentController::onTextResponse);
    connect(&m_client, &LLMClient::toolsParsed,
            this, &AgentController::onToolsParsed);
    connect(&m_client, &LLMClient::errorOccurred,
            this, &AgentController::onError);
}

void AgentController::setSystemPrompt(const QString& prompt)
{
    m_systemPrompt = prompt;
}

void AgentController::applyConfig(const AgentConfig& config)
{
    // The chat agent only ever runs Assistant presets. Generative presets are
    // routed to ImageGenProvider (src/ai/) and must not enter the chat loop.
    if (config.kind != Assistant)
        return;

    m_config = config;
    m_client.applyConfig(config);
    regenerateSystemPrompt();
}

void AgentController::regenerateSystemPrompt()
{
    if (!m_config.behavior.systemPrompt.isEmpty()) {
        m_systemPrompt = m_config.behavior.systemPrompt;
        return;
    }

    std::vector<std::string> enabled;
    for (const auto& t : m_config.behavior.enabledTools)
        enabled.push_back(t.toStdString());

    m_systemPrompt = QString::fromStdString(ToolCatalog::systemPrompt(enabled));
}

void AgentController::processNaturalLanguage(const QString& userInput)
{
    if ((m_client.isBusy() && !m_awaitingFeedback) || !m_executor) {
        emit errorMessage("Agent is busy or executor not available");
        return;
    }

    m_awaitingFeedback = false;
    m_lastUserInput = userInput;
    m_currentTools.clear();
    m_toolIndex = 0;
    m_toolResults.clear();

    emit processingStarted();

    if (m_config.maxHistoryTurns > 0)
        m_client.sendWithHistory(m_systemPrompt, userInput);
    else
        m_client.sendAsync(m_systemPrompt, userInput);
}

void AgentController::onTextResponse(const QString& text)
{
    if (!text.isEmpty())
        emit assistantResponse(text);
}

void AgentController::onToolsParsed(const std::vector<ToolCall>& tools)
{
    if (tools.empty()) {
        m_awaitingFeedback = false;
        emit processingFinished();
        return;
    }

    if (!m_awaitingFeedback) {
        m_client.addAssistantMessage(m_lastUserInput);
    }

    m_currentTools = tools;
    m_toolIndex = 0;
    m_toolResults.clear();

    emit toolsGenerated(tools);

    for (auto& tool : tools) {
        QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
        Q_UNUSED(timestamp)
        ToolResult r = m_executor->execute(tool);
        bool ok = r.success;

        QString resultStr = QString("  tool: %1\n  success: %2\n  message: %3\n")
            .arg(QString::fromStdString(tool.name))
            .arg(ok ? "true" : "false")
            .arg(QString::fromStdString(r.message));
        m_toolResults.append(resultStr);

        emit toolExecuted(tool, ok);
    }

    if (m_config.resultFeedback && !m_toolResults.isEmpty()) {
        QJsonObject feedbackObj;
        feedbackObj["type"] = "tool_results";
        QJsonArray resultsArr;
        for (const auto& tr : m_toolResults)
            resultsArr.append(tr);
        feedbackObj["results"] = resultsArr;

        QString feedbackMsg = QString(
            "The following tools were executed with these results:\n%1\n"
            "Respond to the user with a brief summary of what was done."
        ).arg(m_toolResults.join("\n"));

        m_client.addToolResultMessage(feedbackMsg);

        m_awaitingFeedback = true;
        m_client.sendWithHistory(m_systemPrompt,
            "Briefly summarize what was done in 1-2 sentences.");
    } else {
        m_client.addAssistantMessage("Tools executed.");
        m_awaitingFeedback = false;
        emit processingFinished();
    }
}

void AgentController::sendFeedbackToLlm()
{
    m_awaitingFeedback = true;
    m_client.sendWithHistory(m_systemPrompt,
        "Briefly summarize what was done.");
}

void AgentController::onError(const QString& error)
{
    m_awaitingFeedback = false;
    emit errorMessage(error);
    emit processingFinished();
}
