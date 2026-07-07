#include "McpSettingsPage.hpp"
#include "AppSettingsMetrics.hpp"
#include "AppCheckBox.hpp"
#include "mcp/McpSettings.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QFormLayout>
#include <QSpinBox>
#include <QGroupBox>
#include <QLabel>
#include <QSettings>

McpSettingsPage::McpSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* th = ThemeManager::instance()->current();

    auto* mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(24, 16, 24, 16);
    mainLay->setSpacing(th->spaceMD);

    // ── Description ──
    auto* description = new QLabel(
        tr("Hazor Studio embeds an MCP (Model Context Protocol) server that "
           "exposes the application's tools over a local HTTP endpoint. External "
           "AI assistants and agents can connect to this endpoint to inspect and "
           "control the current document — creating layers, running filters, and "
           "executing other actions programmatically.\n\n"
           "The server listens only on localhost (127.0.0.1) on the configured "
           "port. Changes take effect immediately."),
        this);
    description->setWordWrap(true);
    mainLay->addWidget(description);

    // ── Server ──
    auto* serverGroup = new QGroupBox(tr("MCP Server"), this);
    auto* serverForm = new QFormLayout(serverGroup);
    serverForm->setSpacing(th->spaceSM);
    serverForm->setContentsMargins(th->spaceLG, th->spaceMD, th->spaceLG, th->spaceMD);

    m_enabledCheck = new AppCheckBox(tr("Enable MCP server"), this);
    serverForm->addRow(QString(), m_enabledCheck);

    m_portSpin = new QSpinBox(this);
    m_portSpin->setRange(McpSettings::kMinPort, McpSettings::kMaxPort);
    m_portSpin->setFixedWidth(AppSettingsMetrics::kNumericFieldWidth);
    m_portSpin->setToolTip(tr("TCP port the MCP server listens on (localhost only)."));
    serverForm->addRow(tr("Port:"), m_portSpin);

    mainLay->addWidget(serverGroup);
    mainLay->addStretch();

    // Enable/disable the port field with the checkbox.
    connect(m_enabledCheck, &QCheckBox::toggled, m_portSpin, &QWidget::setEnabled);

    loadSettings();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &McpSettingsPage::applyTheme);
    applyTheme();
}

void McpSettingsPage::applyTheme()
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral("McpSettingsPage { background: %1; }")
        .arg(th->colorSurface.name()));
}

void McpSettingsPage::loadSettings()
{
    const bool enabled = McpSettings::enabled();
    m_enabledCheck->setChecked(enabled);
    m_portSpin->setValue(McpSettings::port());
    m_portSpin->setEnabled(enabled);
}

void McpSettingsPage::saveSettings()
{
    QSettings settings;
    settings.setValue(McpSettings::kEnabledKey, m_enabledCheck->isChecked());
    settings.setValue(McpSettings::kPortKey, m_portSpin->value());
}
