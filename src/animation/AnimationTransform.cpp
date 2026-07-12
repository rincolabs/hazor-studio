#include "AnimationTransform.hpp"

#include <cmath>

#include <QtGlobal>

#include "transform/TransformController.hpp"

namespace anim {

TransformComponents decomposeTransform(const QTransform& t) {
    // Reuse the project's one transform convention verbatim.
    float hw = 1.0f, hh = 1.0f, rot = 0.0f;
    QPointF center;
    TransformController::decompose(t, hw, hh, center, rot);

    TransformComponents c;
    c.position = center;
    c.scale = QPointF(hw, hh);
    c.rotation = rot;
    c.skew = QPointF(0.0, 0.0);   // existing model has no skew DOF
    c.pivot = QPointF(0.0, 0.0);  // ...and no explicit pivot DOF
    return c;
}

QTransform composeTransform(const TransformComponents& c) {
    const bool noSkew = qFuzzyIsNull(c.skew.x()) && qFuzzyIsNull(c.skew.y());
    const bool noPivot = qFuzzyIsNull(c.pivot.x()) && qFuzzyIsNull(c.pivot.y());
    if (noSkew && noPivot) {
        // Reduced case == the existing transform, byte-for-byte (same function),
        // so static documents are never perturbed by going through animation.
        return TransformController::compose(
            static_cast<float>(c.scale.x()), static_cast<float>(c.scale.y()),
            c.position, c.rotation);
    }

    // Extended case: linear part L = Scale * Skew * Rotate (row-vector
    // convention, matching TransformController), rotation/scale about `pivot`.
    const double sx = c.scale.x(), sy = c.scale.y();
    const double kx = c.skew.x(),  ky = c.skew.y();
    const double cth = std::cos(c.rotation), sth = std::sin(c.rotation);

    // S*K = [[sx, sx*ky], [sy*kx, sy]]
    const double a11 = sx,      a12 = sx * ky;
    const double a21 = sy * kx, a22 = sy;
    // (S*K) * R, with R = [[cth, sth], [-sth, cth]]
    const double l11 = a11 * cth - a12 * sth;
    const double l12 = a11 * sth + a12 * cth;
    const double l21 = a21 * cth - a22 * sth;
    const double l22 = a21 * sth + a22 * cth;

    // Translation so the pivot is the fixed point: pos + pivot*(I - L).
    const double px = c.pivot.x(), py = c.pivot.y();
    const double tx = c.position.x() + px - (px * l11 + py * l21);
    const double ty = c.position.y() + py - (px * l12 + py * l22);

    QTransform t;
    t.setMatrix(l11, l12, 0.0,
                l21, l22, 0.0,
                tx,  ty,  1.0);
    return t;
}

bool isCleanlyDecomposable(const QTransform& t) {
    // Node transforms are affine; a projective one cannot round-trip.
    if (!qFuzzyCompare(t.m33(), 1.0)
        || !qFuzzyIsNull(t.m13()) || !qFuzzyIsNull(t.m23()))
        return false;

    // Shear-free <=> the decompose/compose round-trip is lossless.
    const QTransform r = composeTransform(decomposeTransform(t));
    const double eps = 1e-4;
    return std::abs(r.m11() - t.m11()) < eps && std::abs(r.m12() - t.m12()) < eps
        && std::abs(r.m21() - t.m21()) < eps && std::abs(r.m22() - t.m22()) < eps
        && std::abs(r.m31() - t.m31()) < eps && std::abs(r.m32() - t.m32()) < eps;
}

} // namespace anim
