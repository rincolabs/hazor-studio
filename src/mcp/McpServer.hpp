#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include "HttpParser.hpp"
#include "McpSettings.hpp"

class ToolExecutor;
class ToolDefinition;

class McpServer : public QObject {
    Q_OBJECT

public:
    explicit McpServer(ToolExecutor* executor, QObject* parent = nullptr);

    bool start(quint16 port = McpSettings::kDefaultPort);
    void stop();
    bool isListening() const;
    quint16 serverPort() const;

signals:
    void serverStarted(quint16 port);
    void serverStopped();
    void requestHandled(const QString& toolName, bool success, int durationMs);
    void serverError(const QString& message);

private slots:
    void onNewConnection();
    void onReadyRead();
    void onClientDisconnected();

private:
    void handleRequest(QTcpSocket* socket, const McpRequest& req);
    void sendJsonResponse(QTcpSocket* socket, int statusCode,
                          const QJsonObject& body);
    void handleExecute(QTcpSocket* socket, const McpRequest& req);
    void handleListTools(QTcpSocket* socket);

    struct ClientState {
        QTcpSocket* socket;
        HttpParser parser;
    };

    QTcpServer* m_server = nullptr;
    ToolExecutor* m_executor = nullptr;
    QHash<QTcpSocket*, ClientState> m_clients;
};
