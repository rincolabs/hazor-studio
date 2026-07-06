#pragma once

#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <array>
#include <vector>

// Reusable model for a Hue/Saturation adjustment. Holds the per-range
// Hue/Saturation/Lightness shifts (Master + Reds/Yellows/Greens/Cyans/Blues/
// Magentas), the global Colorize flag and the selected preset/range, and
// applies the effect to a single straight-alpha RGB pixel. Both the CPU
// adjustment path (core/AdjustmentTypes.cpp) and the GPU adjustment shader
// (via the resolved per-range uniforms uploaded by LayerCompositor) consume
// the *same* math here so the live preview, the cached projection and the
// export stay pixel-identical.
//
// Like ColorBalanceData / CurvesData, the model is deliberately decoupled from
// any widget: HueSaturationAdjustmentWidget edits it, AdjustmentTypes applies
// it, the LayerCompositor uploads its resolved ranges, and it can be reused in
// a standalone dialog later.
namespace huesaturation {

// Index 0 is the global Master range; 1..6 are the six hue bands. Persisted
// order matters for encode().
enum class Range {
    Master = 0,
    Reds = 1,
    Yellows = 2,
    Greens = 3,
    Cyans = 4,
    Blues = 5,
    Magentas = 6
};

constexpr int kRangeCount = 7;        // Master + 6 hue bands
constexpr int kBandCount = 6;         // hue bands only (Reds..Magentas)

// Per-range slider triple. hue ∈ [-180, 180]; saturation/lightness ∈ [-100, 100].
struct HslValues {
    int hue = 0;
    int saturation = 0;
    int lightness = 0;

    bool isZero() const { return hue == 0 && saturation == 0 && lightness == 0; }
};

// Hue-band geometry (degrees). Centres are the six primary/secondary hues.
// Exposed so default ranges / the recenter eyedropper can anchor to them.
extern const std::array<float, kBandCount> kBandCenters;   // Reds..Magentas
// Default half-width of the fully-affected core and of each falloff shoulder.
// A default band therefore reaches from centre−(core+falloff) to
// centre+(core+falloff); adjacent centres are 60° apart so the shoulders of
// neighbours overlap and form a smooth partition of unity over the hue circle.
// These match the previous fixed band geometry exactly, so adjustments saved
// before editable ranges existed reload pixel-identically with default ranges.
constexpr float kDefaultCoreHalf = 15.0f;
constexpr float kDefaultFalloff = 30.0f;

// ── Editable hue band ────────────────────────────────────────────────────────
// Geometry of one refinable hue band (Reds..Magentas), edited by the eyedroppers
// and the spectrum-bar handles. The four edges live in a *continuous* degree
// frame anchored near `centerHue`, ordered
//     outerStart ≤ innerStart ≤ innerEnd ≤ outerEnd
// so wrap (Reds crossing 0/360) is handled by simply storing negative offsets:
// Reds defaults to −45..−20..20..45. The fully-affected core is
// [innerStart, innerEnd] (weight 1); the shoulders [outerStart, innerStart] and
// [innerEnd, outerEnd] ramp linearly to 0; outside [outerStart, outerEnd] the
// weight is 0. Mirrored exactly by the GPU shader (uHsBandRange uniforms).
struct HueRange {
    bool enabled = true;
    float centerHue = 0.0f;
    float outerStart = -(kDefaultCoreHalf + kDefaultFalloff);
    float innerStart = -kDefaultCoreHalf;
    float innerEnd = kDefaultCoreHalf;
    float outerEnd = kDefaultCoreHalf + kDefaultFalloff;
    bool supportsWrap = false;
    // Optional history of hues sampled by the eyedroppers (degrees, 0..360).
    // Persisted for round-tripping; not consumed by applyPixel/weight().
    std::vector<int> sampledColors;

    // Membership weight of absolute hue `h` (deg) in this band: 1 in the core,
    // linear in the shoulders, 0 outside. Wrap-safe. Returns 0 when disabled.
    float weight(float h) const;
    // Recentre the whole band on `hueDeg`, preserving its current widths.
    void recenter(float hueDeg);
    // Grow the band so `hueDeg` is fully (or, for the shoulders, partially)
    // covered — used by the Add eyedropper. Keeps a falloff shoulder.
    void expandToInclude(float hueDeg);
    // Shrink the band so `hueDeg` is excluded — used by the Remove eyedropper.
    // Single-segment limitation: pulls the nearest edge in past `hueDeg`,
    // preserving the larger remaining side (documented; no band splitting).
    void contractToExclude(float hueDeg);
};

// Default professional ranges (Reds..Magentas) keyed by band index 0..5.
std::array<HueRange, kBandCount> defaultBandRanges();

struct HueSaturationData {
    QString presetName = QStringLiteral("Default");
    Range activeRange = Range::Master;
    bool colorize = false;
    // ranges[0] = Master, ranges[1..6] = Reds..Magentas. When colorize is on the
    // Master triple drives the global tint (hue → target hue, saturation →
    // intensity, lightness → brightness) and the bands are ignored.
    std::array<HslValues, kRangeCount> ranges{};
    // Per-band geometry for the six hue bands (index 0 = Reds … 5 = Magentas);
    // Master has no range. Initialised to defaultBandRanges() by reset().
    std::array<HueRange, kBandCount> bands{};

    HueSaturationData() { reset(); }

    HslValues& range(Range r) { return ranges[static_cast<int>(r)]; }
    const HslValues& range(Range r) const { return ranges[static_cast<int>(r)]; }

    // Band geometry accessor for a hue Range (Master returns bands[0]; callers
    // must guard Master separately). r in Reds..Magentas → bands[0..5].
    HueRange& band(Range r) { return bands[static_cast<int>(r) - 1]; }
    const HueRange& band(Range r) const { return bands[static_cast<int>(r) - 1]; }

    // The hue band (Reds..Magentas) whose centre is angularly closest to
    // `hueDeg` (0..360); used by the eyedropper to pick the range to refine.
    Range nearestBand(float hueDeg) const;

    // True when the effect is a no-op (no tint, every slider neutral).
    bool isIdentity() const;
    // Reset one range (or everything) back to neutral.
    void resetRange(Range r);
    void reset();

    // Applies the adjustment to one straight-alpha RGB pixel in place (each
    // channel 0..1). Shared by the CPU apply() and mirrored by the GPU shader.
    void applyPixel(float& r, float& g, float& b) const;

    // Compact, round-trippable string for the per-range sliders only
    // ("m:0,0,0;re:..;ye:..;gr:..;cy:..;bl:..;ma:.."). Stored as one QVariant
    // string so it survives the scalar-only adjustmentParams serializer;
    // colorize / activeRange / presetName ride as separate scalar params.
    QString encode() const;
    static HueSaturationData decode(const QString& s);

    // Bridges to/from AdjustmentData::params.
    QVariantMap toParams() const;
    static HueSaturationData fromParams(const QVariantMap& params);
};

// ── Presets ──────────────────────────────────────────────────────────────────
// Built-in presets, in display order. The first entry is always "Default".
// "Custom" is *not* in this list — the editor selects it when the user edits a
// control by hand. Defined as a clear, extensible table.
struct PresetInfo {
    QString name;
    HueSaturationData data;
};
const std::vector<PresetInfo>& presets();
// Returns the named preset's data; falls back to Default for unknown names
// (including "Custom").
HueSaturationData presetData(const QString& name);
bool isPreset(const QString& name);

inline const QString& kCustomPresetName()
{
    static const QString s = QStringLiteral("Custom");
    return s;
}

} // namespace huesaturation
