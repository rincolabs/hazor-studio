#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>

class AgentController;

// Narrow QWebChannel surface for the experimental web presentation. The
// AgentController remains the only owner of request, tool, and document state.
class AgentWebBridge : public QObject {
    Q_OBJECT

public:
    explicit AgentWebBridge(AgentController* controller, QObject* parent = nullptr);

    void setAgentList(const QStringList& names);
    void setActiveAgent(const QString& name);
    void setModelInfo(const QString& info);

    Q_INVOKABLE QVariantMap initialState() const;
    Q_INVOKABLE void sendMessage(const QString& text);
    Q_INVOKABLE void selectAgent(const QString& name);
    Q_INVOKABLE void requestConfiguration();
    Q_INVOKABLE void openExternalLink(const QString& url);

signals:
    void messageSubmitted(const QString& text);
    void agentSelected(const QString& name);
    void configurationRequested();

    void userMessageAdded(const QString& text, const QString& timestamp);
    void assistantMessageStarted(const QString& timestamp);
    void assistantMessageChunk(const QString& text);
    void assistantMessageFinished();
    void statusMessageAdded(const QString& text, const QString& timestamp);
    void toolMessageAdded(const QString& name, bool success, const QString& timestamp);
    void errorAdded(const QString& text, const QString& timestamp);
    void processingChanged(bool processing);
    void agentStateChanged(const QStringList& names, const QString& activeName);
    void modelInfoChanged(const QString& info);
    void themeChanged(const QVariantMap& theme);

private:
    static QString timestamp();
    static QVariantMap currentTheme();

    AgentController* m_controller = nullptr;
    QStringList m_agentNames;
    QString m_activeAgent;
    QString m_modelInfo;
    bool m_assistantTextShown = false;
};
