#pragma once

#include <QColor>
#include <QPointF>
#include <QImage>
#include <QString>
#include <QVector>
#include <cstdint>
#include <algorithm>
#include <cmath>

#include "DabContext.hpp"

enum class BrushBlendMode {
    Normal = 0,
    Multiply,
    Screen,
    Overlay,
    Darken,
    Lighten,
    ColorDodge,
    ColorBurn,
    HardLight,
    SoftLight,
    Difference,
    Exclusion,
    Hue,
    Saturation,
    Color,
    Luminosity
};

enum class SmoothingMode {
    Basic = 0,
    Off,
    PulledString,
    CubicSpline
};

// Scatter: offsets each dab randomly around the stroke path. The offset
// magnitude is driven by the scatter curve option (strength x sensors) times the
// dab diameter; these flags pick the axes. With both axes on the offset is a free
// rectangle; with a single axis it follows the stroke direction (X = along the
// stroke, Y = perpendicular to it).
struct ScatterConfig {
    bool axisX = true;
    bool axisY = true;
};

// Color variation applied per dab: random shifts of hue/saturation/brightness,
// plus a foreground->background blend driven by `purity`. Amounts are 0..1.
struct ColorDynamics {
    bool enabled = false;
    QColor fgColor = Qt::black;
    QColor bgColor = Qt::white;
    float hueAmount = 0.0f;
    float saturationAmount = 0.0f;
    float brightnessAmount = 0.0f;
    float purityAmount = 0.0f;
};

struct TextureConfig {
    bool enabled = false;
    QImage texture;
    // Reference to a texture in the global library (BrushTextureLibrary). The
    // QImage above is the resolved pixels; textureId is what the preset persists so
    // textures stay reusable resources rather than being trapped inside one brush.
    QString textureId;

    float scale = 1.0f;
    float brightness = 0.0f;
    float contrast = 1.0f;        // 1.0 = identity (pivots around neutralPoint)
    bool invert = false;          // invert the pattern value
    float depth = 1.0f;

    // How the pattern modulates coverage. The CPU dab fully implements
    // Multiply/Subtract; the remaining modes are stored for fidelity and
    // approximated by the engine (see BrushDab::rasterize).
    enum class TexturingMode {
        Multiply = 0, Subtract, LightnessMap, GradientMap, Darken, Overlay,
        ColorDodge, ColorBurn, LinearBurn, Height, LinearHeight
    };
    TexturingMode texturingMode = TexturingMode::Multiply;
    // Kept in sync with texturingMode (Multiply/Subtract) for the GPU stamp path.
    enum class Texturing { Multiply = 0, Subtract = 1 };
    Texturing texturing = Texturing::Multiply;

    bool softTexturing = false;   // soften the texture's effect on coverage
    float neutralPoint = 0.5f;    // pattern value that means "no effect" (contrast pivot)

    // Cutoff: values outside [cutoffMin, cutoffMax] are killed; what the cutoff
    // applies to is the policy. This is what breaks a light stroke into grain
    // (the pencil "tooth").
    enum class CutoffPolicy { Disabled = 0, Pattern, BrushMask, Both };
    CutoffPolicy cutoffPolicy = CutoffPolicy::Disabled;
    float cutoffMin = 0.0f;
    float cutoffMax = 1.0f;

    int horizontalOffset = 0;     // pattern offset in pixels
    int verticalOffset = 0;
    bool randomHorizontalOffset = false;
    bool randomVerticalOffset = false;

    bool autoInvertForEraser = false;
};

struct DualBrushConfig {
    bool enabled = false;
    int tipType = 0; // 0=Round, 1=Square (mirrors BrushType)
    float size = 20.0f;
    float spacing = 1.0f;
    float scatter = 0.0f;
    int count = 1;
    float hardness = 0.8f;
};

struct DynamicDabParams {
    float effectiveSize = 20.0f;
    float effectiveAngle = 0.0f;
    float effectiveRoundness = 1.0f;
    float effectiveOpacity = 1.0f;
    float effectiveFlow = 1.0f;
    QColor effectiveColor = Qt::black;
    QPointF scatterOffset{0.0f, 0.0f};
    bool effectiveFlipX = false;
    bool effectiveFlipY = false;
};

// ── Curve options + sensors (the engine's only dynamics path) ───────────────
//
// Every dynamic (size/opacity/flow/rotation/ratio/scatter) is a "curve option":
// a set of sensors, each mapping one input axis through an editable response
// curve, the outputs combined (multiply/add/max/min/difference) and mapped into
// [minValue, maxValue].

// Which input axis a sensor reads from a DabContext.
enum class SensorType {
    Pressure = 0,
    TiltX,
    TiltY,
    TiltElevation,
    Speed,
    DrawingAngle,
    Rotation,
    Distance,
    Time,
    Fade,
    Fuzzy,        // per-dab random
    FuzzyStroke,  // per-stroke random (one value held for the whole stroke)
    // Appended at the end so existing presets (serialized by enum index 0..11) keep
    // their sensor. The pen's tilt AZIMUTH (atan2 of the tilt components): the
    // additive sensor that rotates a flat tip to follow the way the stylus leans.
    TiltDirection
};

// Editable response curve: control points in [0,1]x[0,1], sorted by x. Empty
// (or a single point) behaves as the identity y = x, which is what an un-edited
// identity curve ("0,0;1,1;") evaluates to.
struct ResponseCurve {
    QVector<QPointF> points;   // x ascending, both coords clamped to 0..1

    float evaluate(float x) const {
        x = std::clamp(x, 0.0f, 1.0f);
        const int n = points.size();
        if (n == 0)
            return x;                       // identity
        if (n == 1)
            return std::clamp(float(points.first().y()), 0.0f, 1.0f);
        // Outside the defined x-range the endpoint value holds flat.
        if (x <= float(points.first().x()))
            return std::clamp(float(points.first().y()), 0.0f, 1.0f);
        if (x >= float(points.last().x()))
            return std::clamp(float(points.last().y()), 0.0f, 1.0f);

        // Two points (or more than the stack budget) → straight segment.
        if (n == 2 || n > 32) {
            for (int i = 1; i < n; ++i) {
                const float x1 = float(points[i].x());
                if (x <= x1) {
                    const float x0 = float(points[i - 1].x());
                    const float y0 = float(points[i - 1].y());
                    const float y1 = float(points[i].y());
                    const float span = x1 - x0;
                    const float t = span > 1e-6f ? (x - x0) / span : 0.0f;
                    return std::clamp(y0 + (y1 - y0) * t, 0.0f, 1.0f);
                }
            }
            return std::clamp(float(points.last().y()), 0.0f, 1.0f);
        }

        // Natural cubic spline (second derivative 0 at both ends): standard
        // tridiagonal sweep for the second derivatives, then the cubic on the
        // containing interval. Stack arrays keep it allocation-free.
        float xs[32], ys[32], y2[32], u[32];
        for (int i = 0; i < n; ++i) {
            xs[i] = float(points[i].x());
            ys[i] = float(points[i].y());
        }
        y2[0] = u[0] = 0.0f;
        for (int i = 1; i < n - 1; ++i) {
            const float sig = (xs[i] - xs[i - 1]) / (xs[i + 1] - xs[i - 1]);
            const float p = sig * y2[i - 1] + 2.0f;
            y2[i] = (sig - 1.0f) / p;
            const float du = (ys[i + 1] - ys[i]) / (xs[i + 1] - xs[i])
                           - (ys[i] - ys[i - 1]) / (xs[i] - xs[i - 1]);
            u[i] = (6.0f * du / (xs[i + 1] - xs[i - 1]) - sig * u[i - 1]) / p;
        }
        y2[n - 1] = 0.0f;
        for (int k = n - 2; k >= 0; --k)
            y2[k] = y2[k] * y2[k + 1] + u[k];

        int khi = 1;
        while (khi < n - 1 && x > xs[khi]) ++khi;
        const int klo = khi - 1;
        const float h = xs[khi] - xs[klo];
        if (h <= 1e-6f)
            return std::clamp(ys[klo], 0.0f, 1.0f);
        const float a = (xs[khi] - x) / h;
        const float b = (x - xs[klo]) / h;
        const float y = a * ys[klo] + b * ys[khi]
            + ((a * a * a - a) * y2[klo] + (b * b * b - b) * y2[khi]) * (h * h) / 6.0f;
        return std::clamp(y, 0.0f, 1.0f);
    }

    enum class Preset { Linear = 0, Soft, Hard };

    static ResponseCurve identity() { return ResponseCurve{}; }
    static ResponseCurve linePreset(Preset type) {
        ResponseCurve c;
        // Fixed-feel presets expressed as a few control points; Linear stays the
        // identity.
        switch (type) {
        case Preset::Soft:   // sqrt-like: quick ramp, eases into full strength
            c.points = { {0.0f, 0.0f}, {0.25f, 0.5f}, {0.5f, 0.707f}, {1.0f, 1.0f} };
            break;
        case Preset::Hard:   // squared-like: slow ramp, needs a heavy press
            c.points = { {0.0f, 0.0f}, {0.5f, 0.25f}, {0.75f, 0.5625f}, {1.0f, 1.0f} };
            break;
        case Preset::Linear:
        default:
            break;
        }
        return c;
    }
};

struct Sensor {
    SensorType type = SensorType::Pressure;
    ResponseCurve curve;          // identity by default
    bool enabled = true;

    // Ramp reference for the Fade/Distance/Time sensors: the sensor reads
    // progress/length, clamped at 1 (or wrapped when `periodic`). Units per type:
    // Fade = dabs, Distance = px, Time = seconds. <= 0 falls back to the default.
    int length = 0;
    bool periodic = false;

    // DrawingAngle only: constant offset (degrees) added to the stroke angle
    // before normalization.
    float angleOffset = 0.0f;

    int effectiveLength() const {
        if (length > 0)
            return length;
        switch (type) {
        case SensorType::Fade:     return 1000;  // dabs
        case SensorType::Distance: return 30;    // px
        case SensorType::Time:     return 30;    // seconds
        default:                   return 1;
        }
    }

    // Raw 0..1 axis value before the curve.
    float rawValue(const DabContext& c) const {
        switch (type) {
        // Tilt X/Y: 1 − |tilt|/max. c.tiltX/Y are already the tilt fraction, so
        // this is 1 − |fraction|: 1 at no tilt, 0 fully laid down.
        case SensorType::Pressure:      return std::clamp(c.pressure, 0.0f, 1.0f);
        case SensorType::TiltX:         return std::clamp(1.0f - std::fabs(c.tiltX), 0.0f, 1.0f);
        case SensorType::TiltY:         return std::clamp(1.0f - std::fabs(c.tiltY), 0.0f, 1.0f);
        case SensorType::TiltElevation: return std::clamp(c.tiltElevation, 0.0f, 1.0f);
        case SensorType::TiltDirection: return std::clamp(c.tiltDirection, 0.0f, 1.0f);
        case SensorType::Speed:         return std::clamp(c.speed, 0.0f, 1.0f);
        case SensorType::DrawingAngle: {
            // 0.5 + angle/2π (+ the configured offset), wrapped into [0,1): the
            // stroke pointing right reads 0.5.
            float v = c.drawingAngle + angleOffset / 360.0f;
            v -= std::floor(v);
            return v;
        }
        case SensorType::Rotation:      return std::clamp(c.rotation, 0.0f, 1.0f);
        case SensorType::Distance: {
            const float len = float(effectiveLength());
            const float d = std::max(0.0f, c.strokeDistance);
            return std::clamp((periodic ? std::fmod(d, len) : std::min(d, len)) / len,
                              0.0f, 1.0f);
        }
        case SensorType::Time: {
            const float len = float(effectiveLength());
            const float t = std::max(0.0f, c.strokeTime);
            return std::clamp((periodic ? std::fmod(t, len) : std::min(t, len)) / len,
                              0.0f, 1.0f);
        }
        case SensorType::Fade: {
            const float len = float(effectiveLength());
            const float i = float(std::max(0, c.dabIndex));
            return std::clamp((periodic ? std::fmod(i, len) : std::min(i, len)) / len,
                              0.0f, 1.0f);
        }
        case SensorType::Fuzzy:         return c.nextRandom();
        case SensorType::FuzzyStroke:   return std::clamp(c.strokeRandom, 0.0f, 1.0f);
        }
        return 1.0f;
    }

    float value(const DabContext& c) const {
        if (curve.points.isEmpty())
            return rawValue(c);
        if (type == SensorType::DrawingAngle) {
            // The response curve for the stroke angle is authored in the frame
            // where 0 = stroke pointing right: shift by half a turn, evaluate,
            // shift back.
            float v = rawValue(c) + 0.5f;
            v -= std::floor(v);
            v = curve.evaluate(v) + 0.5f;
            v -= std::floor(v);
            return v;
        }
        return curve.evaluate(rawValue(c));
    }
};

// One dynamic option. evaluate() returns a value mapped into [minValue, maxValue]
// driven by the combined sensor outputs. Disabled or sensor-less options return
// maxValue (full strength), so multiplying a base by evaluate() with a neutral
// max of 1 is a no-op — letting callers apply it uniformly.
struct CurveOption {
    // Sensor-combination modes: Multiply (default), Add, Max, Min, Difference.
    // Only matters when an option stacks more than one sensor.
    enum class Combine { Multiply = 0, Add, Max, Min, Difference };

    bool enabled = false;
    QVector<Sensor> sensors;
    Combine combine = Combine::Multiply;
    float minValue = 0.0f;
    // Doubles as the option strength: the combined sensor output is scaled into
    // [minValue, maxValue], so a flat option (no sensors) contributes exactly
    // maxValue. Scatter uses 0..5; every other axis uses 0..1.
    float maxValue = 1.0f;

    float combinedSensor(const DabContext& c) const {
        if (sensors.isEmpty())
            return 1.0f;
        bool any = false;
        float prodOrSum = (combine == Combine::Add || combine == Combine::Difference)
                              ? 0.0f : 1.0f;
        float mn = 1.0f, mx = 0.0f;
        for (const Sensor& s : sensors) {
            if (!s.enabled) continue;
            any = true;
            const float v = s.value(c);
            switch (combine) {
            case Combine::Add:        prodOrSum += v; break;
            case Combine::Max:        mx = std::max(mx, v); break;
            case Combine::Min:        mn = std::min(mn, v); break;
            case Combine::Difference: mn = std::min(mn, v); mx = std::max(mx, v); break;
            case Combine::Multiply:
            default:                  prodOrSum *= v; break;
            }
        }
        if (!any) return 1.0f;
        float result;
        switch (combine) {
        case Combine::Max:        result = mx; break;
        case Combine::Min:        result = mn; break;
        case Combine::Difference: result = mx - mn; break;
        case Combine::Add:
        case Combine::Multiply:
        default:                  result = prodOrSum; break;
        }
        return std::clamp(result, 0.0f, 1.0f);
    }

    // Value in [minValue, maxValue]; disabled → maxValue.
    float evaluate(const DabContext& c) const {
        if (!enabled)
            return maxValue;
        return minValue + (maxValue - minValue) * combinedSensor(c);
    }

    // Dab rotation in RADIANS, replacing the base tip angle. `baseAngleRad` (the
    // static Brush-Tip angle) is added to the option's output, so with the option
    // disabled the method returns exactly `baseAngleRad`.
    //
    // The sensors combine three different ways, matching the reference dynamics:
    //   • additive   (Rotation, TiltDirection, Fuzzy): summed in directly. The
    //     random sensors span the full circle both ways (value 2v−1).
    //   • absolute   (DrawingAngle): replaces the neutral offset so the tip
    //     follows the stroke direction.
    //   • scaling    (Pressure, Tilt X/Y, Elevation, Speed, Distance, Time,
    //     Fade): combined via `combine`, mapped −1..1 and negated (the scaling
    //     part rotates counterclockwise), sweeping the full circle across the
    //     sensor range with the neutral mid-value at a half turn.
    // The option strength is maxValue; the combined value is wrapped to [−1,1],
    // then flipped (rotation is measured in the opposite direction relative to
    // the framework's screen convention) and scaled by π.
    //
    // Tilt sensors contribute nothing with no tablet tilt (a mouse must not
    // inject rotation); while hovering the scaling and random parts are disabled
    // so the hover cursor sits at the neutral angle.
    float evaluateRotation(const DabContext& c, float baseAngleRad,
                           float strength, bool hover = false) const {
        constexpr float kPi = 3.14159265358979323846f;
        if (!enabled)
            return baseAngleRad;

        const bool tiltActive = (c.tiltX != 0.0f || c.tiltY != 0.0f);

        bool hasAdditive = false, hasScaling = false, hasAbsolute = false;
        float additive = 0.0f;
        float absoluteOffset = 0.0f;

        // Scaling sensors combine exactly like combinedSensor() (honours `combine`).
        float prodOrSum = (combine == Combine::Add || combine == Combine::Difference)
                              ? 0.0f : 1.0f;
        float mn = 1.0f, mx = 0.0f;
        auto accumulateScaling = [&](float v) {
            hasScaling = true;
            switch (combine) {
            case Combine::Add:        prodOrSum += v; break;
            case Combine::Max:        mx = std::max(mx, v); break;
            case Combine::Min:        mn = std::min(mn, v); break;
            case Combine::Difference: mn = std::min(mn, v); mx = std::max(mx, v); break;
            case Combine::Multiply:
            default:                  prodOrSum *= v; break;
            }
        };

        for (const Sensor& s : sensors) {
            if (!s.enabled)
                continue;
            switch (s.type) {
            case SensorType::Rotation:
                // Additive: value = rotation/half-turn. c.rotation is fractional
                // turns [0,1) for [0,360)°, so that is 2·value.
                additive += 2.0f * s.value(c);
                hasAdditive = true;
                break;
            case SensorType::TiltDirection:
                if (!tiltActive) break;          // no tablet tilt → no injection
                // Additive, mapped to −1..1 around the neutral azimuth.
                additive += 2.0f * s.value(c) - 1.0f;
                hasAdditive = true;
                break;
            case SensorType::Fuzzy:
            case SensorType::FuzzyStroke:
                if (hover) break;
                // Additive random: full circle both directions.
                additive += 2.0f * s.value(c) - 1.0f;
                hasAdditive = true;
                break;
            case SensorType::DrawingAngle:
                // Absolute: replaces the neutral offset (tip follows the stroke).
                absoluteOffset = s.value(c);
                hasAbsolute = true;
                break;
            case SensorType::TiltX:
            case SensorType::TiltY:
            case SensorType::TiltElevation:
                if (!tiltActive) break;
                accumulateScaling(s.value(c));
                break;
            default:                              // Pressure, Speed, Distance, Time, Fade
                accumulateScaling(s.value(c));
                break;
            }
        }

        float scaling = 1.0f;
        if (hasScaling) {
            switch (combine) {
            case Combine::Max:        scaling = mx; break;
            case Combine::Min:        scaling = mn; break;
            case Combine::Difference: scaling = mx - mn; break;
            case Combine::Add:
            case Combine::Multiply:
            default:                  scaling = prodOrSum; break;
            }
            scaling = std::clamp(scaling, 0.0f, 1.0f);
        }

        const float offset = hasAbsolute ? absoluteOffset : 0.0f;
        // While hovering the scaling part is fully disabled so the cursor
        // outline does not spin with pressure/speed noise.
        const float realScaling  = (hasScaling && !hover) ? (2.0f * scaling - 1.0f) : 0.0f;
        const float realAdditive = hasAdditive ? additive : 0.0f;

        // The scaling part is negated (rotates counterclockwise).
        float value = 2.0f * offset + strength * (-realScaling + realAdditive);
        value = std::fmod(value + 1.0f, 2.0f);
        if (value < 0.0f) value += 2.0f;
        value -= 1.0f;                            // wrapped into [−1, 1)
        value = 1.0f - value;                     // opposite direction to the screen convention
        return value * kPi + baseAngleRad;
    }

    // ── Pressure-sensor helpers (drive the "Use Pen Pressure" toggle via a
    // Pressure sensor inside the curve option). They never touch non-pressure
    // sensors or curves, so a velocity/tilt sensor or an edited response curve
    // the user set is preserved.
    bool hasActivePressureSensor() const {
        for (const Sensor& s : sensors)
            if (s.enabled && s.type == SensorType::Pressure)
                return true;
        return false;
    }

    // Enable/disable the Pressure sensor. When enabling and there is no Pressure
    // sensor yet, one is appended with an identity curve (raw pressure → value).
    // When disabling, the option as a whole is switched off if no other sensor
    // keeps it alive, so it stops affecting the dab.
    void setPressureSensorEnabled(bool on) {
        Sensor* found = nullptr;
        for (Sensor& s : sensors)
            if (s.type == SensorType::Pressure) { found = &s; break; }
        if (on) {
            if (!found) {
                Sensor s;
                s.type = SensorType::Pressure;
                s.enabled = true;            // identity curve by default
                sensors.append(s);
            } else {
                found->enabled = true;
            }
            enabled = true;
        } else {
            if (found)
                found->enabled = false;
            // Keep the option enabled only if some other sensor still drives it.
            bool anyOther = false;
            for (const Sensor& s : sensors)
                if (s.enabled) { anyOther = true; break; }
            if (!anyOther)
                enabled = false;
        }
    }
};

// Procedural-tip (AutoBrush / mask generator) parameters that aren't already a
// top-level BrushSettings field. Defaults reproduce the plain round/square dab
// exactly (Default profile, isotropic fade derived from `hardness`, no spikes,
// full density), so a preset that doesn't set these paints identically.
struct AutoBrushConfig {
    int profile = 0;          // AutoBrush::Profile — 0=Default, 1=Soft, 2=Gaussian
    int spikes = 2;           // 2 = none; >2 = star/polygon
    float hFade = -1.0f;      // <0 → isotropic (derive from hardness)
    float vFade = -1.0f;      // <0 → isotropic (derive from hardness)
    float density = 1.0f;     // 0..1 stochastic coverage
    float randomness = 0.0f;  // 0..1 edge noise
    ResponseCurve softnessCurve;  // identity = unmodified profile
};
