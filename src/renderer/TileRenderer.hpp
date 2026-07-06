#pragma once

#include <QMatrix4x4>
#include <QVector2D>
#include <vector>

#include "core/RenderScheduler.hpp"

class Layer;
class Document;
namespace core { struct Tile; class ViewportCamera; }

struct TileDraw {
    const core::Tile* tile = nullptr;
    unsigned int textureId = 0;
    QMatrix4x4 mvp;          // tile-level model-view-projection
    QVector2D uvOffset;      // uv origin in texture [0,1] space
    QVector2D uvScale;       // uv range in texture [0,1] space
    QVector2D maskUvOffset;  // uv origin for full-layer mask sampling
    QVector2D maskUvScale;   // uv range for full-layer mask sampling
};

class TileRenderer {
public:
    TileRenderer() = default;

    void initGL();

    // Compute visible TileDraws for all tiled layers (batch).
    // lod controls coarse grid size for multi-resolution rendering.
    std::vector<TileDraw> computeVisibleTiles(
        Document* doc, const core::ViewportCamera& camera,
        int viewportW, int viewportH,
        core::RenderScheduler::LOD lod = core::RenderScheduler::LOD::Full);

    // Compute visible tiles for a single layer (called from render loop).
    // fullMvp is the layer's MVP (pan * zoom * halfExtents * layerTransform).
    // lod controls coarse grid size for multi-resolution rendering.
    std::vector<TileDraw> computeTilesForLayer(
        Layer* layer, int docW, int docH,
        const QRectF& viewportPixelDocRect,
        const QMatrix4x4& fullMvp,
        core::RenderScheduler::LOD lod = core::RenderScheduler::LOD::Full);

    // Upload dirty tile sub-rects to the layer's full GPU texture.
    void uploadDirtyTiles(Layer* layer);
    void uploadDirtyRasterTiles(Layer* layer);
    std::vector<TileDraw> computeRasterTilesForLayer(
        Layer* layer, int docW, int docH,
        const QRectF& viewportPixelDocRect,
        const QMatrix4x4& layerMvp);

    // Helpers to enable/disable and decide.
    static void enableLayerTiling(Layer* layer, int tileSize = 256);
    static void disableLayerTiling(Layer* layer);
    static bool shouldTile(const Layer* layer);

private:
    bool m_glReady = false;
};
