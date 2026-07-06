#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>

#include <map>
#include <set>
#include <utility>
#include <vector>

namespace core {

struct RasterTile {
    QPoint tileCoord;
    QRect bounds;
    QImage cpuImage;
    unsigned int textureId = 0;
    unsigned int fbo = 0;
    // Size the GPU texture was last allocated at (glTexImage2D). While it matches
    // the cpuImage we update in place with glTexSubImage2D instead of reallocating.
    int texW = 0;
    int texH = 0;
    bool dirtyCpu = false;
    bool dirtyGpu = true;
};

struct RasterTileChange {
    QPoint coord;
    bool existedBefore = false;
    QImage before;
    bool existedAfter = false;
    QImage after;
};

class RasterLayerStorage {
public:
    using Key = std::pair<int, int>;

    bool isEnabled() const { return m_enabled; }
    bool hasTiles() const { return !m_tiles.empty(); }
    int tileSize() const { return m_tileSize; }
    QSize baseSize() const { return m_baseSize; }

    void clear();
    void setBaseSize(const QSize& size);
    void enableFromImage(const QImage& image, int tileSize = 256);
    void replaceWithImage(const QImage& image, const QPoint& origin = QPoint(0, 0),
                          int tileSize = 256);

    RasterTile* tileAt(const QPoint& coord);
    const RasterTile* tileAt(const QPoint& coord) const;
    RasterTile* ensureTile(const QPoint& coord);
    void removeTile(const QPoint& coord);

    QPoint tileCoordForPixel(int x, int y) const;
    QRect tileBounds(const QPoint& coord) const;
    std::vector<RasterTile*> tilesInRect(const QRect& localRect, bool createMissing);
    std::vector<const RasterTile*> tilesInRect(const QRect& localRect) const;

    QRect logicalBounds() const;
    QRect contentBounds() const;
    QImage toImage(QRect* outBounds = nullptr) const;

    void markTileModified(const QPoint& coord);
    void markAllGpuDirty();

    void beginChangeTracking();
    void recordBefore(const QPoint& coord);
    std::vector<RasterTileChange> endChangeTracking();
    void applyChanges(const std::vector<RasterTileChange>& changes, bool useAfter);

    template <typename F>
    void forEachTile(F&& callback)
    {
        for (auto& [_, tile] : m_tiles)
            callback(tile);
    }

    template <typename F>
    void forEachTile(F&& callback) const
    {
        for (const auto& [_, tile] : m_tiles)
            callback(tile);
    }

private:
    static Key keyFor(const QPoint& coord) { return {coord.x(), coord.y()}; }
    static int floorDiv(int value, int divisor);
    static bool imageHasContent(const QImage& image);
    static QRect imageContentBounds(const QImage& image);

    bool m_enabled = false;
    int m_tileSize = 256;
    QSize m_baseSize;
    std::map<Key, RasterTile> m_tiles;
    mutable bool m_contentBoundsDirty = true;
    mutable QRect m_contentBoundsCache;

    bool m_tracking = false;
    std::map<Key, RasterTileChange> m_changeLog;
};

} // namespace core
