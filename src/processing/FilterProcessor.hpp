#pragma once

#include <string>
#include <vector>
#include <utility>
#include <QVariantMap>
#include <QRect>
#include <QImage>

namespace core { struct Tile; }

namespace processing {

class FilterProcessor {
public:
    FilterProcessor() = delete;

    // Apply filter to specific tiles. Returns number of tiles processed.
    // image is modified in-place.
    static int processTiles(QImage& image,
                            const std::vector<core::Tile*>& tiles,
                            const std::string& toolName,
                            const QVariantMap& params);

    // Apply filter to a pixel rect. Finds intersecting tiles via the provided list
    // and processes them. Returns number of tiles processed.
    static int processRect(QImage& image,
                           int imageW, int imageH,
                           const QRect& pixelRect,
                           int tileSize,
                           const std::vector<core::Tile*>& intersectingTiles,
                           const std::string& toolName,
                           const QVariantMap& params);

    // Legacy full-image processing. Returns a new QImage.
    static QImage processFull(const QImage& image,
                              const std::string& toolName,
                              const QVariantMap& params);

    // Apply multiple filters in sequence on the same cv::Mat conversion,
    // avoiding repeated QImage↔cv::Mat round-trips.
    static QImage processBatch(const QImage& image,
                               const std::vector<std::pair<std::string, QVariantMap>>& chain);

    // Tiled batch: apply chain of filters per tile.
    static int processBatchTiles(QImage& image,
                                 const std::vector<core::Tile*>& tiles,
                                 const std::vector<std::pair<std::string, QVariantMap>>& chain);

    // Padding pixels needed around a tile for neighborhood filters.
    // Returns 0 for point filters.
    static int kernelRadius(const std::string& toolName,
                            const QVariantMap& params);

    // Can this tool be applied per-tile (not a global/transform operation)?
    static bool isTileable(const std::string& toolName);

    // Does this tool read neighboring pixels (needs padding)?
    static bool isNeighborhoodFilter(const std::string& toolName);

};

} // namespace processing
