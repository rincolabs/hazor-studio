#pragma once

#include "BrushTypes.hpp"
#include "BrushInputState.hpp"
#include "DynamicsEvaluator.hpp"
#include <QImage>
#include <QElapsedTimer>
#include <vector>

class Layer;
class BrushRenderer;

class BrushEngine {
public:
    void beginStroke(const BrushInputState& state);
    void beginStroke(const BrushInputState& state, bool targetMask);
    void strokeTo(const BrushInputState& state, const BrushSettings& settings,
                  BrushRenderer& renderer, Layer* layer,
                  unsigned int selectMaskTex, int selectMaskW, int selectMaskH,
                  const QImage* selectMaskImage = nullptr);
    void strokeCloneTo(const BrushInputState& state, const BrushSettings& settings,
                       BrushRenderer& renderer, Layer* layer,
                       unsigned int selectMaskTex, int selectMaskW, int selectMaskH,
                       const CloneStampContext& cloneContext,
                       const QImage* selectMaskImage = nullptr);
    void airbrushDab(const BrushInputState& state, const BrushSettings& settings,
                     BrushRenderer& renderer, Layer* layer,
                     unsigned int selectMaskTex, int selectMaskW, int selectMaskH,
                     const QImage* selectMaskImage = nullptr);
    void airbrushCloneDab(const BrushInputState& state, const BrushSettings& settings,
                          BrushRenderer& renderer, Layer* layer,
                          unsigned int selectMaskTex, int selectMaskW, int selectMaskH,
                          const CloneStampContext& cloneContext,
                          const QImage* selectMaskImage = nullptr);
    void endStroke();

    bool isActive() const { return m_active; }

    // Global minimum pressure floor (0..1). Applied to every dab's pressure before the
    // dynamics, on top of whatever the active preset does, so the pen never registers
    // below this — a global override that takes priority over the preset.
    void setMinPressure(float p) { m_minPressure = p < 0.0f ? 0.0f : (p > 1.0f ? 1.0f : p); }
    float minPressure() const { return m_minPressure; }

private:
    QPointF smoothBasic(QPointF raw);
    QPointF smoothPulledString(QPointF raw, float radius);
    void strokeCubicSpline(const BrushInputState& state,
                           const BrushSettings& settings,
                           BrushRenderer& renderer, Layer* layer,
                           unsigned int selectMaskTex, int selectMaskW, int selectMaskH,
                           const QImage* selectMaskImage,
                           const CloneStampContext* cloneContext = nullptr);
    QPointF catmullRom(QPointF p0, QPointF p1, QPointF p2, QPointF p3, float t);
    void drawDabsBetween(QPointF from, QPointF to,
                         const BrushInputState& state,
                         const BrushSettings& settings,
                         BrushRenderer& renderer, Layer* layer,
                         unsigned int selectMaskTex, int selectMaskW, int selectMaskH,
                         const QImage* selectMaskImage,
                         const CloneStampContext* cloneContext = nullptr);
    void drawDynamicDab(QPointF imagePos, const BrushInputState& state,
                        const BrushSettings& settings,
                        BrushRenderer& renderer, Layer* layer,
                        unsigned int selectMaskTex, int selectMaskW, int selectMaskH,
                        const QImage* selectMaskImage,
                        uint32_t& seed,
                        const CloneStampContext* cloneContext = nullptr);

    bool m_active = false;
    bool m_targetMask = false;
    StrokeState m_state;

    // Spacing accumulator: dabs are emitted only when the travelled distance
    // crosses the spacing step, with the remainder carried across input events.
    // The first dab of a stroke is pending until the first strokeTo().
    bool m_pendingFirstDab = false;
    float m_distSinceDab = 0.0f;
    // Dab sequence number within the stroke (drives the Fade sensor).
    int m_dabCount = 0;

    // Last received input sample's pressure/tilt, so the dabs the engine
    // generates between two events get a value interpolated along the segment
    // rather than a single end-of-segment value. Mouse strokes keep these at
    // 1.0/0.0 so nothing changes.
    float m_lastPressure = 1.0f;
    float m_lastTiltX = 0.0f;
    float m_lastTiltY = 0.0f;
    float m_lastRotation = 0.0f;

    QPointF m_stringPos;
    std::vector<QPointF> m_splineHistory;
    uint32_t m_dabSeed = 0;

    // Per-stroke fuzzy: m_strokeRandom is regenerated once per beginStroke() from the
    // long-lived m_strokeRng, then carried by every dab of that stroke.
    uint32_t m_strokeRng = 0x9e3779b9u;
    float m_strokeRandom = 0.5f;

    // Global minimum-pressure floor (see setMinPressure).
    float m_minPressure = 0.0f;

    // Wall-clock since beginStroke(), so the Layer-B Time sensor reads real elapsed
    // time. m_state.accumulatedDistance tracks the companion stroke distance.
    QElapsedTimer m_strokeTimer;

    static constexpr float PI = 3.14159265358979323846f;
};
