#pragma once

#include "AnimationTypes.hpp"
#include "AnimationValue.hpp"

namespace anim {

// One keyframe: a value pinned to an integer frame, plus the interpolation used
// on the segment leaving it. Keyframes are sparse — a track stores only the
// frames the user set, never one per frame.
struct Keyframe {
    Frame frame = 0;
    AnimationValue value;
    Interpolation interpolation = Interpolation::Linear;

    // Bézier tangents (reserved for Etapa 5). Kept here so the on-disk and
    // in-memory shape is already curve-ready; unused by Linear/Hold.
    float inTangentX = 0.0f, inTangentY = 0.0f;
    float outTangentX = 0.0f, outTangentY = 0.0f;

    Keyframe() = default;
    Keyframe(Frame f, AnimationValue v, Interpolation i = Interpolation::Linear)
        : frame(f), value(v), interpolation(i) {}
};

} // namespace anim
