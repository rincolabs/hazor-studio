#pragma once

#include "AnimationTrack.hpp"
#include "AnimationTypes.hpp"
#include "AnimationValue.hpp"

namespace anim {

// Sample a track at `frame`.
//   - Float tracks interpolate per the leaving keyframe's interpolation:
//       Linear -> lerp; Hold -> step; Bezier -> Linear for now (reserved).
//   - Bool / Enum tracks always hold (a discrete value cannot be blended).
//   - Before the first / after the last keyframe the endpoint value holds.
// The track must be non-empty (the evaluator only samples non-empty tracks); an
// empty track yields a default-constructed value.
AnimationValue sampleTrack(const AnimationTrack& track, Frame frame);

// Plain float lerp, exposed for reuse/testing. t is NOT clamped by the caller
// here because `sampleTrack` only ever passes t in [0,1].
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

} // namespace anim
