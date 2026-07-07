#pragma once

#include <QWidget>

class QSpinBox;
class AppCheckBox;

// Settings page for the embedded MCP (Model Context Protocol) server: lets the
// user toggle the server on/off and choose the TCP port it listens on.
class McpSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit McpSettingsPage(QWidget* parent = nullptr);

    void loadSettings();
    void saveSettings();

private:
    void applyTheme();

    AppCheckBox* m_enabledCheck = nullptr;
    QSpinBox* m_portSpin = nullptr;
};
