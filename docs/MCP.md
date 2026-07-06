# MCP / Tool HTTP Server

Hazor Studio exposes its internal tool system over a small HTTP server so that
external AI agents and automation scripts can drive the running editor — list the
available tools and execute them with parameters, exactly as the built-in AI
agent does.

## Overview

| | |
|-|-|
| **Protocol** | HTTP/1.1, JSON request/response, `Connection: close` (one request per connection) |
| **Host** | `127.0.0.1` (localhost only — never exposed to the network) |
| **Port** | `8080` (hardcoded) |
| **Base URL** | `http://127.0.0.1:8080` |
| **Lifecycle** | Started automatically at app launch; stops when the app closes |

The server binds to **localhost only** (`QHostAddress::LocalHost`, see
[`McpServer::start`](../src/mcp/McpServer.cpp)), so it is only reachable from
processes running on the same machine. There is no authentication — treat it as a
local, single-user automation surface. Each connection serves exactly one request
and is then closed (`Connection: close`).

Each request is handled against the currently focused document in the running
editor, and tool executions are undoable from the app's history just like manual
edits.

> 🚧 **Coming soon:** these settings (port, and whether the server starts at all)
> will be exposed in a configuration dialog in the next version. For now they are
> hardcoded.

### If port 8080 is already in use

The port is **hardcoded** and there is **no fallback, retry, or configuration
option**. If another process already holds port 8080,
[`QTcpServer::listen`](../src/mcp/McpServer.cpp) fails (`AddressInUseError`) and:

- `McpServer::start()` returns `false` and emits a `serverError` signal.
- The app logs `Failed to start MCP server on port 8080` (via `qWarning`) and
  **continues launching normally** — startup is not aborted and no error dialog
  is shown.
- The editor runs fully, but the tool HTTP server is **unavailable** — external
  agents cannot connect until you free port 8080 (stop the other process, or the
  other Hazor Studio instance) and restart the editor.

To check what is holding the port:

```bash
# Linux / macOS
lsof -i :8080          # or: ss -ltnp 'sport = :8080'
# Windows
netstat -ano | findstr :8080
```

A common cause is a **second Hazor Studio instance** already running — only the
first one to start gets the server.

## Endpoints

### `GET /mcp/tools`

Returns every tool the editor supports (~93 tools), with its parameters, value
ranges, and defaults. The catalog is grouped into categories: `Layer`,
`Selection`, `Filter`, `Paint`, `Adjust`, `Transform`, `Document`, `Text`,
`Viewport`, and `AI`.

```bash
curl http://127.0.0.1:8080/mcp/tools
```

Response:

```json
{
  "success": true,
  "data": {
    "tools": [
      {
        "name": "adjust_brightness",
        "description": "…",
        "category": "Adjust",
        "params": [
          { "name": "value", "type": "float", "min": -1, "max": 1, "default": 0 }
        ]
      }
    ]
  },
  "error": null
}
```

Each param's `type` is one of `int`, `float`, or `string`.

### `POST /mcp/execute`

Executes a single tool. The body is a JSON object with the tool `name` and a
`params` object.

```bash
curl -X POST http://127.0.0.1:8080/mcp/execute \
  -H "Content-Type: application/json" \
  -d '{
        "tool": "adjust_brightness",
        "params": { "value": 0.4 }
      }'
```

Success response:

```json
{
  "success": true,
  "data": {
    "tool": "adjust_brightness",
    "message": "…",
    "durationMs": 12
  },
  "error": null
}
```

The `message` string comes straight from the executed tool's `ToolResult`. On
failure the same `message` is returned in the `error` field with HTTP `500`.

## Response envelope

Every response uses the same envelope:

```json
{ "success": true|false, "data": <object|null>, "error": <string|null> }
```

| HTTP status | Meaning |
|-------------|---------|
| `200` | Tool executed / tools listed successfully |
| `400` | Malformed request or invalid JSON body / missing `tool` field |
| `404` | Unknown route, or unknown tool name |
| `422` | A parameter is the wrong type or out of its `[min, max]` range |
| `500` | The tool ran but failed (message in `error`) |

## Parameter rules

Validated in [`McpServer::handleExecute`](../src/mcp/McpServer.cpp) against the
definitions from `GET /mcp/tools`:

- **`string`** params are read from the JSON `params` object as-is; if missing or
  not a string, they default to an empty string.
- **`int` / `float`** params (everything that is not `string`) must be JSON
  numbers. An omitted numeric param falls back to its `default`. A non-number
  value returns `422`. A value outside the tool's `[min, max]` range returns
  `422`. Note that `int` bounds are enforced by range but values are not rounded
  — pass integer-valued numbers for `int` params.
- Any parameter not listed in the tool definition is ignored.

## Using it from an AI agent

The server speaks plain HTTP+JSON (not the JSON-RPC MCP wire protocol), so any
agent that can make HTTP calls can drive it directly:

1. Call `GET /mcp/tools` to discover the tool catalog and parameter schemas.
2. For each action, `POST /mcp/execute` with the chosen `tool` and `params`.
3. Read the `success` / `error` fields to decide the next step.

### Claude Code

Claude Code can call the endpoint in two ways.

**A) Directly via Bash / curl** — the simplest path. Ask Claude Code to hit the
running editor:

> "The Hazor Studio editor is running with a tool server on
> `http://127.0.0.1:8080`. Fetch `GET /mcp/tools` to see what's available, then
> `POST /mcp/execute` to raise the brightness and add a gaussian blur."

**B) As a registered MCP server (via a bridge)** — Claude Code's `mcp`
integration expects the JSON-RPC MCP protocol, while this server is a REST
endpoint. Wrap it with a thin MCP stdio bridge that maps MCP `tools/list` →
`GET /mcp/tools` and MCP `tools/call` → `POST /mcp/execute`, then register the
bridge:

```bash
claude mcp add hazor -- node /path/to/hazor-mcp-bridge.js
```

The bridge translates each MCP tool call into an HTTP request to
`http://127.0.0.1:8080` and returns the JSON `data`/`error` back to Claude Code.

> The editor must be **running** for the server to answer — it is an in-process
> HTTP server, not a standalone daemon.

## Notes

- The port is fixed at `8080` in the current build (`McpServer::start(8080)` in
  [`src/main.cpp`](../src/main.cpp)).
- Implementation lives in [`src/mcp/`](../src/mcp/) (`McpServer`, plus a
  lightweight `HttpParser` state machine over `QTcpServer`).
- The tool catalog is shared with the in-app AI agent, defined in
  [`src/tools/ToolCatalog.cpp`](../src/tools/ToolCatalog.cpp).
