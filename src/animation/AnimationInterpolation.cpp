#include "AnimationInterpolation.hpp"

namespace anim {

AnimationValue sampleTrack(const AnimationTrack& track, Frame frame) {
    const Keyframe* lo = nullptr;
    const Keyframe* hi = nullptr;
    if (!track.bracket(frame, lo, hi))
        return AnimationValue();  // empty track

    // Endpoints (before first / after last) and exact-key hits hold the value.
    if (lo == hi)
        return lo->value;

    // Discrete properties (visibility, blend mode) never interpolate.
    if (track.valueType() != ValueType::Float)
        return lo->value;

    // Step interpolation holds until the next keyframe.
    if (lo->interpolation == Interpolation::Hold)
        return lo->value;

    // Linear (and Bezier, which reuses linear until the curve editor exists).
    const float span = static_cast<float>(hi->frame - lo->frame);
    const float t = span > 0.0f
        ? static_cast<float>(frame - lo->frame) / span
        : 0.0f;
    return AnimationValue(lerp(lo->value.asFloat(), hi->value.asFloat(), t));
}

} // namespace anim
