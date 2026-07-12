#include "AnimationEvaluator.hpp"

#include <algorithm>

#include "AnimationInterpolation.hpp"
#include "AnimationModel.hpp"
#include "AnimationTransform.hpp"
#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"

namespace anim {

namespace {

// Write one interpolated transform-component value into the component bundle.
void applyTransformComponent(TransformComponents& c, Property p, float v) {
    switch (p) {
    case Property::PositionX: c.position.setX(v); break;
    case Property::PositionY: c.position.setY(v); break;
    case Property::ScaleX:    c.scale.setX(v); break;
    case Property::ScaleY:    c.scale.setY(v); break;
    case Property::Rotation:  c.rotation = v; break;
    case Property::SkewX:     c.skew.setX(v); break;
    case Property::SkewY:     c.skew.setY(v); break;
    case Property::PivotX:    c.pivot.setX(v); break;
    case Property::PivotY:    c.pivot.setY(v); break;
    default: break;  // non-transform property, handled by the caller
    }
}

} // namespace

bool AnimationEvaluator::evaluateNode(LayerTreeNode* node,
                                      const AnimationModel& model, Frame frame) {
    const AnimationModel::LayerTracks* tracks = model.tracksFor(node->id);
    if (!tracks || tracks->empty())
        return false;  // no animation on this node — evaluated stays == base

    // Snapshot the previous evaluated state for change detection.
    const QTransform prevXf = node->transform();
    const float prevOp = node->opacity();
    const bool prevVis = node->isVisible();
    const BlendMode prevBm = node->blendMode();

    // Start from the base state, then overlay the animated properties.
    node->resetEvaluatedState();

    bool haveTransformTrack = false;
    TransformComponents comp;  // populated lazily from the base transform

    for (const auto& [prop, track] : *tracks) {
        if (track.isEmpty())
            continue;
        const AnimationValue v = sampleTrack(track, frame);
        switch (prop) {
        case Property::Opacity:
            node->setEvaluatedOpacity(std::clamp(v.asFloat(), 0.0f, 1.0f));
            break;
        case Property::Visibility:
            node->setEvaluatedVisible(v.asBool());
            break;
        case Property::BlendMode:
            node->setEvaluatedBlendMode(static_cast<BlendMode>(v.asEnum()));
            break;
        default:  // a transform component
            if (!haveTransformTrack) {
                comp = decomposeTransform(node->baseTransform());
                haveTransformTrack = true;
            }
            applyTransformComponent(comp, prop, v.asFloat());
            break;
        }
    }

    if (haveTransformTrack) {
        // Only recompose when the base is shear-free; otherwise recomposing
        // would silently drop the shear, so preserve the base transform (which
        // resetEvaluatedState already restored into the evaluated state).
        if (isCleanlyDecomposable(node->baseTransform()))
            node->setEvaluatedTransform(composeTransform(comp));
    }

    const bool changed = prevXf != node->transform()
        || prevOp != node->opacity()
        || prevVis != node->isVisible()
        || prevBm != node->blendMode();
    if (changed) {
        // Same invalidation a manual transform/opacity/blend/visibility edit
        // uses: mark the composite dirty. Pixels, masks, thumbnails and GPU
        // textures are intentionally left untouched.
        node->compositeDirty = true;
    }
    return changed;
}

bool evaluateRasterNode(LayerTreeNode* node, const AnimationModel& model,
                        Frame frame)
{
    if (!node || !node->layer || node->type != LayerTreeNode::Type::Layer)
        return false;

    Layer* layer = node->layer.get();
    const RasterCelTrack* track = model.rasterTrack(node->id);
    if (!track || track->isEmpty()) {
        const bool changed = layer->hasEvaluatedRasterContent();
        layer->clearEvaluatedRasterContent();
        return changed;
    }

    const std::optional<CelId> celId = track->celAt(frame);
    if (!celId) {
        const bool changed = !layer->hasEvaluatedRasterContent()
            || !layer->evaluatedCelId().isNull();
        layer->setEvaluatedRasterEmpty();
        return changed;
    }

    auto content = model.celStorage().contentHandle(*celId);
    const bool changed = !layer->hasEvaluatedRasterContent()
        || layer->evaluatedCelId() != *celId
        || layer->evaluatedRasterContentHandle().get() != content.get();
    if (content)
        layer->setEvaluatedRasterContent(
            *celId, std::const_pointer_cast<RasterCelContent>(content));
    else
        layer->setEvaluatedRasterEmpty();
    return changed;
}

int AnimationEvaluator::evaluateSubtree(LayerTreeNode* node,
                                        const AnimationModel& model, Frame frame) {
    if (!node)
        return 0;
    int changed = evaluateNode(node, model, frame) ? 1 : 0;
    if (evaluateRasterNode(node, model, frame)) {
        node->compositeDirty = true;
        ++changed;
    }
    for (const auto& child : node->children)
        changed += evaluateSubtree(child.get(), model, frame);
    return changed;
}

int AnimationEvaluator::evaluateTree(
    const std::vector<std::unique_ptr<LayerTreeNode>>& roots,
    const AnimationModel& model, Frame frame) {
    // Fast path: a document with no tracks at all costs one branch and nothing
    // per node.
    if (!model.hasAnyTracks())
        return 0;
    int changed = 0;
    for (const auto& root : roots)
        changed += evaluateSubtree(root.get(), model, frame);
    return changed;
}

int AnimationEvaluator::evaluate(Document& doc, Frame frame) {
    const int changed = evaluateTree(doc.roots, doc.animation, frame);
    if (changed > 0)
        ++doc.compositionGeneration;  // existing composite-invalidation signal
    return changed;
}

} // namespace anim
