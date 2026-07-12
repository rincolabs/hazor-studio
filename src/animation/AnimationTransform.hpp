#pragma once

#include <QPointF>
#include <QTransform>

namespace anim {

// A node transform expressed as animatable components, NOT as raw matrix
// elements. position/scale/rotation reuse exactly the convention of
// TransformController (matrix = Scale * Rotate * Translate); skew and pivot are
// extensions that are 0 for every existing/static document, so composing them
// reduces byte-for-byte to the current transform for skew==0 && pivot==0.
struct TransformComponents {
    QPointF position{0.0, 0.0};   // world/NDC translation (matrix m31/m32)
    QPointF scale{1.0, 1.0};      // frame half-extents (sign of y carries flip)
    float   rotation = 0.0f;      // RADIANS, matching TransformController
    QPointF skew{0.0, 0.0};       // shear factors; 0 = none
    QPointF pivot{0.0, 0.0};      // local pivot; 0 = existing convention
};

// Decompose using the SAME rules as TransformController::decompose (position,
// scale, rotation). skew and pivot come back 0 because the existing transform
// model has no such degrees of freedom — nothing is invented for static docs.
TransformComponents decomposeTransform(const QTransform& t);

// Compose components back into a QTransform. With skew==0 && pivot==0 this is
// exactly TransformController::compose(scale.x, scale.y, position, rotation) —
// there is no second transform convention. Skew/pivot only add DOF on top.
QTransform composeTransform(const TransformComponents& c);

// True when `t` survives a decompose/compose round-trip without losing shear
// (its basis vectors are orthogonal up to sign). When false, animating a
// transform component would silently drop the shear, so the evaluator preserves
// the original base transform instead.
bool isCleanlyDecomposable(const QTransform& t);

} // namespace anim
