#include "AnimationModel.hpp"

#include <algorithm>
#include <utility>

#include <QSet>

namespace anim {

bool AnimationModel::hasAnyTracks() const {
    if (!m_tracks.isEmpty())
        return true;
    for (auto it = m_rasterTracks.constBegin(); it != m_rasterTracks.constEnd(); ++it) {
        if (!it->isEmpty())
            return true;
    }
    return false;
}

void AnimationModel::setFrameRange(Frame start, Frame end) {
    if (end < start)
        std::swap(start, end);
    m_startFrame = start;
    m_endFrame = end;
    // Keep the playback range inside the document range.
    m_playbackStart = std::max(m_playbackStart, m_startFrame);
    m_playbackEnd = std::min(m_playbackEnd < m_playbackStart ? m_endFrame : m_playbackEnd,
                             m_endFrame);
    if (m_playbackEnd < m_playbackStart)
        m_playbackEnd = m_playbackStart;
}

void AnimationModel::setPlaybackRange(Frame start, Frame end) {
    if (end < start)
        std::swap(start, end);
    m_playbackStart = start;
    m_playbackEnd = end;
}

bool AnimationModel::hasTracks(const LayerId& id) const {
    auto it = m_tracks.constFind(id);
    return it != m_tracks.constEnd() && !it->empty();
}

const AnimationTrack* AnimationModel::track(const LayerId& id, Property p) const {
    auto it = m_tracks.constFind(id);
    if (it == m_tracks.constEnd())
        return nullptr;
    auto pit = it->find(p);
    return pit == it->end() ? nullptr : &pit->second;
}

AnimationTrack* AnimationModel::track(const LayerId& id, Property p) {
    auto it = m_tracks.find(id);
    if (it == m_tracks.end())
        return nullptr;
    auto pit = it->find(p);
    return pit == it->end() ? nullptr : &pit->second;
}

AnimationTrack& AnimationModel::ensureTrack(const LayerId& id, Property p) {
    LayerTracks& tracks = m_tracks[id];
    auto pit = tracks.find(p);
    if (pit == tracks.end())
        pit = tracks.emplace(p, AnimationTrack(p)).first;
    return pit->second;
}

bool AnimationModel::removeTrack(const LayerId& id, Property p) {
    auto it = m_tracks.find(id);
    if (it == m_tracks.end())
        return false;
    const bool erased = it->erase(p) > 0;
    if (it->empty())
        m_tracks.erase(it);
    return erased;
}

void AnimationModel::removeLayer(const LayerId& id) {
    m_tracks.remove(id);
    m_rasterTracks.remove(id);
    pruneUnusedCels();
}

const AnimationModel::LayerTracks* AnimationModel::tracksFor(const LayerId& id) const {
    auto it = m_tracks.constFind(id);
    return it == m_tracks.constEnd() ? nullptr : &it.value();
}

bool AnimationModel::hasRasterTrack(const LayerId& id) const {
    auto it = m_rasterTracks.constFind(id);
    return it != m_rasterTracks.constEnd() && !it->isEmpty();
}

const RasterCelTrack* AnimationModel::rasterTrack(const LayerId& id) const {
    auto it = m_rasterTracks.constFind(id);
    return it == m_rasterTracks.constEnd() ? nullptr : &it.value();
}

RasterCelTrack* AnimationModel::rasterTrack(const LayerId& id) {
    auto it = m_rasterTracks.find(id);
    return it == m_rasterTracks.end() ? nullptr : &it.value();
}

RasterCelTrack& AnimationModel::ensureRasterTrack(const LayerId& id) {
    return m_rasterTracks[id];
}

bool AnimationModel::removeRasterTrack(const LayerId& id) {
    const bool removed = m_rasterTracks.remove(id) > 0;
    if (removed)
        pruneUnusedCels();
    return removed;
}

int AnimationModel::celReferenceCount(const CelId& id) const {
    if (id.isNull())
        return 0;
    int count = 0;
    for (auto it = m_rasterTracks.constBegin(); it != m_rasterTracks.constEnd(); ++it) {
        for (const auto& [_, celId] : it.value().keyframes()) {
            if (celId && *celId == id)
                ++count;
        }
    }
    return count;
}

CelId AnimationModel::detachRasterCelForEdit(const LayerId& id, Frame frame) {
    RasterCelTrack* track = rasterTrack(id);
    if (!track || !track->hasKeyframe(frame))
        return {};
    const std::optional<CelId> current = track->keyframeCel(frame);
    if (!current || !m_celStorage.contains(*current))
        return {};
    if (celReferenceCount(*current) <= 1)
        return *current;

    const CelId detached = m_celStorage.cloneCel(*current);
    if (!detached.isNull())
        track->setCel(frame, detached);
    return detached;
}

void AnimationModel::pruneUnusedCels() {
    QSet<CelId> referenced;
    for (auto it = m_rasterTracks.constBegin(); it != m_rasterTracks.constEnd(); ++it) {
        for (const auto& [_, celId] : it.value().keyframes()) {
            if (celId)
                referenced.insert(*celId);
        }
    }
    for (const CelId& id : m_celStorage.ids()) {
        if (!referenced.contains(id))
            m_celStorage.removeCel(id);
    }
}

void AnimationModel::copyTracks(const LayerId& from, const LayerId& to) {
    auto it = m_tracks.constFind(from);
    if (it != m_tracks.constEnd() && !it->empty()) {
        LayerTracks& dst = m_tracks[to];
        for (const auto& [prop, track] : it.value())
            dst[prop] = track;
    }
    auto rasterIt = m_rasterTracks.constFind(from);
    if (rasterIt != m_rasterTracks.constEnd() && !rasterIt->isEmpty())
        m_rasterTracks[to] = rasterIt.value();
}

void AnimationModel::remapLayer(const LayerId& from, const LayerId& to) {
    auto it = m_tracks.find(from);
    if (it != m_tracks.end()) {
        m_tracks[to] = std::move(it.value());
        m_tracks.erase(it);
    }
    auto rasterIt = m_rasterTracks.find(from);
    if (rasterIt != m_rasterTracks.end()) {
        m_rasterTracks[to] = std::move(rasterIt.value());
        m_rasterTracks.erase(rasterIt);
    }
}

void AnimationModel::clear() {
    m_tracks.clear();
    m_rasterTracks.clear();
    m_celStorage.clear();
    m_fps = 24.0;
    m_startFrame = m_endFrame = 0;
    m_playbackStart = m_playbackEnd = 0;
}

} // namespace anim
