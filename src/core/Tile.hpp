#pragma once

#include <QRect>
#include <QImage>
#include <cstdint>
#include <chrono>

namespace core {

struct Tile {
    int col = 0;
    int row = 0;

    // Pixel bounds in layer-local space.
    // Top-left = (col * tileSize, row * tileSize).
    // May be smaller than tileSize at right / bottom edge.
    QRect bounds;

    enum class State : uint8_t {
        Empty    = 0,
        Dirty    = 1,  // cpuImage changed, needs GPU upload
        GPUReady = 2,  // texture uploaded and valid
        Cached   = 3   // has cpuCache populated (optional opt)
    };
    State state = State::Empty;

    uint32_t version = 0;     // incremented on each dirty cycle
    uint64_t lastAccess = 0;  // monotonic ms timestamp (LRU)

    // Optional CPU-side cache — populated by evictLRU() for
    // tiles that may become visible again soon.
    QImage cpuCache;

    bool intersects(const QRect& viewportRect) const
    {
        return bounds.intersects(viewportRect);
    }

    void markDirty()
    {
        state = State::Dirty;
        ++version;
    }
};

// Monotonic millisecond clock — used for LRU timestamps.
inline uint64_t currentTimestampMs()
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               now.time_since_epoch()).count();
}

} // namespace core
