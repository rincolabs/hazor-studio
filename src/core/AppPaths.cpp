#include "AppPaths.hpp"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace AppPaths {

QString dataDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share/Rincolabs/Hazor Studio");
    QDir().mkpath(base);
    return base;
}

QString dataFile(const QString& fileName)
{
    return QDir(dataDir()).filePath(fileName);
}

QString subDir(const QString& name)
{
    const QString path = QDir(dataDir()).filePath(name);
    QDir().mkpath(path);
    return path;
}

QString cacheDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache/Rincolabs/Hazor Studio");
    QDir().mkpath(base);
    return base;
}

QString cacheSubDir(const QString& name)
{
    const QString path = QDir(cacheDir()).filePath(name);
    QDir().mkpath(path);
    return path;
}

QString aiCacheDir()
{
    const QString root = cacheSubDir(QStringLiteral("ai"));
    for (const QString& sub : { QStringLiteral("embeddings"), QStringLiteral("previews"),
                                QStringLiteral("temp"), QStringLiteral("downloads") })
        QDir().mkpath(QDir(root).filePath(sub));
    return root;
}

namespace {

// Resolves a read-only directory shipped with the app, searching relative to
// the executable: next to the binary, a Unix <prefix>/bin layout (../), the
// AppImage AppDir root (usr/bin → ../../), and a macOS bundle. Returns an
// empty string when no candidate exists, so callers decide what absence means.
QString shippedDir(const QString& rel)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates {
        QDir(appDir).filePath(rel),
        QDir(appDir).filePath(QStringLiteral("../") + rel),
        QDir(appDir).filePath(QStringLiteral("../../") + rel),
        QDir(appDir).filePath(QStringLiteral("../Resources/") + rel), // macOS bundle
    };
    for (const QString& c : candidates) {
        const QFileInfo fi(c);
        if (fi.exists() && fi.isDir())
            return fi.absoluteFilePath();
    }
    return {};
}

} // namespace

QString thirdPartyDir()
{
    const QString rel = QStringLiteral("3rdparty");
    const QString found = shippedDir(rel);
    if (!found.isEmpty())
        return found;

#ifdef HAZOR_SOURCE_DIR
    // Dev runs from the build directory.
    const QFileInfo inSource(QDir(QStringLiteral(HAZOR_SOURCE_DIR)).filePath(rel));
    if (inSource.exists() && inSource.isDir())
        return inSource.absoluteFilePath();
#endif

    // None present yet: return a stable path so callers have something to scan
    // and report against (the registry/backends fail gracefully when missing).
    return QDir(QCoreApplication::applicationDirPath()).absoluteFilePath(rel);
}

QString bundledModelsDir()
{
    // Models distributed with the app live under the read-only 3rdparty tree,
    // never under the user-modifiable models directory.
    return QDir(thirdPartyDir()).filePath(QStringLiteral("onnx/models"));
}

QString rawEngineResourceDir()
{
    return QDir(thirdPartyDir()).filePath(QStringLiteral("rawengine/rtdata"));
}

QString lensfunDatabaseDir()
{
    // "version_1" is lensfun's DATABASE_SUBDIR — the database *format*
    // version, not the library version; it moves only with a format bump,
    // which would need a different lensfun than the pinned 0.3.4 anyway.
    return shippedDir(QStringLiteral("share/lensfun/version_1"));
}

QString rawUserDir()
{
    return subDir(QStringLiteral("raw"));
}

} // namespace AppPaths
