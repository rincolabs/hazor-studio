#pragma once

#include <memory>
#include <vector>

#include "AnimationTypes.hpp"

class Document;
class LayerTreeNode;

namespace anim {

class AnimationModel;

// Evaluates the animation model at a frame and writes the result into each
// node's EVALUATED state. It is independent of the UI and the renderers: it
// only reads baseState + tracks and writes evaluatedState (via the node's
// evaluator-only setters), then marks the composite dirty exactly the way a
// manual transform/opacity/blend/visibility edit does.
//
// Keyframe logic lives ONLY here — the getters and the CPU/GPU renderers never
// evaluate animation; they read the already-computed evaluated getters.
class AnimationEvaluator {
public:
    // Evaluate `doc.animation` at `frame` over `doc.roots`. Bumps
    // doc.compositionGeneration when anything changed (the existing invalidation
    // signal). Returns the number of nodes whose evaluated state changed.
    static int evaluate(Document& doc, Frame frame);

    // Lower-level variant for an isolated tree (e.g. an export/render clone in a
    // later stage). Does NOT bump any generation — the caller owns invalidation.
    static int evaluateTree(
        const std::vector<std::unique_ptr<LayerTreeNode>>& roots,
        const AnimationModel& model, Frame frame);

private:
    static int evaluateSubtree(LayerTreeNode* node,
                               const AnimationModel& model, Frame frame);
    // Returns true if this node had tracks AND its evaluated state changed.
    static bool evaluateNode(LayerTreeNode* node,
                             const AnimationModel& model, Frame frame);
};

} // namespace anim
