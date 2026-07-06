#pragma once

#include <QImage>
#include <QTransform>
#include <QVariantMap>
#include <QRect>
#include <QString>
#include <atomic>
#include <cstdint>
#include <string>
#include <vector>

class Layer;

enum class AsyncJobType {
    FilterApply,
    FilterApplyProgressive
};

struct AsyncJob {
    AsyncJobType type = AsyncJobType::FilterApply;
    std::string toolName;
    QVariantMap params;
    int targetFlatIndex = -1;

    // For undo
    QImage beforeImage;
    QTransform beforeTransform;

    // Thread-safe copy of layer's CPU image
    QImage sourceImage;
    QImage maskImage;

    // Result (filled by worker thread)
    QImage resultImage;

    // Cancellation
    std::atomic<bool> cancelled{false};

    // Identification
    uint64_t jobId = 0;
    Layer* weakLayer = nullptr;

    // The dispatch flushed a rasterStorage (dab) layer into cpuImage so the
    // worker could process the composited pixels. On completion the result is
    // re-tiled back into rasterStorage, keeping the dab-layer representation
    // (content-bounds transform outline, incremental brush tiles) instead of
    // leaving the layer flattened.
    bool retileRasterStorage = false;

    // ── Progressive tiled processing fields ──────────────────
    // Tile rects to process, sorted by viewport center priority.
    std::vector<QRect> tileRects;
    // Viewport center in pixel coordinates (for priority ordering).
    int viewportCenterX = 0;
    int viewportCenterY = 0;
    // Kernel radius for this filter (padding per tile).
    int kernelRadius = 0;
    // Tile size (typically 256).
    int tileSize = 256;
    // Number of tiles per batch (default 4).
    int batchSize = 4;
    // Total tiles to process (for completion tracking).
    int totalTiles = 0;
};
