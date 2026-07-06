#include "RenderScheduler.hpp"
#include "Tile.hpp"
#include "Layer.hpp"

namespace core {

RenderScheduler::LOD RenderScheduler::decideLOD(float zoom, int tileSize) const
{
    float psp = zoom * static_cast<float>(tileSize);
    if (psp < m_lodHalfThresh)    return LOD::Eighth;
    if (psp < m_lodQuarterThresh) return LOD::Quarter;
    if (psp < m_lodEighthThresh)  return LOD::Half;
    return LOD::Full;
}

void RenderScheduler::setLodThresholds(float half, float quarter, float eighth)
{
    m_lodHalfThresh = half;
    m_lodQuarterThresh = quarter;
    m_lodEighthThresh = eighth;
}

void RenderScheduler::beginFrame()
{
    m_frameStart = std::chrono::steady_clock::now();
}

bool RenderScheduler::hasBudgetLeft() const
{
    return elapsedFrameMs() < m_frameBudgetMs;
}

float RenderScheduler::elapsedFrameMs() const
{
    auto now = std::chrono::steady_clock::now();
    float ms = std::chrono::duration<float, std::milli>(now - m_frameStart).count();
    return ms;
}

void RenderScheduler::scheduleTileUpload(Tile* tile, Layer* layer, Priority p)
{
    m_jobs.push_back({tile, layer, p, m_nextJobId++});
}

void RenderScheduler::cancelForLayer(Layer* layer)
{
    auto it = std::remove_if(m_jobs.begin(), m_jobs.end(),
        [layer](const TileJob& j) { return j.layer == layer; });
    m_jobs.erase(it, m_jobs.end());
}

void RenderScheduler::cancelAll()
{
    m_jobs.clear();
}

} // namespace core
