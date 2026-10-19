#pragma once

#include <QImage>
#include <QObject>
#include <QSet>
#include <QSize>
#include <QString>

#include <functional>

class Document;

// Persistent previews for the Home workspace's recent-document cards. The
// cache is deliberately outside project files: the file format stays unchanged
// and opening the Home page never needs to deserialize a .hazor project merely
// to draw its recent list.
class RecentThumbnailCache : public QObject {
    Q_OBJECT

public:
    explicit RecentThumbnailCache(QObject* parent = nullptr);

    void request(const QString& path);
    void storeDocument(const QString& path, const Document* document);
    void remove(const QString& path);

signals:
    void thumbnailAvailable(const QString& path, const QImage& thumbnail,
                            const QSize& originalSize);

private:
    struct ThumbnailResult {
        QString path;
        QImage thumbnail;
        QSize originalSize;
    };

    static QSize thumbnailBounds();
    static bool isProjectPath(const QString& path);
    QString cacheImagePath(const QString& path) const;
    QString cacheMetadataPath(const QString& path) const;
    bool loadCached(const QString& path, ThumbnailResult* result) const;
    void saveCached(const ThumbnailResult& result) const;
    void runTask(const QString& pendingKey, std::function<ThumbnailResult()> task);

    QSet<QString> m_pendingReads;
};
