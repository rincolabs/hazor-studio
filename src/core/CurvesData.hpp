#pragma once

#include <QPoint>
#include <QString>
#include <QVariantMap>
#include <array>
#include <vector>

// Reusable model for a Curves adjustment. Holds the control points of the
// master (RGB) curve plus the per-channel (R/G/B/Alpha) curves, and builds the
// 256-entry lookup tables that both the CPU adjustment path
// (core/AdjustmentTypes.cpp) and the GPU adjustment shader (via a LUT texture)
// consume — so the live preview, the cached projection and the export stay in
// sync.
//
// The model is deliberately decoupled from any widget: CurvesEditorWidget edits
// it, AdjustmentTypes applies it, and the LayerCompositor uploads its combined
// LUTs. Curve interpolation uses a monotone cubic (Fritsch–Carlson / PCHIP)
// spline so the result is smooth and overshoot-free.
namespace curves {

// Editable channel. The persisted order matters: RGB (master) is applied first,
// then the individual R/G/B channels. Alpha is reserved (the compositing
// pipeline does not curve alpha yet) and stays disabled in the UI.
enum class Channel { RGB = 0, Red = 1, Green = 2, Blue = 3, Alpha = 4 };

constexpr int kChannelCount = 5;

using Point = QPoint;             // x = input 0..255, y = output 0..255
using PointList = std::vector<Point>;

// A linear ramp (0,0)→(255,255): the identity curve.
PointList identityPoints();

struct CurvesData {
    // One point list per Channel (index by static_cast<int>(Channel)).
    std::array<PointList, kChannelCount> channels;
    // The preset the curve last matched ("Custom" once edited manually).
    QString preset = QStringLiteral("Default");

    CurvesData();

    PointList& points(Channel c) { return channels[static_cast<int>(c)]; }
    const PointList& points(Channel c) const { return channels[static_cast<int>(c)]; }

    // True when every channel is the identity ramp.
    bool isIdentity() const;
    // True when one channel is the identity ramp.
    bool isChannelIdentity(Channel c) const;

    // Reset one channel (or all) back to identity. Resetting does not by itself
    // change `preset`; callers decide the preset bookkeeping.
    void resetChannel(Channel c);
    void resetAll();

    // 256-entry LUT for a single channel's own points.
    void buildChannelLut(Channel c, std::array<unsigned char, 256>& lut) const;

    // Combined LUTs: combined[chan][v] = chanLut[ masterLut[v] ]. Apply these
    // directly to a straight-alpha pixel to get the final R/G/B. The order
    // (master first, then per-channel) matches the spec.
    void buildCombinedLuts(std::array<unsigned char, 256>& rLut,
                           std::array<unsigned char, 256>& gLut,
                           std::array<unsigned char, 256>& bLut) const;

    // Compact, round-trippable string ("rgb:0,0,255,255;red:..."). Stored as a
    // single QVariant string so it survives the generic adjustmentParams
    // (scalar-only) JSON serializer.
    QString encode() const;
    static CurvesData decode(const QString& s);

    // Bridges to/from AdjustmentData::params. `params["curves"]` holds encode(),
    // `params["preset"]` holds the preset id.
    QVariantMap toParams() const;
    static CurvesData fromParams(const QVariantMap& params);
};

// Sorts points by x, clamps to 0..255, removes duplicate-x points, keeps the
// two endpoints. Returns a sanitized copy.
PointList sanitize(const PointList& pts);

// Builds a 256-entry monotone-spline LUT directly from a point list. Used by
// the graph widget to draw the curve for the channel it is editing.
void lutFromPoints(const PointList& pts, std::array<unsigned char, 256>& lut);

// ── Presets ────────────────────────────────────────────────────────────────
// A preset only specifies the master (RGB) curve; per-channel curves stay
// identity. Defined in one table so new presets are trivial to add.
struct Preset {
    QString id;            // also the combo display text
    PointList rgb;         // master curve points
};

const std::vector<Preset>& presets();
// Returns the preset's master points, or identity if unknown.
const Preset* findPreset(const QString& id);

} // namespace curves
