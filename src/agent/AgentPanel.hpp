#pragma once

#include <QStringList>
#include <QWidget>

class AgentController;
class AgentWebBridge;
class QWebChannel;
class WebViewHost;

class AgentPanel : public QWidget {
    Q_OBJECT

public:
    explicit AgentPanel(AgentController* controller, QWidget* parent = nullptr);

    void setAgentList(const QStringList& names);
    void setActiveAgent(const QString& name);
    void setModelInfo(const QString& info);

signals:
    void inputSubmitted(const QString& text);
    void agentSelected(const QString& name);
    void configureClicked();

private:
    AgentController* m_controller = nullptr;
    WebViewHost* m_webHost = nullptr;
    AgentWebBridge* m_webBridge = nullptr;
    QWebChannel* m_webChannel = nullptr;
};
