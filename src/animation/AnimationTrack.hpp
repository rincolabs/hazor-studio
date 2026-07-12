#pragma once

#include <vector>

#include "AnimationKeyframe.hpp"
#include "AnimationTypes.hpp"

namespace anim {

// The sparse, sorted set of keyframes for ONE (layer, property). Frames are
// unique and the vector is kept ordered, so lookups and interpolation brackets
// use binary search. Interpolation of the values themselves lives in a later
// stage; this class only owns and organizes the keyframes.
class AnimationTrack {
public:
    AnimationTrack() = default;
    explicit AnimationTrack(Property p)
        : m_property(p), m_defaultInterp(anim::defaultInterpolation(p)) {}

    Property property() const { return m_property; }
    ValueType valueType() const { return anim::valueType(m_property); }
    Interpolation defaultInterpolation() const { return m_defaultInterp; }
    void setDefaultInterpolation(Interpolation i) { m_defaultInterp = i; }

    bool isEmpty() const { return m_keys.empty(); }
    int count() const { return static_cast<int>(m_keys.size()); }
    const std::vector<Keyframe>& keyframes() const { return m_keys; }

    // Insert or overwrite the keyframe at `frame`, keeping the track sorted and
    // frame-unique. Returns the index of the affected keyframe.
    int setKeyframe(Frame frame, const AnimationValue& value);
    int setKeyframe(const Keyframe& kf);

    bool removeKeyframe(Frame frame);

    bool hasKeyframeAt(Frame frame) const;
    const Keyframe* keyframeAt(Frame frame) const;  // exact frame, else nullptr

    // Index of the last keyframe with frame <= f, or -1 if none. Binary search.
    int indexAtOrBefore(Frame f) const;

    // Fill lo/hi with the keyframes bracketing `f` for interpolation:
    //   - before the first key: lo == hi == &first
    //   - after the last key:   lo == hi == &last
    //   - otherwise lo->frame <= f < hi->frame (or lo == hi when exactly on a key)
    // Returns false only when the track is empty.
    bool bracket(Frame f, const Keyframe*& lo, const Keyframe*& hi) const;

    Frame firstFrame() const { return m_keys.empty() ? 0 : m_keys.front().frame; }
    Frame lastFrame() const { return m_keys.empty() ? 0 : m_keys.back().frame; }

private:
    // First index whose frame >= f (std::lower_bound over frames).
    int lowerBoundIndex(Frame f) const;

    Property m_property = Property::Opacity;
    Interpolation m_defaultInterp = Interpolation::Linear;
    std::vector<Keyframe> m_keys;  // sorted by frame, unique frames
};

} // namespace anim
