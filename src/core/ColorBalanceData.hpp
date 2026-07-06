#pragma once

#include <QString>
#include <QVariantMap>
#include <array>

// Reusable model for a Color Balance adjustment. Holds the per-tone-range
// channel-axis shifts (Shadows / Midtones / Highlights × Cyan-Red /
// Magenta-Green / Yellow-Blue) plus the Preserve Luminosity flag, and builds
// the per-channel 256-entry transfer LUTs that both the CPU adjustment path
// (core/AdjustmentTypes.cpp) and the GPU adjustment shader (via the shared LUT
// texture) consume — so the live preview, the cached projection and the export
// stay in sync.
//
// Like CurvesData, the model is deliberately decoupled from any widget:
// ColorBalanceAdjustmentWidget edits it, AdjustmentTypes applies it, and the
// LayerCompositor uploads its transfer LUTs.
namespace colorbalance {

// Tonal range a slider group targets. Persisted order matters for encode().
enum class Tone { Shadows = 0, Midtones = 1, Highlights = 2 };
// Colour axis. Negative favours the left label (Cyan/Magenta/Yellow), positive
// the right (Red/Green/Blue) — i.e. axis order matches the RGB channels.
enum class Axis { CyanRed = 0, MagentaGreen = 1, YellowBlue = 2 };

constexpr int kToneCount = 3;
constexpr int kAxisCount = 3;

struct ColorBalanceData {
    // [tone][axis] sliders, each clamped to [-100, +100]; 0 is neutral.
    std::array<std::array<int, kAxisCount>, kToneCount> values{};
    bool preserveLuminosity = true;

    ColorBalanceData() { reset(); }

    int value(Tone t, Axis a) const {
        return values[static_cast<int>(t)][static_cast<int>(a)];
    }
    void setValue(Tone t, Axis a, int v);

    // All sliders zero (preserveLuminosity is irrelevant to the result then).
    bool isIdentity() const;
    // Reset every slider in one tone (or all tones) back to 0.
    void resetTone(Tone t);
    void reset();

    // Per-channel 256-entry transfer LUTs: out[v] = clamp(v + shift(v)). The R
    // LUT carries the Cyan-Red axis, G the Magenta-Green, B the Yellow-Blue.
    // Preserve Luminosity is applied separately (it couples the channels), by
    // both the CPU apply() and the GPU shader, on top of these LUTs.
    void buildTransferLuts(std::array<unsigned char, 256>& rLut,
                           std::array<unsigned char, 256>& gLut,
                           std::array<unsigned char, 256>& bLut) const;

    // Compact, round-trippable string ("sh:cr,mg,yb;mi:...;hi:..."). Stored as a
    // single QVariant string so it survives the scalar-only adjustmentParams
    // serializer; preserveLuminosity rides as a separate bool param.
    QString encode() const;
    static ColorBalanceData decode(const QString& s);

    // Bridges to/from AdjustmentData::params. params["colorbalance"] holds
    // encode(), params["preserveLuminosity"] holds the bool.
    QVariantMap toParams() const;
    static ColorBalanceData fromParams(const QVariantMap& params);
};

} // namespace colorbalance
