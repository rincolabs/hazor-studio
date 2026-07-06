#pragma once

#include <QPixmap>
#include <QHash>
#include <QSize>
#include <QColor>
#include "BrushPreset.hpp"

// Caches generated brush thumbnails so the preset list never regenerates a
// preview during a repaint. The cache key is (preset name, content version,
// kind, size, dpr, ink): the *version* is a hash of the preset's serialized
// settings, so a preview is regenerated only when the dab, spacing, dynamics or
// any other setting that affects appearance actually changes — and never when
// the list merely scrolls or repaints.
class BrushPreviewCache {
public:
    QPixmap tip(const BrushPreset& preset, const QSize& size,
                qreal devicePixelRatio, const QColor& ink);
    QPixmap stroke(const BrushPreset& preset, const QSize& size,
                   qreal devicePixelRatio, const QColor& ink);

    void clear() { m_cache.clear(); }

    // A stable content version for a preset (hash of its serialized settings).
    static quint64 versionFor(const BrushPreset& preset);

    // Cache key used by tip()/stroke(). Exposed so a background warm job can
    // pre-render under the exact key the lazy path will look up. kind is 't'
    // (tip) or 's' (stroke).
    static QString keyFor(const BrushPreset& preset, char kind,
                          const QSize& size, qreal dpr, const QColor& ink);

    // Insert a pre-rendered pixmap (must be built on the GUI thread).
    void put(const QString& key, const QPixmap& pixmap) { slot(key) = pixmap; }

    // The tip-column thumbnail image: the preset's own preview (decoded and fit into
    // size) when it carries one, otherwise the rendered tip. Static and thread-safe
    // (QImage only) so the lazy tip() path and the background warm job share it.
    static QImage tipThumbnail(const BrushPreset& preset, const QSize& size,
                               qreal devicePixelRatio, const QColor& ink);

private:
    QPixmap& slot(const QString& key);

    QHash<QString, QPixmap> m_cache;
    static constexpr int kMaxEntries = 2048;
};
