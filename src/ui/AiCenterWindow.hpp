#pragma once

#include <QWidget>

class AgentPresetManager;
class AiCenterBridge;
class LLMClient;
class McpServer;
class QLabel;
class QStackedLayout;
class QWebChannel;
class WebViewHost;

// Top-level window for the Hazor AI Center: the Vue dashboard from
// embedded-ui/, hosted in the same WebViewHost/QWebChannel infrastructure
// Neural Flow uses, but as its own window rather than a dock — this is a
// full settings-style application with its own navigation, not a panel
// alongside the canvas.
//
// Like NeuralFlowPanel, this is only the host: it creates the view, registers
// the one QWebChannel object the page may talk to (AiCenterBridge) and loads
// the compiled application out of the Qt resource system. Everything the page
// does goes through the bridge into existing AI subsystems.
//
// Hidden on close rather than destroyed, so reopening never re-pays the first
// load and the page keeps whatever state the user had (same reasoning as
// MainWindow's m_neuralFlowDock).
class AiCenterWindow : public QWidget {
    Q_OBJECT

public:
    explicit AiCenterWindow(AgentPresetManager* presetManager, LLMClient* client,
                            McpServer* mcpServer, QWidget* parent = nullptr);
    ~AiCenterWindow() override;

    // Shows/raises the window and navigates to `section`/`subsection`
    // ("" for a section's default landing tab). Safe to call whether or not
    // the window (or the page inside it) has been shown before.
    void openSection(const QString& section, const QString& subsection = {});

signals:
    // Connected to MainWindow::applyMcpSettings() — the server's lifecycle
    // stays owned there, as it is for every other caller of MCP settings.
    void mcpRestartRequested();

protected:
    void showEvent(QShowEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void showLoadFailure(const QString& detail);

    WebViewHost* m_webHost = nullptr;
    AiCenterBridge* m_bridge = nullptr;
    QWebChannel* m_channel = nullptr;
    QStackedLayout* m_stack = nullptr;
    QLabel* m_failure = nullptr;
    bool m_loadStarted = false;
    QString m_pendingSection;
    QString m_pendingSubsection;
};
