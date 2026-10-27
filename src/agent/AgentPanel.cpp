#include "AgentPanel.hpp"

#include "AgentWebBridge.hpp"
#include "ui/WebViewHost.hpp"

#include <QVBoxLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

AgentPanel::AgentPanel(AgentController* controller, QWidget* parent)
    : QWidget(parent)
    , m_controller(controller)
{
    setObjectName(QStringLiteral("agentPanel"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_webHost = new WebViewHost(this);
    m_webHost->setContextMenuPolicy(Qt::NoContextMenu);
    layout->addWidget(m_webHost);

    m_webBridge = new AgentWebBridge(m_controller, this);
    m_webChannel = new QWebChannel(this);
    m_webChannel->registerObject(QStringLiteral("agentBridge"), m_webBridge);
    m_webHost->page()->setWebChannel(m_webChannel);

    auto* settings = m_webHost->view()->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);

    connect(m_webBridge, &AgentWebBridge::messageSubmitted,
            this, &AgentPanel::inputSubmitted);
    connect(m_webBridge, &AgentWebBridge::agentSelected,
            this, &AgentPanel::agentSelected);
    connect(m_webBridge, &AgentWebBridge::configurationRequested,
            this, &AgentPanel::configureClicked);

    m_webHost->load(QUrl(QStringLiteral("qrc:/agent-web/index.html")));
}

void AgentPanel::setAgentList(const QStringList& names)
{
    m_webBridge->setAgentList(names);
}

void AgentPanel::setActiveAgent(const QString& name)
{
    m_webBridge->setActiveAgent(name);
}

void AgentPanel::setModelInfo(const QString& info)
{
    m_webBridge->setModelInfo(info);
}
