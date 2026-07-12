#pragma once

#include <map>

#include <QHash>
#include <QList>

#include "core/LayerId.hpp"
#include "AnimationTrack.hpp"
#include "AnimationTypes.hpp"
#include "RasterCelModel.hpp"

namespace anim {

// The document's animation data. It belongs to the Document, NOT to the UI or
// the renderers. It holds the timing/range settings and the sparse animation
// tracks, keyed by (LayerId, Property) so a node can be found by its stable id
// without any permanent pointer.
//
// This is a pure data model: no interpolation (Etapa 5), no frame evaluation
// (Etapa 6) and no serialization (Etapa 10) live here yet.
class AnimationModel {
public:
    // ── Timing / range ──────────────────────────────────────────────────────
    double fps() const { return m_fps; }
    void setFps(double v) { if (v > 0.0) m_fps = v; }

    Frame startFrame() const { return m_startFrame; }
    Frame endFrame() const { return m_endFrame; }
    void setFrameRange(Frame start, Frame end);

    Frame playbackStart() const { return m_playbackStart; }
    Frame playbackEnd() const { return m_playbackEnd; }
    void setPlaybackRange(Frame start, Frame end);

    // ── Tracks ──────────────────────────────────────────────────────────────
    using LayerTracks = std::map<Property, AnimationTrack>;

    bool hasAnyTracks() const;
    bool hasTracks(const LayerId& id) const;

    const AnimationTrack* track(const LayerId& id, Property p) const;
    AnimationTrack* track(const LayerId& id, Property p);

    // Return the track for (id, p), creating an empty one if absent.
    AnimationTrack& ensureTrack(const LayerId& id, Property p);

    // Remove one track; removes the layer entry if it becomes empty. Returns
    // true if a track was actually removed.
    bool removeTrack(const LayerId& id, Property p);
    void removeLayer(const LayerId& id);

    // The layers that own at least one track — the evaluator's fast path only
    // visits these, so nodes without animation cost nothing per frame.
    QList<LayerId> animatedLayers() const { return m_tracks.keys(); }
    const LayerTracks* tracksFor(const LayerId& id) const;

    // ── Raster cel animation (Etapa 13) ────────────────────────────────────
    bool hasRasterTrack(const LayerId& id) const;
    const RasterCelTrack* rasterTrack(const LayerId& id) const;
    RasterCelTrack* rasterTrack(const LayerId& id);
    RasterCelTrack& ensureRasterTrack(const LayerId& id);
    bool removeRasterTrack(const LayerId& id);
    QList<LayerId> rasterAnimatedLayers() const { return m_rasterTracks.keys(); }

    CelStorage& celStorage() { return m_celStorage; }
    const CelStorage& celStorage() const { return m_celStorage; }

    // Number of explicit frame keys that reference this cel across all layer
    // tracks. Used by the edit controller to decide when a duplicated frame
    // must detach its pixels before modification.
    int celReferenceCount(const CelId& id) const;

    // If `frame` is an explicit key sharing its CelId with another key, clone
    // the payload and repoint only this key. Returns the writable CelId, or null
    // when the frame is empty/held and no explicit cel exists there.
    CelId detachRasterCelForEdit(const LayerId& id, Frame frame);

    // Remove payloads no longer referenced by any raster track.
    void pruneUnusedCels();

    // ── Duplicate / copy-paste support (used by Etapa 10) ───────────────────
    // Copy every track from `from` onto `to` (overwriting `to`'s existing tracks
    // for the same properties). No-op if `from` has none.
    void copyTracks(const LayerId& from, const LayerId& to);
    // Move tracks from `from` to `to` (e.g. an id changed). No-op if none.
    void remapLayer(const LayerId& from, const LayerId& to);

    void clear();

private:
    double m_fps = 24.0;
    Frame m_startFrame = 0;
    Frame m_endFrame = 0;
    Frame m_playbackStart = 0;
    Frame m_playbackEnd = 0;

    QHash<LayerId, LayerTracks> m_tracks;
    QHash<LayerId, RasterCelTrack> m_rasterTracks;
    CelStorage m_celStorage;
};

} // namespace anim
