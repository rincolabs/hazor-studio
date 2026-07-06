#include "DynamicsEvaluator.hpp"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <climits>

static constexpr float PI = 3.14159265358979323846f;

float DynamicsEvaluator::randomFloat(uint32_t& seed)
{
    seed = seed * 1103515245u + 12345u;
    return static_cast<float>(seed % 10000u) / 10000.0f;
}

DabContext DynamicsEvaluator::makeContext(const BrushInputState& input, uint32_t seed)
{
    DabContext ctx;
    ctx.pressure = std::clamp(input.pressure, 0.0f, 1.0f);
    ctx.tiltX = std::clamp(input.tiltX, -1.0f, 1.0f);
    ctx.tiltY = std::clamp(input.tiltY, -1.0f, 1.0f);
    // Elevation (the max tilt already folded into the -1..1 fractions): 1 when
    // the pen is perpendicular (no tilt), 0 when fully laid down.
    {
        const float ex = ctx.tiltX, ey = ctx.tiltY;
        const float e = (std::fabs(ex) > std::fabs(ey))
                            ? std::sqrt(1.0f + ey * ey)
                            : std::sqrt(1.0f + ex * ex);
        const float cosAlpha = std::sqrt(ex * ex + ey * ey) / e;
        ctx.tiltElevation = std::clamp(std::acos(cosAlpha) / (PI * 0.5f), 0.0f, 1.0f);
    }
    // Tilt direction (azimuth), normalized to 0..1. atan2(-tiltX, tiltY) — the
    // negated x and swapped argument order are the sign convention that makes a
    // flat tip lean the right way with the stylus. With no tilt it stays at the
    // neutral 3-o'clock position (0.5 after normalization).
    if (input.tiltX == 0.0f && input.tiltY == 0.0f) {
        ctx.tiltDirection = 0.5f;
    } else {
        float dir = std::atan2(-ctx.tiltX, ctx.tiltY);   // -PI..PI
        ctx.tiltDirection = dir / (2.0f * PI) + 0.5f;     // 0..1
    }
    ctx.speed = std::min(input.velocity / 2000.0f, 1.0f);
    // Stroke angle mapped so 0.5 = stroke pointing right (0.5 + angle/2π,
    // wrapped). input.direction is atan2(dy, dx) in screen coords, -PI..PI.
    {
        float v = 0.5f + input.direction / (2.0f * PI);
        v -= std::floor(v);
        ctx.drawingAngle = v;
    }
    ctx.rotation = std::fmod(input.rotation / (2.0f * PI) + 1.0f, 1.0f);
    // Raw stroke accumulators; the Fade/Distance/Time sensors normalize them by
    // their own configurable length.
    ctx.strokeDistance = std::max(0.0f, input.strokeDistance);
    ctx.strokeTime = std::max(0.0f, input.strokeTime);
    ctx.dabIndex = std::max(0, input.dabIndex);
    ctx.rngState = seed ? seed : 0x9e3779b9u;
    ctx.strokeRandom = std::clamp(input.strokeRandom, 0.0f, 1.0f);
    return ctx;
}

// Scatter offset: magnitude = (2·rand − 1) · diameter · (strength × sensors),
// with strength = scatterOption.maxValue (0..5). Both axes → independent X/Y
// offsets; a single axis follows the stroke direction (X = along the stroke,
// Y = perpendicular to it).
QPointF DynamicsEvaluator::scatterOffset(const BrushSettings& settings,
                                         const DabContext& ctx,
                                         float diameter, uint32_t& seed)
{
    const ScatterConfig& sc = settings.scatter;
    if (!settings.scatterOption.enabled || (!sc.axisX && !sc.axisY))
        return QPointF(0.0f, 0.0f);

    const float value = settings.scatterOption.minValue
        + (settings.scatterOption.maxValue - settings.scatterOption.minValue)
              * settings.scatterOption.combinedSensor(ctx);
    const float jitter = (2.0f * randomFloat(seed) - 1.0f) * diameter * value;

    if (sc.axisX && sc.axisY) {
        const float jitterY = (2.0f * randomFloat(seed) - 1.0f) * diameter * value;
        return QPointF(jitter, jitterY);
    }

    // Stroke direction in radians (drawingAngle stores 0.5 + angle/2π).
    const float angle = (ctx.drawingAngle - 0.5f) * 2.0f * PI;
    const float cx = std::cos(angle);
    const float cy = std::sin(angle);
    if (sc.axisX)
        return QPointF(cx * jitter, cy * jitter);
    return QPointF(-cy * jitter, cx * jitter);
}

QColor DynamicsEvaluator::applyColorDynamics(const QColor& base,
                                             const ColorDynamics& cd,
                                             uint32_t& seed)
{
    if (!cd.enabled)
        return base;

    QColor c = base;

    // Purity: blend toward a random point between the configured fg/bg colours.
    if (cd.purityAmount > 0.0f) {
        const float t = std::clamp(
            0.5f + cd.purityAmount * (2.0f * randomFloat(seed) - 1.0f), 0.0f, 1.0f);
        const float r = cd.fgColor.redF() + (cd.bgColor.redF() - cd.fgColor.redF()) * t;
        const float g = cd.fgColor.greenF() + (cd.bgColor.greenF() - cd.fgColor.greenF()) * t;
        const float b = cd.fgColor.blueF() + (cd.bgColor.blueF() - cd.fgColor.blueF()) * t;
        const float a = cd.fgColor.alphaF() + (cd.bgColor.alphaF() - cd.fgColor.alphaF()) * t;
        c = QColor::fromRgbF(r, g, b, a);
    }

    float h = c.hueF();
    float s = c.saturationF();
    float l = c.lightnessF();
    const float a = c.alphaF();

    if (cd.hueAmount > 0.0f)
        h = std::fmod(h + cd.hueAmount * (2.0f * randomFloat(seed) - 1.0f) + 1.0f, 1.0f);
    if (cd.saturationAmount > 0.0f)
        s = std::clamp(s + cd.saturationAmount * (2.0f * randomFloat(seed) - 1.0f),
                       0.0f, 1.0f);
    if (cd.brightnessAmount > 0.0f)
        l = std::clamp(l + cd.brightnessAmount * (2.0f * randomFloat(seed) - 1.0f),
                       0.0f, 1.0f);

    return QColor::fromHslF(
        std::fmod(h + 1.0f, 1.0f),
        std::clamp(s, 0.0f, 1.0f),
        std::clamp(l, 0.0f, 1.0f),
        std::clamp(a, 0.0f, 1.0f));
}

DynamicDabParams DynamicsEvaluator::evaluate(
    const BrushSettings& settings,
    const BrushInputState& input,
    uint32_t seed)
{
    DynamicDabParams params;

    // Per-dab snapshot for the curve-option sensors. Computed even when no
    // option is enabled (cheap); it then has no effect.
    const DabContext ctx = makeContext(input, seed);

    float size = settings.size;
    float opacity = settings.opacity;
    float flow = settings.flow;
    // Static Brush Tip Shape values are the base the dynamics vary around.
    float angle = settings.angle;
    float roundness = std::clamp(settings.roundness, 0.05f, 1.0f);
    QColor color = settings.color;

    // ── Size ──
    if (settings.sizeOption.enabled)
        size = std::max(settings.size * settings.sizeOption.evaluate(ctx), 1.0f);

    // ── Angle (rotation) ──
    // The option produces the dab angle with the static tip angle folded in as
    // the neutral base, so this replaces `angle`. strength = the option's max.
    if (settings.rotationOption.enabled)
        angle = settings.rotationOption.evaluateRotation(
            ctx, settings.angle, settings.rotationOption.maxValue);

    // ── Roundness (ratio) ──
    if (settings.ratioOption.enabled)
        roundness = std::clamp(roundness * settings.ratioOption.evaluate(ctx),
                               0.05f, 1.0f);

    // ── Opacity ──
    if (settings.opacityOption.enabled)
        opacity = std::clamp(opacity * settings.opacityOption.evaluate(ctx), 0.0f, 1.0f);

    // ── Flow ──
    if (settings.flowOption.enabled)
        flow = std::clamp(flow * settings.flowOption.evaluate(ctx), 0.0f, 1.0f);

    // ── Color ──
    if (settings.colorDynamics.enabled)
        color = applyColorDynamics(settings.color, settings.colorDynamics, seed);

    // ── Scatter ── uses the dynamic dab DIAMETER (before the radius conversion
    // below), so the offset scales with the painted dab like the dab itself.
    params.scatterOffset = scatterOffset(settings, ctx, size, seed);

    // BrushSettings::size is a DIAMETER (the UI's "N px" readout); the dab
    // geometry downstream consumes effectiveSize as a RADIUS (footprint =
    // 2×radius). Convert once here — this is the single seam every geometry
    // consumer (CPU dab, GPU stamp/mask, dual brush, bounding box) goes through,
    // so size means the same diameter for native and imported brushes alike.
    size *= 0.5f;
    params.effectiveSize = size;
    params.effectiveAngle = angle;
    params.effectiveRoundness = roundness;
    params.effectiveOpacity = opacity;
    params.effectiveFlow = flow;
    params.effectiveColor = color;
    params.effectiveFlipX = settings.flipX;
    params.effectiveFlipY = settings.flipY;

    return params;
}
