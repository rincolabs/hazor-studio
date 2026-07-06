#pragma once

#include "Brush.hpp"
#include "BrushTypes.hpp"   // BrushApplication
#include <QImage>
#include <QColor>
#include <QVector>
#include <algorithm>

// Lightness remap for a predefined (bitmap) tip — brightness/contrast
// adjustment. brightness shifts, contrast scales around `midpoint`. Neutral
// (brightness 0, contrast 0, midpoint 0.5) is the identity, so existing tips are
// untouched until an importer (or the UI) sets non-neutral values.
struct TipRemap {
    float brightness = 0.0f;   // -1..1 additive shift
    float contrast = 0.0f;     // -1..1 (scale around midpoint)
    float midpoint = 0.5f;     // 0..1 pivot for the contrast scale

    bool isNeutral() const {
        return std::abs(brightness) < 1e-4f && std::abs(contrast) < 1e-4f;
    }

    float apply(float l) const {
        if (isNeutral())
            return std::clamp(l, 0.0f, 1.0f);
        const float c = std::clamp(contrast, -0.999f, 0.999f);
        const float factor = (1.0f + c) / (1.0f - c);   // c == 0 → 1 (identity)
        float v = (l - midpoint) * factor + midpoint + brightness;
        return std::clamp(v, 0.0f, 1.0f);
    }
};

// Layer A — predefined bitmap tip.
//
// Wraps a source tip image, scales it to any dab size through a halving scale
// pyramid (cheap, smooth downscaling of large tips), and turns a tip pixel into
// paint through the three brush applications:
//   AlphaMask    — tip alpha is coverage; the ink colour is painted through it.
//   ColorStamp   — the tip's own RGBA is painted (brushes with their own colours).
//   LightnessMap — the ink colour is modulated by the tip's (remapped) lightness.
//
// The per-pixel decision lives in the static applyTip() so the canvas raster path
// (BrushDab::rasterize), the panel preview (which calls rasterize too) and the GPU
// stamp all agree pixel-for-pixel. The pyramid feeds the normalized GPU stamp and
// the polymorphic renderCoverage(); the live raster path samples a caller-scaled
// tip directly and only borrows applyTip().
class PredefinedBrush : public Brush {
public:
    struct Params {
        QImage tip;                                       // source tip (any format)
        BrushApplication application = BrushApplication::AlphaMask;
        TipRemap remap;
    };

    explicit PredefinedBrush(const Params& p);

    // One scaled-tip pixel (RGBA8888, 4 bytes R,G,B,A) → final dab sample given
    // the ink colour. coverage is 0..1 BEFORE flow/opacity; r/g/b are the final
    // dab RGB. Static so the hot canvas loop calls it without owning a brush.
    struct Sample { float coverage; int r, g, b; };
    static Sample applyTip(const uchar* rgba, BrushApplication app,
                           const TipRemap& remap, const QColor& ink);

    // RGBA8888 tip scaled to side x side (IgnoreAspectRatio, to fill the dab box,
    // matching the engine's historical tip scaling). Uses the scale pyramid.
    QImage scaledTip(int side) const;

    // Brush interface
    void renderCoverage(QImage& sprite, int side, float cx, float cy,
                        float radius, const DabShape& shape,
                        const DabContext& ctx) const override;
    bool variesPerDab() const override { return false; }
    QImage staticStamp(int diam) const override;

    const Params& params() const { return m_p; }

private:
    void ensurePyramid() const;

    Params m_p;
    QImage m_source;                     // RGBA8888 (level 0)
    mutable QVector<QImage> m_pyramid;   // each level half the previous size
    mutable bool m_pyramidBuilt = false;
};
