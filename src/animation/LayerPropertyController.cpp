#include "LayerPropertyController.hpp"

#include <algorithm>
#include <memory>

#include "core/Document.hpp"
#include "core/Layer.hpp"           // BlendMode
#include "core/LayerTreeNode.hpp"
#include "controller/CommandHistory.hpp"
#include "AnimationCommands.hpp"
#include "AnimationModel.hpp"
#include "AnimationTransform.hpp"
#include "AnimationValue.hpp"

namespace anim {

bool LayerPropertyController::shouldKeyframe(const LayerTreeNode* node, Property prop) const {
    if (!m_doc || !node)
        return false;
    if (m_doc->animation.track(node->id, prop) != nullptr)
        return true;         // already animated → edits keyframe
    return m_autoKey;        // otherwise only when auto-key is on
}

void LayerPropertyController::pushKeyframe(const LayerTreeNode* node, Property prop,
                                           Frame frame, const AnimationValue& value,
                                           const QString& name) {
    auto cmd = std::make_unique<SetKeyframeCommand>(m_doc, node->id, prop, frame, value, name);
    cmd->execute();                       // apply now (push() does not execute)
    m_history->push(std::move(cmd));      // store for undo/redo
}

bool LayerPropertyController::editScalar(LayerTreeNode* node, Property prop,
                                         const AnimationValue& value, Frame frame,
                                         const QString& name) {
    if (!m_doc || !m_history || !node)
        return false;
    if (!shouldKeyframe(node, prop))
        return false;                     // caller performs its base-value edit
    pushKeyframe(node, prop, frame, value, name);
    return true;
}

bool LayerPropertyController::editOpacity(LayerTreeNode* node, float value, Frame frame) {
    return editScalar(node, Property::Opacity,
                      AnimationValue(std::clamp(value, 0.0f, 1.0f)), frame,
                      QStringLiteral("Keyframe Opacity"));
}

bool LayerPropertyController::editVisibility(LayerTreeNode* node, bool value, Frame frame) {
    return editScalar(node, Property::Visibility, AnimationValue(value), frame,
                      QStringLiteral("Keyframe Visibility"));
}

bool LayerPropertyController::editBlendMode(LayerTreeNode* node, BlendMode value, Frame frame) {
    return editScalar(node, Property::BlendMode,
                      AnimationValue::fromEnum(static_cast<int>(value)), frame,
                      QStringLiteral("Keyframe Blend Mode"));
}

bool LayerPropertyController::editTransform(LayerTreeNode* node,
                                            const QTransform& newTransform, Frame frame) {
    if (!m_doc || !m_history || !node)
        return false;

    static const Property kComponents[9] = {
        Property::PositionX, Property::PositionY,
        Property::ScaleX, Property::ScaleY, Property::Rotation,
        Property::SkewX, Property::SkewY,
        Property::PivotX, Property::PivotY
    };
    bool anyTrack = false;
    for (Property p : kComponents) {
        if (m_doc->animation.track(node->id, p) != nullptr) { anyTrack = true; break; }
    }
    if (!anyTrack && !m_autoKey)
        return false;                     // caller performs its base transform edit

    // Keyframe the decomposed components as one undo entry. Skew/pivot are left
    // at their base (0) — this path animates the current 5-DOF transform.
    const TransformComponents c = decomposeTransform(newTransform);
    m_history->beginMacro(QStringLiteral("Keyframe Transform"));
    pushKeyframe(node, Property::PositionX, frame,
                 AnimationValue(static_cast<float>(c.position.x())), QStringLiteral("Keyframe Position X"));
    pushKeyframe(node, Property::PositionY, frame,
                 AnimationValue(static_cast<float>(c.position.y())), QStringLiteral("Keyframe Position Y"));
    pushKeyframe(node, Property::ScaleX, frame,
                 AnimationValue(static_cast<float>(c.scale.x())), QStringLiteral("Keyframe Scale X"));
    pushKeyframe(node, Property::ScaleY, frame,
                 AnimationValue(static_cast<float>(c.scale.y())), QStringLiteral("Keyframe Scale Y"));
    pushKeyframe(node, Property::Rotation, frame,
                 AnimationValue(c.rotation), QStringLiteral("Keyframe Rotation"));
    pushKeyframe(node, Property::SkewX, frame,
                 AnimationValue(static_cast<float>(c.skew.x())), QStringLiteral("Keyframe Skew X"));
    pushKeyframe(node, Property::SkewY, frame,
                 AnimationValue(static_cast<float>(c.skew.y())), QStringLiteral("Keyframe Skew Y"));
    pushKeyframe(node, Property::PivotX, frame,
                 AnimationValue(static_cast<float>(c.pivot.x())), QStringLiteral("Keyframe Pivot X"));
    pushKeyframe(node, Property::PivotY, frame,
                 AnimationValue(static_cast<float>(c.pivot.y())), QStringLiteral("Keyframe Pivot Y"));
    m_history->endMacro();
    return true;
}

} // namespace anim
