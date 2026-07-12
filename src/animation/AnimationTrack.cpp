#include "AnimationTrack.hpp"

#include <algorithm>

namespace anim {

int AnimationTrack::lowerBoundIndex(Frame f) const {
    auto it = std::lower_bound(
        m_keys.begin(), m_keys.end(), f,
        [](const Keyframe& k, Frame frame) { return k.frame < frame; });
    return static_cast<int>(it - m_keys.begin());
}

int AnimationTrack::setKeyframe(const Keyframe& kf) {
    const int i = lowerBoundIndex(kf.frame);
    if (i < static_cast<int>(m_keys.size()) && m_keys[i].frame == kf.frame) {
        m_keys[i] = kf;  // overwrite the existing keyframe at this frame
        return i;
    }
    m_keys.insert(m_keys.begin() + i, kf);
    return i;
}

int AnimationTrack::setKeyframe(Frame frame, const AnimationValue& value) {
    const int i = lowerBoundIndex(frame);
    if (i < static_cast<int>(m_keys.size()) && m_keys[i].frame == frame) {
        m_keys[i].value = value;  // keep the existing interpolation/tangents
        return i;
    }
    m_keys.insert(m_keys.begin() + i, Keyframe(frame, value, m_defaultInterp));
    return i;
}

bool AnimationTrack::removeKeyframe(Frame frame) {
    const int i = lowerBoundIndex(frame);
    if (i < static_cast<int>(m_keys.size()) && m_keys[i].frame == frame) {
        m_keys.erase(m_keys.begin() + i);
        return true;
    }
    return false;
}

bool AnimationTrack::hasKeyframeAt(Frame frame) const {
    const int i = lowerBoundIndex(frame);
    return i < static_cast<int>(m_keys.size()) && m_keys[i].frame == frame;
}

const Keyframe* AnimationTrack::keyframeAt(Frame frame) const {
    const int i = lowerBoundIndex(frame);
    if (i < static_cast<int>(m_keys.size()) && m_keys[i].frame == frame)
        return &m_keys[i];
    return nullptr;
}

int AnimationTrack::indexAtOrBefore(Frame f) const {
    const int i = lowerBoundIndex(f);
    if (i < static_cast<int>(m_keys.size()) && m_keys[i].frame == f)
        return i;       // exact hit
    return i - 1;       // last key strictly before f (-1 if none)
}

bool AnimationTrack::bracket(Frame f, const Keyframe*& lo, const Keyframe*& hi) const {
    if (m_keys.empty())
        return false;
    const int i = lowerBoundIndex(f);  // first key with frame >= f
    if (i == 0) {                      // f is at/before the first key
        lo = hi = &m_keys.front();
        return true;
    }
    if (i >= static_cast<int>(m_keys.size())) {  // f is after the last key
        lo = hi = &m_keys.back();
        return true;
    }
    if (m_keys[i].frame == f) {  // exactly on a key
        lo = hi = &m_keys[i];
        return true;
    }
    lo = &m_keys[i - 1];         // frame < f
    hi = &m_keys[i];             // frame > f
    return true;
}

} // namespace anim
