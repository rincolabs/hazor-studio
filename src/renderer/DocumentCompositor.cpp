#include "DocumentCompositor.hpp"
#include "RenderContext.hpp"
#include "BlendMath.hpp"
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
#include <vector>

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

// Non-separable (HSL) blend math comes from renderer/BlendMath.hpp — the
// shared CPU/GPU specification (GLSL mirror: renderer/BlendShaderLib.hpp).
using blendmath::Vec3;

inline uchar toByte(float v)
{
    return static_cast<uchar>(std::clamp(static_cast<int>(std::lround(v * 255.0f)), 0, 255));
}

// ── Resampling (CPU half of the sampling specification) ──────────────────
//
// GLSL mirror: sm_boxWeights / sm_sampleBox in renderer/SamplingShaderLib.hpp.
// One exact box filter over premultiplied samples, so a layer that the GPU
// live preview minifies and the CPU bakes into the document land on the same
// pixels (docs/RENDER-PIPELINE.md §4.2).

// One output sample's worth of box weights for a `width`-texel box centred at
// `p`, in texel-centre coordinates (texel i covers [i-0.5, i+0.5]): the weight
// of texel i is its overlap with [p-width/2, p+width/2] over `width`. Same
// algebra as sm_boxWeights in SamplingShaderLib.hpp, but evaluated per sample
// rather than tabulated per axis index — under a rotation the two axes are not
// separable, so there is no row/column to tabulate against.
struct BoxTaps {
    int first = 0;
    int n     = 1;
};

BoxTaps boxWeights(double p, double width, double inv, float* w, int maxTaps)
{
    BoxTaps t;
    const double lo = p - width * 0.5;
    const double hi = p + width * 0.5;
    t.first = static_cast<int>(std::floor(lo + 0.5));
    // The caller sizes the buffer at ceil(width)+2, which is one slot more
    // than this can ask for; the clamp is there so a float edge case cannot
    // turn into an overrun.
    t.n = std::clamp(static_cast<int>(std::floor(hi + 0.5)) - t.first + 1,
                     1, maxTaps);
    for (int i = 0; i < t.n; ++i) {
        const double c = t.first + i;
        w[i] = static_cast<float>(
            std::max(std::min(hi, c + 0.5) - std::max(lo, c - 0.5), 0.0) * inv);
    }
    return t;
}

// Single-pass affine resample of `source` into a canvas-sized premultiplied
// image: for every document pixel, inverse-map its centre into source texels
// and evaluate the box there.
//
// This exists to remove a *second* resample. The minifying path used to box
// the source down to a rounded integer size and then let QPainter place that
// result, which resampled it again: `lround` leaves a residual scale, the
// destination offset is generally fractional, and QPainter's bilinear tent
// therefore ran over pixels that were already correctly filtered — at a
// half-pixel offset that is a plain two-texel average, i.e. blur with no
// filtering purpose. Evaluating the box once at the exact inverse-mapped
// position skips it.
//
// Mirror of sm_sampleBox (SamplingShaderLib.hpp) with two deliberate
// differences, both documented in docs/RENDER-PIPELINE.md §4.2:
//
//  - The tap count is not capped. The shader's SM_MAX_TAPS budget is a GPU
//    constraint; this is the path that writes the export, so a 20× downscale
//    stays exact instead of degrading to a narrower centred box.
//  - The box width comes from the per-axis *scale* (the caller's `width`), not
//    from GLSL's `fwidth()`. fwidth sums |∂u/∂x| + |∂u/∂y|, so it widens the
//    box to 1.41 texels for a 45° rotation at scale 1 — prefiltering a layer
//    that needs none. Rotation is left at width 1, which is also what the
//    QPainter path it replaces did.
//
// Coverage is decided on the destination pixel centre falling inside the
// source rect, matching the GPU rasterising the layer quad; taps that fall
// outside clamp to the edge texel, which re-homes their weight rather than
// dropping it so the weights still sum to 1 at the border.
QImage resampleAffine(const QImage& source, const QTransform& imgToCanvas,
                      const QSize& docSize, double widthX, double widthY)
{
    bool invertible = false;
    const QTransform inv = imgToCanvas.inverted(&invertible);
    // isAffine(): the row walk below steps the inverse-mapped position
    // linearly, which only holds without a projective part. A projective
    // transform falls back to the QPainter path.
    if (!invertible || !inv.isAffine() || source.isNull() || docSize.isEmpty())
        return QImage();

    const QImage rgba = source.convertToFormat(QImage::Format_RGBA8888);
    const int inW = rgba.width();
    const int inH = rgba.height();
    if (inW <= 0 || inH <= 0)
        return QImage();

    QImage out(docSize, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);

    // Only the layer's own footprint is touched; the rest stays transparent.
    const QRect dest = imgToCanvas.mapRect(QRectF(0, 0, inW, inH))
                           .toAlignedRect()
                           .intersected(QRect(QPoint(0, 0), docSize));
    if (dest.isEmpty())
        return out;

    // Affine ⇒ constant Jacobian, so the tap budget is the same for every
    // sample and can be sized once.
    const int maxX = static_cast<int>(std::ceil(widthX)) + 2;
    const int maxY = static_cast<int>(std::ceil(widthY)) + 2;
    std::vector<float> wx(maxX), wy(maxY);
    const double invWx = 1.0 / widthX;
    const double invWy = 1.0 / widthY;

    // Without rotation or shear the two axes separate again: the x taps are the
    // same on every row and the y taps are constant along a row. Tabulating
    // them matters — recomputing weights per sample costs ~20% of the inner
    // loop, which would make this slower than the two-pass path it replaces for
    // the very common near-1.0 scales. The table is filled by the same
    // boxWeights(), so the kernel still has exactly one definition.
    const bool separable = inv.m12() == 0.0 && inv.m21() == 0.0;
    const int  destW     = dest.width();
    std::vector<int>   colFirst;
    std::vector<int>   colN;
    std::vector<float> colW;
    if (separable) {
        colFirst.resize(destW);
        colN.resize(destW);
        colW.assign(static_cast<size_t>(destW) * maxX, 0.0f);
        for (int c = 0; c < destW; ++c) {
            const double px = inv.m11() * (dest.left() + c + 0.5) + inv.m31();
            const BoxTaps t = boxWeights(px - 0.5, widthX, invWx,
                                         colW.data() + static_cast<size_t>(c) * maxX,
                                         maxX);
            colFirst[c] = t.first;
            colN[c]     = t.n;
        }
    }

    for (int y = dest.top(); y <= dest.bottom(); ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(out.scanLine(y));
        // Walk the row incrementally rather than calling inv.map() per pixel.
        const double y0 = y + 0.5;
        double sx = inv.m11() * (dest.left() + 0.5) + inv.m21() * y0 + inv.m31();
        double sy = inv.m12() * (dest.left() + 0.5) + inv.m22() * y0 + inv.m32();
        BoxTaps tyRow;
        if (separable)
            tyRow = boxWeights(sy - 0.5, widthY, invWy, wy.data(), maxY);
        for (int x = dest.left(); x <= dest.right(); ++x,
             sx += inv.m11(), sy += inv.m12()) {
            if (sx < 0.0 || sx > inW || sy < 0.0 || sy > inH)
                continue;                       // outside the layer quad

            BoxTaps      tx;
            BoxTaps      ty = tyRow;            // already correct when separable
            const float* wxp;
            if (separable) {
                const int c = x - dest.left();
                tx.first = colFirst[c];
                tx.n     = colN[c];
                wxp      = colW.data() + static_cast<size_t>(c) * maxX;
            } else {
                tx  = boxWeights(sx - 0.5, widthX, invWx, wx.data(), maxX);
                ty  = boxWeights(sy - 0.5, widthY, invWy, wy.data(), maxY);
                wxp = wx.data();
            }

            float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.0f;
            for (int j = 0; j < ty.n; ++j) {
                const int    py   = std::clamp(ty.first + j, 0, inH - 1);
                const uchar* line = rgba.constScanLine(py);
                float rr = 0.0f, gg = 0.0f, bb = 0.0f, aa = 0.0f;
                for (int i = 0; i < tx.n; ++i) {
                    const int    px = std::clamp(tx.first + i, 0, inW - 1);
                    const uchar* s  = line + px * 4;
                    const float  av = s[3] * (1.0f / 255.0f);
                    const float  wi = wxp[i] * (1.0f / 255.0f);
                    rr += wi * s[0] * av;
                    gg += wi * s[1] * av;
                    bb += wi * s[2] * av;
                    aa += wxp[i] * av;
                }
                r += wy[j] * rr;
                g += wy[j] * gg;
                b += wy[j] * bb;
                a += wy[j] * aa;
            }

            // ARGB32_Premultiplied: keep the premultiplied values, which is
            // also one fewer round trip than the QPainter path (that one had
            // to unpremultiply the boxed intermediate and premultiply again).
            // min(c, a) only guards the last LSB — the box weights are
            // non-negative and sum to 1, so colour can only exceed alpha by
            // rounding, and a premultiplied pixel that violates it would make
            // Qt's compositing produce a speck.
            const uchar av8 = toByte(a);
            row[x] = qRgba(std::min(toByte(r), av8), std::min(toByte(g), av8),
                           std::min(toByte(b), av8), av8);
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

            const Vec3 B = blendmath::blendHsl(mode, cb, cs);
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

    const QTransform imgToCanvas = buildImgToCanvas(node, layer, sourceBounds,
                                                    baseW, baseH, ndcToPixel, isShapeSprite);

    // Any minification goes through the premultiplied box filter, mirroring the
    // shader (renderer/SamplingShaderLib.hpp) — which covers the whole range
    // because the exact box degenerates to plain bilinear at footprint 1, so
    // there is no band left to subsampled bilinear.
    //
    // It runs as a *single* pass straight into the stage: box prefilter and
    // placement at once, so the fractional destination offset no longer costs a
    // second (bilinear) resample on top of a correctly filtered image. See
    // resampleAffine. QPainter keeps the magnifying case, where it is already
    // one pass and is the same bilinear the box degenerates to.
    const double effScaleX = std::hypot(imgToCanvas.m11(), imgToCanvas.m12());
    const double effScaleY = std::hypot(imgToCanvas.m21(), imgToCanvas.m22());
    QImage stage;
    if (effScaleX > 1e-9 && effScaleY > 1e-9
        && (effScaleX < 1.0 || effScaleY < 1.0)) {
        stage = resampleAffine(source, imgToCanvas, doc->size,
                               std::max(1.0 / effScaleX, 1.0),
                               std::max(1.0 / effScaleY, 1.0));
    }
    if (stage.isNull()) {
        stage = QImage(doc->size, QImage::Format_ARGB32_Premultiplied);
        stage.fill(Qt::transparent);
        QPainter p(&stage);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        p.setTransform(imgToCanvas);
        p.drawImage(QRectF(0, 0, lw, lh), source, QRectF(0, 0, lw, lh));
    }

    QPainter layerPainter(&stage);
    layerPainter.setRenderHint(QPainter::SmoothPixmapTransform);
    layerPainter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    layerPainter.setOpacity(1.0);
    layerPainter.setTransform(imgToCanvas);

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
                if (layer->maskDensity < 1.0f) {
                    // mix(1, mask, density) — same ramp as the GPU shaders
                    // (mix(1.0, maskAlpha, uMaskDensity)): density 1 applies the
                    // mask as-is, density 0 turns it off.
                    val = static_cast<int>(std::lround(
                        255.0f * blendmath::maskDensityRamp(val / 255.0f,
                                                            layer->maskDensity)));
                }
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
            const float f = blendmath::maskDensityRamp(m / 255.0f, density);
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
                         bool applyAdjustments,
                         bool groupsPassThrough);

void renderNodes(QImage& target,
                 const std::vector<std::unique_ptr<LayerTreeNode>>& nodes,
                 const Document* doc, const QTransform& ndcToPixel)
{
    renderNodesFiltered(target, nodes, doc, ndcToPixel,
        [](const LayerTreeNode*) { return true; }, true,
        /*groupsPassThrough=*/false);
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
                         bool applyAdjustments,
                         bool groupsPassThrough)
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
            if (groupsPassThrough || !node->needsIsolatedComposite()) {
                // Fast path: an opacity-1, Normal-blend, effect-free, mask-free
                // group composites identically whether its children are merged
                // straight into the parent or isolated first, so skip the extra
                // buffer. (A future PassThrough group would also take this path
                // by design — see LayerTreeNode::needsIsolatedComposite.)
                //
                // groupsPassThrough forces this path for merge subsets whose
                // result stays inside the group: its opacity/blend/effects
                // remain live on the tree, so baking them here would apply
                // them twice (see DocumentCompositor::compositeSubset).
                renderNodesFiltered(target, node->children, doc, ndcToPixel,
                                    includeLayer, applyAdjustments,
                                    groupsPassThrough);
            } else {
                // Isolated group model — the group's mask/effects/opacity/blend
                // apply once to the composed children, in this order:
                //   compose children -> group mask -> group effects -> opacity/blend
                QImage groupStage(doc->size, QImage::Format_ARGB32_Premultiplied);
                groupStage.fill(Qt::transparent);
                renderNodesFiltered(groupStage, node->children, doc, ndcToPixel,
                                    includeLayer, applyAdjustments,
                                    groupsPassThrough);
                applyGroupMask(groupStage, node);                 // no-op until group masks exist
                applyGroupEffects(groupStage, node, doc->size);
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
                         bool applyAdjustments,
                         bool groupsPassThrough = false)
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
                        applyAdjustments, groupsPassThrough);

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
    const RenderContext& /*ctx*/,
    bool ancestorGroupsPassThrough)
{
    if (!doc)
        return QImage();
    return compositeFiltered(doc, [&includeLayers](const LayerTreeNode* node) {
        return includeLayers.count(node) > 0;
    }, applyAdjustments, ancestorGroupsPassThrough);
}
