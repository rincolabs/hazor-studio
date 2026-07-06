#pragma once

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QTransform>
#include <QString>
#include <memory>
#include <cstdint>

#include "TileManager.hpp"
#include "DirtyRegion.hpp"
#include "RenderScheduler.hpp"
#include "RasterLayerStorage.hpp"

struct TextLayerData;
struct ShapeData;
struct DistortData;
struct GradientFillData;
class LayerTreeNode;

enum class BlendMode {
    Normal = 0,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity
};

enum class LayerType {
    Raster = 0,
    Text,
    Shape,
    Adjustment,
    Group
};

// Per-node editing locks (stored on LayerTreeNode::lockFlags). Each is an
// independent bit so the four lock toggles can coexist. LockAll is a
// distinct *master* bit (not the OR of the others) so "fully locked" can block
// operations the partial locks allow (delete, merge, rasterize, mask editing).
// Always query lock state through the LayerTreeNode::is*Locked()/canEdit*()
// helpers rather than testing bits directly — they fold LockAll in correctly.
enum LockFlag {
    LockNone = 0,
    LockTransparent = 1 << 0,   // protect transparency (alpha-preserve)
    LockImage = 1 << 1,         // protect pixels (no raster/content edits)
    LockPosition = 1 << 2,      // protect position (no move/transform)
    LockAll = 1 << 3            // master lock (everything but selection/visibility)
};

class Layer {
public:
    QString name;
    QImage cpuImage;
    unsigned int textureId = 0;
    unsigned int fbo = 0;

    QImage maskImage;
    QImage maskRawImage;
    // maskOrigin: logical layer-pixel coordinate that mask pixel (0,0) corresponds to.
    // Non-zero when the layer expanded into negative coordinates (e.g. tiles above/left).
    QPoint maskOrigin;
    unsigned int maskTextureId = 0;
    unsigned int maskFbo = 0;
    bool maskVisible = true;
    float maskDensity = 1.0f;
    float maskFeather = 0.0f;
    bool maskTextureOutdated = false;

    LayerTreeNode* owner = nullptr;

    QImage cachedMaskThumb;
    bool maskThumbDirty = true;

    bool isTextLayer() const { return textData != nullptr; }
    bool isShapeLayer() const { return shapeData != nullptr; }
    bool isDistortLayer() const { return distortData != nullptr; }
    bool isGradientFillLayer() const { return gradientFillData != nullptr; }

    // Single predicate for "render this shape layer through its viewport-adaptive
    // sprite cache". A layer mask vetoes the sprite: the mask lives in layer-image
    // space while the sprite covers world-aligned shape bounds, and the GPU mask
    // UV (scale+offset only) cannot map between the two. With a mask present every
    // path (live GPU, CPU projection, merge) draws cpuImage with the node's
    // accumulated transform so the mask geometry is identical everywhere.
    bool shapeSpriteRenderable() const {
        return isShapeLayer() && !shapeCache.image.isNull() && maskImage.isNull();
    }

    std::shared_ptr<TextLayerData> textData;
    std::shared_ptr<ShapeData> shapeData;
    // Non-destructive Distort/Perspective metadata (original pixels + quad).
    // When present, cpuImage holds the rendered warp; the warp stays re-editable.
    std::shared_ptr<DistortData> distortData;
    // Future non-destructive Gradient Fill layer payload. Raster application is
    // implemented today; this keeps the layer model ready for editable fills.
    std::shared_ptr<GradientFillData> gradientFillData;

    // Transform snapshot used by "Reset Transform" to restore the layer's
    // initial placement/scale when the layer was created/imported/pasted.
    QTransform resetTransform;
    bool hasResetTransform = false;

    // ── Tile system (Phase 1) ────────────────────────────────
    core::TileManager tileManager;
    core::DirtyRegion dirtyRegion;
    bool tiledSystem = false;
    core::RasterLayerStorage rasterStorage;

    int imageWidth()  const { return cpuImage.width(); }
    int imageHeight() const { return cpuImage.height(); }

    bool usesRasterStorage() const { return rasterStorage.isEnabled(); }

    QSize rasterBaseSize() const {
        if (rasterStorage.isEnabled() && rasterStorage.baseSize().isValid()
            && !rasterStorage.baseSize().isEmpty()) {
            return rasterStorage.baseSize();
        }
        return cpuImage.size();
    }

    QRect maskTargetBounds() const {
        QRect bounds(QPoint(0, 0), rasterBaseSize());
        if (rasterStorage.isEnabled()) {
            const QRect content = rasterStorage.contentBounds();
            if (!content.isEmpty())
                bounds = bounds.isEmpty() ? content : bounds.united(content);
        }
        return bounds;
    }

    QRectF renderImageBounds() const {
        if (rasterStorage.isEnabled()) {
            const QRect bounds = rasterStorage.logicalBounds();
            if (!bounds.isEmpty())
                return QRectF(bounds);
        }
        return QRectF(QPointF(0, 0), cpuImage.size());
    }

    QRectF contentImageBounds() const {
        if (rasterStorage.isEnabled()) {
            QRect bounds = rasterStorage.contentBounds();
            if (bounds.isEmpty())
                bounds = rasterStorage.logicalBounds();
            if (!bounds.isEmpty())
                return QRectF(bounds);
        }
        return QRectF(QPointF(0, 0), cpuImage.size());
    }

    // Alpha content bounds of cpuImage (flat raster layers): the tight rect of
    // pixels with alpha > 0, in image coordinates. Cached — the scan is O(W×H) —
    // and revalidated by a (cacheKey, size) match, which covers every mutation
    // that assigns a new QImage. In-place writes (scanLine/bits on a sole-owner
    // buffer keep the cacheKey) must call invalidateContentBounds(); the central
    // hooks are ImageController::markLayerDirty and the GPU→CPU readbacks.
    // Returns the full image rect for formats without alpha (nothing to trim).
    QRect cpuContentBounds() const;
    void invalidateContentBounds() { cpuContentBoundsDirty = true; }

    QImage renderImage() const {
        if (rasterStorage.isEnabled()) {
            QRect bounds;
            QImage image = rasterStorage.toImage(&bounds);
            if (!image.isNull())
                return image;
        }
        return cpuImage;
    }

    // Returns a full base-size image compositing rasterStorage tiles at their correct
    // pixel positions on top of cpuImage. Use this (not renderImage()) whenever a
    // destructive operation needs a position-correct, full-size pixel snapshot.
    QImage compositeImage() const;

    // Like compositeImage(), but the buffer is grown to also include rasterStorage
    // tiles painted OUTSIDE baseSize (out-of-bounds brush dabs that expanded the
    // layer). outBounds reports — in layer-pixel coordinates — the rect the returned
    // image covers; its origin may be negative or extend past baseSize. Use this (not
    // compositeImage()) when an op must preserve out-of-bounds pixels (e.g. Distort).
    QImage compositeImageExpanded(QRect* outBounds = nullptr) const;

    void enableRasterStorage(int tileSize = 256) {
        if (!isTextLayer() && !isShapeLayer()) {
            rasterStorage.enableFromImage(cpuImage, tileSize);
            tiledSystem = false;
            tileManager.clear();
            dirtyRegion.clear();
            pendingGpuUpload = true;
            textureOutdated = true;
        }
    }

    void replaceRasterStorageWithImage(const QImage& image,
                                       const QPoint& origin = QPoint(0, 0),
                                       int tileSize = 256) {
        rasterStorage.replaceWithImage(image, origin, tileSize);
        cpuImage = image.convertToFormat(QImage::Format_RGBA8888);
        tiledSystem = false;
        tileManager.clear();
        dirtyRegion.clear();
        pendingGpuUpload = true;
        textureOutdated = true;
    }

    // Ensures the raster storage covers localRect (in image-local pixel coords).
    // If the backing storage expanded, *outAdjust receives the QTransform that the
    // caller must pre-multiply onto the node's transform to preserve visual position.
    // Returns true if expansion occurred (outAdjust is meaningful).
    bool ensureWritableRect(const QRect& localRect, QTransform* outAdjust = nullptr);

    void enableTiling(int tileSize = 256) {
        if (imageWidth() > 0 && imageHeight() > 0) {
            tileManager.init(imageWidth(), imageHeight(), tileSize);
            tiledSystem = true;
        }
    }

    void disableTiling() {
        tileManager.clear();
        dirtyRegion.clear();
        tiledSystem = false;
    }

    bool textureOutdated = true;
    bool pendingGpuUpload = false;

    // cpuContentBounds() cache (see above). Mutable: the scan is a logically
    // const query performed lazily on first use after an edit.
    mutable QRect cpuContentBoundsCache;
    mutable qint64 cpuContentBoundsKey = 0;
    mutable bool cpuContentBoundsDirty = true;

    struct ShapeRenderCache {
        QImage image;
        QTransform spriteTransform;
        bool dirty = true;
        float zoom = -1.0f;
        QSize documentSize;
        std::shared_ptr<ShapeData> shapeSnapshot;
        QTransform transformSnapshot;
        float scaleX = 0.0f;
        float scaleY = 0.0f;
    };
    ShapeRenderCache shapeCache;

    // ── Mipmap / LOD (Phase 8) ───────────────────────────────
    unsigned int lodTextures[4] = {0, 0, 0, 0};
    QImage lodLevels[4];
    bool lodDirty[4] = {true, true, true, true};

    int lodImageWidth(core::RenderScheduler::LOD lod) const {
        int w = imageWidth();
        int s = static_cast<int>(lod);
        return std::max(1, w >> s);
    }

    int lodImageHeight(core::RenderScheduler::LOD lod) const {
        int h = imageHeight();
        int s = static_cast<int>(lod);
        return std::max(1, h >> s);
    }
};
