#pragma once

#include "BrushTypes.hpp"
#include "BrushInputState.hpp"
#include "DynamicsConfig.hpp"
#include <cstdint>

class DynamicsEvaluator {
public:
    static DynamicDabParams evaluate(
        const BrushSettings& settings,
        const BrushInputState& input,
        uint32_t seed);

    static float randomFloat(uint32_t& seed);

    // Snapshot the raw input axes (normalized) into the per-dab context the
    // sensors read. `seed` seeds the Fuzzy sensor's per-dab RNG. Public so the
    // stroke orchestrator can evaluate a single option (e.g. size for spacing)
    // without running the full dab evaluation.
    static DabContext makeContext(const BrushInputState& input, uint32_t seed);

private:
    static QPointF scatterOffset(const BrushSettings& settings,
                                 const DabContext& ctx,
                                 float diameter, uint32_t& seed);
    static QColor applyColorDynamics(const QColor& base, const ColorDynamics& cd,
                                     uint32_t& seed);
};
