#pragma once

#include "Brush.hpp"
#include "BrushTypes.hpp"       // BrushSettings (autoBrushParamsFromSettings)
#include "DynamicsConfig.hpp"   // ResponseCurve (softness curve)
#include <QVector>
#include <algorithm>

// Layer A — procedural mask generator (standard procedural tip family).
//
// Reproduces the auto-brush tip: a circle or rectangle with a falloff edge whose
// profile is Default / Soft / Gaussian, an anisotropic horizontal/vertical fade,
// optional spikes (star/polygon), antialiasing, and stochastic density/randomness.
//
// The Default + isotropic case is pixel-identical to the engine's historical
// brushFalloff() so existing round/square brushes are unchanged. The hot canvas
// loop calls the non-virtual coverage() sampler (no per-pixel virtual dispatch);
// the polymorphic Brush overrides feed the preview and the GPU/mask stamp path.
class AutoBrush : public Brush {
public:
    enum class Shape { Circle = 0, Rectangle = 1 };
    enum class Profile { Default = 0, Soft, Gaussian };

    struct Params {
        Shape shape = Shape::Circle;
        Profile profile = Profile::Default;
        float hardness = 0.8f;       // 0..1; high = sharp edge (legacy "hardness")
        float hFade = -1.0f;         // <0 → derive from hardness (isotropic)
        float vFade = -1.0f;         // <0 → derive from hardness (isotropic)
        int   spikes = 2;            // 2 = none; >2 = star/polygon
        bool  antialias = true;
        float density = 1.0f;        // 0..1 stochastic coverage
        float randomness = 0.0f;     // 0..1 edge noise
        ResponseCurve softnessCurve; // identity = unmodified profile
    };

    explicit AutoBrush(const Params& p);

    // Coverage in 0..1 at a dab-frame pixel (rx, ry already rotated, roundness-
    // scaled and flipped by the caller) for a dab of the given pixel `radius`.
    // ctx supplies the per-dab RNG for density/randomness. This is the parity-
    // critical hot path.
    float coverage(float rx, float ry, float radius, const DabContext& ctx) const;

    // Brush interface
    void renderCoverage(QImage& sprite, int side, float cx, float cy,
                        float radius, const DabShape& shape,
                        const DabContext& ctx) const override;
    bool variesPerDab() const override {
        return m_p.randomness > 0.0f || m_p.density < 1.0f;
    }
    QImage staticStamp(int diam) const override;

    const Params& params() const { return m_p; }

private:
    // Normalized-distance falloff (d in 0..1 → coverage), honoring profile,
    // directional hardness and the softness curve. aa = antialias edge width.
    float falloff1D(float d, float hardness, float aa) const;
    // Directional hardness at angle theta (folds hFade/vFade + spikes).
    float directionalHardness(float theta) const;

    Params m_p;
    float m_baseHardness = 0.8f;  // hardness used when fades are isotropic
    bool m_isotropic = true;      // hFade == vFade and no spikes → radial fast path
};

// Build the AutoBrush parameters for a preset. Single source of truth so the
// canvas dab (BrushDab::rasterize), the preview and the GPU stamp all agree.
// With a default AutoBrushConfig this is the legacy round/square dab.
inline AutoBrush::Params autoBrushParamsFromSettings(const BrushSettings& s)
{
    AutoBrush::Params ap;
    ap.shape = (s.type == BrushType::Square) ? AutoBrush::Shape::Rectangle
                                             : AutoBrush::Shape::Circle;
    ap.profile = static_cast<AutoBrush::Profile>(std::clamp(s.autoBrush.profile, 0, 2));
    ap.hardness = std::clamp(s.hardness, 0.0f, 1.0f);
    ap.hFade = s.autoBrush.hFade;
    ap.vFade = s.autoBrush.vFade;
    ap.spikes = s.autoBrush.spikes;
    ap.antialias = true;
    ap.density = s.autoBrush.density;
    ap.randomness = s.autoBrush.randomness;
    ap.softnessCurve = s.autoBrush.softnessCurve;
    return ap;
}
