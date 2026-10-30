#include "AiCenterWindow.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "ui/AiCenterBridge.hpp"
#include "ui/WebViewHost.hpp"

#include <QCloseEvent>
#include <QDebug>
#include <QGuiApplication>
#include <QLabel>
#include <QScreen>
#include <QShowEvent>
#include <QStackedLayout>
#include <QUrl>
#include <QWebChannel>
#include <QWebEnginePage>
#include <QWebEngineSettings>
#include <QWebEngineView>

namespace {

// Where rcc put the compiled frontend — same scheme as Neural Flow's, see
// cmake/GenerateEmbeddedUiQrc.cmake and embedded-ui/vite.config.ts's
// `ai-center` entry.
const char* kAiCenterUrl = "qrc:/embedded-ui/ai-center/index.html";

// The name the page looks the bridge up under. Changing it breaks
// embedded-ui/src/ai-center/services/client.ts.
const char* kBridgeObjectName = "aiCenter";

} // namespace

AiCenterWindow::AiCenterWindow(AgentPresetManager* presetManager, McpServer* mcpServer,
                               QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setObjectName(QStringLiteral("aiCenterWindow"));
    setWindowTitle(tr("Hazor AI Center"));
    resize(1280, 860);

    m_stack = new QStackedLayout(this);
    m_stack->setContentsMargins(0, 0, 0, 0);
    m_stack->setStackingMode(QStackedLayout::StackOne);

    m_webHost = new WebViewHost(this);
    m_stack->addWidget(m_webHost);

    m_failure = new QLabel(this);
    m_failure->setObjectName(QStringLiteral("aiCenterFailure"));
    m_failure->setAlignment(Qt::AlignCenter);
    m_failure->setWordWrap(true);
    m_stack->addWidget(m_failure);

    m_bridge = new AiCenterBridge(presetManager, mcpServer, this);
    m_bridge->setDialogParent(this);
    connect(m_bridge, &AiCenterBridge::mcpRestartRequested, this, &AiCenterWindow::mcpRestartRequested);

    m_channel = new QWebChannel(this);
    m_channel->registerObject(QString::fromUtf8(kBridgeObjectName), m_bridge);
    m_webHost->page()->setWebChannel(m_channel);

    auto* settings = m_webHost->view()->settings();
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
    settings->setAttribute(QWebEngineSettings::JavascriptCanOpenWindows, false);

    connect(m_webHost, &WebViewHost::loadSucceeded, this, [this](const QUrl& url) {
        qInfo().noquote() << "[AiCenter] interface loaded |" << url.toString();
        m_stack->setCurrentWidget(m_webHost);
    });
    connect(m_webHost, &WebViewHost::loadFailed, this,
            [this](const QUrl& url, const QString& error) {
        qWarning().noquote() << "[AiCenter] interface failed to load |"
                             << url.toString() << "|" << error;
        showLoadFailure(error);
    });
    connect(m_webHost, &WebViewHost::consoleMessage, this,
            [](int level, const QString& message, int lineNumber, const QString& sourceId) {
        const QString where = sourceId.isEmpty()
            ? QString()
            : QStringLiteral(" (%1:%2)").arg(sourceId).arg(lineNumber);
        if (level >= static_cast<int>(QWebEnginePage::ErrorMessageLevel))
            qWarning().noquote() << "[AiCenter] js error |" << message << where;
        else if (level == static_cast<int>(QWebEnginePage::WarningMessageLevel))
            qWarning().noquote() << "[AiCenter] js warning |" << message << where;
        else
            qDebug().noquote() << "[AiCenter] js |" << message << where;
    });

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this] {
        const auto* t = ThemeManager::instance()->current();
        if (!t)
            return;
        // AI Center's own design system is static (not theme-driven, see
        // embedded-ui/src/ai-center/design-system/); this only keeps the gap
        // behind a resizing web view, and a load failure, from flashing white.
        setStyleSheet(QStringLiteral("AiCenterWindow { background: %1; } "
                                     "QLabel#aiCenterFailure { background: %1; color: %2; "
                                     "font-family: \"%3\"; font-size: %4px; padding: %5px; }")
            .arg(t->colorPanelBackground.name(), t->colorTextSecondary.name(), t->fontFamily)
            .arg(t->fontSizeMD).arg(t->spaceLG));
    });
}

AiCenterWindow::~AiCenterWindow() = default;

void AiCenterWindow::openSection(const QString& section, const QString& subsection)
{
    m_bridge->setNavigationTarget(section, subsection);

    if (!isVisible()) {
        if (QScreen* screen = QGuiApplication::primaryScreen()) {
            const QRect available = screen->availableGeometry();
            move(available.center().x() - width() / 2, available.center().y() - height() / 2);
        }
    }
    show();
    raise();
    activateWindow();
}

void AiCenterWindow::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (m_loadStarted)
        return;
    m_loadStarted = true;
    m_webHost->load(QUrl(QString::fromUtf8(kAiCenterUrl)));
}

void AiCenterWindow::closeEvent(QCloseEvent* event)
{
    // Hidden, not destroyed: the page and whatever the user had open in it
    // survive, same reasoning as MainWindow's Neural Flow dock.
    event->ignore();
    hide();
}

void AiCenterWindow::showLoadFailure(const QString& detail)
{
    m_failure->setText(tr("The AI Center interface could not be loaded.\n\n%1\n\n"
                          "Build the embedded interfaces (embedded-ui/) and rebuild Hazor.")
                           .arg(detail));
    m_stack->setCurrentWidget(m_failure);
}
