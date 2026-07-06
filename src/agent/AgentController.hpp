#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <vector>
#include "tools/ToolCall.hpp"
#include "LLMClient.hpp"
#include "AgentConfig.hpp"

class ToolExecutor;
class ToolCatalog;

class AgentController : public QObject {
    Q_OBJECT

public:
    explicit AgentController(ToolExecutor* executor, QObject* parent = nullptr);

    void processNaturalLanguage(const QString& userInput);
    void setSystemPrompt(const QString& prompt);
    void applyConfig(const AgentConfig& config);

    LLMClient* client() { return &m_client; }
    bool isProcessing() const { return m_client.isBusy() || m_awaitingFeedback; }
    const AgentConfig& config() const { return m_config; }

signals:
    void processingStarted();
    void processingFinished();
    void assistantResponse(const QString& text);
    void toolsGenerated(const std::vector<ToolCall>& tools);
    void toolExecuted(const ToolCall& tool, bool success);
    void errorMessage(const QString& msg);

private slots:
    void onTextResponse(const QString& text);
    void onToolsParsed(const std::vector<ToolCall>& tools);
    void onError(const QString& error);

private:
    void regenerateSystemPrompt();
    void sendFeedbackToLlm();

    LLMClient m_client;
    ToolExecutor* m_executor = nullptr;
    AgentConfig m_config;
    QString m_systemPrompt;

    bool m_awaitingFeedback = false;
    QString m_lastUserInput;
    std::vector<ToolCall> m_currentTools;
    int m_toolIndex = 0;
    QStringList m_toolResults;
};
