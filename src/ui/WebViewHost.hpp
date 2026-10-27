#pragma once

#include <QString>
#include <QUrl>
#include <QWidget>

class QWebEnginePage;
class QWebEngineView;

// Qt Widgets host for embedded local HTML/CSS/JavaScript interfaces. Feature-
// specific QWebChannel contracts stay outside this generic view wrapper.
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

private:
    QWebEngineView* m_view = nullptr;
};
