#pragma once

#include <QString>
#include <QTransform>

#include "AnimationTypes.hpp"

class Document;
class LayerTreeNode;
class CommandHistory;
enum class BlendMode;

namespace anim {

class AnimationValue;

// The single place where animatable properties are edited (Etapa 8). Tools and
// panels route their edits here instead of each implementing its own auto-key
// rule. It decides — from whether the property is already animated and the
// auto-key state — between a base-value edit and a keyframe, and emits the
// matching undo command (Etapa 9). It never consults a "global frame" from
// inside a node setter; the current frame is passed in explicitly.
//
// Decision matrix for an edit of (node, property) at `frame`:
//   * property has a track  -> keyframe at `frame` (update if present, else add),
//                              regardless of auto-key (an animated property's
//                              edit must stick; a base edit would be invisible).
//   * no track, auto-key ON  -> create track + keyframe at `frame`.
//   * no track, auto-key OFF -> NOT handled here: the caller performs its normal
//                              base-value edit, so static documents are unchanged.
//
// The scalar edit methods return true when they handled the edit as a keyframe
// (the caller must then skip its base edit) and false when the caller should do
// its existing base-value edit.
class LayerPropertyController {
public:
    LayerPropertyController() = default;

    // Rebound whenever the active document changes; history is stable.
    void setContext(Document* doc, CommandHistory* history) {
        m_doc = doc;
        m_history = history;
    }

    bool autoKey() const { return m_autoKey; }
    void setAutoKey(bool on) { m_autoKey = on; }

    bool editOpacity(LayerTreeNode* node, float value, Frame frame);
    bool editVisibility(LayerTreeNode* node, bool value, Frame frame);
    bool editBlendMode(LayerTreeNode* node, BlendMode value, Frame frame);

    // Decomposes `newTransform` and keyframes all transform components
    // (position x/y, scale x/y, rotation) at `frame` as one undo group. Returns
    // true if handled as keyframes; false => caller does its base transform edit.
    bool editTransform(LayerTreeNode* node, const QTransform& newTransform, Frame frame);

private:
    bool editScalar(LayerTreeNode* node, Property prop, const AnimationValue& value,
                    Frame frame, const QString& name);
    bool shouldKeyframe(const LayerTreeNode* node, Property prop) const;
    void pushKeyframe(const LayerTreeNode* node, Property prop, Frame frame,
                      const AnimationValue& value, const QString& name);

    Document* m_doc = nullptr;
    CommandHistory* m_history = nullptr;
    bool m_autoKey = false;
};

} // namespace anim
