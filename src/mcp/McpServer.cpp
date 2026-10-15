#include "McpServer.hpp"
#include "McpProtocol.hpp"
#include "tools/ToolExecutor.hpp"
#include "tools/ToolCatalog.hpp"
#include "tools/ToolCall.hpp"

#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QDebug>
#include <QElapsedTimer>
#include <QHostAddress>
#include <optional>

namespace {

// A tool execution error (bad parameter, business-logic failure) is a normal
// `tools/call` *result* with `isError: true`, not a JSON-RPC error — it is
// feedback the calling model can act on and retry, per the MCP tools error
// handling model.
QJsonObject buildToolExecutionError(const QJsonValue& id, const QString& message)
{
    return McpProtocol::buildResult(id, QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"},
            {"text", message},
        }}},
        {"structuredContent", QJsonObject{
            {"success", false},
            {"message", message},
        }},
        {"isError", true},
    });
}

// A response is either `{..., "result": {...}}` or `{..., "error": {"code": ...}}`;
// map the latter's code to the HTTP status the transport spec requires.
int statusCodeForResponse(const QJsonObject& response)
{
    if (!response.contains("error"))
        return 200;
    int code = response.value("error").toObject().value("code").toInt();
    return McpProtocol::httpStatusFor(static_cast<McpProtocol::ErrorCode>(code));
}

} // namespace

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
    if (req.path != "/mcp") {
        sendJsonResponse(socket, 404, {
            {"error", QString("Not found: %1 %2")
                          .arg(QString::fromStdString(req.method),
                               QString::fromStdString(req.path))}
        });
        return;
    }

    if (req.method != "POST") {
        sendJsonResponse(socket, 405, {
            {"error", "Only POST is supported on the MCP endpoint"}
        });
        return;
    }

    handleMcpEndpoint(socket, req);
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

bool McpServer::checkOrigin(const McpRequest& req) const
{
    auto it = req.headers.find("origin");
    if (it == req.headers.end())
        return true; // non-browser clients (curl, SDKs) send no Origin at all.

    const QString origin = QString::fromStdString(it->second);
    const quint16 port = serverPort();
    return origin.compare(QString("http://127.0.0.1:%1").arg(port), Qt::CaseInsensitive) == 0
        || origin.compare(QString("http://localhost:%1").arg(port), Qt::CaseInsensitive) == 0;
}

bool McpServer::validateHeaders(const McpRequest& req, const QJsonValue& id, const QString& method,
                                const QJsonObject& params, QJsonObject& errorOut) const
{
    auto header = [&req](const char* name) -> std::optional<std::string> {
        auto it = req.headers.find(name);
        if (it == req.headers.end())
            return std::nullopt;
        return it->second;
    };

    auto protocolVersionHeader = header("mcp-protocol-version");
    if (!protocolVersionHeader) {
        errorOut = McpProtocol::buildError(id, McpProtocol::ErrorCode::HeaderMismatch,
            "Missing required header: 'MCP-Protocol-Version'");
        return false;
    }
    const QString bodyVersion = params.value("_meta").toObject()
        .value(McpProtocol::kMetaProtocolVersion).toString();
    const QString headerVersion = QString::fromStdString(*protocolVersionHeader);
    if (headerVersion != bodyVersion) {
        errorOut = McpProtocol::buildError(id, McpProtocol::ErrorCode::HeaderMismatch,
            QString("Header mismatch: 'MCP-Protocol-Version' (%1) does not match "
                    "body value (%2)").arg(headerVersion, bodyVersion));
        return false;
    }

    auto methodHeader = header("mcp-method");
    if (!methodHeader) {
        errorOut = McpProtocol::buildError(id, McpProtocol::ErrorCode::HeaderMismatch,
            "Missing required header: 'Mcp-Method'");
        return false;
    }
    const QString headerMethod = QString::fromStdString(*methodHeader);
    if (headerMethod != method) {
        errorOut = McpProtocol::buildError(id, McpProtocol::ErrorCode::HeaderMismatch,
            QString("Header mismatch: 'Mcp-Method' (%1) does not match body "
                    "'method' (%2)").arg(headerMethod, method));
        return false;
    }

    if (method == QLatin1String("tools/call")) {
        auto nameHeader = header("mcp-name");
        if (!nameHeader) {
            errorOut = McpProtocol::buildError(id, McpProtocol::ErrorCode::HeaderMismatch,
                "Missing required header: 'Mcp-Name'");
            return false;
        }
        const QString headerName = QString::fromStdString(
            McpProtocol::decodeHeaderValue(*nameHeader));
        const QString bodyName = params.value("name").toString();
        if (headerName != bodyName) {
            errorOut = McpProtocol::buildError(id, McpProtocol::ErrorCode::HeaderMismatch,
                QString("Header mismatch: 'Mcp-Name' (%1) does not match body "
                        "'params.name' (%2)").arg(headerName, bodyName));
            return false;
        }
    }

    return true;
}

void McpServer::handleMcpEndpoint(QTcpSocket* socket, const McpRequest& req)
{
    if (!checkOrigin(req)) {
        sendJsonResponse(socket, 403, {{"error", "Origin not allowed"}});
        return;
    }

    QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(req.body));
    if (!doc.isObject()) {
        QJsonObject error = McpProtocol::buildError(QJsonValue(),
            McpProtocol::ErrorCode::ParseError, "Invalid JSON-RPC request body");
        sendJsonResponse(socket, statusCodeForResponse(error), error);
        return;
    }
    QJsonObject body = doc.object();

    QJsonValue id;
    QString method;
    QJsonObject params;
    QJsonObject error;

    if (!McpProtocol::parseRequest(body, id, method, params, error)) {
        sendJsonResponse(socket, statusCodeForResponse(error), error);
        return;
    }

    if (!McpProtocol::validateMeta(id, params, error)) {
        sendJsonResponse(socket, statusCodeForResponse(error), error);
        return;
    }

    if (!validateHeaders(req, id, method, params, error)) {
        sendJsonResponse(socket, statusCodeForResponse(error), error);
        return;
    }

    QJsonObject response = dispatch(id, method, params);
    sendJsonResponse(socket, statusCodeForResponse(response), response);
}

QJsonObject McpServer::dispatch(const QJsonValue& id, const QString& method, const QJsonObject& params)
{
    if (method == QLatin1String("server/discover"))
        return handleDiscover(id);
    if (method == QLatin1String("tools/list"))
        return handleToolsList(id);
    if (method == QLatin1String("tools/call"))
        return handleToolsCall(id, params);

    return McpProtocol::buildError(id, McpProtocol::ErrorCode::MethodNotFound,
        QString("Unknown method: '%1'").arg(method));
}

QJsonObject McpServer::handleDiscover(const QJsonValue& id)
{
    return McpProtocol::buildResult(id, QJsonObject{
        {"supportedVersions", QJsonArray{QString(McpProtocol::kVersion)}},
        {"capabilities", QJsonObject{{"tools", QJsonObject{}}}},
        {"instructions",
         "Hazor Studio image editor automation. Tools act on the document "
         "currently focused in the running editor and are undoable from the "
         "app's history like manual edits. Call tools/list for parameters."},
    });
}

QJsonObject McpServer::handleToolsList(const QJsonValue& id)
{
    QJsonArray tools;
    for (const auto& def : ToolCatalog::allTools())
        tools.append(McpProtocol::toolToJson(def));

    return McpProtocol::buildResult(id, QJsonObject{{"tools", tools}});
}

QJsonObject McpServer::handleToolsCall(const QJsonValue& id, const QJsonObject& params)
{
    if (!params.value("name").isString()) {
        return McpProtocol::buildError(id, McpProtocol::ErrorCode::InvalidParams,
            "Missing required field: 'params.name'");
    }
    const std::string toolName = params.value("name").toString().toStdString();

    const ToolDefinition* def = ToolCatalog::findTool(toolName);
    if (!def) {
        return McpProtocol::buildError(id, McpProtocol::ErrorCode::InvalidParams,
            QString("Unknown tool: '%1'").arg(QString::fromStdString(toolName)));
    }

    QJsonObject arguments = params.value("arguments").toObject();

    ToolCall call;
    call.name = toolName;

    for (const auto& pdef : def->params) {
        const QString pname = QString::fromStdString(pdef.name);

        if (pdef.type == "string") {
            std::string val;
            QJsonValue v = arguments.value(pname);
            if (v.isString())
                val = v.toString().toStdString();
            call.params[pdef.name] = val;
            continue;
        }

        double val = pdef.defaultVal;
        if (arguments.contains(pname)) {
            QJsonValue v = arguments.value(pname);
            if (!v.isDouble()) {
                return buildToolExecutionError(id,
                    QString("Parameter '%1' must be a number").arg(pname));
            }
            val = v.toDouble();
        }

        if (val < pdef.minVal || val > pdef.maxVal) {
            return buildToolExecutionError(id,
                QString("Parameter '%1': value %2 out of range [%3, %4]")
                    .arg(pname).arg(val).arg(pdef.minVal).arg(pdef.maxVal));
        }

        call.params[pdef.name] = val;
    }

    QElapsedTimer timer;
    timer.start();
    ToolResult result = m_executor->execute(call);
    int elapsed = static_cast<int>(timer.elapsed());

    emit requestHandled(QString::fromStdString(toolName), result.success, elapsed);

    const QString message = QString::fromStdString(result.message);
    return McpProtocol::buildResult(id, QJsonObject{
        {"content", QJsonArray{QJsonObject{
            {"type", "text"},
            {"text", message},
        }}},
        {"structuredContent", QJsonObject{
            {"success", result.success},
            {"message", message},
            {"durationMs", elapsed},
        }},
        {"isError", !result.success},
    });
}
