#include "BrushTextureLibrary.hpp"
#include "core/AppPaths.hpp"

#include <QDir>
#include <QFileInfo>

BrushTextureLibrary* BrushTextureLibrary::instance()
{
    static BrushTextureLibrary s_instance;
    return &s_instance;
}

BrushTextureLibrary::BrushTextureLibrary(QObject* parent)
    : QObject(parent)
{
}

QString BrushTextureLibrary::makeId(const QString& name)
{
    QString id = name.trimmed().toLower();
    for (QChar& c : id) {
        if (!c.isLetterOrNumber())
            c = QLatin1Char('_');
    }
    if (id.isEmpty())
        id = QStringLiteral("texture");
    return id;
}

quint64 BrushTextureLibrary::contentHash(const QImage& img)
{
    // FNV-1a over the pixel rows (handles row padding by hashing per scanline).
    quint64 h = 1469598103934665603ull;
    auto mix = [&h](quint64 v) { h ^= v; h *= 1099511628211ull; };
    mix(static_cast<quint64>(img.width()));
    mix(static_cast<quint64>(img.height()));
    const int bpl = img.width();   // Grayscale8: 1 byte per pixel
    for (int y = 0; y < img.height(); ++y) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < bpl; ++x)
            mix(line[x]);
    }
    return h;
}

const BrushTexture* BrushTextureLibrary::find(const QString& id) const
{
    if (id.isEmpty())
        return nullptr;
    for (const BrushTexture& t : m_textures)
        if (t.id == id)
            return &t;
    return nullptr;
}

QString BrushTextureLibrary::add(const QString& name, const QImage& image,
                                 const QString& path)
{
    if (image.isNull())
        return {};

    const QImage gray = image.format() == QImage::Format_Grayscale8
                            ? image
                            : image.convertToFormat(QImage::Format_Grayscale8);

    // Dedup by PIXEL CONTENT, not by name: many brushes ship textures under the
    // same/blank name, so deduping by name collapsed them all into one entry.
    const quint64 key = contentHash(gray);
    for (const BrushTexture& t : m_textures)
        if (t.contentKey == key && t.image.size() == gray.size())
            return t.id;   // same texture already registered

    // Unique id derived from the name (append a counter on collision).
    const QString base = makeId(name);
    QString id = base;
    for (int n = 2; find(id) != nullptr; ++n)
        id = base + QStringLiteral("_") + QString::number(n);

    BrushTexture tex;
    tex.id = id;
    tex.name = name.trimmed().isEmpty() ? id : name.trimmed();
    tex.path = path;
    tex.image = gray;
    tex.contentKey = key;
    m_textures.push_back(tex);
    emit texturesChanged();
    return id;
}

QString BrushTextureLibrary::importFromFile(const QString& path)
{
    QImage img(path);
    if (img.isNull())
        return {};
    const QString name = QFileInfo(path).completeBaseName();
    return add(name, img, path);
}

void BrushTextureLibrary::remove(const QString& id)
{
    for (int i = 0; i < m_textures.size(); ++i) {
        if (m_textures[i].id == id) {
            m_textures.remove(i);
            emit texturesChanged();
            return;
        }
    }
}

void BrushTextureLibrary::scanFolder(const QString& dir)
{
    QDir d(dir);
    if (!d.exists())
        return;
    const QStringList filters{ QStringLiteral("*.png"), QStringLiteral("*.PNG"),
                               QStringLiteral("*.jpg"), QStringLiteral("*.JPG"),
                               QStringLiteral("*.jpeg"), QStringLiteral("*.JPEG"),
                               QStringLiteral("*.bmp"), QStringLiteral("*.BMP"),
                               QStringLiteral("*.webp"), QStringLiteral("*.WEBP") };
    const QFileInfoList files = d.entryInfoList(filters, QDir::Files, QDir::Name);
    for (const QFileInfo& fi : files) {
        QImage img(fi.absoluteFilePath());
        if (img.isNull())
            continue;
        add(fi.completeBaseName(), img, fi.absoluteFilePath());
    }
}

void BrushTextureLibrary::reload()
{
    // Default textures shipped with the app, embedded in the Qt resource system.
    scanFolder(defaultTextureDir());
    // User folder for additions.
    scanFolder(AppPaths::subDir(QStringLiteral("brush-textures")));
    m_scanned = true;
}

void BrushTextureLibrary::ensureLoaded()
{
    if (!m_scanned)
        reload();
}

QString BrushTextureLibrary::defaultTextureDir()
{
    // Bundled into the Qt resource system from src/resources/brushes/textures
    // (see resources.qrc), so it ships inside the binary on every platform.
    return QStringLiteral(":/brushes/textures");
}
