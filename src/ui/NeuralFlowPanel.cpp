#include "NeuralFlowPanel.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/NeuralFlowBridge.hpp"
#include "ui/WebViewHost.hpp"

#include <QDebug>
#include <QLabel>
#include <QShowEvent>
#include <QStackedLayout>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace {

// Where rcc put the compiled frontend. The resource prefix is set by the
// generated embedded-ui.qrc (see cmake/GenerateEmbeddedUiQrc.cmake), and the
// path inside it is Vite's output layout: one directory per entry point, with
// the hashed assets shared beside them.
const char* kNeuralFlowUrl = "qrc:/embedded-ui/neural-flow/index.html";

// The name the page looks the bridge up under. Changing it breaks
// embedded-ui/src/neural-flow/services/neuralFlow.ts.
const char* kBridgeObjectName = "neuralFlow";

} // namespace

NeuralFlowPanel::NeuralFlowPanel(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("neuralFlowPanel"));
    setAttribute(Qt::WA_StyledBackground, true);
    // A node editor is unusable in a narrow right-hand column. The dock starts
    // floating and large (MainWindow), and this keeps it from being squeezed
    // back down to a sidebar width when it is docked.
    setMinimumSize(480, 320);

    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->setStackingMode(QStackedLayout::StackOne);

    m_webHost = new WebViewHost(this);
    // LiteGraph serves its own canvas context menu; Qt's would cover it.
    m_webHost->setContextMenuPolicy(Qt::NoContextMenu);
    m_stack->addWidget(m_webHost);

    // Shown in place of the view when the resource is missing or fails to
    // parse. Silence there would look exactly like an empty editor.
    m_failure = new QLabel(this);
    m_failure->setObjectName(QStringLiteral("neuralFlowFailure"));
    m_failure->setAlignment(Qt::AlignCenter);
    m_failure->setWordWrap(true);
    m_stack->addWidget(m_failure);

    m_bridge = new NeuralFlowBridge(this);
    m_channel = new QWebChannel(this);
    m_channel->registerObject(QString::fromUtf8(kBridgeObjectName), m_bridge);
    m_webHost->page()->setWebChannel(m_channel);

    auto* settings = m_webHost->view()->settings();
    // The editor is entirely local: nothing it loads may reach the network or
    // the filesystem, and it has no reason to open a window.
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);

    connect(m_webHost, &WebViewHost::loadSucceeded, this, [this](const QUrl& url) {
        qInfo().noquote() << "[NeuralFlow] interface loaded |" << url.toString();
        m_stack->setCurrentWidget(m_webHost);
    });
    connect(m_webHost, &WebViewHost::loadFailed, this,
            [this](const QUrl& url, const QString& error) {
        qWarning().noquote() << "[NeuralFlow] interface failed to load |"
                             << url.toString() << "|" << error;
        showLoadFailure(error);
    });
    // A script that fails after the document loaded would otherwise be
    // completely silent: the load succeeded and the panel is simply empty.
    connect(m_webHost, &WebViewHost::consoleMessage, this,
            [](int level, const QString& message, int lineNumber, const QString& sourceId) {
        const QString where = sourceId.isEmpty()
            ? QString()
            : QStringLiteral(" (%1:%2)").arg(sourceId).arg(lineNumber);
        if (level >= static_cast<int>(QWebEnginePage::ErrorMessageLevel))
            qWarning().noquote() << "[NeuralFlow] js error |" << message << where;
        else if (level == static_cast<int>(QWebEnginePage::WarningMessageLevel))
            qWarning().noquote() << "[NeuralFlow] js warning |" << message << where;
        else
            qDebug().noquote() << "[NeuralFlow] js |" << message << where;
    });

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &NeuralFlowPanel::applyTheme);
    applyTheme();
}

void NeuralFlowPanel::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_loadStarted)
        return;
    m_loadStarted = true;
    m_webHost->load(QUrl(QString::fromUtf8(kNeuralFlowUrl)));
}

NeuralFlowPanel::~NeuralFlowPanel()
{
    // Stop the inference and free the context while there is still a thread to
    // free it on, before any Qt object a result would be posted to goes away.
    if (m_bridge)
        m_bridge->shutdown();
}

void NeuralFlowPanel::setController(ImageController* ctrl)
{
    // A generation already in flight keeps running: the canvas bridge holds its
    // own latch on the document it started against and drops a result whose
    // document is no longer the active one.
    m_bridge->setController(ctrl);
}

void NeuralFlowPanel::showLoadFailure(const QString& detail)
{
    m_failure->setText(tr("The Neural Flow interface could not be loaded.\n\n%1\n\n"
                          "Build the embedded interfaces (embedded-ui/) and rebuild Hazor.")
                           .arg(detail));
    m_stack->setCurrentWidget(m_failure);
}

void NeuralFlowPanel::applyTheme()
{
    const auto* t = ThemeManager::instance()->current();
    if (!t)
        return;
    // The page themes itself from the values NeuralFlowBridge sends over the
    // channel; this is only the frame around it, so that a load failure and the
    // gap behind a resizing web view are the panel colour and not white.
    setStyleSheet(QStringLiteral(R"(
        NeuralFlowPanel {
            background: %1;
        }
        QLabel#neuralFlowFailure {
            background: %1;
            color: %2;
            font-family: "%3";
            font-size: %4px;
            padding: %5px;
        }
    )")
        .arg(t->colorPanelBackground.name())
        .arg(t->colorTextSecondary.name())
        .arg(t->fontFamily)
        .arg(t->fontSizeMD)
        .arg(t->spaceLG));
    update();
}
