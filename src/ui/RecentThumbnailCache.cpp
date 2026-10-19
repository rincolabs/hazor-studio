#include "RecentThumbnailCache.hpp"

#include "core/AppPaths.hpp"
#include "core/Document.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QImageReader>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QtConcurrent>

namespace {

QString cacheKeyForPath(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    return QString::fromLatin1(QCryptographicHash::hash(
        absolute.toUtf8(), QCryptographicHash::Sha256).toHex());
}

std::shared_ptr<Document> thumbnailSnapshot(const Document* document)
{
    if (!document || document->size.isEmpty())
        return {};

    // Match ProjectionCache's COW boundary: the worker reads this immutable
    // shallow clone while edits to the live document detach their pixel data.
    auto snapshot = std::make_shared<Document>();
    snapshot->size = document->size;
    snapshot->compositionGeneration = document->compositionGeneration;
    snapshot->roots.reserve(document->roots.size());
    for (const auto& root : document->roots) {
        if (root)
            snapshot->roots.push_back(root->shallowClone());
    }
    return snapshot;
}

} // namespace

RecentThumbnailCache::RecentThumbnailCache(QObject* parent)
    : QObject(parent)
{
}

QSize RecentThumbnailCache::thumbnailBounds()
{
    return QSize(320, 200);
}

bool RecentThumbnailCache::isProjectPath(const QString& path)
{
    return path.endsWith(QStringLiteral(".hazor"), Qt::CaseInsensitive)
        || path.endsWith(QStringLiteral(".hzs"), Qt::CaseInsensitive);
}

QString RecentThumbnailCache::cacheImagePath(const QString& path) const
{
    return AppPaths::cacheSubDir(QStringLiteral("recent-thumbnails"))
        + QLatin1Char('/') + cacheKeyForPath(path) + QStringLiteral(".png");
}

QString RecentThumbnailCache::cacheMetadataPath(const QString& path) const
{
    return AppPaths::cacheSubDir(QStringLiteral("recent-thumbnails"))
        + QLatin1Char('/') + cacheKeyForPath(path) + QStringLiteral(".json");
}

bool RecentThumbnailCache::loadCached(const QString& path, ThumbnailResult* result) const
{
    if (!result)
        return false;

    const QFileInfo source(path);
    const QFileInfo cache(cacheImagePath(path));
    if (!cache.isFile())
        return false;

    QFile metadataFile(cacheMetadataPath(path));
    if (!metadataFile.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject metadata = QJsonDocument::fromJson(metadataFile.readAll()).object();
    const qint64 cachedModified = metadata.value(QStringLiteral("sourceModifiedMs")).toVariant().toLongLong();
    if (source.exists() && cachedModified < source.lastModified().toMSecsSinceEpoch())
        return false;

    QImage image(cache.absoluteFilePath());
    if (image.isNull())
        return false;

    result->path = source.absoluteFilePath();
    result->thumbnail = image;
    result->originalSize = QSize(metadata.value(QStringLiteral("width")).toInt(),
                                 metadata.value(QStringLiteral("height")).toInt());
    return true;
}

void RecentThumbnailCache::saveCached(const ThumbnailResult& result) const
{
    if (result.path.isEmpty() || result.thumbnail.isNull())
        return;

    QSaveFile imageFile(cacheImagePath(result.path));
    if (!imageFile.open(QIODevice::WriteOnly))
        return;
    if (!result.thumbnail.save(&imageFile, "PNG") || !imageFile.commit())
        return;

    const QFileInfo source(result.path);
    QJsonObject metadata;
    metadata.insert(QStringLiteral("sourceModifiedMs"),
                    static_cast<double>(source.lastModified().toMSecsSinceEpoch()));
    metadata.insert(QStringLiteral("width"), result.originalSize.width());
    metadata.insert(QStringLiteral("height"), result.originalSize.height());

    QSaveFile metadataFile(cacheMetadataPath(result.path));
    if (metadataFile.open(QIODevice::WriteOnly)) {
        metadataFile.write(QJsonDocument(metadata).toJson(QJsonDocument::Compact));
        metadataFile.commit();
    }
}

void RecentThumbnailCache::runTask(const QString& pendingKey,
                                   std::function<ThumbnailResult()> task)
{
    auto* watcher = new QFutureWatcher<ThumbnailResult>(this);
    connect(watcher, &QFutureWatcher<ThumbnailResult>::finished, this,
            [this, watcher, pendingKey]() {
        const ThumbnailResult result = watcher->result();
        watcher->deleteLater();
        if (!pendingKey.isEmpty())
            m_pendingReads.remove(pendingKey);
        if (result.thumbnail.isNull())
            return;
        saveCached(result);
        emit thumbnailAvailable(result.path, result.thumbnail, result.originalSize);
    });
    watcher->setFuture(QtConcurrent::run(std::move(task)));
}

void RecentThumbnailCache::request(const QString& path)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    ThumbnailResult cached;
    if (loadCached(absolutePath, &cached)) {
        emit thumbnailAvailable(cached.path, cached.thumbnail, cached.originalSize);
        return;
    }

    // There is no embedded project preview in the current .hazor format. Its
    // cache is made during a normal open/save, rather than loading a complete
    // project solely for a Home card. Until then the UI shows its placeholder.
    if (isProjectPath(absolutePath) || m_pendingReads.contains(absolutePath))
        return;

    m_pendingReads.insert(absolutePath);
    runTask(absolutePath, [absolutePath]() {
        ThumbnailResult result;
        result.path = absolutePath;

        QImageReader reader(absolutePath);
        reader.setAutoTransform(true);
        result.originalSize = reader.size();
        if (result.originalSize.isEmpty())
            return result;

        const QSize requested = result.originalSize.scaled(
            RecentThumbnailCache::thumbnailBounds(), Qt::KeepAspectRatio);
        reader.setScaledSize(requested);
        result.thumbnail = reader.read();
        if (!result.thumbnail.isNull())
            result.thumbnail = result.thumbnail.scaled(
                RecentThumbnailCache::thumbnailBounds(), Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
        return result;
    });
}

void RecentThumbnailCache::storeDocument(const QString& path, const Document* document)
{
    const QString absolutePath = QFileInfo(path).absoluteFilePath();
    const auto snapshot = thumbnailSnapshot(document);
    if (absolutePath.isEmpty() || !snapshot)
        return;

    runTask(QString(), [absolutePath, snapshot]() {
        ThumbnailResult result;
        result.path = absolutePath;
        result.originalSize = snapshot->size;

        RenderContext context;
        context.document = snapshot.get();
        context.outputSize = snapshot->size;
        context.targetType = RenderTargetType::Thumbnail;
        context.highQuality = true;
        result.thumbnail = DocumentCompositor::composite(snapshot.get(), context);
        if (!result.thumbnail.isNull())
            result.thumbnail = result.thumbnail.scaled(
                RecentThumbnailCache::thumbnailBounds(), Qt::KeepAspectRatio,
                Qt::SmoothTransformation);
        return result;
    });
}

void RecentThumbnailCache::remove(const QString& path)
{
    QFile::remove(cacheImagePath(path));
    QFile::remove(cacheMetadataPath(path));
}
