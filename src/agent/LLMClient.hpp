#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QJsonArray>
#include <string>
#include <vector>
#include <functional>

#include "tools/ToolCall.hpp"
#include "AgentConfig.hpp"

class QNetworkAccessManager;
class QNetworkReply;

class LLMClient : public QObject {
    Q_OBJECT

public:
    explicit LLMClient(QObject* parent = nullptr);
    ~LLMClient() override;

    void applyConfig(const AgentConfig& config);

    void setEndpoint(const QString& host, int port = 11434);
    void setModel(const QString& model);
    void setTimeout(int seconds);

    void sendPrompt(const QString& systemPrompt, const QString& userMessage,
                    std::function<void(bool, QString)> callback);
    void sendAsync(const QString& systemPrompt, const QString& userMessage);

    void sendWithHistory(const QString& systemPrompt,
                         const QString& userMessage);

    void addAssistantMessage(const QString& content);
    void addToolResultMessage(const QString& toolResults);
    void clearHistory();

    void testConnection();
    void fetchModels();

    QStringList lastModelList() const { return m_lastModelList; }

    std::vector<ToolCall> parseToolCalls(const QString& json);
    QString stripToolCalls(QString& text);

    bool isBusy() const { return m_busy; }

    const AgentConfig& config() const { return m_config; }

signals:
    void responseReceived(const QString& rawResponse);
    void textResponse(const QString& text);
    void toolsParsed(const std::vector<ToolCall>& tools);
    void errorOccurred(const QString& errorMessage);

    void connectionTestResult(bool ok, const QString& details, const QStringList& models);
    void modelsFetched(const QStringList& models);

private:
    void doSend(const QJsonArray& messages,
                std::function<void(bool, QString)> callback);
    void handleReply(QNetworkReply* reply);
    void handleConnectionReply(QNetworkReply* reply);
    void handleModelListReply(QNetworkReply* reply);

    QString extractContent(const QString& rawResponse);
    QString extractErrorMessage(const QString& rawResponse);

    QNetworkAccessManager* m_net = nullptr;
    AgentConfig m_config;
    QString m_lastEndpoint;
    int m_timeout = 30;
    bool m_busy = false;
    QStringList m_lastModelList;

    QJsonArray m_history;
};
