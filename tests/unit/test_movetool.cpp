#define BOOST_TEST_MODULE MoveToolTest
#include <boost/test/included/unit_test.hpp>

#include "tools/MoveTool.hpp"

#include <QTransform>
#include <QPointF>
#include <QSize>

BOOST_AUTO_TEST_SUITE(movetool)

BOOST_AUTO_TEST_CASE(initial_state_not_dragging)
{
    MoveTool tool;
    BOOST_CHECK(!tool.isDragging());
}

BOOST_AUTO_TEST_CASE(begin_drag_sets_active)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(800, 600));
    BOOST_CHECK(tool.isDragging());
}

BOOST_AUTO_TEST_CASE(endDrag_clears_active)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(800, 600));
    tool.endDrag();
    BOOST_CHECK(!tool.isDragging());
}

BOOST_AUTO_TEST_CASE(drag_right_moves_transform_positive_x)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(800, 600));

    tool.drag(QPointF(400, 0));
    BOOST_CHECK_CLOSE(xf.m31(), 1.0f, 0.001f); // ndcX = 2*400/800 = 1.0
    BOOST_CHECK_CLOSE(double(xf.m32()), 0.0, 0.001);
}

BOOST_AUTO_TEST_CASE(drag_left_moves_transform_negative_x)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(400, 0), 1.0f, QSize(800, 600));

    tool.drag(QPointF(0, 0));
    BOOST_CHECK_CLOSE(xf.m31(), -1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(drag_up_moves_transform_positive_y)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 300), 1.0f, QSize(800, 600));

    tool.drag(QPointF(0, 0));
    BOOST_CHECK_CLOSE(xf.m32(), 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(drag_down_moves_transform_negative_y)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(800, 600));

    tool.drag(QPointF(0, 300));
    BOOST_CHECK_CLOSE(xf.m32(), -1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(drag_zoom_scales_movement_inversely)
{
    MoveTool tool;
    QTransform xf;

    tool.beginDrag(&xf, QPointF(0, 0), 2.0f, QSize(800, 600));
    tool.drag(QPointF(400, 0));

    float ndcX = 2.0f * 400.0f / 800.0f; // = 1.0
    float dx = ndcX / 2.0f;              // = 0.5
    BOOST_CHECK_CLOSE(xf.m31(), dx, 0.001f);
}

BOOST_AUTO_TEST_CASE(drag_accumulates_movement)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(800, 600));

    tool.drag(QPointF(200, 0));   // ndcX = 2*200/800 = 0.5, dx = 0.5/1 = 0.5
    BOOST_CHECK_CLOSE(xf.m31(), 0.5f, 0.001f);

    tool.drag(QPointF(400, 0));   // delta from 200 to 400 = 200, ndcX = 0.5, dx = 0.5
    BOOST_CHECK_CLOSE(xf.m31(), 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(drag_without_begindrag_no_crash)
{
    MoveTool tool;
    tool.drag(QPointF(100, 100));
    BOOST_CHECK(!tool.isDragging());
}

BOOST_AUTO_TEST_CASE(drag_after_enddrag_no_op)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(800, 600));
    tool.drag(QPointF(400, 0));
    float xBefore = xf.m31();

    tool.endDrag();
    tool.drag(QPointF(800, 0));
    BOOST_CHECK_CLOSE(xf.m31(), xBefore, 0.001f);
}

BOOST_AUTO_TEST_CASE(ndc_conversion_screen_to_ndc_exact)
{
    MoveTool tool;
    QTransform xf;
    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(1024, 768));

    QPointF mousePos(512, 384);
    tool.drag(mousePos);
    float expectedNdcX = 2.0f * 512.0f / 1024.0f;
    float expectedNdcY = -2.0f * 384.0f / 768.0f;
    BOOST_CHECK_CLOSE(xf.m31(), expectedNdcX, 0.001f);
    BOOST_CHECK_CLOSE(xf.m32(), expectedNdcY, 0.001f);
}

BOOST_AUTO_TEST_CASE(beginDrag_with_existing_transform_preserves_elements)
{
    MoveTool tool;
    QTransform xf;
    xf.scale(2.0f, 1.5f);

    tool.beginDrag(&xf, QPointF(0, 0), 1.0f, QSize(800, 600));
    BOOST_CHECK_CLOSE(xf.m11(), 2.0f, 0.001f);
    BOOST_CHECK_CLOSE(xf.m22(), 1.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(double_beginDrag_replaces_previous)
{
    MoveTool tool;
    QTransform xf1, xf2;
    tool.beginDrag(&xf1, QPointF(0, 0), 1.0f, QSize(800, 600));
    tool.beginDrag(&xf2, QPointF(0, 0), 1.0f, QSize(800, 600));
    tool.drag(QPointF(400, 0));
    BOOST_CHECK_CLOSE(xf2.m31(), 1.0f, 0.001f);
    BOOST_CHECK_CLOSE(double(xf1.m31()), 0.0, 0.001);
}

BOOST_AUTO_TEST_SUITE_END()
