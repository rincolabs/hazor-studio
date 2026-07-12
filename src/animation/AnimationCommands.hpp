#pragma once

#include <QString>
#include <utility>

#include "core/Document.hpp"
#include "controller/CommandHistory.hpp"
#include "AnimationModel.hpp"
#include "AnimationTrack.hpp"
#include "AnimationTypes.hpp"
#include "AnimationValue.hpp"

// Undo/redo commands for the animation model (Etapa 9). They integrate with the
// existing CommandHistory (no parallel undo system) and only touch
// Document::animation, keyed by the stable LayerId (so they survive tree
// reorganization). After each mutation they re-run the evaluator at the current
// frame via Document::setCurrentFrame(currentFrame()), which updates the
// evaluated state and bumps compositionGeneration — exactly the existing
// invalidation path. They never touch the base LayerNodeState.
namespace anim {

// Coarse-grained state command used for compound raster-cel operations. Cel
// storage copy construction is deep, so later pixel edits cannot mutate the
// undo snapshot through shared QImage buffers.
class AnimationModelStateCommand : public Command {
public:
    AnimationModelStateCommand(Document* doc, AnimationModel before,
                               AnimationModel after, QString label)
        : m_doc(doc), m_before(std::move(before)), m_after(std::move(after)),
          m_label(std::move(label)) {}

    void execute() override {
        if (!m_doc) return;
        m_doc->animation = m_after;
        applyEvaluation();
    }
    void undo() override {
        if (!m_doc) return;
        m_doc->animation = m_before;
        applyEvaluation();
    }
    QString name() const override { return m_label; }

private:
    void applyEvaluation() {
        for (auto* node : m_doc->flatten()) {
            if (node && node->layer
                && !m_doc->animation.hasRasterTrack(node->id))
                node->layer->clearEvaluatedRasterContent();
        }
        m_doc->setCurrentFrame(m_doc->currentFrame());
        ++m_doc->compositionGeneration;
    }
    Document* m_doc = nullptr;
    AnimationModel m_before;
    AnimationModel m_after;
    QString m_label;
};

// Re-evaluate the current frame after a model change (clamp + evaluate + bump
// composition generation). Cheap for static regions; only animated nodes cost.
inline void reevaluateDoc(Document* doc) {
    if (doc)
        doc->setCurrentFrame(doc->currentFrame());
}

// Add or update the keyframe at `frame` for (layer, property). Creates the track
// if needed. This single command covers AddKeyframe and ChangeKeyframeValue.
class SetKeyframeCommand : public Command {
public:
    SetKeyframeCommand(Document* doc, LayerId layer, Property prop,
                       Frame frame, AnimationValue value, QString name = {})
        : m_doc(doc), m_layer(std::move(layer)), m_prop(prop),
          m_frame(frame), m_newValue(value),
          m_name(name.isEmpty() ? QStringLiteral("Set Keyframe") : std::move(name))
    {
        // Capture the pre-state for undo (construct-then-execute contract).
        const AnimationTrack* t = m_doc ? m_doc->animation.track(m_layer, m_prop) : nullptr;
        m_trackExisted = (t != nullptr);
        if (const Keyframe* k = t ? t->keyframeAt(m_frame) : nullptr) {
            m_keyExisted = true;
            m_oldValue = k->value;
        }
    }

    void execute() override {
        if (!m_doc) return;
        m_doc->animation.ensureTrack(m_layer, m_prop).setKeyframe(m_frame, m_newValue);
        reevaluateDoc(m_doc);
    }

    void undo() override {
        if (!m_doc) return;
        if (m_keyExisted) {
            if (AnimationTrack* t = m_doc->animation.track(m_layer, m_prop))
                t->setKeyframe(m_frame, m_oldValue);
        } else {
            if (AnimationTrack* t = m_doc->animation.track(m_layer, m_prop))
                t->removeKeyframe(m_frame);
            if (!m_trackExisted)
                m_doc->animation.removeTrack(m_layer, m_prop);  // track we created
        }
        reevaluateDoc(m_doc);
    }

    QString name() const override { return m_name; }

private:
    Document* m_doc;
    LayerId m_layer;
    Property m_prop;
    Frame m_frame;
    AnimationValue m_newValue;
    bool m_trackExisted = false;
    bool m_keyExisted = false;
    AnimationValue m_oldValue;
    QString m_name;
};

// Remove the keyframe at `frame`. Restores it (and the track, if this was the
// last keyframe) on undo.
class RemoveKeyframeCommand : public Command {
public:
    RemoveKeyframeCommand(Document* doc, LayerId layer, Property prop, Frame frame)
        : m_doc(doc), m_layer(std::move(layer)), m_prop(prop), m_frame(frame)
    {
        const AnimationTrack* t = m_doc ? m_doc->animation.track(m_layer, m_prop) : nullptr;
        if (const Keyframe* k = t ? t->keyframeAt(m_frame) : nullptr) {
            m_had = true;
            m_key = *k;
            m_wasLast = (t->count() == 1);
        }
    }

    void execute() override {
        if (!m_doc || !m_had) return;
        if (AnimationTrack* t = m_doc->animation.track(m_layer, m_prop)) {
            t->removeKeyframe(m_frame);
            if (t->isEmpty())
                m_doc->animation.removeTrack(m_layer, m_prop);
        }
        reevaluateDoc(m_doc);
    }

    void undo() override {
        if (!m_doc || !m_had) return;
        m_doc->animation.ensureTrack(m_layer, m_prop).setKeyframe(m_key);
        reevaluateDoc(m_doc);
    }

    QString name() const override { return QStringLiteral("Remove Keyframe"); }

private:
    Document* m_doc;
    LayerId m_layer;
    Property m_prop;
    Frame m_frame;
    bool m_had = false;
    bool m_wasLast = false;
    Keyframe m_key;
};

// Move a keyframe from `fromFrame` to `toFrame` (value/interpolation preserved).
// If `toFrame` was occupied, that keyframe is overwritten and restored on undo.
class MoveKeyframeCommand : public Command {
public:
    MoveKeyframeCommand(Document* doc, LayerId layer, Property prop,
                        Frame fromFrame, Frame toFrame)
        : m_doc(doc), m_layer(std::move(layer)), m_prop(prop),
          m_from(fromFrame), m_to(toFrame)
    {
        const AnimationTrack* t = m_doc ? m_doc->animation.track(m_layer, m_prop) : nullptr;
        if (const Keyframe* k = t ? t->keyframeAt(m_from) : nullptr) {
            m_valid = true;
            m_moved = *k;
        }
        if (const Keyframe* k = t ? t->keyframeAt(m_to) : nullptr) {
            m_overwrote = true;
            m_overwritten = *k;
        }
    }

    void execute() override {
        if (!m_doc || !m_valid || m_from == m_to) return;
        if (AnimationTrack* t = m_doc->animation.track(m_layer, m_prop)) {
            Keyframe k = m_moved;
            k.frame = m_to;
            t->removeKeyframe(m_from);
            t->setKeyframe(k);
        }
        reevaluateDoc(m_doc);
    }

    void undo() override {
        if (!m_doc || !m_valid || m_from == m_to) return;
        if (AnimationTrack* t = m_doc->animation.track(m_layer, m_prop)) {
            t->removeKeyframe(m_to);
            t->setKeyframe(m_moved);                       // back at m_from
            if (m_overwrote)
                t->setKeyframe(m_overwritten);             // restore clobbered key
        }
        reevaluateDoc(m_doc);
    }

    QString name() const override { return QStringLiteral("Move Keyframe"); }

private:
    Document* m_doc;
    LayerId m_layer;
    Property m_prop;
    Frame m_from, m_to;
    bool m_valid = false;
    bool m_overwrote = false;
    Keyframe m_moved, m_overwritten;
};

// Change the interpolation of an existing keyframe.
class ChangeKeyframeInterpolationCommand : public Command {
public:
    ChangeKeyframeInterpolationCommand(Document* doc, LayerId layer, Property prop,
                                       Frame frame, Interpolation interp)
        : m_doc(doc), m_layer(std::move(layer)), m_prop(prop),
          m_frame(frame), m_new(interp)
    {
        const AnimationTrack* t = m_doc ? m_doc->animation.track(m_layer, m_prop) : nullptr;
        if (const Keyframe* k = t ? t->keyframeAt(m_frame) : nullptr) {
            m_valid = true;
            m_old = k->interpolation;
        }
    }

    void execute() override { apply(m_new); }
    void undo() override { apply(m_old); }
    QString name() const override { return QStringLiteral("Change Interpolation"); }

private:
    void apply(Interpolation interp) {
        if (!m_doc || !m_valid) return;
        if (AnimationTrack* t = m_doc->animation.track(m_layer, m_prop)) {
            if (const Keyframe* k = t->keyframeAt(m_frame)) {
                Keyframe nk = *k;
                nk.interpolation = interp;
                t->setKeyframe(nk);
            }
        }
        reevaluateDoc(m_doc);
    }
    Document* m_doc;
    LayerId m_layer;
    Property m_prop;
    Frame m_frame;
    Interpolation m_new, m_old = Interpolation::Linear;
    bool m_valid = false;
};

// Create an (empty) track for a property. Undo removes it.
class CreateTrackCommand : public Command {
public:
    CreateTrackCommand(Document* doc, LayerId layer, Property prop)
        : m_doc(doc), m_layer(std::move(layer)), m_prop(prop)
    {
        m_existed = m_doc && m_doc->animation.track(m_layer, m_prop) != nullptr;
    }
    void execute() override {
        if (m_doc && !m_existed) m_doc->animation.ensureTrack(m_layer, m_prop);
        reevaluateDoc(m_doc);
    }
    void undo() override {
        if (m_doc && !m_existed) m_doc->animation.removeTrack(m_layer, m_prop);
        reevaluateDoc(m_doc);
    }
    QString name() const override { return QStringLiteral("Create Track"); }
private:
    Document* m_doc;
    LayerId m_layer;
    Property m_prop;
    bool m_existed = false;
};

// Remove a track (and all its keyframes). Undo restores the whole track.
class RemoveTrackCommand : public Command {
public:
    RemoveTrackCommand(Document* doc, LayerId layer, Property prop)
        : m_doc(doc), m_layer(std::move(layer)), m_prop(prop)
    {
        if (m_doc)
            if (const AnimationTrack* t = m_doc->animation.track(m_layer, m_prop)) {
                m_had = true;
                m_saved = *t;
            }
    }
    void execute() override {
        if (m_doc && m_had) m_doc->animation.removeTrack(m_layer, m_prop);
        reevaluateDoc(m_doc);
    }
    void undo() override {
        if (m_doc && m_had)
            m_doc->animation.ensureTrack(m_layer, m_prop) = m_saved;
        reevaluateDoc(m_doc);
    }
    QString name() const override { return QStringLiteral("Remove Track"); }
private:
    Document* m_doc;
    LayerId m_layer;
    Property m_prop;
    bool m_had = false;
    AnimationTrack m_saved;
};

// Change the document + playback frame ranges.
class ChangeAnimationRangeCommand : public Command {
public:
    ChangeAnimationRangeCommand(Document* doc, Frame start, Frame end,
                                Frame playStart, Frame playEnd)
        : m_doc(doc), m_newStart(start), m_newEnd(end),
          m_newPlayStart(playStart), m_newPlayEnd(playEnd)
    {
        if (m_doc) {
            m_oldStart = m_doc->animation.startFrame();
            m_oldEnd = m_doc->animation.endFrame();
            m_oldPlayStart = m_doc->animation.playbackStart();
            m_oldPlayEnd = m_doc->animation.playbackEnd();
        }
    }
    void execute() override { apply(m_newStart, m_newEnd, m_newPlayStart, m_newPlayEnd); }
    void undo() override { apply(m_oldStart, m_oldEnd, m_oldPlayStart, m_oldPlayEnd); }
    QString name() const override { return QStringLiteral("Change Animation Range"); }
private:
    void apply(Frame s, Frame e, Frame ps, Frame pe) {
        if (!m_doc) return;
        m_doc->animation.setFrameRange(s, e);
        m_doc->animation.setPlaybackRange(ps, pe);
        reevaluateDoc(m_doc);  // re-clamps currentFrame into the new range
    }
    Document* m_doc;
    Frame m_newStart, m_newEnd, m_newPlayStart, m_newPlayEnd;
    Frame m_oldStart = 0, m_oldEnd = 0, m_oldPlayStart = 0, m_oldPlayEnd = 0;
};

// Change the frame rate (fps).
class ChangeFrameRateCommand : public Command {
public:
    ChangeFrameRateCommand(Document* doc, double fps)
        : m_doc(doc), m_new(fps)
    {
        if (m_doc) m_old = m_doc->animation.fps();
    }
    void execute() override { if (m_doc) { m_doc->animation.setFps(m_new); reevaluateDoc(m_doc); } }
    void undo() override { if (m_doc) { m_doc->animation.setFps(m_old); reevaluateDoc(m_doc); } }
    QString name() const override { return QStringLiteral("Change Frame Rate"); }
private:
    Document* m_doc;
    double m_new, m_old = 24.0;
};

} // namespace anim
