#pragma once

#include "core/Layer.hpp" // BlendMode
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────────────
// Shared blend/composite math — the CPU half of the render specification.
//
// Together with renderer/BlendShaderLib.hpp (the GLSL half, function-for-
// function identical) and renderer/BlendRules.hpp (mode → backend mapping),
// this header is the single place where compositing formulas live:
//
//   - BlendRules.hpp      which backend primitive implements each BlendMode
//   - BlendMath.hpp       the formulas, C++ (CPU compositor / projection)
//   - BlendShaderLib.hpp  the formulas, GLSL (live GPU preview)
//
// Any change here MUST be mirrored in BlendShaderLib.hpp, and vice versa —
// the two files are kept line-comparable on purpose. The separable modes on
// the CPU are executed by QPainter (see blend::painterMode); the GLSL side
// therefore matches Qt's raster engine semantics, including the
// ColorDodge/ColorBurn boundary cases (see bm_dodge/bm_burn there).
// ─────────────────────────────────────────────────────────────────────────
namespace blendmath {

using Vec3 = std::array<float, 3>;

// Rec.601 luma, the same weights the GLSL lib and QPainter use.
inline float lum(const Vec3& c)
{
    return 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
}

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

// W3C setSat, vectorized: min channel → 0, max channel → s, mid scaled.
inline Vec3 setSat(Vec3 c, float s)
{
    const float mn = std::min({c[0], c[1], c[2]});
    const float mx = std::max({c[0], c[1], c[2]});
    if (mn >= mx) return {0.0f, 0.0f, 0.0f};
    for (float& v : c) v = (v - mn) * s / (mx - mn);
    return c;
}

// ── Separable per-channel modes (b = backdrop, s = source) ───────────────
// Channels are the document's own gamma-encoded values, which is what these
// formulas are defined on. Dodge/burn carry the raster-engine boundary
// semantics (see the header comment); the light modes are expressed through
// them so Vivid Light and ColorDodge/ColorBurn agree on those boundaries.

inline float dodge(float b, float s)
{
    return (b + s >= 1.0f) ? 1.0f : std::min(1.0f, b / (1.0f - s));
}

inline float burn(float b, float s)
{
    return (s <= 0.0f || b + s <= 1.0f) ? 0.0f
                                        : 1.0f - std::min(1.0f, (1.0f - b) / s);
}

// Burn on the lower half of the source range, dodge on the upper half, each
// driven at twice the distance from mid grey.
inline float vividLight(float b, float s)
{
    return (s <= 0.5f) ? burn(b, 2.0f * s) : dodge(b, 2.0f * s - 1.0f);
}

// Replaces the backdrop only where the doubled source overshoots it.
inline float pinLight(float b, float s)
{
    return (s > 0.5f) ? std::max(b, 2.0f * s - 1.0f) : std::min(b, 2.0f * s);
}

// b / s, saturating at white. A black source divides to white for every
// backdrop, including black: the ratio is undefined there and the surrounding
// limit is white, which is also how dodge resolves its own 0/0 corner.
inline float divide(float b, float s)
{
    if (s <= 0.0f) return 1.0f;
    return std::min(1.0f, b / s);
}

// Blend math for every mode the CPU composites per pixel: the non-separable
// HSL modes (W3C compositing-1) and the separable modes QPainter has no
// CompositionMode for. The natively-supported modes never reach here — they
// run through the raster engine (blend::painterHasNativeMode). Clamped where
// the GLSL mirror clamps, so both paths round the same values.
inline Vec3 blend(BlendMode mode, const Vec3& cb, const Vec3& cs)
{
    const auto perChannel = [&](auto f) -> Vec3 {
        return {f(cb[0], cs[0]), f(cb[1], cs[1]), f(cb[2], cs[2])};
    };
    Vec3 r;
    switch (mode) {
    case BlendMode::Hue:        r = setLum(setSat(cs, sat(cb)), lum(cb)); break;
    case BlendMode::Saturation: r = setLum(setSat(cb, sat(cs)), lum(cb)); break;
    case BlendMode::Color:      r = setLum(cs, lum(cb)); break;
    case BlendMode::Luminosity: r = setLum(cb, lum(cs)); break;
    case BlendMode::LinearBurn:
        r = perChannel([](float b, float s) { return b + s - 1.0f; });
        break;
    case BlendMode::LinearDodge:
        r = perChannel([](float b, float s) { return b + s; });
        break;
    case BlendMode::VividLight:  r = perChannel(vividLight); break;
    case BlendMode::LinearLight:
        r = perChannel([](float b, float s) { return b + 2.0f * s - 1.0f; });
        break;
    case BlendMode::PinLight:    r = perChannel(pinLight); break;
    case BlendMode::HardMix:
        // The sum thresholded at white: every channel ends up 0 or 1.
        r = perChannel([](float b, float s) { return (b + s < 1.0f) ? 0.0f : 1.0f; });
        break;
    case BlendMode::DarkerColor:
        // Whole-colour choice by luma, not per channel — no new hues.
        r = (lum(cs) < lum(cb)) ? cs : cb;
        break;
    case BlendMode::LighterColor:
        r = (lum(cs) > lum(cb)) ? cs : cb;
        break;
    case BlendMode::Subtract:
        r = perChannel([](float b, float s) { return b - s; });
        break;
    case BlendMode::Divide:      r = perChannel(divide); break;
    default:                     r = cs; break; // Dissolve (see coverage), Normal
    }
    for (float& v : r) v = std::clamp(v, 0.0f, 1.0f);
    return r;
}

// ── Dissolve ────────────────────────────────────────────────────────────
// Deterministic hash of a document pixel → [0,1). Integer math on purpose:
// a float hash (fract(sin(...))) is not reproducible between the CPU and the
// GPU, and the two paths must dither the *same* pixels or the pattern shifts
// when a live frame is replaced by the projection.
inline float ditherNoise(int x, int y)
{
    uint32_t h = static_cast<uint32_t>(x) * 73856093u
               ^ static_cast<uint32_t>(y) * 19349663u;
    h ^= h >> 13;
    h *= 2654435761u;
    h ^= h >> 16;
    return static_cast<float>(h & 0xFFFFFFu) * (1.0f / 16777216.0f);
}

// Dissolve spends coverage as probability instead of translucency: the source
// wins whole pixels where the threshold passes and is absent elsewhere, so
// opacity, mask and soft edges turn into a dither. Every other mode keeps its
// coverage untouched.
inline float coverage(BlendMode mode, float alpha, int x, int y)
{
    if (mode != BlendMode::Dissolve) return alpha;
    return (ditherNoise(x, y) < alpha) ? 1.0f : 0.0f;
}

// Layer-mask density ramp: density 1 applies the mask as-is, density 0
// disables it (mask reads as fully white). mix(1, m, density) — identical to
// the `mix(1.0, maskAlpha, uMaskDensity)` in every GPU shader.
inline float maskDensityRamp(float maskValue, float density)
{
    return 1.0f + (maskValue - 1.0f) * density;
}

} // namespace blendmath
