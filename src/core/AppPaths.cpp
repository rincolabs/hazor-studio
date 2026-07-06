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

QString thirdPartyDir()
{
    const QString rel = QStringLiteral("3rdparty");
    const QString appDir = QCoreApplication::applicationDirPath();

    // Search relative to the executable, covering: next to the binary, a Unix
    // <prefix>/bin layout (../), the AppImage AppDir root (usr/bin → ../../),
    // a macOS bundle, and the source tree (dev runs from the build directory).
    QStringList candidates;
    candidates << QDir(appDir).filePath(rel)
               << QDir(appDir).filePath(QStringLiteral("../") + rel)
               << QDir(appDir).filePath(QStringLiteral("../../") + rel)
               << QDir(appDir).filePath(QStringLiteral("../Resources/") + rel); // macOS bundle
#ifdef HAZOR_SOURCE_DIR
    candidates << QDir(QStringLiteral(HAZOR_SOURCE_DIR)).filePath(rel);
#endif

    for (const QString& c : candidates) {
        const QFileInfo fi(c);
        if (fi.exists() && fi.isDir())
            return fi.absoluteFilePath();
    }
    // None present yet: return a stable path so callers have something to scan
    // and report against (the registry/backends fail gracefully when missing).
    return QDir(appDir).absoluteFilePath(rel);
}

QString bundledModelsDir()
{
    // Models distributed with the app live under the read-only 3rdparty tree,
    // never under the user-modifiable models directory.
    return QDir(thirdPartyDir()).filePath(QStringLiteral("onnx/models"));
}

} // namespace AppPaths
