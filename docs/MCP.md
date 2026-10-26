# MCP Server

Hazor Studio embeds a [Model Context Protocol](https://modelcontextprotocol.io)
server so that external AI agents and automation scripts can drive the running
editor — discover the available tools and execute them with parameters, exactly
as the built-in AI agent does.

## Overview

| | |
|-|-|
| **Protocol** | [MCP](https://modelcontextprotocol.io) revision `2026-07-28`, JSON-RPC 2.0 over the **Streamable HTTP** transport |
| **Host** | `127.0.0.1` (localhost only — never exposed to the network) |
| **Port** | `8085` by default; configurable in **Settings → MCP Server** |
| **Endpoint** | `POST http://127.0.0.1:8085/mcp` (the single MCP endpoint) |
| **Lifecycle** | Started automatically at app launch (if enabled); stops when the app closes |

The server binds to **localhost only** (`QHostAddress::LocalHost`, see
[`McpServer::start`](../src/mcp/McpServer.cpp)), so it is only reachable from
processes running on the same machine. There is no authentication — treat it as
a local, single-user automation surface. Each connection serves exactly one
request and is then closed (`Connection: close`).

Every tool call is executed against the currently focused document in the
running editor, and is undoable from the app's history just like a manual edit.

> ⚙️ **Configuration:** the port and whether the server starts at all are exposed
> in **Settings → MCP Server**. The server is **enabled by default**. Changes
> apply immediately (the server is stopped/restarted on the new port when you
> click **OK** — no app restart required). The defaults are defined in
> [`McpSettings.hpp`](../src/mcp/McpSettings.hpp).

### If the configured port is already in use

There is **no automatic fallback or retry**. If another process already holds
the configured port, [`QTcpServer::listen`](../src/mcp/McpServer.cpp) fails
(`AddressInUseError`) and:

- `McpServer::start()` returns `false` and emits a `serverError` signal.
- The app logs `Failed to start MCP server on port <port>` (via `qWarning`) and
  **continues launching normally** — startup is not aborted and no error dialog
  is shown.
- The editor runs fully, but the MCP server is **unavailable** — clients cannot
  connect until you either free the port (stop the other process, or the other
  Hazor Studio instance) or pick a different port in **Settings → MCP Server**.

To check what is holding the port (default `8085`):

```bash
# Linux / macOS
lsof -i :8085          # or: ss -ltnp 'sport = :8085'
# Windows
netstat -ano | findstr :8085
```

A common cause is a **second Hazor Studio instance** already running — only the
first one to start gets the server.

## Protocol model

This is a conformant MCP `2026-07-28` server: **stateless**, with no
`initialize`/`initialized` handshake. Every JSON-RPC request carries its own
protocol version and client capabilities in `params._meta`, and the server
answers each request independently — there is no session and no connection
state to establish first.

The server implements only the capabilities Hazor actually uses:

- **`tools`** — list and call the ~93 editor operations shared with the
  in-app AI agent ([`ToolCatalog`](../src/tools/ToolCatalog.cpp)). The catalog
  is fixed at build time, so the server does not advertise `listChanged`.
- **`server/discover`** — the required discovery method that replaces the
  role `initialize` used to play (supported versions, capabilities, server
  identity), without a handshake.

Resources, prompts, elicitation/sampling/roots, change-notification streams
(`subscriptions/listen`) and optional extensions (Tasks, Apps, ...) are not
implemented — Hazor has no use for them, and the task asked explicitly not to
add capabilities the app doesn't use.

### Required headers

Every `POST /mcp` request **must** include:

| Header | Must match |
|---|---|
| `MCP-Protocol-Version` | `params._meta["io.modelcontextprotocol/protocolVersion"]` in the body |
| `Mcp-Method` | the JSON-RPC `method` field |
| `Mcp-Name` | `params.name`, **only for `tools/call`** |

A missing or mismatched header is rejected with JSON-RPC error `-32020`
(`HeaderMismatch`), HTTP `400`.

The server always answers with a single `Content-Type: application/json`
response (never a `text/event-stream`/SSE stream) — every tool call is a fast,
synchronous, in-process operation, so there is nothing to stream.

### `Origin` validation

To prevent DNS-rebinding attacks, a request carrying an `Origin` header is only
accepted if it is `http://127.0.0.1:<port>` or `http://localhost:<port>` for the
server's own port; anything else gets HTTP `403`. Requests without an `Origin`
header (curl, SDKs, most MCP clients) are accepted — only browser-issued
requests set it.

## Methods

### `server/discover`

Returns the protocol versions, capabilities and identity of the server. Call it
once to confirm you're talking to this server; it is **not** required before
calling other methods.

```bash
curl -s http://127.0.0.1:8085/mcp \
  -H 'Content-Type: application/json' \
  -H 'MCP-Protocol-Version: 2026-07-28' \
  -H 'Mcp-Method: server/discover' \
  -d '{
        "jsonrpc": "2.0",
        "id": 1,
        "method": "server/discover",
        "params": {
          "_meta": {
            "io.modelcontextprotocol/protocolVersion": "2026-07-28",
            "io.modelcontextprotocol/clientCapabilities": {}
          }
        }
      }'
```

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "resultType": "complete",
    "supportedVersions": ["2026-07-28"],
    "capabilities": { "tools": {} },
    "instructions": "Hazor Studio image editor automation. ...",
    "_meta": {
      "io.modelcontextprotocol/serverInfo": { "name": "Hazor Studio", "version": "0.0.2" }
    }
  }
}
```

### `tools/list`

Returns every tool Hazor supports, with its `inputSchema` (JSON Schema
2020-12) and a shared `outputSchema`. There is no pagination — the catalog is
small and static, so the full list is always returned in one response.

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "resultType": "complete",
    "tools": [
      {
        "name": "adjust_brightness",
        "description": "…",
        "inputSchema": {
          "type": "object",
          "properties": {
            "value": { "type": "number", "minimum": -1, "maximum": 1, "default": 0 }
          }
        },
        "outputSchema": {
          "type": "object",
          "properties": {
            "success": { "type": "boolean" },
            "message": { "type": "string" },
            "durationMs": { "type": "integer" }
          },
          "required": ["success", "message"]
        },
        "_meta": { "dev.hazor/category": "Adjust" }
      }
    ]
  }
}
```

`dev.hazor/category` is a vendor-prefixed `_meta` extension (one of `Layer`,
`Channels`, `Selection`, `Filter`, `Paint`, `Adjust`, `Transform`, `Document`,
`Text`, `Viewport`, `AI`) — purely informational grouping, not part of core MCP.

#### Smart Object tools (category `Layer`)

| Tool | Arguments | Effect |
|---|---|---|
| `smart_object_create` | `index` (default: the selection) | Converts the layer(s) into a Smart Object: the content moves into a new document inside the project and one instance takes its place |
| `smart_object_place_embedded` | `path` | Places a `.hazor` as an **embedded** Smart Object — its documents are copied into this project |
| `smart_object_place_linked` | `path` | Places a `.hazor` as a **linked** Smart Object — it keeps referencing that file |
| `smart_object_via_copy` | `index` (default: active) | Duplicates the instance with an **independent** copy of its document. Contrast with an ordinary duplicate, which gives a second instance of the *same* document |
| `smart_object_replace` | `index` (default: active), `path` | Replaces the content every instance of that document shows. On a linked instance it re-points the link |
| `smart_object_update` | `index` (default: active) | Re-reads a linked Smart Object's file and re-renders it. Hazor has no file watcher, so this is the explicit refresh |
| `smart_object_rasterize` | `index` (default: active) | Drops the reference, keeping the rendered pixels. The referenced document is untouched |

"Edit Contents" has no tool: it opens a tab rather than performing a document
operation, so there is nothing to report back. Note that `smart_object_update`
and the render it triggers are **asynchronous** — the call returns once the
render is scheduled, and the result lands on the canvas when it completes.

See [SMART-OBJECTS.md](SMART-OBJECTS.md) for the model behind these.

#### Channel tools (category `Channels`)

| Tool | Arguments | Effect |
|---|---|---|
| `channel_new` | `name`, `selected_areas`, `opacity` | Creates an empty (black) alpha channel |
| `channel_new_spot` | `name`, `solidity` | Creates a spot channel, inked inside the active selection |
| `channel_duplicate` | `index`, `name`, `invert` | Duplicates a channel in this document |
| `channel_delete` | `index` | Deletes a channel. Affects no other channel and no layer |
| `channel_rename` | `index`, `name` | Renames a channel without touching its pixels |
| `channel_reorder` | `from`, `to` | Moves a channel in the panel order, which is also the spot overprint order |
| `channel_set_visible` | `index`, `visible` | Shows or hides a channel. A view change, so it records no history entry |
| `channel_select` | `index` (`-1` = the RGB composite), `add` | Arms a channel as the edit target for the pixel tools |
| `channel_invert` | `index` | Inverts a channel's contents, honouring the active selection's coverage |
| `channel_fill` | `value` (0 = black, 1 = white) | Fills the armed channels, honouring the active selection's coverage |
| `channel_merge_spot` | `index` | Bakes a spot's ink into the colour channels. Destructive: it flattens the image |
| `channel_save_selection` | `index`, `mode` | Combines the active selection into an existing channel (replace/add/subtract/intersect) |
| `select_save_channel` | `name` | Saves the active selection as a new alpha channel |
| `select_load_channel` | `index`, `mode`, `invert` | Loads a channel as the selection. A spot's ink coverage is what gets selected |

With a channel armed, the grayscale-compatible filters and adjustments
(`gaussian_blur`, `adjust_brightness`, `posterize`, …) apply to that channel
instead of to the active layer. Split Channels and Merge Channels have no tool:
they create and consume documents (tabs), which is a UI operation rather than a
document one.

### `tools/call`

```bash
curl -s http://127.0.0.1:8085/mcp \
  -H 'Content-Type: application/json' \
  -H 'MCP-Protocol-Version: 2026-07-28' \
  -H 'Mcp-Method: tools/call' \
  -H 'Mcp-Name: adjust_brightness' \
  -d '{
        "jsonrpc": "2.0",
        "id": 3,
        "method": "tools/call",
        "params": {
          "name": "adjust_brightness",
          "arguments": { "value": 0.4 },
          "_meta": {
            "io.modelcontextprotocol/protocolVersion": "2026-07-28",
            "io.modelcontextprotocol/clientCapabilities": {}
          }
        }
      }'
```

Success:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "resultType": "complete",
    "content": [{ "type": "text", "text": "Tool 'adjust_brightness' executed successfully" }],
    "structuredContent": { "success": true, "message": "…", "durationMs": 12 },
    "isError": false
  }
}
```

A bad parameter (wrong type, or out of its `[minimum, maximum]` range) or a
tool that fails to run is **not** a transport/JSON-RPC error — it is a normal
`tools/call` result with `isError: true`, so a calling model can read the
message and retry with corrected arguments:

```json
{
  "jsonrpc": "2.0",
  "id": 3,
  "result": {
    "resultType": "complete",
    "content": [{ "type": "text", "text": "Parameter 'value': value 3 out of range [-1, 1]" }],
    "structuredContent": { "success": false, "message": "Parameter 'value': value 3 out of range [-1, 1]" },
    "isError": true
  }
}
```

An **unknown tool name** is a protocol error (`-32602 InvalidParams`), since no
retry with different arguments can fix it.

Any parameter not listed in the tool's `inputSchema` is ignored. An omitted
parameter falls back to its schema `default`.

## Error codes

| Code | Name | When |
|---|---|---|
| `-32700` | `ParseError` | Request body is not valid JSON |
| `-32600` | `InvalidRequest` | Missing/wrong `jsonrpc`, a missing/null/non-string/non-number `id`, or a notification (no `id`) — this server accepts no notifications |
| `-32601` | `MethodNotFound` | `method` is not `server/discover`, `tools/list`, or `tools/call` |
| `-32602` | `InvalidParams` | Missing required field, or unknown tool name in `tools/call` |
| `-32020` | `HeaderMismatch` | A required header is missing, or doesn't match the request body |
| `-32022` | `UnsupportedProtocolVersion` | `params._meta["io.modelcontextprotocol/protocolVersion"]` isn't `2026-07-28` |

| HTTP status | Meaning |
|-------------|---------|
| `200` | A JSON-RPC result — including a tool result with `isError: true` |
| `400` | `ParseError`, `InvalidRequest`, `InvalidParams`, `HeaderMismatch`, `UnsupportedProtocolVersion` |
| `403` | `Origin` header present and not an allowed localhost origin |
| `404` | Path other than `/mcp`, or `MethodNotFound` |
| `405` | Non-`POST` request to `/mcp` |

## Using it from an AI agent

The server speaks the real MCP wire protocol over HTTP, so any MCP client that
supports an HTTP-based transport can register it **directly** — no bridge or
wrapper script needed.

### Claude Code

```bash
claude mcp add --transport http hazor http://127.0.0.1:8085/mcp
```

The editor must be **running** for the server to answer — it is an in-process
HTTP server embedded in the app, not a standalone daemon.

### Anything else

1. Call `server/discover` to confirm the server and its supported protocol
   version.
2. Call `tools/list` to discover the tool catalog and parameter schemas.
3. For each action, call `tools/call` with the chosen tool `name` and
   `arguments`, and read `isError`/`structuredContent` to decide the next step.

## Breaking changes from the previous (pre-MCP) server

Earlier versions of Hazor exposed a proprietary REST API (`GET /mcp/tools`,
`POST /mcp/execute`, response envelope `{success, data, error}`) that was not
JSON-RPC and required a hand-written bridge script to use from a real MCP
client. That API is **gone**, replaced entirely by the conformant endpoint
above:

- Both old routes are removed; everything goes through `POST /mcp`.
- The call payload changed from `{"tool": "...", "params": {...}}` to the
  JSON-RPC `tools/call` shape (`method`/`params.name`/`params.arguments`/`_meta`).
- A parameter validation failure changed from HTTP `422` to HTTP `200` with
  `isError: true`.
- A tool execution failure changed from HTTP `500` to HTTP `200` with
  `isError: true`.

## Notes

- The port and enabled flag are read from `QSettings` at launch
  ([`MainWindow::applyMcpSettings`](../src/ui/MainWindow.cpp)); defaults live in
  [`McpSettings.hpp`](../src/mcp/McpSettings.hpp) (port `8085`, enabled).
- Implementation lives in [`src/mcp/`](../src/mcp/): [`McpServer`](../src/mcp/McpServer.cpp)
  owns the `QTcpServer`/HTTP transport and method dispatch;
  [`McpProtocol`](../src/mcp/McpProtocol.cpp) owns the JSON-RPC envelope,
  `_meta`/header validation, and the `ToolDefinition` → MCP `Tool` JSON
  translation — it has no knowledge of sockets or tool execution.
- The tool catalog is shared with the in-app AI agent, defined in
  [`src/tools/ToolCatalog.cpp`](../src/tools/ToolCatalog.cpp).
