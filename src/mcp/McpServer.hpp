#pragma once

#include <QObject>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonValue>
#include "HttpParser.hpp"
#include "McpSettings.hpp"

class ToolExecutor;
class ToolDefinition;

// Transport and method dispatch for the MCP 2026-07-28 Streamable HTTP
// endpoint. Owns the QTcpServer/HttpParser plumbing and the single `POST
// /mcp` route; delegates JSON-RPC envelope/`_meta` shaping to McpProtocol and
// tool execution to ToolExecutor/ToolCatalog. See docs/MCP.md.
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
    void handleMcpEndpoint(QTcpSocket* socket, const McpRequest& req);
    void sendJsonResponse(QTcpSocket* socket, int statusCode, const QJsonObject& body);

    bool checkOrigin(const McpRequest& req) const;
    bool validateHeaders(const McpRequest& req, const QJsonValue& id, const QString& method,
                         const QJsonObject& params, QJsonObject& errorOut) const;

    QJsonObject dispatch(const QJsonValue& id, const QString& method, const QJsonObject& params);
    QJsonObject handleDiscover(const QJsonValue& id);
    QJsonObject handleToolsList(const QJsonValue& id);
    QJsonObject handleToolsCall(const QJsonValue& id, const QJsonObject& params);

    struct ClientState {
        QTcpSocket* socket;
        HttpParser parser;
    };

    QTcpServer* m_server = nullptr;
    ToolExecutor* m_executor = nullptr;
    QHash<QTcpSocket*, ClientState> m_clients;
};
