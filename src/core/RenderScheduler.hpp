#pragma once

#include <cstdint>
#include <vector>
#include <chrono>
#include <algorithm>
#include <QtGlobal>

namespace core {

// Forward decls
// Tile is in core:: namespace Layer is in global namespace
namespace core { struct Tile; }
class Layer;

class RenderScheduler {
public:
    enum class Priority : int {
        Immediate  = 3,
        Visible    = 2,
        Prefetch   = 1,
        Background = 0
    };

    enum class LOD : int {
        Full    = 0,
        Half    = 1,
        Quarter = 2,
        Eighth  = 3
    };

    RenderScheduler() = default;
    void setLodThresholds(float half, float quarter, float eighth);

    // ── LOD ─────────────────────────────────────────────────
    LOD decideLOD(float zoom, int tileSize) const;

    // ── Frame budget ────────────────────────────────────────
    void beginFrame();
    bool hasBudgetLeft() const;
    float elapsedFrameMs() const;
    void setFrameBudgetMs(float ms) { m_frameBudgetMs = ms; }

    // ── Job queue (placeholder — Phase 6) ───────────────────
    struct TileJob {
        struct Tile* tile    = nullptr;
        Layer*       layer   = nullptr;
        Priority     priority = Priority::Background;
        uint64_t     jobId    = 0;
    };

    void scheduleTileUpload(struct Tile* tile, Layer* layer, Priority p);
    void cancelForLayer(class Layer* layer);
    void cancelAll();
    int  pendingJobCount() const { return static_cast<int>(m_jobs.size()); }

private:
    float m_frameBudgetMs = 8.0f;
    std::chrono::steady_clock::time_point m_frameStart;

    std::vector<TileJob> m_jobs;
    uint64_t m_nextJobId = 1;

    float m_lodHalfThresh    = 4.0f;
    float m_lodQuarterThresh = 8.0f;
    float m_lodEighthThresh  = 16.0f;
};

} // namespace core
