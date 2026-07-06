#define BOOST_TEST_MODULE TileTest
#include <boost/test/included/unit_test.hpp>

#include "core/Tile.hpp"
#include <QRect>

BOOST_AUTO_TEST_SUITE(tile)

BOOST_AUTO_TEST_CASE(default_construction)
{
    core::Tile t;
    BOOST_CHECK_EQUAL(t.col, 0);
    BOOST_CHECK_EQUAL(t.row, 0);
    BOOST_CHECK(t.bounds.isNull());
    BOOST_CHECK(t.state == core::Tile::State::Empty);
    BOOST_CHECK_EQUAL(t.version, 0);
    BOOST_CHECK(t.cpuCache.isNull());
}

BOOST_AUTO_TEST_CASE(intersects_overlapping)
{
    core::Tile t;
    t.bounds = QRect(0, 0, 256, 256);
    BOOST_CHECK(t.intersects(QRect(128, 128, 32, 32)));
    BOOST_CHECK(t.intersects(QRect(200, 200, 100, 100)));
}

BOOST_AUTO_TEST_CASE(intersects_contained)
{
    core::Tile t;
    t.bounds = QRect(0, 0, 256, 256);
    BOOST_CHECK(t.intersects(QRect(100, 100, 50, 50)));
}

BOOST_AUTO_TEST_CASE(intersects_separate)
{
    core::Tile t;
    t.bounds = QRect(0, 0, 256, 256);
    BOOST_CHECK(!t.intersects(QRect(300, 300, 50, 50)));
    BOOST_CHECK(!t.intersects(QRect(-200, -200, 100, 100)));
}

BOOST_AUTO_TEST_CASE(intersects_touching_edge)
{
    core::Tile t;
    t.bounds = QRect(0, 0, 256, 256);
    // QRect::intersects is pixel-inclusive: (0,0,256,256) covers [0,255]x[0,255]
    BOOST_CHECK(t.intersects(QRect(255, 0, 10, 10)));   // right edge pixel 255
    BOOST_CHECK(t.intersects(QRect(255, 255, 10, 10))); // bottom-right edge
}

BOOST_AUTO_TEST_CASE(mark_dirty_changes_state)
{
    core::Tile t;
    t.state = core::Tile::State::GPUReady;
    t.markDirty();
    BOOST_CHECK(t.state == core::Tile::State::Dirty);
}

BOOST_AUTO_TEST_CASE(mark_dirty_increments_version)
{
    core::Tile t;
    t.state = core::Tile::State::GPUReady;
    uint32_t v0 = t.version;
    t.markDirty();
    BOOST_CHECK_EQUAL(t.version, v0 + 1);
    t.markDirty();
    BOOST_CHECK_EQUAL(t.version, v0 + 2);
}

BOOST_AUTO_TEST_CASE(state_transitions_manual)
{
    core::Tile t;
    BOOST_CHECK(t.state == core::Tile::State::Empty);
    t.state = core::Tile::State::Dirty;
    t.version = 1;
    BOOST_CHECK(t.state == core::Tile::State::Dirty);
    t.state = core::Tile::State::GPUReady;
    BOOST_CHECK(t.state == core::Tile::State::GPUReady);
    t.state = core::Tile::State::Cached;
    BOOST_CHECK(t.state == core::Tile::State::Cached);
}

BOOST_AUTO_TEST_CASE(cpu_cache_empty_on_construction)
{
    core::Tile t;
    BOOST_CHECK(t.cpuCache.isNull());
    t.cpuCache = QImage(64, 64, QImage::Format_RGBA8888);
    BOOST_CHECK(!t.cpuCache.isNull());
}

BOOST_AUTO_TEST_CASE(current_timestamp_is_monotonic)
{
    uint64_t t1 = core::currentTimestampMs();
    uint64_t t2 = core::currentTimestampMs();
    uint64_t t3 = core::currentTimestampMs();
    BOOST_CHECK_GE(t2, t1);
    BOOST_CHECK_GE(t3, t2);
}

BOOST_AUTO_TEST_SUITE_END()
