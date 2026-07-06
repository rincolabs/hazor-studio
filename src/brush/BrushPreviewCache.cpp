#include "BrushPreviewCache.hpp"
#include "BrushPreviewRenderer.hpp"

#include <QImage>
#include <QPainter>
#include <QByteArray>
#include <QJsonDocument>

quint64 BrushPreviewCache::versionFor(const BrushPreset& preset)
{
    // BrushPreset::toJson captures every persisted setting that affects how the
    // brush paints, so hashing it gives a version that changes exactly when the
    // preview would need to change. But toJson re-encodes the texture to PNG and
    // serialises the base64 tip blob — far too expensive to run on every paint of
    // every visible row (this is what slowed the brush list once imported brushes
    // started carrying embedded tips/textures). So we hash the settings with the
    // two heavy image blobs stripped, then fold the images back in cheaply by
    // identity: the texture's QImage cacheKey and a hash of the tip blob both
    // change whenever the actual image changes, without re-encoding anything.
    BrushPreset light = preset;
    const qint64 texKey = light.settings.textureConfig.texture.cacheKey();
    const uint tipHash = qHash(light.settings.tipImageData);
    const uint previewHash = qHash(light.previewImageData);
    light.settings.textureConfig.texture = QImage();
    light.settings.tipImageData.clear();
    light.previewImageData.clear();

    const QByteArray json = QJsonDocument(light.toJson()).toJson(QJsonDocument::Compact);
    quint64 h = qHash(json);
    h = h * 1099511628211ULL + quint64(texKey);
    h = h * 1099511628211ULL + quint64(tipHash);
    h = h * 1099511628211ULL + quint64(previewHash);
    return h;
}

QImage BrushPreviewCache::tipThumbnail(const BrushPreset& preset, const QSize& size,
                                       qreal devicePixelRatio, const QColor& ink)
{
    if (preset.previewImageData.isEmpty())
        return BrushPreviewRenderer::renderTip(preset.settings, size, devicePixelRatio, ink);

    QImage src;
    if (!src.loadFromData(QByteArray::fromBase64(preset.previewImageData.toLatin1()))
        || src.isNull()) {
        // Corrupt/unreadable preview → fall back to the rendered tip.
        return BrushPreviewRenderer::renderTip(preset.settings, size, devicePixelRatio, ink);
    }

    // Fit the preview into the thumbnail box (keep aspect), centred, on a transparent
    // canvas at the device pixel ratio so it lines up with the rendered-tip path.
    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    QImage out(QSize(int(size.width() * dpr), int(size.height() * dpr)),
               QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(dpr);
    out.fill(Qt::transparent);
    const QImage scaled = src.scaled(out.size(), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter p(&out);
    p.drawImage((out.width() - scaled.width()) / 2,
                (out.height() - scaled.height()) / 2, scaled);
    p.end();
    return out;
}

QString BrushPreviewCache::keyFor(const BrushPreset& preset, char kind,
                                  const QSize& size, qreal dpr, const QColor& ink)
{
    return QStringLiteral("%1|%2|%3|%4x%5@%6|%7")
        .arg(preset.name)
        .arg(versionFor(preset))
        .arg(kind)
        .arg(size.width())
        .arg(size.height())
        .arg(dpr)
        .arg(ink.rgba());
}

QPixmap& BrushPreviewCache::slot(const QString& key)
{
    // Coarse cap: if the cache grows past the limit drop it wholesale rather
    // than tracking per-entry LRU — previews are cheap to rebuild and this keeps
    // memory bounded with thousands of presets.
    if (m_cache.size() > kMaxEntries)
        m_cache.clear();
    return m_cache[key];
}

QPixmap BrushPreviewCache::tip(const BrushPreset& preset, const QSize& size,
                               qreal devicePixelRatio, const QColor& ink)
{
    const QString key = keyFor(preset, 't', size, devicePixelRatio, ink);
    QPixmap& px = slot(key);
    if (px.isNull()) {
        px = QPixmap::fromImage(tipThumbnail(preset, size, devicePixelRatio, ink));
    }
    return px;
}

QPixmap BrushPreviewCache::stroke(const BrushPreset& preset, const QSize& size,
                                  qreal devicePixelRatio, const QColor& ink)
{
    const QString key = keyFor(preset, 's', size, devicePixelRatio, ink);
    QPixmap& px = slot(key);
    if (px.isNull()) {
        px = QPixmap::fromImage(
            BrushPreviewRenderer::renderStroke(preset.settings, size, devicePixelRatio, ink));
    }
    return px;
}
