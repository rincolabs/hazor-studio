#include "BrushEngine.hpp"
#include "BrushRenderer.hpp"
#include "core/Layer.hpp"

#include <cmath>
#include <algorithm>

namespace {
inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

// Spacing scales with the dab's effective size so that as pressure shrinks the
// dab the step shrinks too — a low-pressure taper stays continuous instead of
// turning into spaced dots. Only the size option's value (not the per-dab
// random part... which it may include) is folded into the representative size.
inline float effectiveSpacing(const BrushSettings& settings,
                              const BrushInputState& input)
{
    // settings.size is a DIAMETER, so effSize is the dab diameter — both the
    // step (a fraction of the diameter) and the auto-spacing curve read it
    // directly.
    float sizeFactor = 1.0f;
    if (settings.sizeOption.enabled) {
        const DabContext ctx = DynamicsEvaluator::makeContext(input, 0);
        sizeFactor = std::clamp(settings.sizeOption.evaluate(ctx), 0.0f, 10.0f);
    }
    const float effSize = settings.size * sizeFactor;
    if (settings.useAutoSpacing) {
        // Auto-spacing: the step shrinks (relative to the dab) as the brush
        // grows — sqrt of the diameter, scaled by the user coefficient — which
        // keeps big brushes from turning into spaced stamps (air-brush feel).
        const float diam = effSize;
        const float v = diam < 1.0f ? diam : std::sqrt(diam);
        return std::max(settings.autoSpacingCoeff * v, 1.0f);
    }
    return std::max(effSize * settings.spacing, 1.0f);
}
} // namespace

void BrushEngine::beginStroke(const BrushInputState& state)
{
    m_active = true;
    m_state.lastPoint = state.imagePos;
    m_state.smoothPoints[0] = state.imagePos;
    m_state.smoothPoints[1] = state.imagePos;
    m_state.smoothPoints[2] = state.imagePos;
    m_state.smoothCount = 1;
    m_state.accumulatedDistance = 0.0f;

    m_stringPos = state.imagePos;
    m_splineHistory.clear();
    m_splineHistory.push_back(state.imagePos);
    m_dabSeed = 0;
    m_dabCount = 0;
    // The first strokeTo() must place a dab exactly at the press point; after
    // that, dabs are emitted only when the travelled distance crosses the
    // spacing step — an input event closer than the step (or a stationary
    // pressure change) paints nothing.
    m_pendingFirstDab = true;
    m_distSinceDab = 0.0f;
    // One random value for the whole stroke (per-stroke fuzzy source).
    m_strokeRng = m_strokeRng * 1103515245u + 12345u;
    m_strokeRandom = static_cast<float>(m_strokeRng % 10000u) / 10000.0f;
    m_strokeTimer.start();

    m_lastPressure = state.pressure;
    m_lastTiltX = state.tiltX;
    m_lastTiltY = state.tiltY;
    m_lastRotation = state.rotation;
}

void BrushEngine::beginStroke(const BrushInputState& state, bool targetMask)
{
    m_targetMask = targetMask;
    beginStroke(state);
}

void BrushEngine::endStroke()
{
    m_active = false;
    m_state.smoothCount = 0;
    m_splineHistory.clear();
    m_targetMask = false;
}

void BrushEngine::drawDynamicDab(QPointF imagePos,
                                 const BrushInputState& state,
                                 const BrushSettings& settings,
                                 BrushRenderer& renderer, Layer* layer,
                                 unsigned int selectMaskTex,
                                 int selectMaskW, int selectMaskH,
                                 const QImage* selectMaskImage,
                                 uint32_t& seed,
                                 const CloneStampContext* cloneContext)
{
    BrushInputState s = state;
    s.imagePos = imagePos;
    s.strokeRandom = m_strokeRandom;   // per-stroke fuzzy source (steady along stroke)
    s.dabIndex = m_dabCount++;         // dab sequence number (Fade sensor)
    // Global minimum-pressure floor, applied on top of the preset (priority override).
    if (s.pressure < m_minPressure)
        s.pressure = m_minPressure;

    DynamicDabParams params = DynamicsEvaluator::evaluate(settings, s, seed);
    s.imagePos += params.scatterOffset;

    if (m_targetMask)
        renderer.drawMaskDab(layer, s, params, settings,
                             selectMaskTex, selectMaskW, selectMaskH);
    else if (cloneContext)
        renderer.drawCloneDab(layer, s, params, settings,
                              *cloneContext, selectMaskImage);
    else
        renderer.drawDab(layer, s, params, settings,
                         selectMaskTex, selectMaskW, selectMaskH,
                         selectMaskImage);
}

QPointF BrushEngine::smoothBasic(QPointF raw)
{
    m_state.smoothPoints[m_state.smoothCount % 3] = raw;
    m_state.smoothCount++;

    if (m_state.smoothCount < 3)
        return raw;

    float inv = 1.0f / 3.0f;
    float sx = (m_state.smoothPoints[0].x() +
                m_state.smoothPoints[1].x() +
                m_state.smoothPoints[2].x()) * inv;
    float sy = (m_state.smoothPoints[0].y() +
                m_state.smoothPoints[1].y() +
                m_state.smoothPoints[2].y()) * inv;
    return QPointF(sx, sy);
}

QPointF BrushEngine::smoothPulledString(QPointF raw, float radius)
{
    QPointF diff = raw - m_stringPos;
    float dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());

    if (dist > radius) {
        float advance = dist - radius;
        m_stringPos += diff * (advance / dist);
    }

    return m_stringPos;
}

QPointF BrushEngine::catmullRom(QPointF p0, QPointF p1,
                                 QPointF p2, QPointF p3, float t)
{
    float t2 = t * t;
    float t3 = t2 * t;

    return 0.5f * (
        (2.0f * p1) +
        (-p0 + p2) * t +
        (2.0f * p0 - 5.0f * p1 + 4.0f * p2 - p3) * t2 +
        (-p0 + 3.0f * p1 - 3.0f * p2 + p3) * t3
    );
}

void BrushEngine::strokeCubicSpline(const BrushInputState& state,
                                    const BrushSettings& settings,
                                    BrushRenderer& renderer, Layer* layer,
                                    unsigned int selectMaskTex,
                                    int selectMaskW, int selectMaskH,
                                    const QImage* selectMaskImage,
                                    const CloneStampContext* cloneContext)
{
    QPointF raw = state.imagePos;
    m_splineHistory.push_back(raw);
    if (m_splineHistory.size() > 64)
        m_splineHistory.erase(m_splineHistory.begin());

    int n = m_splineHistory.size();
    QPointF from = m_splineHistory[n - 2];

    const float fromPressure = m_lastPressure;
    const float toPressure = state.pressure;
    BrushInputState spacingProbe = state;
    spacingProbe.pressure = 0.5f * (fromPressure + toPressure);
    const float spacing = effectiveSpacing(settings, spacingProbe);
    QPointF diff = raw - from;
    float dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());

    // Stroke accumulators for the Distance/Time sensors (straight-line distance
    // approximates the spline arc; close enough for a sensor ramp).
    const float distStart = m_state.accumulatedDistance;
    const float tNow = m_strokeTimer.isValid()
        ? m_strokeTimer.elapsed() / 1000.0f : 0.0f;

    QPointF p0 = (n >= 4) ? m_splineHistory[n - 4] : from;
    QPointF p1 = (n >= 3) ? m_splineHistory[n - 3] : from;
    auto pointAt = [&](float t) {
        return (n >= 4) ? catmullRom(p0, p1, from, raw, t) : QPointF(from + diff * t);
    };

    // First dab of the stroke lands exactly at the start point.
    if (m_pendingFirstDab) {
        m_pendingFirstDab = false;
        m_distSinceDab = 0.0f;
        BrushInputState s = state;
        s.pressure = fromPressure;
        s.strokeDistance = distStart;
        s.strokeTime = tNow;
        uint32_t seed = m_dabSeed++;
        drawDynamicDab(from, s, settings, renderer, layer,
                       selectMaskTex, selectMaskW, selectMaskH, selectMaskImage, seed,
                       cloneContext);
    }

    // Emit dabs only when the travelled distance crosses the spacing step; the
    // remainder carries over to the next event, so dab spacing is independent
    // of the input event rate.
    float travelled = 0.0f;
    while (dist > 0.0f && m_distSinceDab + (dist - travelled) >= spacing) {
        travelled += spacing - m_distSinceDab;
        m_distSinceDab = 0.0f;
        const float t = travelled / dist;

        BrushInputState s = state;
        s.pressure = lerpf(fromPressure, toPressure, t);
        s.tiltX = lerpf(m_lastTiltX, state.tiltX, t);
        s.tiltY = lerpf(m_lastTiltY, state.tiltY, t);
        s.rotation = lerpf(m_lastRotation, state.rotation, t);
        s.strokeDistance = distStart + dist * t;
        s.strokeTime = tNow;
        uint32_t seed = m_dabSeed++;
        drawDynamicDab(pointAt(t), s, settings, renderer, layer,
                       selectMaskTex, selectMaskW, selectMaskH, selectMaskImage, seed,
                       cloneContext);
    }
    m_distSinceDab += dist - travelled;

    m_state.accumulatedDistance = distStart + dist;
    m_state.lastPoint = raw;
    m_lastPressure = state.pressure;
    m_lastTiltX = state.tiltX;
    m_lastTiltY = state.tiltY;
    m_lastRotation = state.rotation;
}

void BrushEngine::drawDabsBetween(QPointF from, QPointF to,
                                  const BrushInputState& state,
                                  const BrushSettings& settings,
                                  BrushRenderer& renderer, Layer* layer,
                                  unsigned int selectMaskTex,
                                  int selectMaskW, int selectMaskH,
                                  const QImage* selectMaskImage,
                                  const CloneStampContext* cloneContext)
{
    QPointF diff = to - from;
    float dist = std::sqrt(diff.x() * diff.x() + diff.y() * diff.y());

    // Pressure (and tilt) at the segment endpoints — the previous event's value
    // and this event's value — so each generated dab is sampled along the ramp.
    const float fromPressure = m_lastPressure;
    const float toPressure = state.pressure;
    BrushInputState spacingProbe = state;
    spacingProbe.pressure = 0.5f * (fromPressure + toPressure);
    const float spacing = effectiveSpacing(settings, spacingProbe);

    // Stroke accumulators for the Distance/Time sensors: distance ramps along
    // the segment from where the stroke left off; time is the segment's wall
    // clock (sub-event timing isn't tracked, so all dabs share it).
    const float distStart = m_state.accumulatedDistance;
    const float tNow = m_strokeTimer.isValid()
        ? m_strokeTimer.elapsed() / 1000.0f : 0.0f;

    // First dab of the stroke lands exactly at the start point.
    if (m_pendingFirstDab) {
        m_pendingFirstDab = false;
        m_distSinceDab = 0.0f;
        BrushInputState s = state;
        s.pressure = fromPressure;
        s.strokeDistance = distStart;
        s.strokeTime = tNow;
        uint32_t seed = m_dabSeed++;
        drawDynamicDab(from, s, settings, renderer, layer,
                       selectMaskTex, selectMaskW, selectMaskH, selectMaskImage, seed,
                       cloneContext);
    }

    // Emit dabs only when the travelled distance crosses the spacing step; the
    // remainder carries over to the next event. A stationary event (pressure
    // change with no movement) therefore paints nothing — timed dabs while
    // holding still are the airbrush option's job.
    float travelled = 0.0f;
    while (dist > 0.0f && m_distSinceDab + (dist - travelled) >= spacing) {
        travelled += spacing - m_distSinceDab;
        m_distSinceDab = 0.0f;
        const float t = travelled / dist;

        BrushInputState s = state;
        s.pressure = lerpf(fromPressure, toPressure, t);
        s.tiltX = lerpf(m_lastTiltX, state.tiltX, t);
        s.tiltY = lerpf(m_lastTiltY, state.tiltY, t);
        s.rotation = lerpf(m_lastRotation, state.rotation, t);
        s.strokeDistance = distStart + dist * t;
        s.strokeTime = tNow;
        uint32_t seed = m_dabSeed++;
        drawDynamicDab(from + diff * t, s, settings, renderer, layer,
                       selectMaskTex, selectMaskW, selectMaskH, selectMaskImage, seed,
                       cloneContext);
    }
    m_distSinceDab += dist - travelled;

    m_state.accumulatedDistance = distStart + dist;
}

void BrushEngine::strokeTo(const BrushInputState& state,
                           const BrushSettings& settings,
                           BrushRenderer& renderer, Layer* layer,
                           unsigned int selectMaskTex,
                           int selectMaskW, int selectMaskH,
                           const QImage* selectMaskImage)
{
    if (!m_active || !layer) return;

    if (settings.smoothingMode == SmoothingMode::CubicSpline) {
        strokeCubicSpline(state, settings, renderer, layer,
                          selectMaskTex, selectMaskW, selectMaskH, selectMaskImage);
        return;
    }

    QPointF raw = state.imagePos;
    QPointF smoothed;

    switch (settings.smoothingMode) {
    case SmoothingMode::Off:
        smoothed = raw;
        break;
    case SmoothingMode::PulledString:
        smoothed = smoothPulledString(raw, settings.smoothingRadius);
        break;
    default:
        smoothed = smoothBasic(raw);
        break;
    }

    drawDabsBetween(m_state.lastPoint, smoothed, state, settings,
                    renderer, layer,
                    selectMaskTex, selectMaskW, selectMaskH, selectMaskImage);

    m_state.lastPoint = smoothed;
    m_lastPressure = state.pressure;
    m_lastTiltX = state.tiltX;
    m_lastTiltY = state.tiltY;
    m_lastRotation = state.rotation;
}

void BrushEngine::strokeCloneTo(const BrushInputState& state,
                                const BrushSettings& settings,
                                BrushRenderer& renderer, Layer* layer,
                                unsigned int selectMaskTex,
                                int selectMaskW, int selectMaskH,
                                const CloneStampContext& cloneContext,
                                const QImage* selectMaskImage)
{
    if (!m_active || !layer || !cloneContext.isValid()) return;

    if (settings.smoothingMode == SmoothingMode::CubicSpline) {
        strokeCubicSpline(state, settings, renderer, layer,
                          selectMaskTex, selectMaskW, selectMaskH, selectMaskImage,
                          &cloneContext);
        return;
    }

    QPointF raw = state.imagePos;
    QPointF smoothed;

    switch (settings.smoothingMode) {
    case SmoothingMode::Off:
        smoothed = raw;
        break;
    case SmoothingMode::PulledString:
        smoothed = smoothPulledString(raw, settings.smoothingRadius);
        break;
    default:
        smoothed = smoothBasic(raw);
        break;
    }

    drawDabsBetween(m_state.lastPoint, smoothed, state, settings,
                    renderer, layer,
                    selectMaskTex, selectMaskW, selectMaskH, selectMaskImage,
                    &cloneContext);

    m_state.lastPoint = smoothed;
    m_lastPressure = state.pressure;
    m_lastTiltX = state.tiltX;
    m_lastTiltY = state.tiltY;
    m_lastRotation = state.rotation;
}

void BrushEngine::airbrushDab(const BrushInputState& state,
                              const BrushSettings& settings,
                              BrushRenderer& renderer, Layer* layer,
                              unsigned int selectMaskTex,
                              int selectMaskW, int selectMaskH,
                              const QImage* selectMaskImage)
{
    if (!m_active || !layer) return;
    uint32_t seed = m_dabSeed++;
    BrushInputState s = state;
    s.strokeDistance = m_state.accumulatedDistance;   // airbrush holds position
    s.strokeTime = m_strokeTimer.isValid() ? m_strokeTimer.elapsed() / 1000.0f : 0.0f;
    m_pendingFirstDab = false;
    drawDynamicDab(s.imagePos, s, settings, renderer, layer,
                   selectMaskTex, selectMaskW, selectMaskH, selectMaskImage, seed);
}

void BrushEngine::airbrushCloneDab(const BrushInputState& state,
                                   const BrushSettings& settings,
                                   BrushRenderer& renderer, Layer* layer,
                                   unsigned int selectMaskTex,
                                   int selectMaskW, int selectMaskH,
                                   const CloneStampContext& cloneContext,
                                   const QImage* selectMaskImage)
{
    if (!m_active || !layer || !cloneContext.isValid()) return;
    uint32_t seed = m_dabSeed++;
    BrushInputState s = state;
    s.strokeDistance = m_state.accumulatedDistance;   // airbrush holds position
    s.strokeTime = m_strokeTimer.isValid() ? m_strokeTimer.elapsed() / 1000.0f : 0.0f;
    m_pendingFirstDab = false;
    drawDynamicDab(s.imagePos, s, settings, renderer, layer,
                   selectMaskTex, selectMaskW, selectMaskH, selectMaskImage, seed,
                   &cloneContext);
}
