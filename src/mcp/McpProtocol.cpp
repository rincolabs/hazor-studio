#include "McpProtocol.hpp"
#include "tools/ToolCall.hpp"
#include "core/Version.hpp"

#include <QJsonArray>
#include <QByteArray>

namespace McpProtocol {

int httpStatusFor(ErrorCode code)
{
    switch (code) {
    case ErrorCode::MethodNotFound:
        return 404;
    case ErrorCode::ParseError:
    case ErrorCode::InvalidRequest:
    case ErrorCode::InvalidParams:
    case ErrorCode::HeaderMismatch:
    case ErrorCode::UnsupportedProtocolVersion:
        return 400;
    case ErrorCode::InternalError:
        break;
    }
    return 500;
}

QJsonObject buildResult(const QJsonValue& id, QJsonObject resultFields)
{
    QJsonObject meta = resultFields.value("_meta").toObject();
    meta[kMetaServerInfo] = QJsonObject{
        {"name", "Hazor Studio"},
        {"version", QString::fromUtf8(HAZOR_VERSION_STRING)},
    };
    resultFields["_meta"] = meta;
    resultFields["resultType"] = "complete";

    return QJsonObject{
        {"jsonrpc", "2.0"},
        {"id", id},
        {"result", resultFields},
    };
}

QJsonObject buildError(const QJsonValue& id, ErrorCode code, const QString& message,
                       const QJsonValue& data)
{
    QJsonObject error{
        {"code", static_cast<int>(code)},
        {"message", message},
    };
    if (!data.isUndefined() && !data.isNull())
        error["data"] = data;

    QJsonObject resp{
        {"jsonrpc", "2.0"},
        {"error", error},
    };
    if (!id.isUndefined())
        resp["id"] = id;
    return resp;
}

bool parseRequest(const QJsonObject& body, QJsonValue& idOut, QString& methodOut,
                   QJsonObject& paramsOut, QJsonObject& errorOut)
{
    const QJsonValue requestId = body.value("id");
    idOut = (requestId.isString() || requestId.isDouble()) ? requestId : QJsonValue();

    if (body.value("jsonrpc").toString() != QLatin1String("2.0")) {
        errorOut = buildError(idOut, ErrorCode::InvalidRequest,
                              "Invalid or missing 'jsonrpc' field; expected \"2.0\"");
        return false;
    }

    if (!body.contains("id") || requestId.isNull()) {
        // Notifications (no id) and a literal null id are both rejected: this
        // revision defines no client-to-server notifications over Streamable
        // HTTP, and MCP forbids a null id on requests.
        errorOut = buildError(QJsonValue(), ErrorCode::InvalidRequest,
                              "Request 'id' is required and must not be null; "
                              "notifications are not supported by this server");
        return false;
    }

    if (!requestId.isString() && !requestId.isDouble()) {
        errorOut = buildError(QJsonValue(), ErrorCode::InvalidRequest,
                              "Request 'id' must be a string or number");
        return false;
    }

    idOut = requestId;

    if (!body.value("method").isString() || body.value("method").toString().isEmpty()) {
        errorOut = buildError(idOut, ErrorCode::InvalidRequest,
                              "Missing required field: 'method'");
        return false;
    }

    methodOut = body.value("method").toString();
    paramsOut = body.value("params").toObject();
    return true;
}

bool validateMeta(const QJsonValue& id, const QJsonObject& params, QJsonObject& errorOut)
{
    QJsonObject meta = params.value("_meta").toObject();

    QJsonValue versionVal = meta.value(kMetaProtocolVersion);
    if (!versionVal.isString()) {
        errorOut = buildError(id, ErrorCode::InvalidParams,
                              QString("Missing required field: 'params._meta[\"%1\"]'")
                                  .arg(kMetaProtocolVersion));
        return false;
    }

    if (!meta.value(kMetaClientCapabilities).isObject()) {
        errorOut = buildError(id, ErrorCode::InvalidParams,
                              QString("Missing required field: 'params._meta[\"%1\"]'")
                                  .arg(kMetaClientCapabilities));
        return false;
    }

    if (versionVal.toString() != QLatin1String(kVersion)) {
        errorOut = buildError(id, ErrorCode::UnsupportedProtocolVersion,
                              "Unsupported protocol version",
                              QJsonObject{
                                  {"supported", QJsonArray{QString(kVersion)}},
                                  {"requested", versionVal.toString()},
                              });
        return false;
    }

    return true;
}

std::string decodeHeaderValue(const std::string& value)
{
    static const std::string kPrefix = "=?base64?";
    static const std::string kSuffix = "?=";

    if (value.size() <= kPrefix.size() + kSuffix.size()
        || value.compare(0, kPrefix.size(), kPrefix) != 0
        || value.compare(value.size() - kSuffix.size(), kSuffix.size(), kSuffix) != 0) {
        return value;
    }

    std::string encoded = value.substr(kPrefix.size(),
                                        value.size() - kPrefix.size() - kSuffix.size());
    QByteArray decoded = QByteArray::fromBase64(QByteArray::fromStdString(encoded));
    return decoded.toStdString();
}

QJsonObject sharedOutputSchema()
{
    return QJsonObject{
        {"type", "object"},
        {"properties", QJsonObject{
            {"success", QJsonObject{{"type", "boolean"}}},
            {"message", QJsonObject{{"type", "string"}}},
            {"durationMs", QJsonObject{{"type", "integer"}}},
        }},
        {"required", QJsonArray{"success", "message"}},
    };
}

QJsonObject toolToJson(const ToolDefinition& def)
{
    QJsonObject properties;
    for (const auto& p : def.params) {
        QJsonObject schema;
        if (p.type == "string") {
            schema["type"] = "string";
        } else if (p.type == "int") {
            schema["type"] = "integer";
            schema["minimum"] = p.minVal;
            schema["maximum"] = p.maxVal;
            schema["default"] = p.defaultVal;
        } else { // "float"
            schema["type"] = "number";
            schema["minimum"] = p.minVal;
            schema["maximum"] = p.maxVal;
            schema["default"] = p.defaultVal;
        }
        properties[QString::fromStdString(p.name)] = schema;
    }

    QJsonObject inputSchema{{"type", "object"}};
    if (def.params.empty())
        inputSchema["additionalProperties"] = false;
    else
        inputSchema["properties"] = properties;

    return QJsonObject{
        {"name", QString::fromStdString(def.name)},
        {"description", QString::fromStdString(def.description)},
        {"inputSchema", inputSchema},
        {"outputSchema", sharedOutputSchema()},
        {"_meta", QJsonObject{
            {"dev.hazor/category", QString::fromStdString(def.category)},
        }},
    };
}

} // namespace McpProtocol
