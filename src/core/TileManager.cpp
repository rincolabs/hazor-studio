#include "TileManager.hpp"
#include <cmath>
#include <algorithm>

namespace core {

void TileManager::init(int imageW, int imageH, int tileSize)
{
    clear();
    if (imageW <= 0 || imageH <= 0 || tileSize <= 0)
        return;

    m_tileSize = tileSize;
    m_imageW = imageW;
    m_imageH = imageH;
    m_cols = static_cast<int>(std::ceil(static_cast<double>(imageW) / tileSize));
    m_rows = static_cast<int>(std::ceil(static_cast<double>(imageH) / tileSize));

    m_tiles.resize(m_cols * m_rows);

    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            auto& tile = m_tiles[indexOf(c, r)];
            tile.col = c;
            tile.row = r;
            int x = c * tileSize;
            int y = r * tileSize;
            int w = std::min(tileSize, imageW - x);
            int h = std::min(tileSize, imageH - y);
            tile.bounds = QRect(x, y, w, h);
            tile.state = Tile::State::Dirty;
            tile.version = 1;
            tile.lastAccess = currentTimestampMs();
        }
    }
}

void TileManager::clear()
{
    m_tiles.clear();
    m_imageW = m_imageH = 0;
    m_cols = m_rows = 0;
}

void TileManager::resize(int newW, int newH)
{
    if (newW <= 0 || newH <= 0) {
        clear();
        return;
    }

    int oldCols = m_cols, oldRows = m_rows;
    std::vector<Tile> oldTiles;
    oldTiles.swap(m_tiles);

    init(newW, newH, m_tileSize);

    // Preserve cpuCache for tiles whose bounds intersect old grid
    for (int r = 0; r < m_rows; ++r) {
        for (int c = 0; c < m_cols; ++c) {
            auto& newTile = m_tiles[indexOf(c, r)];
            for (int or_ = 0; or_ < oldRows; ++or_) {
                for (int oc = 0; oc < oldCols; ++oc) {
                    auto& oldTile = oldTiles[or_ * oldCols + oc];
                    if (oldTile.bounds.intersects(newTile.bounds)
                        && !oldTile.cpuCache.isNull())
                    {
                        newTile.cpuCache = oldTile.cpuCache;
                        newTile.state = Tile::State::Cached;
                        newTile.version = oldTile.version;
                        goto nextTile;
                    }
                }
            }
            nextTile: ;
        }
    }
}

Tile& TileManager::at(int col, int row)
{
    return m_tiles[indexOf(col, row)];
}

const Tile& TileManager::at(int col, int row) const
{
    return m_tiles[indexOf(col, row)];
}

Tile* TileManager::tileAtPixel(int px, int py)
{
    if (m_cols <= 0 || m_rows <= 0) return nullptr;
    if (px < 0 || py < 0 || px >= m_imageW || py >= m_imageH)
        return nullptr;
    int c = px / m_tileSize;
    int r = py / m_tileSize;
    return &m_tiles[indexOf(c, r)];
}

std::vector<Tile*> TileManager::visibleTiles(const QRect& viewportPixelRect)
{
    std::vector<Tile*> result;
    if (m_tiles.empty()) return result;

    int c0 = std::max(0, viewportPixelRect.left()   / m_tileSize);
    int c1 = std::min(m_cols - 1, viewportPixelRect.right()  / m_tileSize);
    int r0 = std::max(0, viewportPixelRect.top()    / m_tileSize);
    int r1 = std::min(m_rows - 1, viewportPixelRect.bottom() / m_tileSize);

    uint64_t now = currentTimestampMs();

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            auto& tile = m_tiles[indexOf(c, r)];
            if (tile.state == Tile::State::Empty)
                continue;
            tile.lastAccess = now;
            result.push_back(&tile);
        }
    }
    return result;
}

void TileManager::markDirty(const QRect& pixelRect)
{
    if (m_tiles.empty() || pixelRect.isEmpty()) return;

    int c0 = std::max(0, pixelRect.left()   / m_tileSize);
    int c1 = std::min(m_cols - 1, pixelRect.right()  / m_tileSize);
    int r0 = std::max(0, pixelRect.top()    / m_tileSize);
    int r1 = std::min(m_rows - 1, pixelRect.bottom() / m_tileSize);

    for (int r = r0; r <= r1; ++r) {
        for (int c = c0; c <= c1; ++c) {
            auto& tile = m_tiles[indexOf(c, r)];
            if (tile.state != Tile::State::Empty) {
                tile.markDirty();
                tile.cpuCache = {};  // invalidate CPU cache
            }
        }
    }
}

void TileManager::markAllDirty()
{
    for (auto& tile : m_tiles) {
        if (tile.state != Tile::State::Empty) {
            tile.markDirty();
            tile.cpuCache = {};
        }
    }
}

std::vector<Tile*> TileManager::dirtyTiles() const
{
    std::vector<Tile*> result;
    for (auto& tile : const_cast<std::vector<Tile>&>(m_tiles)) {
        if (tile.state == Tile::State::Dirty)
            result.push_back(&tile);
    }
    return result;
}

void TileManager::evictLRU(int maxCached)
{
    if (maxCached < 0) return;

    // Collect tiles with cpuCache
    std::vector<Tile*> cached;
    for (auto& tile : m_tiles) {
        if (tile.state == Tile::State::Cached && !tile.cpuCache.isNull())
            cached.push_back(&tile);
    }

    if (static_cast<int>(cached.size()) <= maxCached)
        return;

    // Sort by lastAccess ascending (oldest first)
    std::sort(cached.begin(), cached.end(),
              [](const Tile* a, const Tile* b) {
                  return a->lastAccess < b->lastAccess;
              });

    int toEvict = static_cast<int>(cached.size()) - maxCached;
    for (int i = 0; i < toEvict; ++i) {
        cached[i]->cpuCache = {};
        cached[i]->state = Tile::State::GPUReady;
    }
}

int TileManager::cpuCacheCount() const
{
    int count = 0;
    for (const auto& tile : m_tiles) {
        if (tile.state == Tile::State::Cached && !tile.cpuCache.isNull())
            ++count;
    }
    return count;
}

} // namespace core
