#include "BrushTipLibrary.hpp"
#include "core/AppPaths.hpp"

#include <QDir>
#include <QFileInfo>

BrushTipLibrary* BrushTipLibrary::instance()
{
    static BrushTipLibrary s_instance;
    return &s_instance;
}

BrushTipLibrary::BrushTipLibrary(QObject* parent)
    : QObject(parent)
{
}

QString BrushTipLibrary::makeId(const QString& name)
{
    QString id = name.trimmed().toLower();
    for (QChar& c : id) {
        if (!c.isLetterOrNumber())
            c = QLatin1Char('_');
    }
    if (id.isEmpty())
        id = QStringLiteral("tip");
    return id;
}

quint64 BrushTipLibrary::contentHash(const QImage& img)
{
    // FNV-1a over the pixel rows (handles row padding by hashing per scanline).
    quint64 h = 1469598103934665603ull;
    auto mix = [&h](quint64 v) { h ^= v; h *= 1099511628211ull; };
    mix(static_cast<quint64>(img.width()));
    mix(static_cast<quint64>(img.height()));
    const int bpl = img.width() * 4;   // ARGB32: 4 bytes per pixel
    for (int y = 0; y < img.height(); ++y) {
        const uchar* line = img.constScanLine(y);
        for (int x = 0; x < bpl; ++x)
            mix(line[x]);
    }
    return h;
}

const BrushTip* BrushTipLibrary::find(const QString& id) const
{
    if (id.isEmpty())
        return nullptr;
    for (const BrushTip& t : m_tips)
        if (t.id == id)
            return &t;
    return nullptr;
}

QString BrushTipLibrary::add(const QString& name, const QImage& image,
                             const QString& path)
{
    if (image.isNull())
        return {};

    const QImage argb = image.format() == QImage::Format_ARGB32
                            ? image
                            : image.convertToFormat(QImage::Format_ARGB32);

    // Dedup by PIXEL CONTENT, not by name: many brushes ship tips under the
    // same/blank name, so deduping by name would collapse them all into one entry.
    const quint64 key = contentHash(argb);
    for (const BrushTip& t : m_tips)
        if (t.contentKey == key && t.image.size() == argb.size())
            return t.id;   // same tip already registered

    // Unique id derived from the name (append a counter on collision).
    const QString base = makeId(name);
    QString id = base;
    for (int n = 2; find(id) != nullptr; ++n)
        id = base + QStringLiteral("_") + QString::number(n);

    BrushTip tip;
    tip.id = id;
    tip.name = name.trimmed().isEmpty() ? id : name.trimmed();
    tip.path = path;
    tip.image = argb;
    tip.contentKey = key;
    m_tips.push_back(tip);
    emit tipsChanged();
    return id;
}

QString BrushTipLibrary::importFromFile(const QString& path)
{
    QImage img(path);
    if (img.isNull())
        return {};
    const QString name = QFileInfo(path).completeBaseName();
    return add(name, img, path);
}

void BrushTipLibrary::remove(const QString& id)
{
    for (int i = 0; i < m_tips.size(); ++i) {
        if (m_tips[i].id == id) {
            m_tips.remove(i);
            emit tipsChanged();
            return;
        }
    }
}

void BrushTipLibrary::scanFolder(const QString& dir)
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

void BrushTipLibrary::reload()
{
    // Default tips shipped with the app, embedded in the Qt resource system
    // (:/brushes/tips) — this is the built-in professional kit's tip set.
    scanFolder(defaultTipDir());
    // Legacy/optional third-party folder, plus a user folder for additions.
    scanFolder(QDir(AppPaths::thirdPartyDir())
                   .filePath(QStringLiteral("resources/brush-tips")));
    scanFolder(AppPaths::subDir(QStringLiteral("brush-tips")));
    m_scanned = true;
}

void BrushTipLibrary::ensureLoaded()
{
    if (!m_scanned)
        reload();
}

QString BrushTipLibrary::defaultTipDir()
{
    // Bundled into the Qt resource system from src/resources/brushes/tips
    // (see resources.qrc), so it ships inside the binary on every platform.
    return QStringLiteral(":/brushes/tips");
}
