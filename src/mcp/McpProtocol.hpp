#pragma once

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <string>

struct ToolDefinition;

// Pure MCP 2026-07-28 protocol layer: JSON-RPC envelopes, the stateless
// per-request `_meta` fields, and MCP error codes. Translates Hazor's
// ToolDefinition into the `Tool` JSON shape the spec defines. Knows nothing
// about sockets/HTTP (see McpServer) or tool execution (see src/tools).
namespace McpProtocol {

inline constexpr char kVersion[] = "2026-07-28";

inline constexpr char kMetaProtocolVersion[] = "io.modelcontextprotocol/protocolVersion";
inline constexpr char kMetaClientCapabilities[] = "io.modelcontextprotocol/clientCapabilities";
inline constexpr char kMetaServerInfo[] = "io.modelcontextprotocol/serverInfo";

enum class ErrorCode : int {
    ParseError = -32700,
    InvalidRequest = -32600,
    MethodNotFound = -32601,
    InvalidParams = -32602,
    InternalError = -32603,
    HeaderMismatch = -32020,
    UnsupportedProtocolVersion = -32022,
};

// HTTP status code the Streamable HTTP transport mandates (or recommends)
// for a given JSON-RPC error code.
int httpStatusFor(ErrorCode code);

// Builds a JSON-RPC success response. Stamps `result.resultType = "complete"`
// and `result._meta[serverInfo]` automatically; `resultFields` supplies the
// method-specific members (e.g. `tools`, `content`, ...).
QJsonObject buildResult(const QJsonValue& id, QJsonObject resultFields);

// Builds a JSON-RPC error response. `id` may be left undefined (default
// QJsonValue) when the request id could not be read at all.
QJsonObject buildError(const QJsonValue& id, ErrorCode code, const QString& message,
                       const QJsonValue& data = QJsonValue());

// Structural JSON-RPC checks on the parsed request body: `jsonrpc == "2.0"`,
// a string or numeric `id`, and a non-empty `method`. On success fills the
// three out-params and returns true; otherwise fills `errorOut` and returns false.
bool parseRequest(const QJsonObject& body, QJsonValue& idOut, QString& methodOut,
                   QJsonObject& paramsOut, QJsonObject& errorOut);

// Validates the required per-request `_meta` fields (protocolVersion,
// clientCapabilities) and that the requested protocol version is supported.
bool validateMeta(const QJsonValue& id, const QJsonObject& params, QJsonObject& errorOut);

// Decodes the `=?base64?...?=` sentinel the Streamable HTTP transport uses
// for header values that cannot be represented as plain ASCII.
std::string decodeHeaderValue(const std::string& value);

// Translates a ToolDefinition into an MCP `Tool` JSON object (name,
// description, inputSchema, outputSchema, and the category under a
// vendor-prefixed `_meta` key).
QJsonObject toolToJson(const ToolDefinition& def);

// Every Hazor tool call resolves to the same ToolResult{success, message}
// shape, so all tools share one outputSchema/structuredContent shape.
QJsonObject sharedOutputSchema();

} // namespace McpProtocol
