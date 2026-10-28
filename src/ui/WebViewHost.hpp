#pragma once

#include <QString>
#include <QUrl>
#include <QWidget>

class QWebEnginePage;
class QWebEngineView;

// Qt Widgets host for embedded local HTML/CSS/JavaScript interfaces. Feature-
// specific QWebChannel contracts stay outside this generic view wrapper.
//
// It also forwards the page's JavaScript console, because an embedded page has
// no console anyone is looking at: without this, a script that fails after the
// document loaded is completely silent — the load succeeded, and the panel is
// simply blank.
class WebViewHost : public QWidget {
    Q_OBJECT

public:
    explicit WebViewHost(QWidget* parent = nullptr);

    QWebEngineView* view() const;
    QWebEnginePage* page() const;

    void load(const QUrl& url);
    void loadLocalFile(const QString& filePath);
    void setHtml(const QString& html, const QUrl& baseUrl = QUrl());
    void runJavaScript(const QString& script);

signals:
    void loadStarted(const QUrl& url);
    void loadSucceeded(const QUrl& url);
    void loadFailed(const QUrl& url, const QString& error);
    // `level` is QWebEnginePage::JavaScriptConsoleMessageLevel (info, warning,
    // error), kept as an int so a listener does not have to include the Qt
    // WebEngine headers to connect to this.
    void consoleMessage(int level, const QString& message, int lineNumber,
                        const QString& sourceId);

private:
    QWebEngineView* m_view = nullptr;
};
