#pragma once

// ─────────────────────────────────────────────────────────────────────────
// Shared GLSL blend library — the GPU half of the render specification.
//
// Function-for-function mirror of renderer/BlendMath.hpp (the C++ half used
// by the CPU compositor). Injected into every fragment shader that blends
// (the blend shader and the adjustment shader's Solid Color branch), so the
// GPU has exactly ONE copy of each formula. Mode ids are blend::shaderId
// (renderer/BlendRules.hpp); anything unmapped (incl. -1 = Normal) falls
// through to the source colour.
//
// The separable modes on the CPU run through QPainter, so bm_dodge/bm_burn
// implement Qt's raster-engine boundary semantics (which also avoid the
// 0/0 → NaN of the naive W3C expressions):
//   dodge: Cb + Cs >= 1 → 1, else Cb / (1 - Cs)
//   burn:  Cs == 0 or Cb + Cs <= 1 → 0, else 1 - (1 - Cb) / Cs
// ─────────────────────────────────────────────────────────────────────────
namespace blend_glsl {

inline constexpr char kLib[] = R"(
    // ── bm_* : shared blend math (mirror of renderer/BlendMath.hpp) ──
    float bm_lum(vec3 c) { return 0.299*c.r + 0.587*c.g + 0.114*c.b; }
    vec3 bm_clipColor(vec3 c) {
        float l = bm_lum(c);
        float n = min(min(c.r, c.g), c.b);
        float x = max(max(c.r, c.g), c.b);
        if (n < 0.0)
            c = l + (c - l) * l / (l - n);
        if (x > 1.0)
            c = l + (c - l) * (1.0 - l) / (x - l);
        return c;
    }
    vec3 bm_setLum(vec3 c, float l) {
        return bm_clipColor(c + (l - bm_lum(c)));
    }
    float bm_sat(vec3 c) {
        return max(max(c.r, c.g), c.b) - min(min(c.r, c.g), c.b);
    }
    // W3C setSat, vectorized: min channel → 0, max channel → s, mid scaled.
    vec3 bm_setSat(vec3 c, float s) {
        float mn = min(min(c.r, c.g), c.b);
        float mx = max(max(c.r, c.g), c.b);
        if (mn >= mx) return vec3(0.0);
        return (c - mn) * s / (mx - mn);
    }
    float bm_dodge(float b, float s) {
        return (b + s >= 1.0) ? 1.0 : min(1.0, b / (1.0 - s));
    }
    float bm_burn(float b, float s) {
        return (s <= 0.0 || b + s <= 1.0) ? 0.0 : 1.0 - min(1.0, (1.0 - b) / s);
    }
    float bm_hardLight(float b, float s) {
        return (s < 0.5) ? 2.0*s*b : 1.0 - 2.0*(1.0 - s)*(1.0 - b);
    }
    float bm_softLight(float b, float s) {
        if (s < 0.5) return b - (1.0 - 2.0*s) * b * (1.0 - b);
        float d = (b < 0.25) ? ((16.0*b - 12.0)*b + 4.0)*b : sqrt(b);
        return b + (2.0*s - 1.0) * (d - b);
    }
    // cb = backdrop, cs = source (both straight, 0..1). Ids from
    // renderer/BlendRules.hpp (blend::shaderId).
    vec3 bm_blend(int mode, vec3 cb, vec3 cs) {
        if (mode == 0)  return vec3(bm_hardLight(cs.r, cb.r),   // Overlay =
                                    bm_hardLight(cs.g, cb.g),   // HardLight
                                    bm_hardLight(cs.b, cb.b));  // swapped
        if (mode == 1)  return vec3(bm_hardLight(cb.r, cs.r),
                                    bm_hardLight(cb.g, cs.g),
                                    bm_hardLight(cb.b, cs.b));
        if (mode == 2)  return vec3(bm_softLight(cb.r, cs.r),
                                    bm_softLight(cb.g, cs.g),
                                    bm_softLight(cb.b, cs.b));
        if (mode == 3)  return vec3(bm_dodge(cb.r, cs.r),
                                    bm_dodge(cb.g, cs.g),
                                    bm_dodge(cb.b, cs.b));
        if (mode == 4)  return vec3(bm_burn(cb.r, cs.r),
                                    bm_burn(cb.g, cs.g),
                                    bm_burn(cb.b, cs.b));
        if (mode == 5)  return abs(cb - cs);                       // Difference
        if (mode == 6)  return cb + cs - 2.0*cb*cs;                // Exclusion
        if (mode == 7)  return bm_setLum(bm_setSat(cs, bm_sat(cb)), bm_lum(cb)); // Hue
        if (mode == 8)  return bm_setLum(bm_setSat(cb, bm_sat(cs)), bm_lum(cb)); // Saturation
        if (mode == 9)  return bm_setLum(cs, bm_lum(cb));          // Color
        if (mode == 10) return bm_setLum(cb, bm_lum(cs));          // Luminosity
        if (mode == 11) return cs * cb;                            // Multiply
        if (mode == 12) return cs + cb - cs*cb;                    // Screen
        if (mode == 13) return min(cs, cb);                        // Darken
        if (mode == 14) return max(cs, cb);                        // Lighten
        return cs;                                                 // Normal / unmapped
    }
)";

} // namespace blend_glsl
