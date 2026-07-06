#define BOOST_TEST_MODULE ViewportCameraTest
#include <boost/test/included/unit_test.hpp>

#include "core/ViewportCamera.hpp"
#include <QPointF>
#include <QRectF>

BOOST_AUTO_TEST_SUITE(viewport_camera)

BOOST_AUTO_TEST_CASE(default_state)
{
    core::ViewportCamera cam;
    BOOST_CHECK_CLOSE(cam.zoom, 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.panOffset.x(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.panOffset.y(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.x(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.y(), 1.0f, 0.001f);
}

// ── updateExtents ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(update_extents_square_doc_square_vp)
{
    core::ViewportCamera cam;
    cam.updateExtents(800, 800, 800, 800);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.x(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.y(), 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(update_extents_wide_doc)
{
    core::ViewportCamera cam;
    // doc 1600x800 (aspect 2.0), vp 800x800 (aspect 1.0)
    // docAspect > vpAspect => hx=1, hy=vpAspect/docAspect = 1/2 = 0.5
    cam.updateExtents(1600, 800, 800, 800);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.x(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.y(), 0.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(update_extents_tall_viewport)
{
    core::ViewportCamera cam;
    // doc 800x800 (aspect 1.0), vp 800x1600 (aspect 0.5)
    // docAspect > vpAspect? 1.0 > 0.5 => hx=1, hy=0.5/1.0 = 0.5
    cam.updateExtents(800, 800, 800, 1600);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.x(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.y(), 0.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(update_extents_tall_doc)
{
    core::ViewportCamera cam;
    // doc 800x1600 (aspect 0.5), vp 800x800 (aspect 1.0)
    // docAspect < vpAspect => hx=docAspect/vpAspect = 0.5, hy=1
    cam.updateExtents(800, 1600, 800, 800);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.x(), 0.5f, 0.001f);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.y(), 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(update_extents_zero_size)
{
    core::ViewportCamera cam;
    cam.updateExtents(0, 800, 800, 800);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.x(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.y(), 1.0f, 0.001f);

    cam.updateExtents(800, 0, 800, 800);
    BOOST_CHECK_CLOSE(cam.canvasHalfExtents.x(), 1.0f, 0.001f);
}

// ── viewProjection ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(view_projection_default)
{
    core::ViewportCamera cam;
    QMatrix4x4 m = cam.viewProjection();
    // Default: translate(0,0) * scale(1) = identity
    BOOST_CHECK_CLOSE(m(0, 0), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(1, 1), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(0, 3), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(1, 3), 0.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(view_projection_with_pan_and_zoom)
{
    core::ViewportCamera cam;
    cam.panOffset = QPointF(2.0f, 3.0f);
    cam.zoom = 4.0f;
    QMatrix4x4 m = cam.viewProjection();
    // translate(2,3) * scale(4) => diagonal 4,0,0,4 with translation (2,3)
    BOOST_CHECK_CLOSE(m(0, 0), 4.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(1, 1), 4.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(2, 2), 4.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(0, 3), 2.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(1, 3), 3.0f, 0.001f);
}

// ── viewCanvasMvp ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(view_canvas_mvp_includes_extents)
{
    core::ViewportCamera cam;
    cam.panOffset = QPointF(1.0f, 2.0f);
    cam.zoom = 2.0f;
    cam.canvasHalfExtents = QPointF(0.5f, 1.0f);
    QMatrix4x4 m = cam.viewCanvasMvp();
    // translate(1,2) * scale(2) * scale(0.5, 1)
    // = translate(1,2) * scale(1, 2)
    // diagonal: 1,2,2,1; translation: 1,2
    BOOST_CHECK_CLOSE(m(0, 0), 1.0f, 0.001f);  // zoom=2 * hx=0.5 = 1
    BOOST_CHECK_CLOSE(m(1, 1), 2.0f, 0.001f);  // zoom=2 * hy=1.0 = 2
    BOOST_CHECK_CLOSE(m(0, 3), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(m(1, 3), 2.0f, 0.001f);
}

// ── zoomAt ────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(zoom_at_center_keeps_pan)
{
    core::ViewportCamera cam;
    cam.zoom = 1.0f;
    cam.panOffset = QPointF(0.0f, 0.0f);
    // Zoom from center of 800x600 viewport
    cam.zoomAt(2.0f, QPointF(400, 300), 800, 600);
    BOOST_CHECK_CLOSE(cam.zoom, 2.0f, 0.001f);
    // Since anchor is center and pan is zero, pan stays zero
    BOOST_CHECK_CLOSE(cam.panOffset.x(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.panOffset.y(), 0.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(zoom_at_corner_adjusts_pan)
{
    core::ViewportCamera cam;
    cam.zoom = 1.0f;
    cam.panOffset = QPointF(0.0f, 0.0f);
    // Zoom from top-left corner of 800x600 viewport
    // screenAnchor=(0,0) → NDC=(-1,1)
    // oldCx = (-1 - 0)/1 = -1
    // oldCy = (1 - 0)/1 = 1
    // new zoom = 2
    // pan.x = -1 - (-1)*2 = 1
    // pan.y = 1 - (1)*2 = -1
    cam.zoomAt(2.0f, QPointF(0, 0), 800, 600);
    BOOST_CHECK_CLOSE(cam.zoom, 2.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.panOffset.x(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.panOffset.y(), -1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(zoom_at_anchor_preserved)
{
    core::ViewportCamera cam;
    cam.zoom = 1.0f;
    cam.panOffset = QPointF(0.5f, -0.3f);
    // Pick anchor at screen center (400, 300) in 800x600 vp
    // ndx = 2*400/800 - 1 = 0, ndy = 1 - 2*300/600 = 0
    // oldCx = (0-0.5)/1 = -0.5, oldCy = (0-(-0.3))/1 = 0.3
    // zoom *= 2 => 2
    // new pan.x = 0 - (-0.5)*2 = 1
    // new pan.y = 0 - 0.3*2 = -0.6
    cam.zoomAt(2.0f, QPointF(400, 300), 800, 600);
    // Verify the anchor point maps to same NDC as before
    BOOST_CHECK_CLOSE(cam.zoom, 2.0f, 0.001f);
}

// ── clampZoom ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(clamp_zoom_min)
{
    core::ViewportCamera cam;
    cam.zoom = 0.001f;
    cam.clampZoom(0.1f, 10.0f);
    BOOST_CHECK_CLOSE(cam.zoom, 0.1f, 0.001f);
}

BOOST_AUTO_TEST_CASE(clamp_zoom_max)
{
    core::ViewportCamera cam;
    cam.zoom = 100.0f;
    cam.clampZoom(0.1f, 10.0f);
    BOOST_CHECK_CLOSE(cam.zoom, 10.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(clamp_zoom_no_op)
{
    core::ViewportCamera cam;
    cam.zoom = 5.0f;
    cam.clampZoom(0.1f, 10.0f);
    BOOST_CHECK_CLOSE(cam.zoom, 5.0f, 0.001f);
}

// ── screenToCanvasNdc ─────────────────────────────────────────

BOOST_AUTO_TEST_CASE(screen_to_canvas_ndc_center)
{
    core::ViewportCamera cam;
    cam.panOffset = QPointF(2.0f, 3.0f);
    cam.zoom = 1.0f;
    // Screen center (400,300) of 800x600 → NDC(0,0)
    // After removing pan: (0-2, 0-3) = (-2, -3)
    QPointF ndc = cam.screenToCanvasNdc(QPointF(400, 300), 800, 600);
    BOOST_CHECK_CLOSE(ndc.x(), -2.0f, 0.001f);
    BOOST_CHECK_CLOSE(ndc.y(), -3.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(screen_to_canvas_ndc_with_zoom)
{
    core::ViewportCamera cam;
    cam.panOffset = QPointF(0.0f, 0.0f);
    cam.zoom = 2.0f;
    // Screen center (400,300) → NDC(0,0)
    QPointF ndc = cam.screenToCanvasNdc(QPointF(400, 300), 800, 600);
    BOOST_CHECK_CLOSE(ndc.x(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(ndc.y(), 0.0f, 0.001f);

    // Screen top-left (0,0) → NDC(-1,1) → invZoom/zoom → (-0.5,0.5)
    ndc = cam.screenToCanvasNdc(QPointF(0, 0), 800, 600);
    BOOST_CHECK_CLOSE(ndc.x(), -0.5f, 0.001f);
    BOOST_CHECK_CLOSE(ndc.y(), 0.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(screen_to_canvas_ndc_zero_viewport)
{
    core::ViewportCamera cam;
    QPointF ndc = cam.screenToCanvasNdc(QPointF(100, 100), 0, 0);
    BOOST_CHECK_CLOSE(ndc.x(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(ndc.y(), 0.0f, 0.001f);
}

// ── visibleNdcRect ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(visible_ndc_rect_default)
{
    core::ViewportCamera cam;
    QRectF r = cam.visibleNdcRect();
    BOOST_CHECK_CLOSE(r.left(), -1.0f, 0.001f);
    BOOST_CHECK_CLOSE(r.right(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(r.top(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(r.bottom(), -1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(visible_ndc_rect_zoomed)
{
    core::ViewportCamera cam;
    cam.zoom = 2.0f;
    QRectF r = cam.visibleNdcRect();
    BOOST_CHECK_CLOSE(r.left(), -0.5f, 0.001f);
    BOOST_CHECK_CLOSE(r.right(), 0.5f, 0.001f);
}

// ── visiblePixelRect ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(visible_pixel_rect_default)
{
    core::ViewportCamera cam;
    cam.updateExtents(800, 600, 800, 600);
    QRectF r = cam.visiblePixelRect(800, 600);
    // With doc=800x600, vp=800x600, zoom=1: visible should be full doc
    BOOST_CHECK_CLOSE(r.left(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(r.right(), 800.0f, 0.001f);
    BOOST_CHECK_CLOSE(r.top(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(r.bottom(), 600.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(visible_pixel_rect_vertical_pan_not_mirrored)
{
    // Regression: pixel Y is flipped vs NDC Y, so panning must not mirror the
    // visible rect around the document's vertical center (which used to cull
    // the wrong tile rows and leave a transparent band while panning).
    core::ViewportCamera cam;
    cam.updateExtents(800, 600, 800, 600); // hx = hy = 1
    cam.zoom = 1.0f;
    cam.panOffset = QPointF(0.0f, 0.5f);   // canvas pushed up → see lower part

    QRectF r = cam.visiblePixelRect(800, 600);
    BOOST_CHECK_CLOSE(r.top(), 150.0f, 0.01f);
    BOOST_CHECK_CLOSE(r.bottom(), 750.0f, 0.01f);
    BOOST_CHECK_CLOSE(r.left(), 0.0f, 0.01f);
    BOOST_CHECK_CLOSE(r.right(), 800.0f, 0.01f);
}

BOOST_AUTO_TEST_CASE(visible_pixel_rect_horizontal_pan)
{
    core::ViewportCamera cam;
    cam.updateExtents(800, 600, 800, 600); // hx = hy = 1
    cam.zoom = 1.0f;
    cam.panOffset = QPointF(0.5f, 0.0f);   // canvas pushed right → see left part

    QRectF r = cam.visiblePixelRect(800, 600);
    BOOST_CHECK_CLOSE(r.left(), -200.0f, 0.01f);
    BOOST_CHECK_CLOSE(r.right(), 600.0f, 0.01f);
    BOOST_CHECK_CLOSE(r.top(), 0.0f, 0.01f);
    BOOST_CHECK_CLOSE(r.bottom(), 600.0f, 0.01f);
}

// ── containsTile ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(contains_tile_inside)
{
    core::ViewportCamera cam;
    cam.updateExtents(800, 600, 800, 600);
    BOOST_CHECK(cam.containsTile(QRect(100, 100, 200, 200), 800, 600));
}

BOOST_AUTO_TEST_CASE(contains_tile_outside)
{
    core::ViewportCamera cam;
    cam.updateExtents(800, 600, 800, 600);
    // Pan so tile is off-screen
    cam.panOffset = QPointF(1000.0f, 1000.0f);
    BOOST_CHECK(!cam.containsTile(QRect(100, 100, 200, 200), 800, 600));
}

BOOST_AUTO_TEST_CASE(contains_tile_zero_doc)
{
    core::ViewportCamera cam;
    BOOST_CHECK(!cam.containsTile(QRect(0, 0, 256, 256), 0, 0));
}

// ── reset ─────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(reset_restores_defaults)
{
    core::ViewportCamera cam;
    cam.zoom = 10.0f;
    cam.panOffset = QPointF(5.0f, -3.0f);
    cam.reset();
    BOOST_CHECK_CLOSE(cam.zoom, 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.panOffset.x(), 0.0f, 0.001f);
    BOOST_CHECK_CLOSE(cam.panOffset.y(), 0.0f, 0.001f);
}

BOOST_AUTO_TEST_SUITE_END()
