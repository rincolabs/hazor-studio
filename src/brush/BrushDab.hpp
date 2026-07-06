#pragma once

#include <QImage>
#include <QColor>
#include <QTransform>
#include "BrushTypes.hpp"
#include "DynamicsConfig.hpp"

// Single source of truth for rasterizing one brush dab sprite on the CPU.
//
// Both the real canvas path (BrushRenderer::drawDabToRasterTiles) and the panel
// preview (BrushPreviewRenderer) call this, so a dab looks pixel-identical in the
// preview and on the canvas — that is the whole point of putting it here. It
// reuses brushFalloff() (BrushTypes.hpp) and applies, in order: tip shape
// (procedural round/square or image tip), angle, roundness, flip, the texture
// modulation, the dual-brush secondary tip, and an optional selection mask.
namespace BrushDab {

// Paper-anchored texture modulation shared by EVERY CPU dab producer (the paint
// dab, the clone stamp and the healing brush), so a textured preset covers the
// same grain no matter which tool paints it. The pattern is sampled by absolute
// layer pixel (grain fixed to the "paper"); the value pipeline is: brightness
// subtracted, contrast pivoting around 0.5, clamp, invert, neutral-point
// piecewise remap, cutoff band, then the Multiply/Subtract coverage op (soft
// scales the source by strength instead of offsetting it). The GPU stamp shader
// mirrors this exact sequence.
struct TextureSampler {
    bool active = false;

    TextureSampler() = default;
    TextureSampler(const TextureConfig& tc, const QImage& texture, bool erase,
                   int strokeOffsetX, int strokeOffsetY)
    {
        active = tc.enabled && !texture.isNull()
            && texture.format() == QImage::Format_Grayscale8
            && tc.scale > 0.0f;
        if (!active)
            return;
        m_image = &texture;
        m_scale = std::max(tc.scale, 0.001f);
        m_depth = std::clamp(tc.depth, 0.0f, 1.0f);
        m_neutral = std::clamp(tc.neutralPoint, 0.0f, 1.0f);
        m_brightness = tc.brightness;
        m_contrast = tc.contrast;
        m_invert = tc.invert ^ (tc.autoInvertForEraser && erase);
        m_cutPattern = tc.cutoffPolicy == TextureConfig::CutoffPolicy::Pattern
            || tc.cutoffPolicy == TextureConfig::CutoffPolicy::Both;
        m_cutLo = std::clamp(tc.cutoffMin, 0.0f, 1.0f);
        m_cutHi = std::clamp(tc.cutoffMax, 0.0f, 1.0f);
        m_offX = tc.horizontalOffset + (tc.randomHorizontalOffset ? strokeOffsetX : 0);
        m_offY = tc.verticalOffset + (tc.randomVerticalOffset ? strokeOffsetY : 0);
        m_subtract = tc.texturingMode == TextureConfig::TexturingMode::Subtract;
        m_soft = tc.softTexturing;
    }

    // Coverage after the pattern at layer pixel (lx, ly). No-op when inactive.
    float apply(float coverage, int lx, int ly) const {
        if (!active)
            return coverage;
        const int tw = m_image->width();
        const int th = m_image->height();
        int tx = (static_cast<int>(std::floor(lx / m_scale)) + m_offX) % tw;
        int ty = (static_cast<int>(std::floor(ly / m_scale)) + m_offY) % th;
        if (tx < 0) tx += tw;
        if (ty < 0) ty += th;
        float m = m_image->constScanLine(ty)[tx] / 255.0f;
        m = m - m_brightness;
        m = (m - 0.5f) * m_contrast + 0.5f;
        m = std::clamp(m, 0.0f, 1.0f);
        if (m_invert)
            m = 1.0f - m;
        // Neutral-point remap: two linear segments (0..neutral → 0..0.5,
        // neutral..1 → 0.5..1) so the chosen grey maps to 0.5 without clipping.
        const float np = m_neutral;
        float nv;
        if (np >= 1.0f || (np > 0.0f && m <= np))
            nv = m / (2.0f * np);
        else
            nv = 0.5f + (m - np) / (2.0f - 2.0f * np);
        if (m_cutPattern && (nv < m_cutLo || nv > m_cutHi))
            nv = 0.0f;
        nv = std::clamp(nv, 0.0f, 1.0f);
        const float s = m_depth;
        if (m_subtract) {
            return m_soft ? std::max(0.0f, coverage - nv * s)
                          : std::max(0.0f, coverage - (nv + (1.0f - s)));
        }
        return m_soft ? coverage * (nv * s + (1.0f - s))
                      : coverage * nv * s;
    }

private:
    const QImage* m_image = nullptr;
    float m_scale = 1.0f, m_depth = 1.0f, m_neutral = 0.5f;
    float m_brightness = 0.0f, m_contrast = 1.0f;
    float m_cutLo = 0.0f, m_cutHi = 1.0f;
    int m_offX = 0, m_offY = 0;
    bool m_invert = false, m_cutPattern = false, m_subtract = false, m_soft = false;
};

// Produce an ARGB32 sprite of size (side x side), with the dab centred at
// (cx, cy) inside it. `color` is the solid dab colour (effectiveColor on the
// canvas; a theme ink or effectiveColor in the preview). `textureImage` is the
// grayscale-8 texture (or null). `selectMask` + `layerToDoc` + `left`/`top`
// apply a selection mask in document space (pass selectMask = nullptr to skip).
// `tipScaledRGBA`, if non-null and already sized side×side (Format_RGBA8888),
// is used as the image tip directly — callers pass a cached scaled tip so a
// stroke does not re-downscale a large source image on every dab. When empty (or
// the wrong size) the source tip is scaled internally.
//
// `scratch`, if non-null, is reused as the dab buffer across calls so a fast
// stroke does not allocate a fresh side×side QImage for every dab — the hot
// canvas path passes a member buffer here. The returned QImage shares it (COW);
// the caller must finish using the result before the next rasterize() call,
// which the per-dab tile loop already does.
QImage rasterize(const DynamicDabParams& params,
                 const BrushSettings& settings,
                 const QColor& color,
                 int left, int top, float cx, float cy, int side,
                 const QImage& textureImage,
                 const QImage* selectMask = nullptr,
                 const QTransform& layerToDoc = QTransform(),
                 const QImage& tipScaledRGBA = QImage(),
                 QImage* scratch = nullptr,
                 int texStrokeOffsetX = 0,
                 int texStrokeOffsetY = 0);

} // namespace BrushDab
