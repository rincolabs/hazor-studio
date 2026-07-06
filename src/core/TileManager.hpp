#pragma once

#include "Tile.hpp"
#include <QRect>
#include <vector>
#include <functional>
#include <algorithm>

namespace core {

class TileManager {
public:
    TileManager() = default;

    void init(int imageW, int imageH, int tileSize = 256);
    void clear();

    // Rebuild grid for new dimensions, preserving cache where bounds overlap.
    void resize(int newW, int newH);

    Tile& at(int col, int row);
    const Tile& at(int col, int row) const;
    Tile* tileAtPixel(int px, int py);

    int tileSize()   const { return m_tileSize; }
    int cols()       const { return m_cols; }
    int rows()       const { return m_rows; }
    int totalTiles() const { return static_cast<int>(m_tiles.size()); }
    int imageWidth()  const { return m_imageW; }
    int imageHeight() const { return m_imageH; }

    // Returns tiles intersecting viewportPixelRect (layer-local pixels).
    // Also updates their lastAccess timestamp.
    std::vector<Tile*> visibleTiles(const QRect& viewportPixelRect);

    void markDirty(const QRect& pixelRect);
    void markAllDirty();
    std::vector<Tile*> dirtyTiles() const;

    template <typename F>
    void forEach(F&& callback)
    {
        for (auto& tile : m_tiles)
            callback(tile);
    }

    void evictLRU(int maxCached);
    int cpuCacheCount() const;

private:
    int m_tileSize = 256;
    int m_imageW = 0, m_imageH = 0;
    int m_cols = 0, m_rows = 0;
    std::vector<Tile> m_tiles;

    int indexOf(int col, int row) const
    {
        return row * m_cols + col;
    }
};

} // namespace core
