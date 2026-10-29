// System > Services (MCP). Settings are the same QSettings keys
// McpSettingsPage::saveSettings() already writes; restarting the live
// McpServer stays MainWindow::applyMcpSettings()'s job, reused via a signal
// rather than duplicated here.
#include "AiCenterBridge.hpp"

#include "mcp/McpServer.hpp"
#include "mcp/McpSettings.hpp"

#include <QSettings>
#include <algorithm>

QVariantMap AiCenterBridge::mcpStateVariant() const
{
    QVariantMap v;
    v[QStringLiteral("enabled")] = McpSettings::enabled();
    v[QStringLiteral("port")] = McpSettings::port();
    v[QStringLiteral("listening")] = m_mcpServer && m_mcpServer->isListening();
    return v;
}

QVariantMap AiCenterBridge::mcpState() const
{
    return mcpStateVariant();
}

QVariantMap AiCenterBridge::applyMcpState(bool enabled, int port)
{
    const int clampedPort = std::clamp(port, static_cast<int>(McpSettings::kMinPort),
                                       static_cast<int>(McpSettings::kMaxPort));

    QSettings settings;
    settings.setValue(QLatin1String(McpSettings::kEnabledKey), enabled);
    settings.setValue(QLatin1String(McpSettings::kPortKey), clampedPort);

    emit mcpRestartRequested(); // MainWindow::applyMcpSettings() reconciles the live server

    const QVariantMap state = mcpStateVariant();
    emit mcpStateChanged(state);
    emitOverviewChanged();
    return state;
}
