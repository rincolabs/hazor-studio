#include "BrushDab.hpp"
#include "AutoBrush.hpp"
#include "PredefinedBrush.hpp"
#include <algorithm>
#include <cmath>

namespace BrushDab {

QImage rasterize(const DynamicDabParams& params,
                 const BrushSettings& settings,
                 const QColor& inColor,
                 int left, int top, float cx, float cy, int side,
                 const QImage& textureImage,
                 const QImage* selectMask,
                 const QTransform& layerToDoc,
                 const QImage& tipScaledRGBA,
                 QImage* scratch,
                 int texStrokeOffsetX,
                 int texStrokeOffsetY)
{
    if (side <= 0)
        return QImage();

    // Reuse the caller's scratch buffer when it already matches, so a fast stroke
    // does not allocate (and free) a side×side image for every dab. fill() detaches
    // only if a prior return still references it, which the per-dab loop avoids.
    QImage local;
    QImage& dab = scratch ? *scratch : local;
    if (dab.width() != side || dab.height() != side
        || dab.format() != QImage::Format_ARGB32)
        dab = QImage(side, side, QImage::Format_ARGB32);
    dab.fill(Qt::transparent);

    const float radius = std::max(0.5f, params.effectiveSize);
    const float angle = params.effectiveAngle;
    const float ca = std::cos(-angle);
    const float sa = std::sin(-angle);
    const float roundness = std::max(0.05f, params.effectiveRoundness);
    const float fx = params.effectiveFlipX ? -1.0f : 1.0f;
    const float fy = params.effectiveFlipY ? -1.0f : 1.0f;
    QColor color = inColor;
    const float baseAlpha = std::clamp(params.effectiveFlow * params.effectiveOpacity
                                       * color.alphaF(), 0.0f, 1.0f);
    color.setAlphaF(1.0f);

    // Paper-anchored texture modulation (shared with the clone/healing dabs).
    // The pattern is sampled by absolute layer pixel so the grain stays fixed to
    // the "paper" as the brush moves; the random offset is a per-stroke shift
    // supplied by the caller, so successive strokes hit the grain at different
    // origins and build up to cover it.
    const TextureSampler texture(settings.textureConfig, textureImage,
                                 settings.mode == BrushMode::Erase,
                                 texStrokeOffsetX, texStrokeOffsetY);
    const bool useTexture = texture.active;

    // Dual brush: a secondary procedural tip whose falloff multiplies the
    // primary dab's coverage (mirrors the GPU path's GL_DST_COLOR multiply of a
    // smaller centred stamp). rx/ry are already in the rotated, roundness-scaled
    // dab frame, in pixels.
    const auto& dc = settings.dualBrushConfig;
    const bool useDual = dc.enabled && dc.size > 0.0f && settings.size > 0.0f;
    const float dualRadius = useDual
        ? std::max(0.5f, radius * (dc.size / settings.size)) : 1.0f;
    const float dualHardness = std::clamp(dc.hardness, 0.0f, 1.0f);
    const float dualAa = std::min(0.25f, 1.5f / dualRadius);
    auto dualFactor = [&](float rx, float ry) -> float {
        if (!useDual)
            return 1.0f;
        const float d = (dc.tipType == 1)
            ? std::max(std::abs(rx), std::abs(ry)) / dualRadius
            : std::sqrt(rx * rx + ry * ry) / dualRadius;
        return brushFalloff(d, dualHardness, dualAa);
    };

    const bool useSelection = selectMask
        && !selectMask->isNull()
        && selectMask->format() == QImage::Format_Grayscale8;
    // Hoist the layer→document mapping out of the per-pixel path: QTransform::map
    // builds a QPointF and dispatches on the transform's type every call, but this
    // transform is always affine (see layerToDocumentPixelTransform), so mapping a
    // point is just two multiply-adds. Selection only applies on the canvas, never
    // the preview, so this changes no preview output.
    const bool selAffine = useSelection && layerToDoc.isAffine();
    const double selM11 = layerToDoc.m11(), selM12 = layerToDoc.m12();
    const double selM21 = layerToDoc.m21(), selM22 = layerToDoc.m22();
    const double selDx  = layerToDoc.dx(),  selDy  = layerToDoc.dy();
    const int selW = useSelection ? selectMask->width() : 0;
    const int selH = useSelection ? selectMask->height() : 0;
    auto selectionFactor = [&](int spriteX, int spriteY) -> float {
        const double px = static_cast<double>(left + spriteX);
        const double py = static_cast<double>(top + spriteY);
        double docx, docy;
        if (selAffine) {
            docx = selM11 * px + selM21 * py + selDx;
            docy = selM12 * px + selM22 * py + selDy;
        } else {
            const QPointF doc = layerToDoc.map(QPointF(px, py));
            docx = doc.x();
            docy = doc.y();
        }
        const int sx = static_cast<int>(std::floor(docx));
        const int sy = static_cast<int>(std::floor(docy));
        if (sx < 0 || sy < 0 || sx >= selW || sy >= selH)
            return 0.0f;
        return selectMask->constScanLine(sy)[sx] / 255.0f;
    };

    if (settings.tipSource == BrushTipSource::Image && !settings.tipImage.isNull()) {
        // Reuse a caller-cached scaled tip when it matches; otherwise scale once.
        const bool prepared = !tipScaledRGBA.isNull()
            && tipScaledRGBA.width() == side && tipScaledRGBA.height() == side
            && tipScaledRGBA.format() == QImage::Format_RGBA8888;
        const QImage scaledTip = prepared
            ? tipScaledRGBA
            : settings.tipImage.scaled(side, side,
                Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGBA8888);
        // Layer A (PredefinedBrush) owns how a tip pixel becomes paint, so the
        // canvas, the panel preview and the GPU stamp agree: AlphaMask paints the
        // ink through the tip's coverage, ColorStamp keeps the tip colours,
        // LightnessMap modulates the ink by the tip's (remapped) lightness.
        const BrushApplication application = settings.application;
        const TipRemap remap{ settings.tipBrightness, settings.tipContrast,
                              settings.tipMidpoint };
        const float half = side * 0.5f;
        for (int y = 0; y < side; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
            for (int x = 0; x < side; ++x) {
                const float dx0 = (static_cast<float>(x) + 0.5f - cx) * fx;
                const float dy0 = (static_cast<float>(y) + 0.5f - cy) * fy;
                const float rx = dx0 * ca - dy0 * sa;
                const float ry = (dx0 * sa + dy0 * ca) / roundness;
                const int sx = static_cast<int>(rx + half);
                const int sy = static_cast<int>(ry + half);
                if (sx < 0 || sx >= side || sy < 0 || sy >= side)
                    continue;
                // scaledTip is Format_RGBA8888: bytes are R,G,B,A in memory, so
                // read them directly (reinterpreting as QRgb would swap R/B).
                const uchar* tp = scaledTip.constScanLine(sy) + static_cast<qint64>(sx) * 4;
                // Texture/dual/selection modulate the tip *coverage* (the full mask,
                // 0..1), and flow/opacity (baseAlpha) are applied only afterwards —
                // the same order as the engine reference (texture is applied to the
                // full dab mask, opacity/flow at composite time). Doing flow/opacity
                // first would shrink the coverage before the texture's subtract, so a
                // low-flow stroke could never build past the grain and the texture
                // would never fill in on repeated passes.
                float coverage = tp[3] / 255.0f;
                // Skip pixels the tip already left transparent — the dominant case in
                // a dab's bounding box — so the per-feature factors below are only
                // evaluated where the tip actually paints.
                if (coverage <= 0.0f)
                    continue;
                if (useTexture)   coverage = texture.apply(coverage, left + x, top + y);
                if (useDual)      coverage *= dualFactor(rx, ry);
                if (useSelection) coverage *= selectionFactor(x, y);
                float alpha = coverage * baseAlpha;
                if (alpha <= 0.0f)
                    continue;
                const PredefinedBrush::Sample s =
                    PredefinedBrush::applyTip(tp, application, remap, color);
                row[x] = qRgba(s.r, s.g, s.b,
                               static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
            }
        }
    } else {
        // Layer A (AutoBrush) is the procedural mask producer. For a plain round/
        // square hand-made preset it reduces to the engine's historical
        // brushFalloff() (Default profile, isotropic, density 1, no randomness), so
        // the dab is pixel-identical to the old path; the extra knobs (gaussian/
        // soft profile, anisotropic fade, spikes, density) ride on the same seam.
        const AutoBrush brush(autoBrushParamsFromSettings(settings));
        // Seed the per-dab RNG from the dab's layer position so density/randomness
        // produce paper-anchored noise (a different pattern per dab, stable for a
        // given spot). Untouched when density 1 / randomness 0 → parity.
        DabContext ctx;
        ctx.rngState = (static_cast<uint32_t>(left) * 73856093u)
                     ^ (static_cast<uint32_t>(top) * 19349663u) ^ 0x9e3779b9u;

        for (int y = 0; y < side; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
            for (int x = 0; x < side; ++x) {
                const float dx0 = (static_cast<float>(x) + 0.5f - cx) * fx;
                const float dy0 = (static_cast<float>(y) + 0.5f - cy) * fy;
                const float rx = dx0 * ca - dy0 * sa;
                const float ry = (dx0 * sa + dy0 * ca) / roundness;
                // coverage() advances ctx's RNG every pixel (density/randomness),
                // so it must stay unconditional to keep the paper-anchored noise
                // identical; only the modulation factors are gated and deferred.
                // Texture/dual/selection modulate the coverage (the full mask), and
                // flow/opacity (baseAlpha) are applied only afterwards — see the
                // image-tip path above for why the order matters.
                float coverage = brush.coverage(rx, ry, radius, ctx);
                if (coverage <= 0.0f)
                    continue;
                if (useTexture)   coverage = texture.apply(coverage, left + x, top + y);
                if (useDual)      coverage *= dualFactor(rx, ry);
                if (useSelection) coverage *= selectionFactor(x, y);
                float alpha = coverage * baseAlpha;
                if (alpha <= 0.0f)
                    continue;
                row[x] = qRgba(color.red(), color.green(), color.blue(),
                               static_cast<int>(std::clamp(alpha, 0.0f, 1.0f) * 255.0f));
            }
        }
    }

    return dab;
}

} // namespace BrushDab
