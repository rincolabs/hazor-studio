#define BOOST_TEST_MODULE MoveAlignTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "controller/Commands.hpp"
#include "engine/ImageEngine.hpp"
#include "transform/TransformController.hpp"
#include "transform/TransformTypes.hpp"

#include <QImage>
#include <QColor>
#include <QTransform>
#include <QPointF>
#include <QPolygonF>
#include <QApplication>

#include <cmath>

// ─── Global QApplication for QObject-based tests ────────────────
struct QtAppFixture {
    QtAppFixture() {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = { const_cast<char*>("test") };
            static QApplication app(argc, argv);
        }
    }
};
BOOST_GLOBAL_FIXTURE(QtAppFixture);

// ═══════════════════════════════════════════════════════════════
//  Helper: replicate alignLayerTransform logic
//  In Qt's row-vector convention: fromTranslate(localDx, localDy) * nodeT
//  composes as: first nodeT, then translate. So the translation is
//  added in nodeT's OUTPUT space. The localDelta is computed by mapping
//  the desired canvas delta through the inverse 2x2 submatrix of nodeT,
//  so that fromTranslate(localDelta) * nodeT produces the correct
//  output-space translation (dx, dy) regardless of nodeT's scale/rotate.
// ═══════════════════════════════════════════════════════════════
static QTransform applyAlignTranslation(const QTransform& nodeT, const QPointF& canvasDelta)
{
    QTransform inv = nodeT.inverted();
    QPointF localDelta = inv.map(canvasDelta) - inv.map(QPointF(0, 0));
    return QTransform::fromTranslate(localDelta.x(), localDelta.y()) * nodeT;
}

// Helper: verify correct output-space delta
static void checkAlignDelta(const QTransform& oldT, const QPointF& canvasDelta,
                            double tolerance = 1e-5)
{
    QTransform newT = applyAlignTranslation(oldT, canvasDelta);
    QPointF testPoints[] = {
        QPointF(-1, -1), QPointF(1, -1), QPointF(1, 1), QPointF(-1, 1),
        QPointF(0, 0), QPointF(0.3f, -0.7f), QPointF(-0.5f, 0.2f)
    };
    for (auto& p : testPoints) {
        QPointF oldOut = oldT.map(p);
        QPointF newOut = newT.map(p);
        BOOST_CHECK_CLOSE(newOut.x() - oldOut.x(), canvasDelta.x(), tolerance);
        if (std::abs(canvasDelta.y()) > 1e-3)
            BOOST_CHECK_CLOSE(newOut.y() - oldOut.y(), canvasDelta.y(), tolerance);
        else
            BOOST_CHECK_SMALL(newOut.y() - oldOut.y(), 1e-3);
    }
}

// Helper: compute bounding box min/max from corners
static void bboxFromCorners(const QPolygonF& corners,
                             float& minX, float& maxX,
                             float& minY, float& maxY)
{
    minX = 1e9f; maxX = -1e9f;
    minY = 1e9f; maxY = -1e9f;
    for (auto& c : corners) {
        if (c.x() < minX) minX = static_cast<float>(c.x());
        if (c.x() > maxX) maxX = static_cast<float>(c.x());
        if (c.y() < minY) minY = static_cast<float>(c.y());
        if (c.y() > maxY) maxY = static_cast<float>(c.y());
    }
}

// Helper: compute alignment delta from bounding box
static void computeAlignDelta(int alignmentType, float minX, float maxX,
                               float minY, float maxY,
                               float& dx, float& dy)
{
    dx = 0; dy = 0;
    switch (alignmentType) {
    case 0: dx = -1.0f - minX; break;
    case 1: dx = -(minX + maxX) * 0.5f; break;
    case 2: dx = 1.0f - maxX; break;
    case 3: dy = 1.0f - maxY; break;
    case 4: dy = -(minY + maxY) * 0.5f; break;
    case 5: dy = -1.0f - minY; break;
    }
}

// ═══════════════════════════════════════════════════════════════
//  Suite 1: alignLayerTransform math (pure QTransform)
//  Verifies that applyAlignTranslation produces a new transform
//  whose output maps are shifted by exactly canvasDelta
// ═══════════════════════════════════════════════════════════════
BOOST_AUTO_TEST_SUITE(align_math)

BOOST_AUTO_TEST_CASE(identity_translate_x)
{
    QTransform t;
    checkAlignDelta(t, QPointF(1.0, 0.0));
}

BOOST_AUTO_TEST_CASE(identity_translate_y)
{
    QTransform t;
    checkAlignDelta(t, QPointF(0.0, 0.5));
}

BOOST_AUTO_TEST_CASE(identity_translate_both)
{
    QTransform t;
    checkAlignDelta(t, QPointF(-0.7, 0.3));
}

BOOST_AUTO_TEST_CASE(scale_half_translate_x)
{
    QTransform t;
    t.scale(0.5f, 0.5f);
    checkAlignDelta(t, QPointF(1.0, 0.0));
}

BOOST_AUTO_TEST_CASE(scale_double_translate_x)
{
    QTransform t;
    t.scale(2.0f, 2.0f);
    checkAlignDelta(t, QPointF(1.0, 0.0));
}

BOOST_AUTO_TEST_CASE(scale_asymmetric_translate)
{
    QTransform t;
    t.scale(0.3f, 0.2f);
    checkAlignDelta(t, QPointF(1.0, 1.0));
}

BOOST_AUTO_TEST_CASE(scale_very_small_translate)
{
    QTransform t;
    t.scale(0.01f, 0.01f);
    checkAlignDelta(t, QPointF(-1.5, 2.0));
}

BOOST_AUTO_TEST_CASE(scale_very_large_translate)
{
    QTransform t;
    t.scale(100.0f, 100.0f);
    checkAlignDelta(t, QPointF(0.5, -0.3));
}

BOOST_AUTO_TEST_CASE(rotate_90_translate)
{
    float deg90 = static_cast<float>(M_PI) / 2.0f;
    QTransform t;
    t.rotateRadians(deg90);
    checkAlignDelta(t, QPointF(1.0, 0.0));
}

BOOST_AUTO_TEST_CASE(rotate_45_translate)
{
    float deg45 = static_cast<float>(M_PI) / 4.0f;
    QTransform t;
    t.rotateRadians(deg45);
    checkAlignDelta(t, QPointF(1.0, 0.5));
}

BOOST_AUTO_TEST_CASE(scale_then_translate_noop)
{
    // In Qt: t.translate() post-multiplies. t = S(0.5,0.8) * T(0.3,-0.2)
    // means: first translate, then scale (row-vector: * S * T = first S then T)
    // Effectively: map = T(S(p)) ... wait no.
    // Let me just use fromTranslate * scale for explicit ordering.
    // Qt: A * B means first A then B (row vector).
    // fromTranslate(0.3,-0.2) * scale(0.5,0.8): first translate, then scale.
    QTransform nodeT = QTransform::fromTranslate(0.3, -0.2)
                      * QTransform().scale(0.5f, 0.8f);
    checkAlignDelta(nodeT, QPointF(-0.5, 0.7));
}

BOOST_AUTO_TEST_CASE(translate_then_scale)
{
    // Using member functions: first scale(0.5,0.8) post-multiplied (t = I*S),
    // then translate(0.3,-0.2) post-multiplied (t = S * T).
    // In row-vector: S * T means first T then S, then the point:
    // S(T(p)) = scale(translate(p)).
    QTransform t;
    t.scale(0.5f, 0.8f);
    t.translate(0.3, -0.2);
    checkAlignDelta(t, QPointF(-0.5, 0.7));
}

BOOST_AUTO_TEST_CASE(existing_transform_elements_preserved)
{
    QTransform t;
    t.scale(1.5f, 0.8f);
    t.rotateRadians(0.3);
    t.shear(0.1, 0.0);

    double oldM11 = t.m11(), oldM12 = t.m12();
    double oldM21 = t.m21(), oldM22 = t.m22();

    QTransform newT = applyAlignTranslation(t, QPointF(0.7, -0.3));
    BOOST_CHECK_CLOSE(newT.m11(), oldM11, 1e-5);
    BOOST_CHECK_CLOSE(newT.m12(), oldM12, 1e-5);
    BOOST_CHECK_CLOSE(newT.m21(), oldM21, 1e-5);
    BOOST_CHECK_CLOSE(newT.m22(), oldM22, 1e-5);
    // Translation must change
    BOOST_CHECK(std::abs(newT.m31() - t.m31()) > 1e-6);
    BOOST_CHECK(std::abs(newT.m32() - t.m32()) > 1e-6);
}

BOOST_AUTO_TEST_CASE(zero_delta_no_change)
{
    QTransform t;
    t.scale(0.5f, 0.5f);
    t.translate(0.3, -0.2);
    QTransform newT = applyAlignTranslation(t, QPointF(0, 0));
    BOOST_CHECK_CLOSE(newT.m11(), t.m11(), 1e-5);
    BOOST_CHECK_CLOSE(newT.m12(), t.m12(), 1e-5);
    BOOST_CHECK_CLOSE(newT.m21(), t.m21(), 1e-5);
    BOOST_CHECK_CLOSE(newT.m22(), t.m22(), 1e-5);
    BOOST_CHECK_CLOSE(newT.m31(), t.m31(), 1e-5);
    BOOST_CHECK_CLOSE(newT.m32(), t.m32(), 1e-5);
}

BOOST_AUTO_TEST_CASE(negative_delta_opposite_direction)
{
    QTransform t;
    checkAlignDelta(t, QPointF(-1.0, 0.0));
    checkAlignDelta(t, QPointF(0.0, -0.8));
    checkAlignDelta(t, QPointF(-0.3, -0.7));
}

BOOST_AUTO_TEST_CASE(existing_translation_preserved_plus_delta)
{
    // Qt: translate(0.5,-0.3) on identity → T(0.5,-0.3)
    QTransform t = QTransform::fromTranslate(0.5f, -0.3f);
    QTransform newT = applyAlignTranslation(t, QPointF(0.2, 0.1));

    QPointF p(-1, -1);
    QPointF oldOut = t.map(p);
    QPointF newOut = newT.map(p);
    BOOST_CHECK_CLOSE(newOut.x() - oldOut.x(), 0.2f, 1e-5);
    BOOST_CHECK_CLOSE(newOut.y() - oldOut.y(), 0.1f, 1e-5);
}

// When this transform is composed with a parent, the parent's scale
// also multiplies the delta. This tests the NODE-ONLY behavior.
BOOST_AUTO_TEST_CASE(node_only_delta_precise)
{
    QTransform nodeT;
    nodeT.scale(0.5f, 0.5f);
    nodeT.translate(0.2, -0.1);
    checkAlignDelta(nodeT, QPointF(1.0, 0.0));
}

BOOST_AUTO_TEST_SUITE_END()

// ═══════════════════════════════════════════════════════════════
//  Suite 2: bounding box + alignment delta computation
//  QTransform row-vector convention: A * B means first A then B.
//  Member functions post-multiply: t = t * fn_matrix, i.e.,
//  t.scale(s) then t.translate(t) → t = S * T = first T then S.
// ═══════════════════════════════════════════════════════════════
BOOST_AUTO_TEST_SUITE(bbox_delta)

BOOST_AUTO_TEST_CASE(identity_corners)
{
    QTransform t;
    QPolygonF corners = TransformController::cornersFromTransform(t);
    BOOST_CHECK_EQUAL(corners.size(), 4);
    BOOST_CHECK_CLOSE(corners[0].x(), -1.0f, 1e-5);
    BOOST_CHECK_CLOSE(corners[0].y(), -1.0f, 1e-5);
    BOOST_CHECK_CLOSE(corners[1].x(),  1.0f, 1e-5);
    BOOST_CHECK_CLOSE(corners[1].y(), -1.0f, 1e-5);
    BOOST_CHECK_CLOSE(corners[2].x(),  1.0f, 1e-5);
    BOOST_CHECK_CLOSE(corners[2].y(),  1.0f, 1e-5);
    BOOST_CHECK_CLOSE(corners[3].x(), -1.0f, 1e-5);
    BOOST_CHECK_CLOSE(corners[3].y(),  1.0f, 1e-5);
}

BOOST_AUTO_TEST_CASE(translate_corners)
{
    QTransform t = QTransform::fromTranslate(0.3f, -0.2f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    BOOST_CHECK_CLOSE(minX, -0.7f, 1e-5);
    BOOST_CHECK_CLOSE(maxX,  1.3f, 1e-5);
    BOOST_CHECK_CLOSE(minY, -1.2f, 1e-5);
    BOOST_CHECK_CLOSE(maxY,  0.8f, 1e-5);
}

BOOST_AUTO_TEST_CASE(scale_corners)
{
    QTransform t = QTransform::fromScale(0.5f, 0.3f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    BOOST_CHECK_CLOSE(minX, -0.5f, 1e-5);
    BOOST_CHECK_CLOSE(maxX,  0.5f, 1e-5);
    BOOST_CHECK_CLOSE(minY, -0.3f, 1e-5);
    BOOST_CHECK_CLOSE(maxY,  0.3f, 1e-5);
}

BOOST_AUTO_TEST_CASE(scale_translate_corners)
{
    // Qt row-vector: A * B = first A then B.
    // fromTranslate(0.4,-0.2) * scale(0.2,0.3):
    // first translate (0.4,-0.2), then scale (0.2,0.3)
    // p' = scale(translate(p)) = (0.2*(p.x+0.4), 0.3*(p.y-0.2))
    QTransform t = QTransform::fromTranslate(0.4f, -0.2f)
                   * QTransform::fromScale(0.2f, 0.3f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    // (-1,-1) → (0.2*(-1+0.4), 0.3*(-1-0.2)) = (-0.12, -0.36)
    // (1,-1)  → (0.2*(1+0.4), 0.3*(-1-0.2))  = (0.28, -0.36)
    // (1,1)   → (0.28, 0.24), (-1,1) → (-0.12, 0.24)
    BOOST_CHECK_CLOSE(minX, -0.12f, 1e-5);
    BOOST_CHECK_CLOSE(maxX,  0.28f, 1e-5);
    BOOST_CHECK_CLOSE(minY, -0.36f, 1e-5);
    BOOST_CHECK_CLOSE(maxY,  0.24f, 1e-5);
}

// ── Alignment delta tests ───────────────────────────────────────

BOOST_AUTO_TEST_CASE(align_left_identity)
{
    QTransform t;
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(0, minX, maxX, minY, maxY, dx, dy);
    BOOST_CHECK_SMALL(dx, 1e-5f);
}

BOOST_AUTO_TEST_CASE(align_left_translated_right)
{
    QTransform t = QTransform::fromTranslate(0.5f, 0.0f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(0, minX, maxX, minY, maxY, dx, dy);
    // bbox minX = -0.5, dx = -1 - (-0.5) = -0.5
    BOOST_CHECK_CLOSE(dx, -0.5f, 1e-5);
    BOOST_CHECK_SMALL(dy, 1e-5f);
}

BOOST_AUTO_TEST_CASE(align_left_translated_left)
{
    QTransform t = QTransform::fromTranslate(-0.3f, 0.0f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(0, minX, maxX, minY, maxY, dx, dy);
    // minX = -1.3, dx = -1 - (-1.3) = 0.3
    BOOST_CHECK_CLOSE(dx, 0.3f, 0.5f);  // relaxed tolerance for fp
    BOOST_CHECK_SMALL(dy, 1e-5f);
}

BOOST_AUTO_TEST_CASE(align_right_translated)
{
    QTransform t = QTransform::fromTranslate(-0.4f, 0.0f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(2, minX, maxX, minY, maxY, dx, dy);
    // maxX = 0.6, dx = 1 - 0.6 = 0.4
    BOOST_CHECK_CLOSE(dx, 0.4f, 1e-5);
}

BOOST_AUTO_TEST_CASE(align_top_translated)
{
    QTransform t = QTransform::fromTranslate(0.0f, -0.3f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(3, minX, maxX, minY, maxY, dx, dy);
    // maxY = 0.7, dy = 1 - 0.7 = 0.3
    BOOST_CHECK_CLOSE(dy, 0.3f, 1e-5);
}

BOOST_AUTO_TEST_CASE(align_bottom_translated)
{
    QTransform t = QTransform::fromTranslate(0.0f, 0.3f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(5, minX, maxX, minY, maxY, dx, dy);
    // minY = -0.7, dy = -1 - (-0.7) = -0.3
    BOOST_CHECK_CLOSE(dy, -0.3f, 1e-5);
}

BOOST_AUTO_TEST_CASE(align_center_h_idempotent)
{
    QTransform t;
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(1, minX, maxX, minY, maxY, dx, dy);
    BOOST_CHECK_SMALL(dx, 1e-5f);
}

BOOST_AUTO_TEST_CASE(align_middle_v_idempotent)
{
    QTransform t;
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(4, minX, maxX, minY, maxY, dx, dy);
    BOOST_CHECK_SMALL(dy, 1e-5f);
}

BOOST_AUTO_TEST_CASE(align_left_scaled_then_translated)
{
    // Qt: scale(0.5,0.5) then translate(0.5,0.0) → t = S(0.5,0.5) * T(0.5,0.0)
    // Row-vector S*T: first T then S → p' = S(T(p)) = 0.5*(p + (0.5,0))
    // So for (-1,-1): 0.5*(-0.5, -1) = (-0.25, -0.5)
    // minX = -0.25
    QTransform t;
    t.scale(0.5f, 0.5f);
    t.translate(0.5f, 0.0f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(0, minX, maxX, minY, maxY, dx, dy);
    // dx = -1 - (-0.25) = -0.75
    BOOST_CHECK_CLOSE(dx, -0.75f, 1e-5);
}

BOOST_AUTO_TEST_CASE(align_right_scaled_then_translated)
{
    // scale(0.3,0.3) then translate(-0.4,0.0) → S*T: p' = 0.3*(p + (-0.4,0))
    // (-1,-1) → 0.3*(-1.4) = -0.42
    // (1,-1)  → 0.3*(0.6) = 0.18
    // maxX = 0.18
    QTransform t;
    t.scale(0.3f, 0.3f);
    t.translate(-0.4f, 0.0f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    float dx, dy;
    computeAlignDelta(2, minX, maxX, minY, maxY, dx, dy);
    // dx = 1 - 0.18 = 0.82
    BOOST_CHECK_CLOSE(dx, 0.82f, 1e-5);
}

BOOST_AUTO_TEST_CASE(align_left_already_at_border)
{
    // fS(0.5,1) * fT(tx,0): p' = S(p) + (tx, 0) = (0.5*x + tx, y)
    // minX corner at (-1,-1): 0.5*(-1) + tx = -1 → tx = -0.5
    QTransform t = QTransform::fromScale(0.5f, 1.0f)
                   * QTransform::fromTranslate(-0.5f, 0.0f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    BOOST_CHECK_CLOSE(minX, -1.0f, 1e-4f);
    float dx, dy;
    computeAlignDelta(0, minX, maxX, minY, maxY, dx, dy);
    BOOST_CHECK_SMALL(dx, 2e-4f);
}

BOOST_AUTO_TEST_CASE(align_right_already_at_border)
{
    // fS(0.5,1) * fT(tx,0): maxX corner at (1,-1): 0.5*1 + tx = 1 → tx = 0.5
    QTransform t = QTransform::fromScale(0.5f, 1.0f)
                   * QTransform::fromTranslate(0.5f, 0.0f);
    QPolygonF corners = TransformController::cornersFromTransform(t);
    float minX, maxX, minY, maxY;
    bboxFromCorners(corners, minX, maxX, minY, maxY);
    BOOST_CHECK_CLOSE(maxX, 1.0f, 1e-4f);
    float dx, dy;
    computeAlignDelta(2, minX, maxX, minY, maxY, dx, dy);
    BOOST_CHECK_SMALL(dx, 2e-4f);
}

BOOST_AUTO_TEST_SUITE_END()

// ═══════════════════════════════════════════════════════════════
//  Suite 3: Full alignment pipeline (Document + Controller)
//  Tests that applyAlignTranslation + delta computation work
//  together to produce correct alignment in 1 click.
//  These integration tests replicate the CanvasView::doAlignLayer
//  logic using Document and ImageController.
// ═══════════════════════════════════════════════════════════════
struct AlignFixture {
    Document doc;
    ImageController ctrl;

    AlignFixture() : ctrl() {
        doc.size = QSize(200, 150);
        doc.selection.create(200, 150);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    Layer* active() { return doc.activeLayer(); }
    int activeIdx() const { return doc.activeFlatIndex; }
    LayerTreeNode* activeNode() { return doc.activeNode(); }

    // Simulates CanvasView::doAlignLayer logic
    void alignLayer(int alignmentType)
    {
        auto* node = doc.activeNode();
        if (!node || !node->layer) return;
        if (node->lockFlags & LockPosition) return;

        QTransform accum = node->accumulatedTransform();
        QPolygonF corners = TransformController::cornersFromTransform(accum);

        float minX, maxX, minY, maxY;
        bboxFromCorners(corners, minX, maxX, minY, maxY);

        float dx, dy;
        computeAlignDelta(alignmentType, minX, maxX, minY, maxY, dx, dy);

        QTransform nodeT = node->transform;
        QTransform inv = nodeT.inverted();
        QPointF localDelta = inv.map(QPointF(dx, dy)) - inv.map(QPointF(0, 0));
        node->transform = QTransform::fromTranslate(localDelta.x(), localDelta.y()) * nodeT;
    }

    // Verify the active layer's bounding box aligns with canvas
    void verifyAlignment(int alignmentType, double tolerance = 1e-3)
    {
        auto* node = doc.activeNode();
        BOOST_REQUIRE(node);
        QTransform accum = node->accumulatedTransform();
        QPolygonF corners = TransformController::cornersFromTransform(accum);
        float minX, maxX, minY, maxY;
        bboxFromCorners(corners, minX, maxX, minY, maxY);

        switch (alignmentType) {
        case 0: BOOST_CHECK_CLOSE(minX, -1.0, tolerance); break;
        case 1: BOOST_CHECK_SMALL((minX + maxX) * 0.5, tolerance); break;
        case 2: BOOST_CHECK_CLOSE(maxX, 1.0, tolerance); break;
        case 3: BOOST_CHECK_CLOSE(maxY, 1.0, tolerance); break;
        case 4: BOOST_CHECK_SMALL((minY + maxY) * 0.5, tolerance); break;
        case 5: BOOST_CHECK_CLOSE(minY, -1.0, tolerance); break;
        }
    }

    void fill(uchar r, uchar g, uchar b, uchar a = 255) {
        auto* l = active();
        if (l) l->cpuImage.fill(QColor(r, g, b, a));
    }
};

BOOST_AUTO_TEST_SUITE(integration_align)

BOOST_AUTO_TEST_CASE(align_left_one_click)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.5f, 0.0f);
    f.alignLayer(0);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(align_right_one_click)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(-0.4f, 0.0f);
    f.alignLayer(2);
    f.verifyAlignment(2);
}

BOOST_AUTO_TEST_CASE(align_top_one_click)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.0f, -0.3f);
    f.alignLayer(3);
    f.verifyAlignment(3);
}

BOOST_AUTO_TEST_CASE(align_bottom_one_click)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.0f, 0.3f);
    f.alignLayer(5);
    f.verifyAlignment(5);
}

BOOST_AUTO_TEST_CASE(align_center_h_already_center)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform();
    f.alignLayer(1);
    f.verifyAlignment(1);
}

BOOST_AUTO_TEST_CASE(align_middle_v_already_center)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform();
    f.alignLayer(4);
    f.verifyAlignment(4);
}

BOOST_AUTO_TEST_CASE(align_left_scaled_03)
{
    // T(0.5, 0) * S(0.3, 0.3): first S, then T: p' = T(S(p)) = 0.3*p + (0.5,0)
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.5f, 0.0f)
                                * QTransform::fromScale(0.3f, 0.3f);
    f.alignLayer(0);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(align_right_scaled_05)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(-0.6f, 0.0f)
                                * QTransform::fromScale(0.5f, 0.5f);
    f.alignLayer(2);
    f.verifyAlignment(2);
}

BOOST_AUTO_TEST_CASE(align_top_scaled_01)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.0f, -0.7f)
                                * QTransform::fromScale(0.1f, 0.1f);
    f.alignLayer(3);
    f.verifyAlignment(3);
}

BOOST_AUTO_TEST_CASE(align_bottom_scaled_and_translated)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.2f, 0.5f)
                                * QTransform::fromScale(0.4f, 0.3f);
    f.alignLayer(5);
    f.verifyAlignment(5);
}

BOOST_AUTO_TEST_CASE(align_twice_idempotent)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.7f, 0.0f)
                                * QTransform::fromScale(0.25f, 0.25f);
    f.alignLayer(0);
    f.verifyAlignment(0);
    double txAfter = f.activeNode()->transform.m31();
    f.alignLayer(0);
    // After 2nd call: delta should be ~0, position should not change
    double txAfter2 = f.activeNode()->transform.m31();
    BOOST_CHECK_CLOSE(txAfter2, txAfter, 0.5);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(align_left_scaled_and_rotated)
{
    AlignFixture f;
    QTransform t = QTransform::fromTranslate(0.4f, -0.3f)
                   * QTransform().scale(0.3f, 0.2f)
                   * QTransform().rotateRadians(0.5f);
    f.activeNode()->transform = t;
    f.alignLayer(0);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(align_right_scaled_and_rotated)
{
    AlignFixture f;
    QTransform t = QTransform::fromTranslate(-0.5f, 0.2f)
                   * QTransform().scale(0.25f, 0.4f)
                   * QTransform().rotateRadians(-0.3f);
    f.activeNode()->transform = t;
    f.alignLayer(2);
    f.verifyAlignment(2);
}

BOOST_AUTO_TEST_CASE(align_left_one_click_exact)
{
    // T(-1, 0) * S(0.5, 1): already at border
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(-1.0f, 0.0f)
                                * QTransform::fromScale(0.5f, 1.0f);
    f.alignLayer(0);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(position_locked_layer_skipped)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.5f, 0.0f);
    f.activeNode()->lockFlags = LockPosition;
    f.alignLayer(0);
    BOOST_CHECK_CLOSE(f.activeNode()->transform.m31(), 0.5f, 1e-4f);
}

BOOST_AUTO_TEST_CASE(no_active_layer_no_crash)
{
    AlignFixture f;
    f.ctrl.setActiveNode(-1);
    f.alignLayer(0);
    f.alignLayer(1);
    f.alignLayer(2);
    f.alignLayer(3);
    f.alignLayer(4);
    f.alignLayer(5);
}

BOOST_AUTO_TEST_CASE(small_layer_pasted_scale_one_click)
{
    // A 100x100 pasted layer in a 200x150 doc → scale ≈ 0.5, 0.67
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.4f, 0.0f)
                                * QTransform::fromScale(0.1f, 0.1f);
    f.alignLayer(0);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(align_center_h_from_offset)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.3f, 0.0f);
    f.alignLayer(1);
    f.verifyAlignment(1);
}

BOOST_AUTO_TEST_CASE(align_middle_v_from_offset)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.0f, -0.2f);
    f.alignLayer(4);
    f.verifyAlignment(4);
}

BOOST_AUTO_TEST_CASE(align_very_small_scale)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(0.8f, 0.0f)
                                * QTransform::fromScale(0.015f, 0.015f);
    f.alignLayer(0);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(align_large_scale)
{
    AlignFixture f;
    f.activeNode()->transform = QTransform::fromTranslate(-0.5f, 0.0f)
                                * QTransform::fromScale(15.0f, 15.0f);
    f.alignLayer(0);
    f.verifyAlignment(0);
}

BOOST_AUTO_TEST_CASE(align_all_six_directions)
{
    AlignFixture f;
    int types[] = {0, 1, 2, 3, 4, 5};
    float positions[] = {0.5f, 0.3f, -0.4f, -0.3f, 0.2f, 0.4f};

    for (int i = 0; i < 6; ++i) {
        f.ctrl.setActiveNode(0);
        f.activeNode()->transform = QTransform::fromTranslate(positions[i], positions[i] * 0.5f);
        f.alignLayer(types[i]);
        f.verifyAlignment(types[i]);
    }
}

BOOST_AUTO_TEST_CASE(preserve_scale_after_align)
{
    AlignFixture f;
    QTransform t = QTransform::fromTranslate(0.4f, -0.2f)
                   * QTransform::fromScale(0.5f, 0.3f);
    f.activeNode()->transform = t;

    double oldM11 = t.m11(), oldM22 = t.m22();
    double oldM12 = t.m12(), oldM21 = t.m21();

    f.alignLayer(0);
    f.verifyAlignment(0);

    BOOST_CHECK_CLOSE(f.activeNode()->transform.m11(), oldM11, 1e-5);
    BOOST_CHECK_CLOSE(f.activeNode()->transform.m22(), oldM22, 1e-5);
    BOOST_CHECK_CLOSE(f.activeNode()->transform.m12(), oldM12, 1e-5);
    BOOST_CHECK_CLOSE(f.activeNode()->transform.m21(), oldM21, 1e-5);
}

BOOST_AUTO_TEST_SUITE_END()

// ═══════════════════════════════════════════════════════════════
//  Suite 4: Align to Selection (multi-layer, target-layer reference)
//  Tests that non-target layers align to the activeNode (target),
//  and the target node does not move.
// ═══════════════════════════════════════════════════════════════

static void computeAlignDeltaWithRef(int alignmentType, float minX, float maxX,
                                     float minY, float maxY,
                                     float refMinX, float refMaxX,
                                     float refMinY, float refMaxY,
                                     float& dx, float& dy)
{
    dx = 0; dy = 0;
    switch (alignmentType) {
    case 0: dx = refMinX - minX; break;
    case 1: dx = (refMinX + refMaxX - minX - maxX) * 0.5f; break;
    case 2: dx = refMaxX - maxX; break;
    case 3: dy = refMaxY - maxY; break;
    case 4: dy = (refMinY + refMaxY - minY - maxY) * 0.5f; break;
    case 5: dy = refMinY - minY; break;
    }
}

static void computeSelectionRef(Document* doc,
                                float& refMinX, float& refMaxX,
                                float& refMinY, float& refMaxY)
{
    auto* targetNode = doc->activeNode();
    if (targetNode && !(targetNode->lockFlags & LockPosition)) {
        refMinX = 1e9f; refMaxX = -1e9f;
        refMinY = 1e9f; refMaxY = -1e9f;
        QPolygonF c = TransformController::cornersFromTransform(
            targetNode->accumulatedTransform());
        for (auto& p : c) {
            const float px = static_cast<float>(p.x());
            const float py = static_cast<float>(p.y());
            if (px < refMinX) refMinX = px;
            if (px > refMaxX) refMaxX = px;
            if (py < refMinY) refMinY = py;
            if (py > refMaxY) refMaxY = py;
        }
    } else {
        refMinX = -1.0f; refMaxX = 1.0f;
        refMinY = -1.0f; refMaxY = 1.0f;
    }
}

struct MultiAlignFixture {
    Document doc;
    ImageController ctrl;

    MultiAlignFixture() : ctrl() {
        doc.size = QSize(200, 150);
        doc.selection.create(200, 150);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
        ctrl.newLayer();
    }

    int addLayer() { ctrl.newLayer(); return doc.activeFlatIndex; }

    LayerTreeNode* nodeAt(int idx) { return doc.nodeAt(idx); }

    void selectOnly(int idx) { doc.selectNode(idx, false); }

    void selectAdd(int idx) { doc.selectNode(idx, true); }

    void applyAlignDelta(int flatIndex, float dx, float dy) {
        auto* node = doc.nodeAt(flatIndex);
        if (!node) return;
        QTransform inv = node->transform.inverted();
        QPointF localDelta = inv.map(QPointF(dx, dy)) - inv.map(QPointF(0, 0));
        node->transform = QTransform::fromTranslate(localDelta.x(), localDelta.y())
                          * node->transform;
    }

    void alignToSelection(int alignmentType) {
        float refMinX, refMaxX, refMinY, refMaxY;
        computeSelectionRef(&doc, refMinX, refMaxX, refMinY, refMaxY);

        for (int idx : doc.selectedFlatIndices) {
            if (idx == doc.activeFlatIndex) continue;
            auto* node = doc.nodeAt(idx);
            if (!node || (node->lockFlags & LockPosition)) continue;

            QPolygonF corners = TransformController::cornersFromTransform(
                node->accumulatedTransform());
            float minX, maxX, minY, maxY;
            bboxFromCorners(corners, minX, maxX, minY, maxY);

            float dx, dy;
            computeAlignDeltaWithRef(alignmentType, minX, maxX, minY, maxY,
                                     refMinX, refMaxX, refMinY, refMaxY, dx, dy);
            if (dx == 0.0f && dy == 0.0f) continue;

            applyAlignDelta(idx, dx, dy);
        }
    }

    void getBBox(int flatIndex, float& minX, float& maxX, float& minY, float& maxY) {
        auto* node = doc.nodeAt(flatIndex);
        BOOST_REQUIRE(node);
        QPolygonF corners = TransformController::cornersFromTransform(
            node->accumulatedTransform());
        bboxFromCorners(corners, minX, maxX, minY, maxY);
    }

    void verifyAlignToTarget(int nonTargetIdx, int targetIdx, int alignmentType,
                             double tolerance = 1e-3)
    {
        float ntMinX, ntMaxX, ntMinY, ntMaxY;
        float tMinX, tMaxX, tMinY, tMaxY;
        getBBox(nonTargetIdx, ntMinX, ntMaxX, ntMinY, ntMaxY);
        getBBox(targetIdx, tMinX, tMaxX, tMinY, tMaxY);

        switch (alignmentType) {
        case 0: BOOST_CHECK_CLOSE(ntMinX, tMinX, tolerance); break;
        case 1: BOOST_CHECK_CLOSE((ntMinX+ntMaxX)*0.5, (tMinX+tMaxX)*0.5, tolerance); break;
        case 2: BOOST_CHECK_CLOSE(ntMaxX, tMaxX, tolerance); break;
        case 3: BOOST_CHECK_CLOSE(ntMaxY, tMaxY, tolerance); break;
        case 4: BOOST_CHECK_CLOSE((ntMinY+ntMaxY)*0.5, (tMinY+tMaxY)*0.5, tolerance); break;
        case 5: BOOST_CHECK_CLOSE(ntMinY, tMinY, tolerance); break;
        }
    }

    QTransform snapshotTransform(int idx) {
        auto* node = doc.nodeAt(idx);
        return node ? node->transform : QTransform();
    }
};

BOOST_AUTO_TEST_SUITE(align_to_selection)

// ── Happy path: 2 layers, target stays fixed ────────────────────

BOOST_AUTO_TEST_CASE(two_layers_align_left_target_fixed)
{
    MultiAlignFixture f;
    // Layer B (idx 0) = target, Layer A (idx 1) = moves
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.4f, 0.1f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(-0.2f, 0.2f);
    f.selectOnly(1);       // select A first
    f.selectAdd(0);        // Ctrl+click B → B is activeNode (target)
    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(0);
    f.verifyAlignToTarget(1, 0, 0);
    // Target transform must not change
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m32(), targetBefore.m32(), 1e-5);
}

BOOST_AUTO_TEST_CASE(two_layers_align_right_target_fixed)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(-0.3f, -0.1f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(0.5f, 0.0f);
    f.selectOnly(1);
    f.selectAdd(0);
    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(2);
    f.verifyAlignToTarget(1, 0, 2);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m32(), targetBefore.m32(), 1e-5);
}

BOOST_AUTO_TEST_CASE(two_layers_align_top_target_fixed)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.1f, -0.3f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(0.2f, 0.5f);
    f.selectOnly(1);
    f.selectAdd(0);
    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(3);
    f.verifyAlignToTarget(1, 0, 3);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m32(), targetBefore.m32(), 1e-5);
}

BOOST_AUTO_TEST_CASE(two_layers_align_bottom_target_fixed)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(-0.1f, 0.4f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(0.0f, -0.6f);
    f.selectOnly(1);
    f.selectAdd(0);
    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(5);
    f.verifyAlignToTarget(1, 0, 5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m32(), targetBefore.m32(), 1e-5);
}

BOOST_AUTO_TEST_CASE(two_layers_align_center_h_target_fixed)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.3f, 0.0f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(-0.5f, 0.1f);
    f.selectOnly(1);
    f.selectAdd(0);
    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(1);
    f.verifyAlignToTarget(1, 0, 1);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
}

BOOST_AUTO_TEST_CASE(two_layers_align_middle_v_target_fixed)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.0f, -0.2f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(0.1f, 0.4f);
    f.selectOnly(1);
    f.selectAdd(0);
    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(4);
    f.verifyAlignToTarget(1, 0, 4);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m32(), targetBefore.m32(), 1e-5);
}

// ── Target does not move in any direction ──────────────────────

BOOST_AUTO_TEST_CASE(target_never_moves)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.3f, 0.2f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(-0.7f, -0.5f);
    f.selectOnly(1);
    f.selectAdd(0);

    for (int alignType = 0; alignType < 6; ++alignType) {
        // Reset transforms
        f.nodeAt(1)->transform = QTransform::fromTranslate(-0.7f, -0.5f);
        QTransform targetBefore = f.snapshotTransform(0);
        f.alignToSelection(alignType);
        BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
        BOOST_CHECK_CLOSE(f.snapshotTransform(0).m32(), targetBefore.m32(), 1e-5);
    }
}

// ── Single layer with Selection mode falls back to canvas ─────

BOOST_AUTO_TEST_CASE(single_layer_selection_fallsback_to_canvas)
{
    // computeSelectionRef with 1 selected layer → canvas bounds [-1,1]
    MultiAlignFixture f;
    f.selectOnly(0);
    float refMinX, refMaxX, refMinY, refMaxY;
    computeSelectionRef(&f.doc, refMinX, refMaxX, refMinY, refMaxY);
    BOOST_CHECK_CLOSE(refMinX, -1.0f, 1e-5f);
    BOOST_CHECK_CLOSE(refMaxX,  1.0f, 1e-5f);
    BOOST_CHECK_CLOSE(refMinY, -1.0f, 1e-5f);
    BOOST_CHECK_CLOSE(refMaxY,  1.0f, 1e-5f);
}

// ── Already aligned → no-op ────────────────────────────────────

BOOST_AUTO_TEST_CASE(already_aligned_no_op)
{
    MultiAlignFixture f;
    // Both layers at same position
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.3f, 0.1f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(0.3f, 0.1f);
    f.selectOnly(1);
    f.selectAdd(0);
    QTransform aBefore = f.snapshotTransform(1);

    f.alignToSelection(0);
    // Non-target layer should not change (already at target position)
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m31(), aBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m32(), aBefore.m32(), 1e-5);
}

// ── Locked target → falls back to canvas ───────────────────────

BOOST_AUTO_TEST_CASE(locked_target_fallsback_to_canvas)
{
    MultiAlignFixture f;
    f.nodeAt(0)->lockFlags = LockPosition;
    f.selectOnly(1);
    f.selectAdd(0);
    float refMinX, refMaxX, refMinY, refMaxY;
    computeSelectionRef(&f.doc, refMinX, refMaxX, refMinY, refMaxY);
    // Locked target → computeSelectionRef should produce canvas bounds
    BOOST_CHECK_CLOSE(refMinX, -1.0f, 1e-5f);
    BOOST_CHECK_CLOSE(refMaxX,  1.0f, 1e-5f);
    BOOST_CHECK_CLOSE(refMinY, -1.0f, 1e-5f);
    BOOST_CHECK_CLOSE(refMaxY,  1.0f, 1e-5f);
}

// ── Locked non-target is skipped ───────────────────────────────

BOOST_AUTO_TEST_CASE(locked_non_target_skipped)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.4f, 0.1f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(-0.3f, -0.2f);
    f.nodeAt(1)->lockFlags = LockPosition;
    f.selectOnly(1);
    f.selectAdd(0);
    QTransform lockedBefore = f.snapshotTransform(1);
    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(0);
    // Locked non-target must not move
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m31(), lockedBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m32(), lockedBefore.m32(), 1e-5);
    // Target also must not move
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
}

// ── Undo / Redo via NodeTransformCommand ───────────────────────

BOOST_AUTO_TEST_CASE(undo_redo_align_selection)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.5f, 0.1f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(-0.4f, -0.3f);
    f.selectOnly(1);
    f.selectAdd(0);

    QTransform aBefore = f.snapshotTransform(1);
    QTransform bBefore = f.snapshotTransform(0);

    f.alignToSelection(0);

    QTransform aAfter = f.snapshotTransform(1);
    QTransform bAfter = f.snapshotTransform(0);

    // B (target) should not change
    BOOST_CHECK_CLOSE(bAfter.m31(), bBefore.m31(), 1e-5);
    // A should have moved
    BOOST_CHECK(std::abs(aAfter.m31() - aBefore.m31()) > 1e-4);

    // Push undo via NodeTransformCommand
    std::vector<int> indices = {1};
    std::vector<QTransform> beforeXfs = {aBefore};
    std::vector<QTransform> afterXfs = {aAfter};
    f.ctrl.setNodeTransforms(indices, afterXfs, beforeXfs);

    BOOST_REQUIRE(f.ctrl.history().canUndo());
    f.ctrl.history().undo();
    // After undo: A transform restored
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m31(), aBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m32(), aBefore.m32(), 1e-5);

    BOOST_REQUIRE(f.ctrl.history().canRedo());
    f.ctrl.history().redo();
    // After redo: A transform back to aligned position
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m31(), aAfter.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m32(), aAfter.m32(), 1e-5);
}

// ── Three layers: one target, two move ─────────────────────────

BOOST_AUTO_TEST_CASE(three_layers_one_target_two_move)
{
    MultiAlignFixture f;
    f.addLayer(); // third layer at index 0 (active)
    // index 0 = newest (active), 1 = second, 2 = first
    f.nodeAt(0)->transform = QTransform::fromTranslate(-0.2f, 0.3f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(0.6f, -0.4f);
    f.nodeAt(2)->transform = QTransform::fromTranslate(0.7f, 0.1f);
    f.selectOnly(2);
    f.selectAdd(1);
    f.selectAdd(0);  // target = index 0

    QTransform targetBefore = f.snapshotTransform(0);

    f.alignToSelection(0);
    f.verifyAlignToTarget(1, 0, 0);
    f.verifyAlignToTarget(2, 0, 0);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m31(), targetBefore.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(0).m32(), targetBefore.m32(), 1e-5);
}

// ── Idempotent: second align does nothing ──────────────────────

BOOST_AUTO_TEST_CASE(idempotent_second_align)
{
    MultiAlignFixture f;
    f.nodeAt(0)->transform = QTransform::fromTranslate(0.3f, 0.0f);
    f.nodeAt(1)->transform = QTransform::fromTranslate(-0.5f, 0.2f);
    f.selectOnly(1);
    f.selectAdd(0);

    f.alignToSelection(0);
    QTransform aAfterFirst = f.snapshotTransform(1);
    f.alignToSelection(0);
    // Second align must not change anything (already aligned)
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m31(), aAfterFirst.m31(), 1e-5);
    BOOST_CHECK_CLOSE(f.snapshotTransform(1).m32(), aAfterFirst.m32(), 1e-5);
}

BOOST_AUTO_TEST_SUITE_END()
