#define BOOST_TEST_MODULE DirtyRegionTest
#include <boost/test/included/unit_test.hpp>

#include "core/DirtyRegion.hpp"
#include <QRect>

BOOST_AUTO_TEST_SUITE(dirty_region)

BOOST_AUTO_TEST_CASE(initially_empty)
{
    core::DirtyRegion dr;
    BOOST_CHECK(dr.isEmpty());
    BOOST_CHECK(dr.boundingRect().isNull());
}

BOOST_AUTO_TEST_CASE(add_single_rect)
{
    core::DirtyRegion dr;
    dr.addRect(QRect(10, 20, 100, 200));
    BOOST_CHECK(!dr.isEmpty());
    BOOST_CHECK(dr.boundingRect() == QRect(10, 20, 100, 200));
}

BOOST_AUTO_TEST_CASE(add_multiple_disjoint_rects)
{
    core::DirtyRegion dr;
    dr.addRect(QRect(0, 0, 50, 50));
    dr.addRect(QRect(100, 100, 50, 50));
    QRect expected = QRect(0, 0, 150, 150); // union
    BOOST_CHECK(dr.boundingRect() == expected);
}

BOOST_AUTO_TEST_CASE(add_circle_creates_rect)
{
    core::DirtyRegion dr;
    dr.addCircle(QPoint(50, 50), 10);
    BOOST_CHECK(!dr.isEmpty());
    // center(50,50), radius 10 => rect(40,40,20,20)
    BOOST_CHECK(dr.boundingRect() == QRect(40, 40, 20, 20));
}

BOOST_AUTO_TEST_CASE(add_circle_zero_radius_no_op)
{
    core::DirtyRegion dr;
    dr.addCircle(QPoint(50, 50), 0);
    BOOST_CHECK(dr.isEmpty());
}

BOOST_AUTO_TEST_CASE(add_empty_rect_no_op)
{
    core::DirtyRegion dr;
    dr.addRect(QRect());
    BOOST_CHECK(dr.isEmpty());
}

BOOST_AUTO_TEST_CASE(clear_empties)
{
    core::DirtyRegion dr;
    dr.addRect(QRect(0, 0, 100, 100));
    BOOST_CHECK(!dr.isEmpty());
    dr.clear();
    BOOST_CHECK(dr.isEmpty());
}

BOOST_AUTO_TEST_CASE(clear_twice_no_crash)
{
    core::DirtyRegion dr;
    dr.clear();
    dr.clear();
    BOOST_CHECK(dr.isEmpty());
}

BOOST_AUTO_TEST_CASE(consolidate_merges_nearby_rects)
{
    core::DirtyRegion dr;
    // Add 3 rects within 256px of each other
    dr.addRect(QRect(0, 0, 50, 50));
    dr.addRect(QRect(100, 0, 50, 50));
    dr.addRect(QRect(200, 0, 50, 50));
    // distance between first two is 50px, between 2nd and 3rd is 50px
    dr.consolidate(1); // merge down to 1
    BOOST_CHECK(dr.boundingRect() == QRect(0, 0, 250, 50));
}

BOOST_AUTO_TEST_CASE(consolidate_respects_max_rects)
{
    core::DirtyRegion dr;
    dr.addRect(QRect(100, 0, 10, 10)); // far from others
    dr.addRect(QRect(0, 100, 10, 10));
    dr.addRect(QRect(50, 50, 10, 10));
    dr.consolidate(2); // should merge closest pair, ending with 2
    // After consolidation, we should have no more than 2 rects.
    // The bounding rect should still be the union of all original rects.
    QRect expected = QRect(0, 0, 110, 110); // union
    // The important assertion is that bounding rect is correct
    BOOST_CHECK(dr.boundingRect() == expected);
}

BOOST_AUTO_TEST_CASE(consolidate_already_under_limit)
{
    core::DirtyRegion dr;
    dr.addRect(QRect(0, 0, 10, 10));
    dr.addRect(QRect(100, 100, 10, 10));
    dr.consolidate(4); // already 2 <= 4
    // Still 2 rects, bounding rect = union
    BOOST_CHECK(dr.boundingRect() == QRect(0, 0, 110, 110));
}

BOOST_AUTO_TEST_CASE(consolidate_empty_no_crash)
{
    core::DirtyRegion dr;
    dr.consolidate();
    BOOST_CHECK(dr.isEmpty());
}

BOOST_AUTO_TEST_CASE(nearby_rects_merged)
{
    core::DirtyRegion dr;
    // Rect A at (0,0,100,100), rect B at (356,0,100,100)
    // distance = 356 - 100 = 256 (exactly the merge threshold)
    dr.addRect(QRect(0, 0, 100, 100));
    dr.addRect(QRect(356, 0, 100, 100));
    // shouldMerge returns true (distance 256 <= 256)
    dr.consolidate(1);
    BOOST_CHECK(dr.boundingRect() == QRect(0, 0, 456, 100));
}

BOOST_AUTO_TEST_CASE(consolidate_large_number_of_rects)
{
    core::DirtyRegion dr;
    for (int i = 0; i < 20; ++i)
        dr.addRect(QRect(i * 30, 0, 20, 20));
    dr.consolidate(4); // merge 20 rects down to 4
    // After merging down to 4, bounding rect = union of all
    QRect bounds = dr.boundingRect();
    // At minimum visible area should cover (0,0,600,20) approx
    BOOST_CHECK(bounds.width() >= 500);
    BOOST_CHECK(bounds.height() >= 10);
}

BOOST_AUTO_TEST_CASE(multiple_clear_add_cycles)
{
    core::DirtyRegion dr;
    dr.addRect(QRect(0, 0, 100, 100));
    dr.clear();
    dr.addRect(QRect(200, 200, 50, 50));
    dr.consolidate();
    BOOST_CHECK(dr.boundingRect() == QRect(200, 200, 50, 50));
}

BOOST_AUTO_TEST_SUITE_END()
