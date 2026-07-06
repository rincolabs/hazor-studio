#include "RasterLayerStorage.hpp"

#include <QPainter>
#include <QDebug>

#include <algorithm>

namespace core {

int RasterLayerStorage::floorDiv(int value, int divisor)
{
    if (divisor <= 0) return 0;
    int q = value / divisor;
    int r = value % divisor;
    if (r != 0 && ((r < 0) != (divisor < 0)))
        --q;
    return q;
}

bool RasterLayerStorage::imageHasContent(const QImage& image)
{
    if (image.isNull()) return false;
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            if (row[x * 4 + 3] != 0)
                return true;
        }
    }
    return false;
}

QRect RasterLayerStorage::imageContentBounds(const QImage& image)
{
    if (image.isNull())
        return {};

    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    int minX = rgba.width();
    int minY = rgba.height();
    int maxX = -1;
    int maxY = -1;

    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            if (row[x * 4 + 3] == 0)
                continue;
            minX = std::min(minX, x);
            minY = std::min(minY, y);
            maxX = std::max(maxX, x);
            maxY = std::max(maxY, y);
        }
    }

    if (maxX < minX || maxY < minY)
        return {};
    return QRect(QPoint(minX, minY), QPoint(maxX, maxY));
}

void RasterLayerStorage::clear()
{
    m_tiles.clear();
    m_changeLog.clear();
    m_tracking = false;
    m_enabled = false;
    m_contentBoundsDirty = true;
    m_contentBoundsCache = QRect();
}

void RasterLayerStorage::setBaseSize(const QSize& size)
{
    if (size.isValid() && !size.isEmpty())
        m_baseSize = size;
}

void RasterLayerStorage::enableFromImage(const QImage& image, int tileSize)
{
    if (m_enabled)
        return;

    replaceWithImage(image, QPoint(0, 0), tileSize);
}

void RasterLayerStorage::replaceWithImage(const QImage& image, const QPoint& origin,
                                          int tileSize)
{
    m_tiles.clear();
    m_changeLog.clear();
    m_tracking = false;
    m_enabled = true;
    m_contentBoundsDirty = true;
    m_contentBoundsCache = QRect();
    m_tileSize = std::max(1, tileSize);
    if (image.isNull()) {
        if (!m_baseSize.isValid() || m_baseSize.isEmpty())
            m_baseSize = QSize(m_tileSize, m_tileSize);
        return;
    }

    m_baseSize = image.size();
    const QRect imageRect(origin, image.size());
    QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);

    const QPoint first = tileCoordForPixel(imageRect.left(), imageRect.top());
    const QPoint last = tileCoordForPixel(imageRect.right(), imageRect.bottom());
    for (int ty = first.y(); ty <= last.y(); ++ty) {
        for (int tx = first.x(); tx <= last.x(); ++tx) {
            const QPoint coord(tx, ty);
            const QRect tileRect = tileBounds(coord);
            const QRect srcLocal = tileRect.intersected(imageRect);
            if (srcLocal.isEmpty())
                continue;

            const QRect srcImageRect(srcLocal.topLeft() - origin, srcLocal.size());
            if (!imageHasContent(rgba.copy(srcImageRect)))
                continue;

            auto* tile = ensureTile(coord);
            if (!tile)
                continue;

            tile->cpuImage.fill(Qt::transparent);
            QPainter painter(&tile->cpuImage);
            const QPoint offset = origin - tile->bounds.topLeft();
            painter.drawImage(offset, rgba);
            painter.end();
            tile->dirtyGpu = true;
        }
    }
}

RasterTile* RasterLayerStorage::tileAt(const QPoint& coord)
{
    auto it = m_tiles.find(keyFor(coord));
    return it == m_tiles.end() ? nullptr : &it->second;
}

const RasterTile* RasterLayerStorage::tileAt(const QPoint& coord) const
{
    auto it = m_tiles.find(keyFor(coord));
    return it == m_tiles.end() ? nullptr : &it->second;
}

RasterTile* RasterLayerStorage::ensureTile(const QPoint& coord)
{
    const Key key = keyFor(coord);
    auto it = m_tiles.find(key);
    if (it != m_tiles.end())
        return &it->second;

    RasterTile tile;
    tile.tileCoord = coord;
    tile.bounds = tileBounds(coord);
    tile.cpuImage = QImage(m_tileSize, m_tileSize, QImage::Format_RGBA8888);
    tile.cpuImage.fill(Qt::transparent);
    tile.dirtyCpu = true;
    tile.dirtyGpu = true;

    auto [inserted, _] = m_tiles.emplace(key, std::move(tile));
    m_contentBoundsDirty = true;
    // [MASK-AUDIT expand] A new tile was allocated → logicalBounds grows here.
    // logicalBounds is the tile allocation envelope, not the layer image bounds;
    // masks must not mirror this value.
    return &inserted->second;
}

void RasterLayerStorage::removeTile(const QPoint& coord)
{
    m_tiles.erase(keyFor(coord));
    m_contentBoundsDirty = true;
}

QPoint RasterLayerStorage::tileCoordForPixel(int x, int y) const
{
    return QPoint(floorDiv(x, m_tileSize), floorDiv(y, m_tileSize));
}

QRect RasterLayerStorage::tileBounds(const QPoint& coord) const
{
    return QRect(coord.x() * m_tileSize, coord.y() * m_tileSize,
                 m_tileSize, m_tileSize);
}

std::vector<RasterTile*> RasterLayerStorage::tilesInRect(const QRect& localRect,
                                                         bool createMissing)
{
    std::vector<RasterTile*> result;
    if (localRect.isEmpty())
        return result;

    const QPoint first = tileCoordForPixel(localRect.left(), localRect.top());
    const QPoint last = tileCoordForPixel(localRect.right(), localRect.bottom());
    for (int ty = first.y(); ty <= last.y(); ++ty) {
        for (int tx = first.x(); tx <= last.x(); ++tx) {
            const QPoint coord(tx, ty);
            auto* tile = createMissing ? ensureTile(coord) : tileAt(coord);
            if (tile)
                result.push_back(tile);
        }
    }
    return result;
}

std::vector<const RasterTile*> RasterLayerStorage::tilesInRect(const QRect& localRect) const
{
    std::vector<const RasterTile*> result;
    if (localRect.isEmpty())
        return result;

    const QPoint first = tileCoordForPixel(localRect.left(), localRect.top());
    const QPoint last = tileCoordForPixel(localRect.right(), localRect.bottom());
    for (int ty = first.y(); ty <= last.y(); ++ty) {
        for (int tx = first.x(); tx <= last.x(); ++tx) {
            auto* tile = tileAt(QPoint(tx, ty));
            if (tile)
                result.push_back(tile);
        }
    }
    return result;
}

QRect RasterLayerStorage::logicalBounds() const
{
    QRect bounds;
    bool first = true;
    for (const auto& [_, tile] : m_tiles) {
        if (first) {
            bounds = tile.bounds;
            first = false;
        } else {
            bounds = bounds.united(tile.bounds);
        }
    }
    return bounds;
}

QRect RasterLayerStorage::contentBounds() const
{
    if (!m_contentBoundsDirty)
        return m_contentBoundsCache;

    QRect bounds;
    bool first = true;
    for (const auto& [_, tile] : m_tiles) {
        const QRect localContent = imageContentBounds(tile.cpuImage);
        if (localContent.isEmpty())
            continue;

        const QRect tileContent(tile.bounds.topLeft() + localContent.topLeft(),
                                localContent.size());
        if (first) {
            bounds = tileContent;
            first = false;
        } else {
            bounds = bounds.united(tileContent);
        }
    }

    m_contentBoundsCache = bounds;
    m_contentBoundsDirty = false;
    return m_contentBoundsCache;
}

QImage RasterLayerStorage::toImage(QRect* outBounds) const
{
    const QRect bounds = logicalBounds();
    if (outBounds)
        *outBounds = bounds;
    if (bounds.isEmpty())
        return QImage();

    QImage image(bounds.size(), QImage::Format_RGBA8888);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    for (const auto& [_, tile] : m_tiles)
        painter.drawImage(tile.bounds.topLeft() - bounds.topLeft(), tile.cpuImage);
    painter.end();
    return image;
}

void RasterLayerStorage::markTileModified(const QPoint& coord)
{
    auto* tile = tileAt(coord);
    if (!tile)
        return;
    tile->dirtyCpu = true;
    tile->dirtyGpu = true;
    m_contentBoundsDirty = true;
}

void RasterLayerStorage::markAllGpuDirty()
{
    for (auto& [_, tile] : m_tiles)
        tile.dirtyGpu = true;
}

void RasterLayerStorage::beginChangeTracking()
{
    m_tracking = true;
    m_changeLog.clear();
}

void RasterLayerStorage::recordBefore(const QPoint& coord)
{
    if (!m_tracking)
        return;

    const Key key = keyFor(coord);
    if (m_changeLog.find(key) != m_changeLog.end())
        return;

    RasterTileChange change;
    change.coord = coord;
    if (auto* tile = tileAt(coord)) {
        change.existedBefore = true;
        change.before = tile->cpuImage.copy();
    }
    m_changeLog.emplace(key, std::move(change));
}

std::vector<RasterTileChange> RasterLayerStorage::endChangeTracking()
{
    std::vector<RasterTileChange> changes;
    changes.reserve(m_changeLog.size());

    for (auto& [key, change] : m_changeLog) {
        const QPoint coord(key.first, key.second);
        if (auto* tile = tileAt(coord)) {
            change.existedAfter = true;
            change.after = tile->cpuImage.copy();
        }
        if (change.existedBefore != change.existedAfter
            || change.before.cacheKey() != change.after.cacheKey()) {
            changes.push_back(std::move(change));
        }
    }

    m_changeLog.clear();
    m_tracking = false;
    return changes;
}

void RasterLayerStorage::applyChanges(const std::vector<RasterTileChange>& changes,
                                      bool useAfter)
{
    m_enabled = true;
    for (const auto& change : changes) {
        const bool exists = useAfter ? change.existedAfter : change.existedBefore;
        const QImage& image = useAfter ? change.after : change.before;
        if (!exists) {
            removeTile(change.coord);
            continue;
        }

        auto* tile = ensureTile(change.coord);
        tile->cpuImage = image.convertToFormat(QImage::Format_RGBA8888);
        tile->dirtyCpu = true;
        tile->dirtyGpu = true;
    }
    m_contentBoundsDirty = true;
}

} // namespace core
