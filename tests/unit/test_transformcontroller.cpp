#define BOOST_TEST_MODULE TransformControllerTest
#include <boost/test/included/unit_test.hpp>

#include "transform/TransformController.hpp"
#include "transform/TransformTypes.hpp"
#include "core/Layer.hpp"

#include <QTransform>
#include <QPointF>
#include <QPolygonF>
#include <QSize>
#include <Qt>
#include <cmath>

static const double EPSILON = 1e-5;

// Helper: convert canvas-NDC → screen pixel for a given viewport
static QPointF canvasToScreen(QPointF canvasNdc, const QSize& vp,
                               float zoom, QPointF pan, QPointF halfExt)
{
    return TransformController::canvasNdcToScreen(
        canvasNdc, zoom, pan, halfExt, vp);
}

// ── decompose / compose roundtrip ─────────────────────────────

BOOST_AUTO_TEST_SUITE(decompose_compose)

BOOST_AUTO_TEST_CASE(identity_roundtrip)
{
    QTransform t;
    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(t, hw, hh, center, rot);
    BOOST_CHECK_CLOSE(static_cast<double>(hw), 1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(hh), 1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(center.x()), 0.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(center.y()), 0.0, EPSILON);
    BOOST_CHECK_SMALL(static_cast<double>(rot), EPSILON);

    QTransform t2 = TransformController::compose(hw, hh, center, rot);
    BOOST_CHECK_CLOSE(t.m11(), t2.m11(), EPSILON);
    BOOST_CHECK_CLOSE(t.m12(), t2.m12(), EPSILON);
    BOOST_CHECK_CLOSE(t.m21(), t2.m21(), EPSILON);
    BOOST_CHECK_CLOSE(t.m22(), t2.m22(), EPSILON);
    BOOST_CHECK_CLOSE(t.m31(), t2.m31(), EPSILON);
    BOOST_CHECK_CLOSE(t.m32(), t2.m32(), EPSILON);
}

BOOST_AUTO_TEST_CASE(translation_roundtrip)
{
    QTransform t;
    t.translate(0.3, -0.7);
    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(t, hw, hh, center, rot);
    BOOST_CHECK_CLOSE(static_cast<double>(hw), 1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(hh), 1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(center.x()), 0.3, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(center.y()), -0.7, EPSILON);
    BOOST_CHECK_SMALL(static_cast<double>(rot), EPSILON);

    QTransform t2 = TransformController::compose(hw, hh, center, rot);
    BOOST_CHECK_CLOSE(t2.m31(), 0.3, EPSILON);
    BOOST_CHECK_CLOSE(t2.m32(), -0.7, EPSILON);
}

BOOST_AUTO_TEST_CASE(scale_roundtrip)
{
    QTransform t;
    t.scale(2.0, 1.5);
    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(t, hw, hh, center, rot);
    BOOST_CHECK_CLOSE(static_cast<double>(hw), 2.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(hh), 1.5, EPSILON);
    BOOST_CHECK_SMALL(static_cast<double>(rot), EPSILON);

    QTransform t2 = TransformController::compose(hw, hh, center, rot);
    BOOST_CHECK_CLOSE(t2.m11(), 2.0, EPSILON);
    BOOST_CHECK_CLOSE(t2.m22(), 1.5, EPSILON);
}

BOOST_AUTO_TEST_CASE(rotation_roundtrip)
{
    float angle = 0.7f;
    QTransform t;
    t.rotateRadians(static_cast<double>(angle));
    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(t, hw, hh, center, rot);
    BOOST_CHECK_CLOSE(static_cast<double>(hw), 1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(hh), 1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(rot), static_cast<double>(angle), EPSILON);

    QTransform t2 = TransformController::compose(hw, hh, center, rot);
    double c = std::cos(static_cast<double>(angle));
    BOOST_CHECK_CLOSE(t2.m11(), c, EPSILON);
    BOOST_CHECK_CLOSE(t2.m22(), c, EPSILON);
}

BOOST_AUTO_TEST_CASE(combined_roundtrip)
{
    QTransform t;
    t.translate(0.2, -0.5);
    t.rotateRadians(0.4);
    t.scale(2.5, 1.3);

    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(t, hw, hh, center, rot);

    QTransform t2 = TransformController::compose(hw, hh, center, rot);

    auto cmp = [](double a, double b) { return std::abs(a - b) < 1e-5; };
    BOOST_CHECK(cmp(t.m11(), t2.m11()));
    BOOST_CHECK(cmp(t.m12(), t2.m12()));
    BOOST_CHECK(cmp(t.m21(), t2.m21()));
    BOOST_CHECK(cmp(t.m22(), t2.m22()));
    BOOST_CHECK(cmp(t.m31(), t2.m31()));
    BOOST_CHECK(cmp(t.m32(), t2.m32()));
}

BOOST_AUTO_TEST_SUITE_END()

// ── cornersFromTransform ──────────────────────────────────────

BOOST_AUTO_TEST_SUITE(corners)

BOOST_AUTO_TEST_CASE(identity_corners)
{
    QTransform t;
    QPolygonF poly = TransformController::cornersFromTransform(t);
    BOOST_REQUIRE_EQUAL(poly.size(), 4);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[0].x()), -1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[0].y()), -1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[1].x()),  1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[1].y()), -1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[2].x()),  1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[2].y()),  1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[3].x()), -1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[3].y()),  1.0, EPSILON);
}

BOOST_AUTO_TEST_CASE(translated_corners)
{
    QTransform t;
    t.translate(0.5, -0.3);
    QPolygonF poly = TransformController::cornersFromTransform(t);
    // All corners shifted by +0.5 x, -0.3 y
    BOOST_CHECK_CLOSE(static_cast<double>(poly[0].x()), -0.5, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[0].y()), -1.3, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[1].x()),  1.5, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[1].y()), -1.3, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[2].x()),  1.5, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[2].y()),  0.7, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[3].x()), -0.5, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[3].y()),  0.7, EPSILON);
}

BOOST_AUTO_TEST_CASE(scaled_corners)
{
    QTransform t;
    t.scale(2.0, 1.5);
    QPolygonF poly = TransformController::cornersFromTransform(t);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[0].x()), -2.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[0].y()), -1.5, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[2].x()),  2.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(poly[2].y()),  1.5, EPSILON);
}

BOOST_AUTO_TEST_SUITE_END()

// ── handleAt ──────────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(handle_hit)

// Helper: build screen-NDC corners for identity transform
static QPolygonF makeScreenNdcCorners()
{
    QPolygonF c;
    c << QPointF(-1, -1) << QPointF(1, -1)
      << QPointF(1, 1) << QPointF(-1, 1);
    return c;
}

BOOST_AUTO_TEST_CASE(hit_top_left_corner)
{
    // Screen pixel (0, 100) → NDC (-1, -1) = TopLeft
    auto h = TransformController::handleAt(QPointF(0, 100),
        makeScreenNdcCorners(), QSize(100, 100));
    BOOST_CHECK(h == HandlePosition::TopLeft);
}

BOOST_AUTO_TEST_CASE(hit_bottom_right_corner)
{
    // Screen pixel (100, 0) → NDC (1, 1) = BottomRight
    auto h = TransformController::handleAt(QPointF(100, 0),
        makeScreenNdcCorners(), QSize(100, 100));
    BOOST_CHECK(h == HandlePosition::BottomRight);
}

BOOST_AUTO_TEST_CASE(hit_top_edge)
{
    // Screen pixel (50, 100) → NDC (0, -1) = Top edge
    auto h = TransformController::handleAt(QPointF(50, 100),
        makeScreenNdcCorners(), QSize(100, 100));
    BOOST_CHECK(h == HandlePosition::Top);
}

BOOST_AUTO_TEST_CASE(hit_right_edge)
{
    // Screen pixel (100, 50) → NDC (1, 0) = Right edge
    auto h = TransformController::handleAt(QPointF(100, 50),
        makeScreenNdcCorners(), QSize(100, 100));
    BOOST_CHECK(h == HandlePosition::Right);
}

BOOST_AUTO_TEST_CASE(hit_center)
{
    // Screen pixel (50, 50) → NDC (0, 0) = Center
    auto h = TransformController::handleAt(QPointF(50, 50),
        makeScreenNdcCorners(), QSize(100, 100));
    BOOST_CHECK(h == HandlePosition::Center);
}

BOOST_AUTO_TEST_CASE(miss_outside)
{
    auto h = TransformController::handleAt(QPointF(200, 200),
        makeScreenNdcCorners(), QSize(100, 100));
    BOOST_CHECK(h == HandlePosition::None);
}

BOOST_AUTO_TEST_SUITE_END()

// ── Coordinate conversion ─────────────────────────────────────

BOOST_AUTO_TEST_SUITE(coordinate_conversion)

BOOST_AUTO_TEST_CASE(screen_to_canvas_identity)
{
    QPointF result = TransformController::screenToCanvasNdc(
        QPointF(50, 50), 1.0f, QPointF(0, 0), QPointF(1, 1), QSize(100, 100));
    BOOST_CHECK_SMALL(result.x(), EPSILON);
    BOOST_CHECK_SMALL(result.y(), EPSILON);
}

BOOST_AUTO_TEST_CASE(screen_to_canvas_top_left)
{
    QPointF result = TransformController::screenToCanvasNdc(
        QPointF(0, 0), 1.0f, QPointF(0, 0), QPointF(1, 1), QSize(100, 100));
    BOOST_CHECK_CLOSE(static_cast<double>(result.x()), -1.0, EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(result.y()), 1.0, EPSILON);
}

BOOST_AUTO_TEST_CASE(roundtrip_screen_canvas)
{
    QPointF screen(30, 70);
    float zoom = 1.5f;
    QPointF pan(0.2f, -0.1f);
    QPointF halfExt(0.8f, 0.9f);
    QSize viewport(200, 150);

    QPointF canvas = TransformController::screenToCanvasNdc(
        screen, zoom, pan, halfExt, viewport);
    QPointF back = TransformController::canvasNdcToScreen(
        canvas, zoom, pan, halfExt, viewport);

    BOOST_CHECK_SMALL(static_cast<double>(back.x() - screen.x()), 1e-4);
    BOOST_CHECK_SMALL(static_cast<double>(back.y() - screen.y()), 1e-4);
}

BOOST_AUTO_TEST_SUITE_END()

// ── updateResize ──────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(resize)

static const QSize VP(200, 200);
static const QPointF HALF_EXT(1, 1);
static const QPointF PAN(0, 0);
static const float ZOOM = 1.0f;

// Helper: build a TransformState for resize from a given handle
static TransformState makeResizeState(HandlePosition handle,
                                       const QTransform& startT,
                                       const QSize& vp,
                                       float zoom,
                                       const QPointF& pan,
                                       const QPointF& halfExt)
{
    TransformState s;
    s.mode = InteractionMode::Resizing;
    s.activeHandle = handle;
    s.flatIndex = 0;
    s.startTransform = startT;

    TransformController::decompose(startT, s.startHw, s.startHh, s.center, s.rotation);

    float hx = 0, hy = 0;
    switch (handle) {
    case HandlePosition::BottomRight: hx = 1; hy = 1; break;
    case HandlePosition::Right:       hx = 1; hy = 0; break;
    case HandlePosition::Bottom:      hx = 0; hy = 1; break;
    default: break;
    }

    // Compute handle position in canvas NDC
    float a = s.rotation;
    float c = std::cos(a);
    float si = std::sin(a);
    QPointF handleCanvas = s.center
        + QPointF(c * s.startHw * hx - si * s.startHh * hy,
                   si * s.startHw * hx + c * s.startHh * hy);

    // Compute anchor
    s.anchorCanvas = s.center
        - QPointF(c * s.startHw * hx - si * s.startHh * hy,
                   si * s.startHw * hx + c * s.startHh * hy);

    // Convert handle to screen pixel position
    s.startMouseScreen = canvasToScreen(handleCanvas, vp, zoom, pan, halfExt);
    return s;
}

BOOST_AUTO_TEST_CASE(no_mouse_movement_returns_start)
{
    QTransform startT;
    auto state = makeResizeState(HandlePosition::BottomRight, startT,
                                  VP, ZOOM, PAN, HALF_EXT);

    QTransform result = TransformController::updateResize(
        state, state.startMouseScreen, ZOOM, PAN, HALF_EXT, VP, Qt::NoModifier);

    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-4; };
    BOOST_CHECK(approx(startT.m11(), result.m11()));
    BOOST_CHECK(approx(startT.m12(), result.m12()));
    BOOST_CHECK(approx(startT.m21(), result.m21()));
    BOOST_CHECK(approx(startT.m22(), result.m22()));
    BOOST_CHECK(approx(startT.m31(), result.m31()));
    BOOST_CHECK(approx(startT.m32(), result.m32()));
}

BOOST_AUTO_TEST_CASE(drag_right_increases_width)
{
    QTransform startT;
    auto state = makeResizeState(HandlePosition::Right, startT,
                                  VP, ZOOM, PAN, HALF_EXT);

    QPointF moved = state.startMouseScreen + QPointF(30, 0);
    QTransform result = TransformController::updateResize(
        state, moved, ZOOM, PAN, HALF_EXT, VP, Qt::NoModifier);

    float newHw, newHh;
    QPointF newCenter;
    float newRot;
    TransformController::decompose(result, newHw, newHh, newCenter, newRot);
    BOOST_CHECK(newHw > state.startHw);
    BOOST_CHECK_SMALL(static_cast<double>(newRot - state.rotation), EPSILON);
}

BOOST_AUTO_TEST_CASE(drag_right_shrink_flips)
{
    // Start with scaled layer
    QTransform startT;
    startT.scale(2.0, 1.0);
    auto state = makeResizeState(HandlePosition::Right, startT,
                                  VP, ZOOM, PAN, HALF_EXT);

    // Drag far left past the anchor → negative m11 (flip)
    QPointF moved = state.startMouseScreen - QPointF(500, 0);
    QTransform result = TransformController::updateResize(
        state, moved, ZOOM, PAN, HALF_EXT, VP, Qt::NoModifier);

    // decompose uses sqrt(m11²+m12²) which loses sign; check matrix directly
    BOOST_CHECK(result.m11() < 0);
}

BOOST_AUTO_TEST_CASE(default_maintains_aspect_ratio)
{
    QTransform startT; // identity → aspect = 1.0
    auto state = makeResizeState(HandlePosition::BottomRight, startT,
                                  VP, ZOOM, PAN, HALF_EXT);

    // Drag more horizontally than vertically
    QPointF moved = state.startMouseScreen + QPointF(60, 10);
    QTransform result = TransformController::updateResize(
        state, moved, ZOOM, PAN, HALF_EXT, VP, Qt::NoModifier);

    float newHw, newHh;
    QPointF newCenter;
    float newRot;
    TransformController::decompose(result, newHw, newHh, newCenter, newRot);
    // Aspect should be ≈ 1.0 (within 5% tolerance since boost uses percentage)
    double ratio = std::abs(static_cast<double>(newHw / newHh));
    BOOST_CHECK_CLOSE(ratio, 1.0, 5.0);
}

BOOST_AUTO_TEST_CASE(alt_keeps_center_fixed)
{
    QTransform startT;
    startT.translate(0.1, 0.2);
    auto state = makeResizeState(HandlePosition::BottomRight, startT,
                                  VP, ZOOM, PAN, HALF_EXT);

    QPointF moved = state.startMouseScreen + QPointF(30, 20);
    QTransform result = TransformController::updateResize(
        state, moved, ZOOM, PAN, HALF_EXT, VP, Qt::AltModifier);

    float newHw, newHh;
    QPointF newCenter;
    float newRot;
    TransformController::decompose(result, newHw, newHh, newCenter, newRot);
    BOOST_CHECK_CLOSE(static_cast<double>(newCenter.x()),
                      static_cast<double>(state.center.x()), EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(newCenter.y()),
                      static_cast<double>(state.center.y()), EPSILON);
}

BOOST_AUTO_TEST_CASE(resize_with_rotation_preserves_angle)
{
    QTransform startT;
    startT.rotateRadians(0.5);
    startT.scale(1.5, 1.0);
    auto state = makeResizeState(HandlePosition::BottomRight, startT,
                                  VP, ZOOM, PAN, HALF_EXT);

    QPointF moved = state.startMouseScreen + QPointF(30, 0);
    QTransform result = TransformController::updateResize(
        state, moved, ZOOM, PAN, HALF_EXT, VP, Qt::NoModifier);

    float newHw, newHh;
    QPointF newCenter;
    float newRot;
    TransformController::decompose(result, newHw, newHh, newCenter, newRot);
    BOOST_CHECK_CLOSE(static_cast<double>(newRot),
                      static_cast<double>(state.rotation), EPSILON);
    BOOST_CHECK(newHw != state.startHw);
}

BOOST_AUTO_TEST_SUITE_END()

// ── updateMove ────────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(move)

BOOST_AUTO_TEST_CASE(no_movement_identity)
{
    TransformState state;
    state.mode = InteractionMode::Moving;
    state.startMouseScreen = QPointF(100, 100);
    state.startTransform = QTransform();

    QTransform result = TransformController::updateMove(
        state, QPointF(100, 100), 1.0f,
        QPointF(1.0f, 1.0f), QTransform(), QSize(200, 200));

    auto approx = [](double a, double b) { return std::abs(a - b) < 1e-4; };
    BOOST_CHECK(approx(result.m11(), state.startTransform.m11()));
    BOOST_CHECK(approx(result.m12(), state.startTransform.m12()));
    BOOST_CHECK(approx(result.m21(), state.startTransform.m21()));
    BOOST_CHECK(approx(result.m22(), state.startTransform.m22()));
    BOOST_CHECK(approx(result.m31(), state.startTransform.m31()));
    BOOST_CHECK(approx(result.m32(), state.startTransform.m32()));
}

BOOST_AUTO_TEST_CASE(drag_right_moves_right)
{
    TransformState state;
    state.mode = InteractionMode::Moving;
    state.startMouseScreen = QPointF(100, 100);
    state.startTransform = QTransform();

    QTransform result = TransformController::updateMove(
        state, QPointF(150, 100), 1.0f,
        QPointF(1.0f, 1.0f), QTransform(), QSize(200, 200));

    double newTx = result.m31();
    double oldTx = state.startTransform.m31();
    BOOST_CHECK(newTx > oldTx);
}

BOOST_AUTO_TEST_CASE(canvas_half_extents_scales_delta)
{
    // Non-square canvas: hx=0.5 should double the X delta compared to hx=1.0
    TransformState state;
    state.mode = InteractionMode::Moving;
    state.startMouseScreen = QPointF(100, 100);
    state.startTransform = QTransform();

    // Drag right by 50px on 200px viewport → ndcDx = 0.5, dx = 0.5/1.0/0.5 = 1.0
    QTransform result = TransformController::updateMove(
        state, QPointF(150, 100), 1.0f,
        QPointF(0.5f, 1.0f), QTransform(), QSize(200, 200));

    BOOST_CHECK_CLOSE(static_cast<double>(result.m31()), 1.0, 1e-4);
}

BOOST_AUTO_TEST_CASE(parent_accum_inverses_delta)
{
    // Parent has scale(2,1): parentAccum.m11()=2 → X delta should be halved
    TransformState state;
    state.mode = InteractionMode::Moving;
    state.startMouseScreen = QPointF(100, 100);
    state.startTransform = QTransform();

    QTransform parentAccum;
    parentAccum.scale(2.0, 1.0);  // m11=2

    // ndcDx = 0.5, /hx(1.0) = 0.5, /zoom(1.0) = 0.5, /parentAccum.m11(2.0) = 0.25
    QTransform result = TransformController::updateMove(
        state, QPointF(150, 100), 1.0f,
        QPointF(1.0f, 1.0f), parentAccum, QSize(200, 200));

    BOOST_CHECK_CLOSE(static_cast<double>(result.m31()), 0.25, 1e-4);
}

BOOST_AUTO_TEST_CASE(combined_canvas_and_parent)
{
    // hx=0.5 + parent scale(2) → divisor = 0.5*2 = 1 → delta = ndcDx/hx/parentScale
    TransformState state;
    state.mode = InteractionMode::Moving;
    state.startMouseScreen = QPointF(100, 100);
    state.startTransform = QTransform();

    QTransform parentAccum;
    parentAccum.scale(2.0, 1.0);  // m11=2

    // ndcDx = 0.5, /hx(0.5) = 1.0, /zoom(1) = 1.0, /parent.m11(2) = 0.5
    QTransform result = TransformController::updateMove(
        state, QPointF(150, 100), 1.0f,
        QPointF(0.5f, 1.0f), parentAccum, QSize(200, 200));

    BOOST_CHECK_CLOSE(static_cast<double>(result.m31()), 0.5, 1e-4);
}

BOOST_AUTO_TEST_SUITE_END()

// ── updateRotate ──────────────────────────────────────────────

BOOST_AUTO_TEST_SUITE(rotate)

static QPointF centerToScreen(QPointF centerCanvas, const QSize& vp,
                               float zoom, const QPointF& pan,
                               const QPointF& halfExt)
{
    return canvasToScreen(centerCanvas, vp, zoom, pan, halfExt);
}

BOOST_AUTO_TEST_CASE(no_movement_identity)
{
    TransformState state;
    state.mode = InteractionMode::Rotating;
    state.center = QPointF(0, 0);
    state.startTransform = QTransform();
    state.startHw = 1.0f;
    state.startHh = 1.0f;
    state.rotation = 0.0f;

    // Mouse directly above center (+y in canvas-NDC = up, +y in screen = down)
    // Center in screen: (100, 100). Point above center: (100, 50)
    state.startMouseScreen = QPointF(100, 50);

    QTransform result = TransformController::updateRotate(
        state, QPointF(100, 50), 1.0f, QPointF(0, 0), QPointF(1, 1),
        QSize(200, 200), Qt::NoModifier);

    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(result, hw, hh, center, rot);
    BOOST_CHECK_SMALL(static_cast<double>(rot), EPSILON);
}

BOOST_AUTO_TEST_CASE(drag_clockwise_increases_angle)
{
    TransformState state;
    state.mode = InteractionMode::Rotating;
    state.center = QPointF(0, 0);
    state.startTransform = QTransform();
    state.startHw = 1.0f;
    state.startHh = 1.0f;
    state.rotation = 0.0f;

    // start: mouse above center at (100, 50) → screen-NDC (0, 0.5)
    // Actually the angle from center to mouse is PI/2 (straight up).
    // But wait, in screen coords: (100, 50) in 200x200 viewport:
    // ndcX = 2*100/200 - 1 = 0, ndcY = 1 - 2*50/200 = 0.5
    // This is NOT straight up from center - it's above-right
    
    // Directly above center → screen (100, 0) → canvas NDC (0, 1) → angle = π/2
    state.startMouseScreen = QPointF(100, 0);

    // Move COUNTERCLOCKWISE: up-left → screen (50, 50) → canvas NDC (-0.5, 0.5) → angle = 3π/4
    // Delta = 3π/4 - π/2 = π/4 > 0 → positive rotation ✓
    QTransform result = TransformController::updateRotate(
        state, QPointF(50, 50), 1.0f, QPointF(0, 0), QPointF(1, 1),
        QSize(200, 200), Qt::NoModifier);

    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(result, hw, hh, center, rot);
    BOOST_CHECK(rot > 0.0f);
}

BOOST_AUTO_TEST_CASE(shift_snaps_to_15_degrees)
{
    TransformState state;
    state.mode = InteractionMode::Rotating;
    state.center = QPointF(0, 0);
    state.startTransform = QTransform();
    state.startHw = 1.0f;
    state.startHh = 1.0f;
    state.rotation = 0.0f;
    state.startMouseScreen = QPointF(100, 0);

    // Move CCW from straight up (90°) to roughly 130° → delta ≈ 40°
    // Screen (63, 20): ndc (-0.37, 0.8) → angle ≈ 115° (1 radian CCW from straight up)
    // 1 radian ≈ 57°, snaps to 60° = 4 × 15°
    QTransform result = TransformController::updateRotate(
        state, QPointF(63, 20), 1.0f, QPointF(0, 0), QPointF(1, 1),
        QSize(200, 200), Qt::ShiftModifier);

    float hw, hh, rot;
    QPointF center;
    TransformController::decompose(result, hw, hh, center, rot);

    double deg = static_cast<double>(rot) * 180.0 / M_PI;
    double remainder = std::abs(std::fmod(std::abs(deg), 15.0));
    BOOST_CHECK(remainder < 0.5 || remainder > 14.5);
}

BOOST_AUTO_TEST_CASE(rotating_rectangular_layer_preserves_scale)
{
    TransformState state;
    state.mode = InteractionMode::Rotating;
    state.center = QPointF(0.15, -0.2);
    state.startHw = 2.0f;
    state.startHh = 0.65f;
    state.rotation = 0.35f;
    state.startTransform = TransformController::composeVisual(
        state.startHw, state.startHh, state.center, state.rotation,
        QPointF(0.75f, 1.0f), QSize(300, 200));
    state.startMouseScreen = QPointF(100, 0);

    QTransform result = TransformController::updateRotate(
        state, QPointF(40, 45), 1.0f, QPointF(0, 0), QPointF(0.75f, 1.0f),
        QSize(300, 200), Qt::NoModifier);

    float hw, hh, rot;
    QPointF center;
    TransformController::decomposeVisual(result, QPointF(0.75f, 1.0f),
        QSize(300, 200), hw, hh, center, rot);

    BOOST_CHECK_CLOSE(static_cast<double>(hw),
                      static_cast<double>(state.startHw), EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(hh),
                      static_cast<double>(state.startHh), EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(center.x()),
                      static_cast<double>(state.center.x()), EPSILON);
    BOOST_CHECK_CLOSE(static_cast<double>(center.y()),
                      static_cast<double>(state.center.y()), EPSILON);
}

BOOST_AUTO_TEST_SUITE_END()
