#pragma once

#include <QHash>
#include <QImage>
#include <QList>
#include <QSize>
#include <QString>
#include <QWidget>

class FlowLayout;
class QLabel;
class QScrollArea;
class QStackedWidget;
class QTabBar;
class RecentThumbnailCache;
class Document;

// Fixed, non-document page hosted by MainWindow's document QTabWidget. It owns
// only presentation and delegates all document creation/opening to MainWindow.
class HomeWorkspace : public QWidget {
    Q_OBJECT

public:
    explicit HomeWorkspace(QWidget* parent = nullptr);

    void storeDocumentThumbnail(const QString& path, const Document* document);

signals:
    void newDocumentRequested();
    void recentDocumentRequested(const QString& path);
    void templateRequested(const QString& name, int width, int height);

private slots:
    void rebuildRecent();
    void rebuildTemplates();
    void applyTheme();

private:
    void applyTemplateFilter(const QString& platform);
    void applyThumbnail(const QString& path, const QImage& thumbnail,
                        const QSize& originalSize);

    QTabBar* m_sectionTabs = nullptr;
    QStackedWidget* m_sections = nullptr;
    FlowLayout* m_recentLayout = nullptr;
    FlowLayout* m_templateLayout = nullptr;
    RecentThumbnailCache* m_thumbnails = nullptr;
    QHash<QString, QWidget*> m_recentCards;
    QWidget* m_recentEmptyState = nullptr;
    QList<QWidget*> m_templateCards;
    QString m_templatePlatform = QStringLiteral("all");
};
