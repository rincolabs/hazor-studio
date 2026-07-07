#pragma once

#include <QSettings>

// Shared configuration keys and defaults for the embedded MCP server, used by
// startup (main.cpp), the settings page, and the runtime restart path in
// MainWindow so they all agree on the same QSettings entries.
namespace McpSettings {

inline constexpr char kEnabledKey[] = "mcp/enabled";
inline constexpr char kPortKey[] = "mcp/port";

inline constexpr bool kDefaultEnabled = true;
inline constexpr int kDefaultPort = 49517;
inline constexpr int kMinPort = 1024;
inline constexpr int kMaxPort = 65535;

inline bool enabled()
{
    return QSettings().value(kEnabledKey, kDefaultEnabled).toBool();
}

inline quint16 port()
{
    int p = QSettings().value(kPortKey, kDefaultPort).toInt();
    if (p < kMinPort || p > kMaxPort)
        p = kDefaultPort;
    return static_cast<quint16>(p);
}

} // namespace McpSettings
