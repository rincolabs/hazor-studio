#include "DocumentCompositor.hpp"
#include "RenderContext.hpp"
#include "BlendRules.hpp"
#include "core/AdjustmentTypes.hpp"
#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SolidColorData.hpp"
#include <QColor>
#include <QPainter>
#include <QTransform>
#include <QDebug>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <set>

// ─────────────────────────────────────────────────────────────────────────
// CPU compositor — single source of truth for the flattened document image.
//
// Walks the layer tree bottom→top, mirroring the GPU compositor
// (renderer/LayerCompositor.cpp) so the result matches what is shown on
// screen: per-layer blend/opacity/mask/effects, group isolation for
// non-Normal group blend modes, and the same transform chain.
//
// Blend-mode semantics come from renderer/BlendRules.hpp (shared with the
// GPU path). The four non-separable HSL modes (Hue/Saturation/Color/
// Luminosity) have no QPainter::CompositionMode and are composited manually
// per pixel below, using the same HSL helpers as the blend shader.
// ─────────────────────────────────────────────────────────────────────────

namespace {

// ── Non-separable (HSL) blend helpers — mirror GPUViewport_Shaders.cpp ──
using Vec3 = std::array<float, 3>;

inline float lum(const Vec3& c) { return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2]; }

inline Vec3 clipColor(Vec3 c)
{
    const float l = lum(c);
    const float n = std::min({c[0], c[1], c[2]});
    const float x = std::max({c[0], c[1], c[2]});
    if (n < 0.0f) {
        const float d = l - n;
        if (d != 0.0f) for (float& v : c) v = l + (v - l) * l / d;
    }
    if (x > 1.0f) {
        const float d = x - l;
        if (d != 0.0f) for (float& v : c) v = l + (v - l) * (1.0f - l) / d;
    }
    return c;
}

inline Vec3 setLum(Vec3 c, float l)
{
    const float d = l - lum(c);
    for (float& v : c) v += d;
    return clipColor(c);
}

inline float sat(const Vec3& c)
{
    return std::max({c[0], c[1], c[2]}) - std::min({c[0], c[1], c[2]});
}

inline Vec3 setSat(Vec3 c, float s)
{
    const float mn = std::min({c[0], c[1], c[2]});
    const float mx = std::max({c[0], c[1], c[2]});
    if (mn == mx) return {0.0f, 0.0f, 0.0f};
    for (float& v : c)
        v = (v == mx) ? s : (v == mn ? 0.0f : (v - mn) * s / (mx - mn));
    return c;
}

inline Vec3 blendHsl(BlendMode mode, const Vec3& cb, const Vec3& cs)
{
    switch (mode) {
    case BlendMode::Hue:        return setLum(setSat(cs, sat(cb)), lum(cb));
    case BlendMode::Saturation: return setLum(setSat(cb, sat(cs)), lum(cb));
    case BlendMode::Color:      return setLum(cs, lum(cb));
    case BlendMode::Luminosity: return setLum(cb, lum(cs));
    default:                    return cs;
    }
}

inline uchar toByte(float v)
{
    return static_cast<uchar>(std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
}

struct PremulSample {
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

PremulSample samplePremultipliedAt(const QImage& rgba, double x, double y)
{
    const int w = rgba.width();
    const int h = rgba.height();
    if (w <= 0 || h <= 0)
        return {};

    const int ix0 = static_cast<int>(std::floor(x));
    const int iy0 = static_cast<int>(std::floor(y));
    const int ix1 = ix0 + 1;
    const int iy1 = iy0 + 1;
    const int x0 = std::clamp(ix0, 0, w - 1);
    const int y0 = std::clamp(iy0, 0, h - 1);
    const int x1 = std::clamp(ix1, 0, w - 1);
    const int y1 = std::clamp(iy1, 0, h - 1);
    const float tx = static_cast<float>(x - ix0);
    const float ty = static_cast<float>(y - iy0);

    auto texel = [&](int px, int py) {
        const uchar* p = rgba.constScanLine(py) + px * 4;
        const float a = p[3] / 255.0f;
        return PremulSample{
            (p[0] / 255.0f) * a,
            (p[1] / 255.0f) * a,
            (p[2] / 255.0f) * a,
            a
        };
    };
    auto mix = [](const PremulSample& a, const PremulSample& b, float t) {
        return PremulSample{
            a.r + (b.r - a.r) * t,
            a.g + (b.g - a.g) * t,
            a.b + (b.b - a.b) * t,
            a.a + (b.a - a.a) * t
        };
    };

    return mix(mix(texel(x0, y0), texel(x1, y0), tx),
               mix(texel(x0, y1), texel(x1, y1), tx),
               ty);
}

QImage resampleLikeGpu(const QImage& source, int outW, int outH)
{
    if (source.isNull() || outW <= 0 || outH <= 0)
        return QImage();

    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    QImage out(outW, outH, QImage::Format_RGBA8888);
    const double footprintX = static_cast<double>(rgba.width()) / outW;
    const double footprintY = static_cast<double>(rgba.height()) / outH;

    for (int y = 0; y < outH; ++y) {
        uchar* row = out.scanLine(y);
        const double centerY = (static_cast<double>(y) + 0.5) * footprintY - 0.5;
        const double stepY = footprintY / 4.0;
        const double firstY = centerY - footprintY * 0.5 + stepY * 0.5;
        for (int x = 0; x < outW; ++x) {
            const double centerX = (static_cast<double>(x) + 0.5) * footprintX - 0.5;
            const double stepX = footprintX / 4.0;
            const double firstX = centerX - footprintX * 0.5 + stepX * 0.5;

            PremulSample sum;
            for (int sy = 0; sy < 4; ++sy) {
                for (int sx = 0; sx < 4; ++sx) {
                    const PremulSample s = samplePremultipliedAt(
                        rgba, firstX + sx * stepX, firstY + sy * stepY);
                    sum.r += s.r;
                    sum.g += s.g;
                    sum.b += s.b;
                    sum.a += s.a;
                }
            }
            sum.r *= 1.0f / 16.0f;
            sum.g *= 1.0f / 16.0f;
            sum.b *= 1.0f / 16.0f;
            sum.a *= 1.0f / 16.0f;

            uchar* p = row + x * 4;
            if (sum.a > 0.0f) {
                p[0] = toByte(sum.r / sum.a);
                p[1] = toByte(sum.g / sum.a);
                p[2] = toByte(sum.b / sum.a);
            } else {
                p[0] = p[1] = p[2] = 0;
            }
            p[3] = toByte(sum.a);
        }
    }
    return out;
}

// Composite `src` over `dst` using a non-separable HSL blend mode.
// Both images are canvas-sized Format_ARGB32_Premultiplied. `opacity` is the
// node opacity folded into the source alpha. Uses the standard W3C
// non-separable compositing formula, consistent with QPainter's handling of
// the separable modes.
void compositeNonSeparable(QImage& dst, const QImage& src, BlendMode mode, float opacity)
{
    const int w = std::min(dst.width(), src.width());
    const int h = std::min(dst.height(), src.height());
    for (int y = 0; y < h; ++y) {
        QRgb*       d = reinterpret_cast<QRgb*>(dst.scanLine(y));
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb sp = s[x];
            const float sA0 = qAlpha(sp) / 255.0f;          // src coverage
            const float sa  = sA0 * opacity;                 // effective src alpha
            if (sa <= 0.0f) continue;

            // Unpremultiply src colour (stage is premultiplied).
            Vec3 cs{0, 0, 0};
            if (sA0 > 0.0f) {
                cs = {qRed(sp) / 255.0f / sA0, qGreen(sp) / 255.0f / sA0,
                      qBlue(sp) / 255.0f / sA0};
            }

            const QRgb dp = d[x];
            const float ab = qAlpha(dp) / 255.0f;            // dst alpha
            Vec3 cb{0, 0, 0};
            if (ab > 0.0f) {
                cb = {qRed(dp) / 255.0f / ab, qGreen(dp) / 255.0f / ab,
                      qBlue(dp) / 255.0f / ab};
            }

            const Vec3 B = blendHsl(mode, cb, cs);
            // W3C: blended source colour Cs' = (1-ab)*Cs + ab*B
            const Vec3 cp{(1.0f - ab) * cs[0] + ab * B[0],
                          (1.0f - ab) * cs[1] + ab * B[1],
                          (1.0f - ab) * cs[2] + ab * B[2]};

            // Source-over with premultiplied output. dst channels are already
            // premultiplied (dpr = qRed(dp)/255).
            const float ao   = sa + ab * (1.0f - sa);
            const float outR = sa * cp[0] + (1.0f - sa) * (qRed(dp)   / 255.0f);
            const float outG = sa * cp[1] + (1.0f - sa) * (qGreen(dp) / 255.0f);
            const float outB = sa * cp[2] + (1.0f - sa) * (qBlue(dp)  / 255.0f);
            d[x] = qRgba(toByte(outR), toByte(outG), toByte(outB), toByte(ao));
        }
    }
}

// Composite a fully-rendered canvas-sized `stage` onto `target` with the
// given blend mode and opacity. Separable/native modes go through QPainter;
// HSL modes are composited manually.
void compositeStage(QImage& target, const QImage& stage, BlendMode mode, float opacity)
{
    if (blend::painterHasNativeMode(mode)) {
        QPainter p(&target);
        p.setOpacity(opacity);
        p.setCompositionMode(blend::painterMode(mode));
        p.drawImage(0, 0, stage);
    } else {
        compositeNonSeparable(target, stage, mode, opacity);
    }
}

// Builds the image-pixel → canvas-pixel transform for a layer node, matching
// the chains used by io/ImageIO and the GPU compositor.
QTransform buildImgToCanvas(const LayerTreeNode* node, const Layer* layer,
                            const QRectF& sourceBounds, int baseW, int baseH,
                            const QTransform& ndcToPixel, bool isShapeSprite)
{
    if (isShapeSprite) {
        // spriteTransform already bakes in accumulatedTransform.
        //   sprite pixel → unit quad → spriteTransform (world NDC) → canvas pixel
        const QTransform& st = layer->shapeCache.spriteTransform;
        QTransform pixToUnit;
        pixToUnit.setMatrix(
            2.0 / baseW, 0.0, 0.0,
            0.0, -2.0 / baseH, 0.0,
            2.0 * sourceBounds.left() / baseW - 1.0,
            1.0 - 2.0 * sourceBounds.top() / baseH,
            1.0);
        return pixToUnit * st * ndcToPixel;
    }
    QTransform imgToNdc;
    imgToNdc.translate(-1.0 + 2.0 * sourceBounds.left() / baseW,
                       1.0 - 2.0 * sourceBounds.top() / baseH);
    imgToNdc.scale(2.0 / baseW, -2.0 / baseH);
    return imgToNdc * node->accumulatedTransform() * ndcToPixel;
}

// Renders a single Layer node into `target` (canvas-sized, premultiplied).
void drawLayerNode(QImage& target, const LayerTreeNode* node, const Document* doc,
                   const QTransform& ndcToPixel)
{
    auto* layer = node->layer.get();
    // Layers with effects OR Single-Layer-Mode adjustments render through the
    // baked effected image (mask included) — same source the GPU live path
    // draws via effectedTexture, so the two stay pixel-identical.
    const bool effectedPipeline = node->usesEffectedPipeline();
    const bool useShapeBase = layer->isShapeLayer() && !effectedPipeline;
    const bool isShapeSprite = !useShapeBase && layer->shapeSpriteRenderable();

    const QSize baseSize = isShapeSprite ? layer->shapeCache.image.size()
                                         : layer->rasterBaseSize();
    const int baseW = baseSize.width();
    const int baseH = baseSize.height();
    if (baseW <= 0 || baseH <= 0) return;

    const QImage source = useShapeBase ? layer->renderImage()
                                       : node->computeEffectedImage();
    const QRectF sourceBounds = useShapeBase ? layer->renderImageBounds()
                                             : node->effectedImageBounds();
    const int lw = source.width();
    const int lh = source.height();
    if (lw <= 0 || lh <= 0) return;

    QImage stage(doc->size, QImage::Format_ARGB32_Premultiplied);
    stage.fill(Qt::transparent);
    QPainter layerPainter(&stage);
    layerPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    layerPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    const QTransform imgToCanvas = buildImgToCanvas(node, layer, sourceBounds,
                                                    baseW, baseH, ndcToPixel, isShapeSprite);
    layerPainter.setOpacity(1.0);
    layerPainter.setTransform(imgToCanvas);

    // Match the GPU preview reference: keep small downscales bilinear, and only
    // switch to premultiplied/alpha-weighted area sampling once the source
    // footprint exceeds the shader's 1.25 texel threshold.
    const double effScaleX = std::hypot(imgToCanvas.m11(), imgToCanvas.m12());
    const double effScaleY = std::hypot(imgToCanvas.m21(), imgToCanvas.m22());
    bool resampled = false;
    if (effScaleX < 0.8 || effScaleY < 0.8) {
        const int sw = std::clamp(
            static_cast<int>(std::lround(lw * std::min(effScaleX, 1.0))), 1, lw);
        const int sh = std::clamp(
            static_cast<int>(std::lround(lh * std::min(effScaleY, 1.0))), 1, lh);
        if (sw < lw || sh < lh) {
            const QImage small = resampleLikeGpu(source, sw, sh);
            if (!small.isNull()) {
                const QTransform smallToCanvas =
                    QTransform::fromScale(static_cast<double>(lw) / sw,
                                          static_cast<double>(lh) / sh) * imgToCanvas;
                layerPainter.setTransform(smallToCanvas);
                layerPainter.drawImage(QRectF(0, 0, sw, sh), small, QRectF(0, 0, sw, sh));
                layerPainter.setTransform(imgToCanvas);
                resampled = true;
            }
        }
    }
    if (!resampled)
        layerPainter.drawImage(QRectF(0, 0, lw, lh), source, QRectF(0, 0, lw, lh));

    // Apply layer mask when the effected pipeline hasn't already baked it.
    if (!effectedPipeline && layer->maskVisible
        && !layer->maskImage.isNull() && layer->maskDensity > 0.0f) {
        const int mw = layer->maskImage.width();
        const int mh = layer->maskImage.height();
        QImage maskAlpha(mw, mh, QImage::Format_ARGB32_Premultiplied);
        maskAlpha.fill(qRgba(255, 255, 255, 255));
        for (int y = 0; y < mh; ++y) {
            uint*        out = reinterpret_cast<uint*>(maskAlpha.scanLine(y));
            const uchar* in  = layer->maskImage.constScanLine(y);
            for (int x = 0; x < mw; ++x) {
                int val = in[x];
                if (layer->maskDensity < 1.0f)
                    val = static_cast<int>(255.0f * layer->maskDensity
                                           + val * (1.0f - layer->maskDensity));
                out[x] = qRgba(255, 255, 255, val);
            }
        }
        const bool isCanvasSized = !layer->isShapeLayer()
            && node->accumulatedTransform().isIdentity()
            && qFuzzyIsNull(sourceBounds.left())
            && qFuzzyIsNull(sourceBounds.top())
            && (mw == doc->size.width() && mh == doc->size.height());
        layerPainter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        if (isCanvasSized) {
            layerPainter.setTransform(QTransform());
            layerPainter.drawImage(0, 0, maskAlpha);
        } else {
            QRectF maskSource;
            if (layer->isShapeLayer()) {
                maskSource = QRectF(0, 0, mw, mh);
            } else {
                maskSource = QRectF(sourceBounds.left() - layer->maskOrigin.x(),
                                    sourceBounds.top() - layer->maskOrigin.y(),
                                    lw, lh);
            }
            layerPainter.drawImage(QRectF(0, 0, lw, lh), maskAlpha, maskSource);
        }
        layerPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    }

    layerPainter.end();

    compositeStage(target, stage, node->blendMode, node->opacity);
}

// Scales every (premultiplied) channel of `stage` by the adjustment factor
// opacity-mask map so a non-Normal-blend adjustment can be composited through
// compositeStage with per-pixel coverage. mix(1, mask, density) mirrors the
// GPU mask semantics.
void modulateAlphaByAdjustmentMask(QImage& stage, const QImage& mask,
                                   const QPoint& maskTopLeft, float maskDensity)
{
    if (mask.isNull())
        return;
    const float density = std::clamp(maskDensity, 0.0f, 1.0f);
    const int w = stage.width();
    const int h = stage.height();
    for (int y = 0; y < h; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(stage.scanLine(y));
        const int my = y - maskTopLeft.y();
        const uchar* maskRow = (my >= 0 && my < mask.height())
            ? mask.constScanLine(my) : nullptr;
        for (int x = 0; x < w; ++x) {
            int m = 255;
            if (maskRow) {
                const int mx = x - maskTopLeft.x();
                if (mx >= 0 && mx < mask.width())
                    m = maskRow[mx];
            }
            const float f = 1.0f + (m / 255.0f - 1.0f) * density;
            if (f >= 0.999f)
                continue;
            const QRgb px = row[x];
            row[x] = qRgba(static_cast<int>(qRed(px) * f),
                           static_cast<int>(qGreen(px) * f),
                           static_cast<int>(qBlue(px) * f),
                           static_cast<int>(qAlpha(px) * f));
        }
    }
}

// Applies a Normal-Mode adjustment node to the already-composited backdrop
// (`target`, canvas-sized premultiplied). The adjustment's mask lives in
// document space (maskOrigin = document pixel of mask (0,0)).
void applyAdjustmentNode(QImage& target, const LayerTreeNode* node)
{
    const Layer* l = node->layer.get();
    QImage mask;
    QPoint maskTopLeft;
    float density = 1.0f;
    if (l && l->maskVisible && !l->maskImage.isNull() && l->maskDensity > 0.0f) {
        mask = l->maskImage;
        maskTopLeft = l->maskOrigin;
        density = l->maskDensity;
    }

    // Solid Color is a fill, not a backdrop transform: it generates a single
    // colour on demand (no permanent full-document bitmap) and composites it
    // source-over the stack with the node's blend mode/opacity/mask — covering
    // transparent areas, exactly like a colour-filled raster layer. The clipped
    // (Single Layer Mode) variant is baked into the parent through
    // adjustments::apply() instead; this branch is the stack path only.
    if (node->adjustment->type == QLatin1String("solidcolor")) {
        const QColor c =
            solidcolor::SolidColorData::fromParams(node->adjustment->params).color;
        QImage stage(target.size(), QImage::Format_ARGB32_Premultiplied);
        stage.fill(qPremultiply(qRgba(c.red(), c.green(), c.blue(), c.alpha())));
        // The mask reveals/hides the fill per pixel (mix(1, mask, density)).
        modulateAlphaByAdjustmentMask(stage, mask, maskTopLeft, density);
        compositeStage(target, stage, node->blendMode, node->opacity);
        return;
    }

    if (node->blendMode == BlendMode::Normal) {
        adjustments::apply(target, *node->adjustment, node->opacity,
                           mask, maskTopLeft, density);
        return;
    }

    // Non-Normal blend: blend the fully-adjusted backdrop over the original
    // with the node's blend mode; the mask modulates the source coverage.
    QImage stage = target.copy();
    adjustments::apply(stage, *node->adjustment, 1.0f);
    modulateAlphaByAdjustmentMask(stage, mask, maskTopLeft, density);
    compositeStage(target, stage, node->blendMode, node->opacity);
}

// Group-mask extension point. Masks currently live on Layer, not on
// LayerTreeNode, so groups have no mask to apply yet (hasGroupMask() == false
// keeps groups off the isolated path for mask reasons alone). When group masks
// are added to the data model, apply the mask to the *composed* group result
// here (destination-in against the canvas-aligned mask alpha) so the order
// stays: compose children -> group mask -> group effects -> opacity/blend.
// Never apply the mask to individual children.
void applyGroupMask(QImage& /*groupStage*/, const LayerTreeNode* /*group*/)
{
    // intentionally empty until LayerTreeNode gains group masks
}

// Applies a group's layer effects to the already-composed (canvas-space) group
// result. Reuses LayerTreeNode::applyEffectStack so layer and group effects run
// through one code path (no duplicated effect ordering). Mirrors
// computeEffectedImage's pre-pad approach so soft effects (shadows/glows) have
// room to grow, then re-aligns the result to the canvas. Effects extending past
// the canvas edge are clipped — a conservative, documented limitation.
void applyGroupEffects(QImage& groupStage, const LayerTreeNode* group,
                       const QSize& canvasSize)
{
    if (group->effects.empty()) return;

    const QMargins pad = LayerEffect::effectPadding(group->effects);
    const QImage src = groupStage.convertToFormat(QImage::Format_RGBA8888);

    QImage padded(src.width() + pad.left() + pad.right(),
                  src.height() + pad.top() + pad.bottom(),
                  QImage::Format_RGBA8888);
    padded.fill(Qt::transparent);
    {
        QPainter p(&padded);
        p.drawImage(pad.left(), pad.top(), src);
    }

    const QImage effected = LayerTreeNode::applyEffectStack(padded, group->effects);

    // `effected`'s (pad.left, pad.top) maps to the group's canvas origin;
    // re-blit at -pad to realign the composed result to the canvas.
    QImage realigned(canvasSize, QImage::Format_ARGB32_Premultiplied);
    realigned.fill(Qt::transparent);
    {
        QPainter p(&realigned);
        p.drawImage(-pad.left(), -pad.top(), effected);
    }
    groupStage = realigned;
}

// Recursively composites a list of sibling nodes (top-to-bottom order in the
// vector) bottom→top into `target`. Mirrors LayerCompositor::renderNodes:
// Normal-blend groups composite their children directly into `target`;
// non-Normal groups are isolated into an intermediate image then composited
// with the group's opacity and blend mode.
void renderNodesFiltered(QImage& target,
                         const std::vector<std::unique_ptr<LayerTreeNode>>& nodes,
                         const Document* doc, const QTransform& ndcToPixel,
                         const std::function<bool(const LayerTreeNode*)>& includeLayer,
                         bool applyAdjustments);

void renderNodes(QImage& target,
                 const std::vector<std::unique_ptr<LayerTreeNode>>& nodes,
                 const Document* doc, const QTransform& ndcToPixel)
{
    renderNodesFiltered(target, nodes, doc, ndcToPixel,
        [](const LayerTreeNode*) { return true; }, true);
}

bool subtreeHasIncludedLayer(const LayerTreeNode* node,
                             const std::function<bool(const LayerTreeNode*)>& includeLayer)
{
    if (!node || !node->visible)
        return false;
    if (node->type == LayerTreeNode::Type::Layer)
        return node->layer && includeLayer(node);
    if (node->type != LayerTreeNode::Type::Group)
        return false;
    for (const auto& child : node->children) {
        if (subtreeHasIncludedLayer(child.get(), includeLayer))
            return true;
    }
    return false;
}

void renderNodesFiltered(QImage& target,
                         const std::vector<std::unique_ptr<LayerTreeNode>>& nodes,
                         const Document* doc, const QTransform& ndcToPixel,
                         const std::function<bool(const LayerTreeNode*)>& includeLayer,
                         bool applyAdjustments)
{
    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
        auto* node = nodes[i].get();
        if (!node->visible) continue;

        // Normal-Mode adjustment: applies non-destructively to everything
        // already composited into `target` (the layers below it in this
        // scope — the document stack at root level, the group's own content
        // inside an isolated group).
        if (node->type == LayerTreeNode::Type::Adjustment) {
            if (applyAdjustments && node->isAdjustmentLayer())
                applyAdjustmentNode(target, node);
            continue;
        }

        if (node->type == LayerTreeNode::Type::Group) {
            if (!subtreeHasIncludedLayer(node, includeLayer))
                continue;
            if (!node->needsIsolatedComposite()) {
                // Fast path: an opacity-1, Normal-blend, effect-free, mask-free
                // group composites identically whether its children are merged
                // straight into the parent or isolated first, so skip the extra
                // buffer. (A future PassThrough group would also take this path
                // by design — see LayerTreeNode::needsIsolatedComposite.)
                renderNodesFiltered(target, node->children, doc, ndcToPixel,
                                    includeLayer, applyAdjustments);
            } else {
                // Isolated group model — the group's mask/effects/opacity/blend
                // apply once to the composed children, in this order:
                //   compose children -> group mask -> group effects -> opacity/blend
                QImage groupStage(doc->size, QImage::Format_ARGB32_Premultiplied);
                groupStage.fill(Qt::transparent);
                renderNodesFiltered(groupStage, node->children, doc, ndcToPixel,
                                    includeLayer, applyAdjustments);
                applyGroupMask(groupStage, node);                 // no-op until group masks exist
                applyGroupEffects(groupStage, node, doc->size);
                auto coverage = [](const QImage& im) {
                    int n = 0;
                    for (int y = 0; y < im.height(); y += 8) {
                        const QRgb* s = reinterpret_cast<const QRgb*>(im.constScanLine(y));
                        for (int x = 0; x < im.width(); x += 8)
                            if (qAlpha(s[x]) > 0) ++n;
                    }
                    return n;
                };
                compositeStage(target, groupStage, node->blendMode, node->opacity);
            }
            continue;
        }

        if (node->type != LayerTreeNode::Type::Layer || !node->layer)
            continue;
        if (!includeLayer(node))
            continue;

        drawLayerNode(target, node, doc, ndcToPixel);
    }
}

QImage compositeFiltered(const Document* doc,
                         const std::function<bool(const LayerTreeNode*)>& includeLayer,
                         bool applyAdjustments)
{
    if (!doc || doc->flatCount() == 0 || doc->size.isEmpty())
        return QImage();

    const double cw = doc->size.width();
    const double ch = doc->size.height();

    QImage result(doc->size, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QTransform ndcToPixel;
    ndcToPixel.translate(cw * 0.5, ch * 0.5);
    ndcToPixel.scale(cw * 0.5, -ch * 0.5);

    renderNodesFiltered(result, doc->roots, doc, ndcToPixel, includeLayer,
                        applyAdjustments);

    return result.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

QImage DocumentCompositor::composite(const Document* doc, const RenderContext& /*ctx*/)
{
    return compositeFiltered(doc, [](const LayerTreeNode*) { return true; }, true);
}

QImage DocumentCompositor::compositeOnlyFlatIndex(const Document* doc, int flatIndex,
                                                  const RenderContext& /*ctx*/)
{
    if (!doc)
        return QImage();
    auto* active = doc->nodeAt(flatIndex);
    if (!active || active->type != LayerTreeNode::Type::Layer)
        return QImage();

    // Clone/heal sampling: exclude Normal-Mode adjustments so painting the
    // sampled pixels back into a layer doesn't bake (and double-apply) them.
    // Single-Layer-Mode adjustments are part of the layer's own render and
    // stay included via computeEffectedImage.
    return compositeFiltered(doc, [active](const LayerTreeNode* node) {
        return node == active;
    }, false);
}

QImage DocumentCompositor::compositeFromFlatIndex(const Document* doc, int flatIndex,
                                                  const RenderContext& /*ctx*/)
{
    if (!doc)
        return QImage();

    const auto flat = doc->flatten();
    std::set<const LayerTreeNode*> included;
    for (int i = std::max(0, flatIndex); i < static_cast<int>(flat.size()); ++i) {
        if (flat[i] && flat[i]->type == LayerTreeNode::Type::Layer)
            included.insert(flat[i]);
    }

    // See compositeOnlyFlatIndex: stack adjustments are excluded from clone
    // sampling sources.
    return compositeFiltered(doc, [included](const LayerTreeNode* node) {
        return included.count(node) > 0;
    }, false);
}

QImage DocumentCompositor::compositeSubset(
    const Document* doc,
    const std::set<const LayerTreeNode*>& includeLayers,
    bool applyAdjustments,
    const RenderContext& /*ctx*/)
{
    if (!doc)
        return QImage();
    return compositeFiltered(doc, [&includeLayers](const LayerTreeNode* node) {
        return includeLayers.count(node) > 0;
    }, applyAdjustments);
}
