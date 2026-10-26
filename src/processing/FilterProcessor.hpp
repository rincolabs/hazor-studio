#pragma once

#include <string>
#include <vector>
#include <utility>
#include <QVariantMap>
#include <QMargins>
#include <QRect>
#include <QSize>
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

    // How far outside the image rect the filter's own output reaches, in image
    // pixels. A layer image is a bounded buffer, so a filter that displaces or
    // spreads pixels (zoom/radial/motion blur, every convolution blur) has its
    // result cropped at the edge and the effect ends in a straight line at the
    // old layer bounds unless the buffer is grown by these margins first.
    // `contentBounds` is where the layer actually has pixels — a filter can only
    // drag what is there, so content inset from the edge needs less room (pass a
    // null rect for "the whole image"). Null margins => nothing to grow.
    static QMargins outwardReach(const std::string& toolName,
                                 const QVariantMap& params,
                                 const QSize& imageSize,
                                 const QRect& contentBounds);

    // `params` rewritten for an image that grew by `margins`: parameters given in
    // normalized image coordinates (the blur centre) denote a different pixel once
    // the image is larger, so they are re-normalized to keep pointing at the same
    // one.
    static QVariantMap paramsForGrownImage(const std::string& toolName,
                                           const QVariantMap& params,
                                           const QSize& oldSize,
                                           const QMargins& margins);

    // Can this tool be applied per-tile (not a global/transform operation)?
    static bool isTileable(const std::string& toolName);

    // Does this tool read neighboring pixels (needs padding)?
    static bool isNeighborhoodFilter(const std::string& toolName);

};

} // namespace processing
