#include "McpServer.hpp"
#include "tools/ToolExecutor.hpp"
#include "tools/ToolCatalog.hpp"
#include "tools/ToolCall.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QElapsedTimer>
#include <QHostAddress>

McpServer::McpServer(ToolExecutor* executor, QObject* parent)
    : QObject(parent)
    , m_server(new QTcpServer(this))
    , m_executor(executor)
{
    connect(m_server, &QTcpServer::newConnection,
            this, &McpServer::onNewConnection);
}

bool McpServer::start(quint16 port)
{
    if (!m_server->listen(QHostAddress::LocalHost, port)) {
        emit serverError("Failed to start MCP server: "
                         + m_server->errorString());
        return false;
    }
    emit serverStarted(m_server->serverPort());
    return true;
}

void McpServer::stop()
{
    for (auto it = m_clients.begin(); it != m_clients.end(); ++it) {
        it->socket->disconnectFromHost();
        it->socket->deleteLater();
    }
    m_clients.clear();
    m_server->close();
    emit serverStopped();
}

bool McpServer::isListening() const
{
    return m_server->isListening();
}

quint16 McpServer::serverPort() const
{
    return m_server->serverPort();
}

void McpServer::onNewConnection()
{
    while (auto* socket = m_server->nextPendingConnection()) {
        ClientState state;
        state.socket = socket;
        m_clients.insert(socket, state);

        connect(socket, &QTcpSocket::readyRead,
                this, &McpServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected,
                this, &McpServer::onClientDisconnected);
    }
}

void McpServer::onReadyRead()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;

    auto it = m_clients.find(socket);
    if (it == m_clients.end()) return;

    ClientState& state = it.value();
    QByteArray data = socket->readAll();

    HttpParser::State result = state.parser.feed(data);

    switch (result) {
    case HttpParser::State::Complete:
        handleRequest(socket, state.parser.request());
        break;
    case HttpParser::State::Error:
        sendJsonResponse(socket, 400, {
            {"success", false},
            {"data", QJsonValue::Null},
            {"error", QString::fromStdString(state.parser.errorMessage())}
        });
        break;
    default:
        break;
    }
}

void McpServer::onClientDisconnected()
{
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    m_clients.remove(socket);
    socket->deleteLater();
}

void McpServer::handleRequest(QTcpSocket* socket, const McpRequest& req)
{
    if (req.method == "POST" && req.path == "/mcp/execute") {
        handleExecute(socket, req);
    } else if (req.method == "GET" && req.path == "/mcp/tools") {
        handleListTools(socket);
    } else {
        sendJsonResponse(socket, 404, {
            {"success", false},
            {"data", QJsonValue::Null},
            {"error", QString("Route not found: %1 %2")
                          .arg(QString::fromStdString(req.method),
                               QString::fromStdString(req.path))}
        });
    }
}

void McpServer::sendJsonResponse(QTcpSocket* socket, int statusCode,
                                  const QJsonObject& body)
{
    QByteArray json = QJsonDocument(body).toJson(QJsonDocument::Compact);
    McpResponse resp;
    resp.statusCode = statusCode;
    resp.body = json.toStdString();
    QByteArray httpResp = HttpParser::buildResponse(resp);
    socket->write(httpResp);
    socket->flush();
    socket->disconnectFromHost();
}

void McpServer::handleExecute(QTcpSocket* socket, const McpRequest& req)
{
    QJsonDocument doc = QJsonDocument::fromJson(
        QByteArray::fromStdString(req.body));

    if (doc.isNull() || !doc.isObject()) {
        sendJsonResponse(socket, 400, {
            {"success", false},
            {"data", QJsonValue::Null},
            {"error", "Invalid JSON body"}
        });
        return;
    }

    QJsonObject obj = doc.object();

    if (!obj.contains("tool") || !obj["tool"].isString()) {
        sendJsonResponse(socket, 400, {
            {"success", false},
            {"data", QJsonValue::Null},
            {"error", "Missing required field: 'tool'"}
        });
        return;
    }

    std::string toolName = obj["tool"].toString().toStdString();

    const ToolDefinition* def = ToolCatalog::findTool(toolName);
    if (!def) {
        sendJsonResponse(socket, 404, {
            {"success", false},
            {"data", QJsonValue::Null},
            {"error", QString("Unknown tool: '%1'")
                          .arg(QString::fromStdString(toolName))}
        });
        return;
    }

    ToolCall call;
    call.name = toolName;

    QJsonObject paramsObj = obj["params"].toObject();

    for (const auto& pdef : def->params) {
        QString pname = QString::fromStdString(pdef.name);

        if (pdef.type == "string") {
            std::string val;
            if (paramsObj.contains(pname)) {
                QJsonValue v = paramsObj[pname];
                if (v.isString())
                    val = v.toString().toStdString();
            }
            call.params[pdef.name] = val;
        } else {
            double val = pdef.defaultVal;

            if (paramsObj.contains(pname)) {
                QJsonValue v = paramsObj[pname];
                if (v.isDouble()) {
                    val = v.toDouble();
                } else {
                    sendJsonResponse(socket, 422, {
                        {"success", false},
                        {"data", QJsonValue::Null},
                        {"error", QString("Parameter '%1' must be a number")
                                      .arg(pname)}
                    });
                    return;
                }
            }

            if (val < pdef.minVal || val > pdef.maxVal) {
                sendJsonResponse(socket, 422, {
                    {"success", false},
                    {"data", QJsonValue::Null},
                    {"error", QString("Parameter '%1': value %2 out of range [%3, %4]")
                                  .arg(pname).arg(val)
                                  .arg(pdef.minVal).arg(pdef.maxVal)}
                });
                return;
            }

            call.params[pdef.name] = val;
        }
    }

    QElapsedTimer timer;
    timer.start();

    ToolResult result = m_executor->execute(call);

    int elapsed = static_cast<int>(timer.elapsed());

    emit requestHandled(QString::fromStdString(toolName),
                        result.success, elapsed);

    if (result.success) {
        sendJsonResponse(socket, 200, {
            {"success", true},
            {"data", QJsonObject{
                {"tool", QString::fromStdString(toolName)},
                {"message", QString::fromStdString(result.message)},
                {"durationMs", elapsed}
            }},
            {"error", QJsonValue::Null}
        });
    } else {
        sendJsonResponse(socket, 500, {
            {"success", false},
            {"data", QJsonValue::Null},
            {"error", QString::fromStdString(result.message)}
        });
    }
}

void McpServer::handleListTools(QTcpSocket* socket)
{
    QJsonArray toolsArr;
    for (const auto& def : ToolCatalog::allTools()) {
        QJsonArray paramsArr;
        for (const auto& p : def.params) {
            QJsonObject pObj;
            pObj["name"] = QString::fromStdString(p.name);
            pObj["type"] = QString::fromStdString(p.type);
            pObj["min"] = p.minVal;
            pObj["max"] = p.maxVal;
            pObj["default"] = p.defaultVal;
            paramsArr.append(pObj);
        }

        QJsonObject tObj;
        tObj["name"] = QString::fromStdString(def.name);
        tObj["description"] = QString::fromStdString(def.description);
        tObj["category"] = QString::fromStdString(def.category);
        tObj["params"] = paramsArr;
        toolsArr.append(tObj);
    }

    QJsonObject dataObj;
    dataObj["tools"] = toolsArr;

    sendJsonResponse(socket, 200, {
        {"success", true},
        {"data", dataObj},
        {"error", QJsonValue::Null}
    });
}
