#pragma once

#include "core/Layer.hpp" // BlendMode
#include <algorithm>
#include <array>
#include <cmath>

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

// Non-separable (HSL) blend modes — W3C compositing-1.
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

// Layer-mask density ramp: density 1 applies the mask as-is, density 0
// disables it (mask reads as fully white). mix(1, m, density) — identical to
// the `mix(1.0, maskAlpha, uMaskDensity)` in every GPU shader.
inline float maskDensityRamp(float maskValue, float density)
{
    return 1.0f + (maskValue - 1.0f) * density;
}

} // namespace blendmath
