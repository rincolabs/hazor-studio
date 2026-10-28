#include "WebViewHost.hpp"

#include <QFileInfo>
#include <QVBoxLayout>
#include <QWebEngineLoadingInfo>
#include <QWebEnginePage>
#include <QWebEngineView>

#include <functional>
#include <utility>

namespace {

// The only way to see a page's console output: javaScriptConsoleMessage() is a
// virtual on the page, not a signal, so forwarding it needs a subclass.
class ConsoleForwardingPage : public QWebEnginePage {
public:
    using Forwarder = std::function<void(int, const QString&, int, const QString&)>;

    ConsoleForwardingPage(Forwarder forward, QObject* parent)
        : QWebEnginePage(parent), m_forward(std::move(forward))
    {}

protected:
    void javaScriptConsoleMessage(JavaScriptConsoleMessageLevel level, const QString& message,
                                  int lineNumber, const QString& sourceId) override
    {
        if (m_forward)
            m_forward(static_cast<int>(level), message, lineNumber, sourceId);
    }

private:
    Forwarder m_forward;
};

} // namespace

WebViewHost::WebViewHost(QWidget* parent)
    : QWidget(parent), m_view(new QWebEngineView(this))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->addWidget(m_view);

    // Installed before anything is loaded, and parented to the view so it
    // shares its lifetime. setPage() disposes of the one the view made for
    // itself, which has not been used for anything yet.
    m_view->setPage(new ConsoleForwardingPage(
        [this](int level, const QString& message, int lineNumber, const QString& sourceId) {
            emit consoleMessage(level, message, lineNumber, sourceId);
        },
        m_view));

    connect(m_view->page(), &QWebEnginePage::loadingChanged, this,
            [this](const QWebEngineLoadingInfo& info) {
        switch (info.status()) {
        case QWebEngineLoadingInfo::LoadStartedStatus:
            emit loadStarted(info.url());
            break;
        case QWebEngineLoadingInfo::LoadSucceededStatus:
            emit loadSucceeded(info.url());
            break;
        case QWebEngineLoadingInfo::LoadFailedStatus:
            emit loadFailed(info.url(), info.errorString());
            break;
        case QWebEngineLoadingInfo::LoadStoppedStatus:
            emit loadFailed(info.url(), tr("Loading was stopped."));
            break;
        }
    });
}

QWebEngineView* WebViewHost::view() const
{
    return m_view;
}

QWebEnginePage* WebViewHost::page() const
{
    return m_view->page();
}

void WebViewHost::load(const QUrl& url)
{
    m_view->load(url);
}

void WebViewHost::loadLocalFile(const QString& filePath)
{
    const QFileInfo file(filePath);
    if (!file.isFile()) {
        emit loadFailed(QUrl::fromLocalFile(file.absoluteFilePath()),
                        tr("Local HTML file does not exist: %1").arg(file.absoluteFilePath()));
        return;
    }
    load(QUrl::fromLocalFile(file.absoluteFilePath()));
}

void WebViewHost::setHtml(const QString& html, const QUrl& baseUrl)
{
    m_view->setHtml(html, baseUrl);
}

void WebViewHost::runJavaScript(const QString& script)
{
    m_view->page()->runJavaScript(script);
}
