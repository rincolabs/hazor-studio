#include "TileRenderer.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/Tile.hpp"
#include "core/ViewportCamera.hpp"
#include "core/TileManager.hpp"

#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <limits>

// ── Helpers ─────────────────────────────────────────────────────

// Compute the NDC sub-rect [left,right] x [bottom,top] for a tile's
// pixel bounds within a layer of size (W, H).  NDC Y points up.
static void tileNdcRect(const QRect& b, int W, int H,
                         float& left, float& right,
                         float& top, float& bottom)
{
    left   = static_cast<float>(b.x())              / W * 2.0f - 1.0f;
    right  = static_cast<float>(b.x() + b.width())  / W * 2.0f - 1.0f;
    top    = 1.0f - static_cast<float>(b.y())               / H * 2.0f;
    bottom = 1.0f - static_cast<float>(b.y() + b.height())  / H * 2.0f;
}

// Build the 4×4 transform that maps the unit quad [-1,1]² onto the
// NDC sub-rect [left,right] x [bottom,top].
static QMatrix4x4 tileSubMatrix(float left, float right,
                                 float top, float bottom)
{
    float midX  = (left + right) * 0.5f;
    float midY  = (top + bottom) * 0.5f;
    float halfW = (right - left) * 0.5f;
    float halfY = (top - bottom) * 0.5f;
    QMatrix4x4 m;
    m.translate(midX, midY);
    m.scale(halfW, halfY);
    return m;
}

// ── Lifecycle ───────────────────────────────────────────────────

void TileRenderer::initGL()
{
    m_glReady = true;
}

void TileRenderer::enableLayerTiling(Layer* layer, int tileSize)
{
    if (!layer || layer->renderCpuImage().isNull()) return;
    layer->enableTiling(tileSize);
}

void TileRenderer::disableLayerTiling(Layer* layer)
{
    if (!layer) return;
    layer->disableTiling();
}

bool TileRenderer::shouldTile(const Layer* layer)
{
    if (!layer || layer->renderCpuImage().isNull()) return false;
    static constexpr int kMinArea = 256 * 256;
    return layer->imageWidth() * layer->imageHeight() >= kMinArea;
}

// ── Upload dirty tiles ──────────────────────────────────────────

void TileRenderer::uploadDirtyTiles(Layer* layer)
{
    if (!layer || !layer->tiledSystem) return;
    if (layer->textureId == 0) return;

    auto tiles = layer->tileManager.dirtyTiles();
    if (tiles.empty()) return;

    auto* gl = QOpenGLContext::currentContext()
                   ? QOpenGLContext::currentContext()->functions()
                   : nullptr;
    if (!gl) return;

    const QImage& src = layer->renderCpuImage();
    gl->glBindTexture(GL_TEXTURE_2D, layer->textureId);

    for (auto* tile : tiles) {
        const QRect& b = tile->bounds;
        if (b.isEmpty()) continue;

        QImage sub = src.copy(b);
        if (sub.isNull()) continue;

        // Convert to RGBA for OpenGL upload
        QImage rgba = sub.convertToFormat(QImage::Format_RGBA8888);
        if (rgba.isNull()) continue;

        gl->glTexSubImage2D(GL_TEXTURE_2D, 0,
                            b.x(), b.y(),
                            b.width(), b.height(),
                            GL_RGBA, GL_UNSIGNED_BYTE,
                            rgba.constBits());

        tile->state = core::Tile::State::GPUReady;
    }
}

void TileRenderer::uploadDirtyRasterTiles(Layer* layer)
{
    if (!layer || !layer->renderRasterStorage().isEnabled())
        return;

    auto* gl = QOpenGLContext::currentContext()
                   ? QOpenGLContext::currentContext()->functions()
                   : nullptr;
    if (!gl) return;

    GLint prevUnpackAlignment = 4;
    gl->glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);

    layer->renderRasterStorage().forEachTile([&](core::RasterTile& tile) {
        if (!tile.dirtyGpu && tile.textureId != 0)
            return;
        if (tile.cpuImage.isNull())
            return;

        QImage rgba = tile.cpuImage.convertToFormat(QImage::Format_RGBA8888);
        const bool fresh = tile.textureId == 0;
        if (fresh)
            gl->glGenTextures(1, &tile.textureId);

        gl->glBindTexture(GL_TEXTURE_2D, tile.textureId);
        gl->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        // Update the existing texture in place when the size is unchanged (the
        // common per-dab case) — reallocating the whole texture each frame is the
        // expensive part. Only (re)allocate on first upload or a size change.
        if (!fresh && tile.texW == rgba.width() && tile.texH == rgba.height()) {
            gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                                rgba.width(), rgba.height(),
                                GL_RGBA, GL_UNSIGNED_BYTE,
                                rgba.constBits());
        } else {
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             rgba.width(), rgba.height(),
                             0, GL_RGBA, GL_UNSIGNED_BYTE,
                             rgba.constBits());
            tile.texW = rgba.width();
            tile.texH = rgba.height();
        }
        tile.dirtyGpu = false;
    });

    gl->glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
}

// ── Per-layer visible tiles ─────────────────────────────────────

std::vector<TileDraw> TileRenderer::computeTilesForLayer(
    Layer* layer, int docW, int docH,
    const QRectF& viewportPixelDocRect,
    const QMatrix4x4& fullMvp,
    core::RenderScheduler::LOD lod)
{
    std::vector<TileDraw> out;
    if (!layer || !layer->tiledSystem || layer->renderCpuImage().isNull())
        return out;

    int lW = layer->imageWidth();
    int lH = layer->imageHeight();
    if (lW <= 0 || lH <= 0) return out;

    // Transform viewportPixelDocRect corners to layer pixel space
    QTransform inv = layer->owner
        ? layer->owner->accumulatedTransform().inverted()
        : QTransform{};

    if (inv.isIdentity() && layer->owner
        && !layer->owner->transform().isIdentity())
        return out; // singular — skip

    QPointF corners[4] = {
        viewportPixelDocRect.topLeft(),
        viewportPixelDocRect.topRight(),
        viewportPixelDocRect.bottomLeft(),
        viewportPixelDocRect.bottomRight()
    };
    float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
    for (auto& cp : corners) {
        float dnx = static_cast<float>(cp.x()) / docW * 2.0f - 1.0f;
        float dny = 1.0f - static_cast<float>(cp.y()) / docH * 2.0f;
        QPointF ln = inv.map(QPointF(dnx, dny));
        float lpx = (static_cast<float>(ln.x()) + 1.0f) * 0.5f * lW;
        float lpy = (1.0f - static_cast<float>(ln.y())) * 0.5f * lH;
        if (lpx < minX) minX = lpx;
        if (lpx > maxX) maxX = lpx;
        if (lpy < minY) minY = lpy;
        if (lpy > maxY) maxY = lpy;
    }
    if (minX > maxX || minY > maxY) return out; // off-screen

    QRect layerPixelRect(QPoint(static_cast<int>(minX), static_cast<int>(minY)),
                          QPoint(static_cast<int>(maxX), static_cast<int>(maxY)));

    if (lod == core::RenderScheduler::LOD::Full) {
        // Full resolution — use TileManager grid
        auto tiles = layer->tileManager.visibleTiles(layerPixelRect);
        for (auto* tile : tiles) {
            float left, right, top, bottom;
            tileNdcRect(tile->bounds, lW, lH, left, right, top, bottom);
            QMatrix4x4 subM = tileSubMatrix(left, right, top, bottom);

            TileDraw td;
            td.tile     = tile;
            td.mvp      = fullMvp * subM;
            td.uvOffset = QVector2D(
                static_cast<float>(tile->bounds.x()) / lW,
                static_cast<float>(tile->bounds.y()) / lH);
            td.uvScale  = QVector2D(
                static_cast<float>(tile->bounds.width())  / lW,
                static_cast<float>(tile->bounds.height()) / lH);
            td.maskUvOffset = td.uvOffset;
            td.maskUvScale = td.uvScale;
            out.push_back(td);
        }
    } else {
        // Low-res LOD — coarse grid: fewer, larger tiles
        int lodShift = static_cast<int>(lod);
        int effectiveTileSize = layer->tileManager.tileSize() << lodShift;

        int firstCol = std::max(0, layerPixelRect.left() / effectiveTileSize);
        int lastCol  = std::min((lW + effectiveTileSize - 1) / effectiveTileSize - 1,
                                 layerPixelRect.right() / effectiveTileSize);
        int firstRow = std::max(0, layerPixelRect.top() / effectiveTileSize);
        int lastRow  = std::min((lH + effectiveTileSize - 1) / effectiveTileSize - 1,
                                 layerPixelRect.bottom() / effectiveTileSize);

        for (int r = firstRow; r <= lastRow; ++r) {
            for (int c = firstCol; c <= lastCol; ++c) {
                QRect tb(c * effectiveTileSize, r * effectiveTileSize,
                         effectiveTileSize, effectiveTileSize);
                tb = tb.intersected(QRect(0, 0, lW, lH));
                if (tb.isEmpty()) continue;

                float left, right, top, bottom;
                tileNdcRect(tb, lW, lH, left, right, top, bottom);
                QMatrix4x4 subM = tileSubMatrix(left, right, top, bottom);

                TileDraw td;
                td.tile     = nullptr;
                td.mvp      = fullMvp * subM;
                td.uvOffset = QVector2D(
                    static_cast<float>(tb.x()) / lW,
                    static_cast<float>(tb.y()) / lH);
                td.uvScale  = QVector2D(
                    static_cast<float>(tb.width())  / lW,
                    static_cast<float>(tb.height()) / lH);
                td.maskUvOffset = td.uvOffset;
                td.maskUvScale = td.uvScale;
                out.push_back(td);
            }
        }
    }
    return out;
}

std::vector<TileDraw> TileRenderer::computeRasterTilesForLayer(
    Layer* layer, int docW, int docH,
    const QRectF& viewportPixelDocRect,
    const QMatrix4x4& layerMvp)
{
    std::vector<TileDraw> out;
    if (!layer || !layer->renderRasterStorage().isEnabled())
        return out;

    const QSize baseSize = layer->rasterBaseSize();
    const int lW = baseSize.width();
    const int lH = baseSize.height();
    if (lW <= 0 || lH <= 0)
        return out;

    QTransform inv = layer->owner
        ? layer->owner->accumulatedTransform().inverted()
        : QTransform{};

    QPointF corners[4] = {
        viewportPixelDocRect.topLeft(),
        viewportPixelDocRect.topRight(),
        viewportPixelDocRect.bottomLeft(),
        viewportPixelDocRect.bottomRight()
    };

    qreal minX = std::numeric_limits<qreal>::max();
    qreal minY = std::numeric_limits<qreal>::max();
    qreal maxX = std::numeric_limits<qreal>::lowest();
    qreal maxY = std::numeric_limits<qreal>::lowest();

    for (auto& cp : corners) {
        const qreal dnx = cp.x() / docW * 2.0 - 1.0;
        const qreal dny = 1.0 - cp.y() / docH * 2.0;
        const QPointF ln = inv.map(QPointF(dnx, dny));
        const qreal lpx = (ln.x() + 1.0) * 0.5 * lW;
        const qreal lpy = (1.0 - ln.y()) * 0.5 * lH;
        minX = std::min(minX, lpx);
        minY = std::min(minY, lpy);
        maxX = std::max(maxX, lpx);
        maxY = std::max(maxY, lpy);
    }

    QRect layerPixelRect(
        QPoint(static_cast<int>(std::floor(minX)), static_cast<int>(std::floor(minY))),
        QPoint(static_cast<int>(std::ceil(maxX)), static_cast<int>(std::ceil(maxY))));

    const auto tiles = layer->renderRasterStorage().tilesInRect(layerPixelRect);
    out.reserve(tiles.size());
    for (const auto* tile : tiles) {
        if (!tile || tile->textureId == 0)
            continue;

        float left, right, top, bottom;
        tileNdcRect(tile->bounds, lW, lH, left, right, top, bottom);
        QMatrix4x4 subM = tileSubMatrix(left, right, top, bottom);

        TileDraw td;
        td.textureId = tile->textureId;
        td.mvp = layerMvp * subM;
        td.uvOffset = QVector2D(0.0f, 0.0f);
        td.uvScale = QVector2D(1.0f, 1.0f);
        // Use mask dimensions + maskOrigin for UV in layer image coordinates.
        // Tiles can extend outside that image envelope; the shader treats those
        // out-of-mask UVs as fully visible instead of clamping the mask edge.
        const bool hasMask = !layer->maskImage.isNull();
        const int mW = hasMask ? layer->maskImage.width()  : lW;
        const int mH = hasMask ? layer->maskImage.height() : lH;
        const int ox = hasMask ? layer->maskOrigin.x() : 0;
        const int oy = hasMask ? layer->maskOrigin.y() : 0;
        td.maskUvOffset = QVector2D(static_cast<float>(tile->bounds.x() - ox) / mW,
                                    static_cast<float>(tile->bounds.y() - oy) / mH);
        td.maskUvScale = QVector2D(static_cast<float>(tile->bounds.width()) / mW,
                                   static_cast<float>(tile->bounds.height()) / mH);
        out.push_back(td);
    }

    return out;
}

// ── Visible tile computation (batch) ────────────────────────────

std::vector<TileDraw> TileRenderer::computeVisibleTiles(
    Document* doc, const core::ViewportCamera& camera,
    int viewportW, int viewportH,
    core::RenderScheduler::LOD lod)
{
    Q_UNUSED(viewportW);
    Q_UNUSED(viewportH);
    std::vector<TileDraw> out;
    if (!doc || doc->size.isNull()) return out;

    int docW = doc->size.width();
    int docH = doc->size.height();

    // ── Document-level MVP (shared by all layers) ──
    QMatrix4x4 docMvp;
    docMvp.translate(static_cast<float>(camera.panOffset.x()),
                     static_cast<float>(camera.panOffset.y()));
    docMvp.scale(camera.zoom);
    docMvp.scale(static_cast<float>(camera.canvasHalfExtents.x()),
                 static_cast<float>(camera.canvasHalfExtents.y()));

    // Viewport bounds in document-pixel space
    QRectF vpPixels = camera.visiblePixelRect(docW, docH);

    // ── Walk visible layers bottom → top ──
    auto flat = doc->flatten();
    for (auto* node : flat) {
        if (!node->isVisible()) continue;
        if (node->type != LayerTreeNode::Type::Layer) continue;
        if (!node->layer || !node->layer->tiledSystem) continue;

        Layer* layer = node->layer.get();
        if (layer->renderCpuImage().isNull()) continue;

        int lW = layer->imageWidth();
        int lH = layer->imageHeight();
        if (lW <= 0 || lH <= 0) continue;

        // ── Layer transform in 4×4 matrix form ──
        QTransform accum = node->accumulatedTransform();
        QMatrix4x4 layerMat;
        layerMat(0,0) = static_cast<float>(accum.m11());
        layerMat(0,1) = static_cast<float>(accum.m21());
        layerMat(0,3) = static_cast<float>(accum.m31());
        layerMat(1,0) = static_cast<float>(accum.m12());
        layerMat(1,1) = static_cast<float>(accum.m22());
        layerMat(1,3) = static_cast<float>(accum.m32());

        // Combined MVP = doc * layer
        QMatrix4x4 fullMvp = docMvp * layerMat;

        // ── Viewport rect → layer-local pixel rect ──
        QTransform inv = accum.inverted();
        if (inv.isIdentity() && !accum.isIdentity())
            continue; // singular transform, skip

        QPointF corners[4] = {
            vpPixels.topLeft(), vpPixels.topRight(),
            vpPixels.bottomLeft(), vpPixels.bottomRight()
        };
        float minX = 1e9f, minY = 1e9f, maxX = -1e9f, maxY = -1e9f;
        for (auto& cp : corners) {
            // doc pixel → doc NDC
            float dnx = static_cast<float>(cp.x()) / docW * 2.0f - 1.0f;
            float dny = 1.0f - static_cast<float>(cp.y()) / docH * 2.0f;
            // doc NDC → layer NDC
            QPointF ln = inv.map(QPointF(dnx, dny));
            float lnx = static_cast<float>(ln.x());
            float lny = static_cast<float>(ln.y());
            // layer NDC → layer pixel
            float lpx = (lnx + 1.0f) * 0.5f * lW;
            float lpy = (1.0f - lny) * 0.5f * lH;
            if (lpx < minX) minX = lpx;
            if (lpx > maxX) maxX = lpx;
            if (lpy < minY) minY = lpy;
            if (lpy > maxY) maxY = lpy;
        }

        QRect layerPixelRect(QPoint(static_cast<int>(minX), static_cast<int>(minY)),
                              QPoint(static_cast<int>(maxX), static_cast<int>(maxY)));

        if (lod == core::RenderScheduler::LOD::Full) {
            // Full resolution — use TileManager grid
            auto tiles = layer->tileManager.visibleTiles(layerPixelRect);
            for (auto* tile : tiles) {
                float left, right, top, bottom;
                tileNdcRect(tile->bounds, lW, lH, left, right, top, bottom);
                QMatrix4x4 subM = tileSubMatrix(left, right, top, bottom);

                TileDraw td;
                td.tile     = tile;
                td.mvp      = fullMvp * subM;
                td.uvOffset = QVector2D(
                    static_cast<float>(tile->bounds.x()) / lW,
                    static_cast<float>(tile->bounds.y()) / lH);
                td.uvScale  = QVector2D(
                    static_cast<float>(tile->bounds.width())  / lW,
                    static_cast<float>(tile->bounds.height()) / lH);
                out.push_back(td);
            }
        } else {
            // Low-res LOD — coarse grid
            int lodShift = static_cast<int>(lod);
            int effectiveTileSize = layer->tileManager.tileSize() << lodShift;

            int firstCol = std::max(0, layerPixelRect.left() / effectiveTileSize);
            int lastCol  = std::min((lW + effectiveTileSize - 1) / effectiveTileSize - 1,
                                     layerPixelRect.right() / effectiveTileSize);
            int firstRow = std::max(0, layerPixelRect.top() / effectiveTileSize);
            int lastRow  = std::min((lH + effectiveTileSize - 1) / effectiveTileSize - 1,
                                     layerPixelRect.bottom() / effectiveTileSize);

            for (int r = firstRow; r <= lastRow; ++r) {
                for (int c = firstCol; c <= lastCol; ++c) {
                    QRect tb(c * effectiveTileSize, r * effectiveTileSize,
                             effectiveTileSize, effectiveTileSize);
                    tb = tb.intersected(QRect(0, 0, lW, lH));
                    if (tb.isEmpty()) continue;

                    float left, right, top, bottom;
                    tileNdcRect(tb, lW, lH, left, right, top, bottom);
                    QMatrix4x4 subM = tileSubMatrix(left, right, top, bottom);

                    TileDraw td;
                    td.tile     = nullptr;
                    td.mvp      = fullMvp * subM;
                    td.uvOffset = QVector2D(
                        static_cast<float>(tb.x()) / lW,
                        static_cast<float>(tb.y()) / lH);
                    td.uvScale  = QVector2D(
                        static_cast<float>(tb.width())  / lW,
                        static_cast<float>(tb.height()) / lH);
                    out.push_back(td);
                }
            }
        }
    }

    return out;
}
