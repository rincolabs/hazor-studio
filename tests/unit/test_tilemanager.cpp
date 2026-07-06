#define BOOST_TEST_MODULE TileManagerTest
#include <boost/test/included/unit_test.hpp>

#include "core/TileManager.hpp"
#include <QRect>
#include <QPoint>

BOOST_AUTO_TEST_SUITE(tile_manager)

// ── init ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(init_square_image)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    BOOST_CHECK_EQUAL(tm.cols(), 2);
    BOOST_CHECK_EQUAL(tm.rows(), 2);
    BOOST_CHECK_EQUAL(tm.totalTiles(), 4);
    BOOST_CHECK_EQUAL(tm.imageWidth(), 512);
    BOOST_CHECK_EQUAL(tm.imageHeight(), 512);
    BOOST_CHECK_EQUAL(tm.tileSize(), 256);
}

BOOST_AUTO_TEST_CASE(init_non_square)
{
    core::TileManager tm;
    tm.init(800, 600, 256);
    // cols = ceil(800/256) = 4, rows = ceil(600/256) = 3
    BOOST_CHECK_EQUAL(tm.cols(), 4);
    BOOST_CHECK_EQUAL(tm.rows(), 3);
    BOOST_CHECK_EQUAL(tm.totalTiles(), 12);
}

BOOST_AUTO_TEST_CASE(init_custom_tile_size)
{
    core::TileManager tm;
    tm.init(500, 500, 128);
    // cols = ceil(500/128) = 4, rows = ceil(500/128) = 4
    BOOST_CHECK_EQUAL(tm.cols(), 4);
    BOOST_CHECK_EQUAL(tm.rows(), 4);
    BOOST_CHECK_EQUAL(tm.tileSize(), 128);
}

BOOST_AUTO_TEST_CASE(init_edge_tiles_partial_bounds)
{
    core::TileManager tm;
    tm.init(300, 300, 256);
    // tile (0,0): (0,0,256,256)
    // tile (1,0): (256,0,44,256)
    // tile (0,1): (0,256,256,44)
    // tile (1,1): (256,256,44,44)
    BOOST_CHECK(tm.at(0, 0).bounds == QRect(0, 0, 256, 256));
    BOOST_CHECK(tm.at(1, 0).bounds == QRect(256, 0, 44, 256));
    BOOST_CHECK(tm.at(0, 1).bounds == QRect(0, 256, 256, 44));
    BOOST_CHECK(tm.at(1, 1).bounds == QRect(256, 256, 44, 44));
}

BOOST_AUTO_TEST_CASE(init_zero_dims_returns_early)
{
    core::TileManager tm;
    tm.init(0, 512, 256);
    BOOST_CHECK_EQUAL(tm.cols(), 0);
    BOOST_CHECK_EQUAL(tm.rows(), 0);
    BOOST_CHECK_EQUAL(tm.totalTiles(), 0);

    tm.init(512, 0, 256);
    BOOST_CHECK_EQUAL(tm.cols(), 0);

    tm.init(-100, 512, 256);
    BOOST_CHECK_EQUAL(tm.cols(), 0);
}

BOOST_AUTO_TEST_CASE(init_sets_tiles_to_dirty)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    // All tiles should be Dirty after init
    for (int r = 0; r < tm.rows(); ++r)
        for (int c = 0; c < tm.cols(); ++c)
            BOOST_CHECK(tm.at(c, r).state == core::Tile::State::Dirty);
}

BOOST_AUTO_TEST_CASE(init_tile_coordinates)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    BOOST_CHECK_EQUAL(tm.at(0, 0).col, 0);
    BOOST_CHECK_EQUAL(tm.at(0, 0).row, 0);
    BOOST_CHECK_EQUAL(tm.at(1, 0).col, 1);
    BOOST_CHECK_EQUAL(tm.at(1, 0).row, 0);
    BOOST_CHECK_EQUAL(tm.at(0, 1).col, 0);
    BOOST_CHECK_EQUAL(tm.at(0, 1).row, 1);
}

// ── tileAtPixel ───────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(tile_at_pixel_mapping)
{
    core::TileManager tm;
    tm.init(800, 600, 256);
    core::Tile* t = tm.tileAtPixel(300, 100);
    BOOST_REQUIRE(t != nullptr);
    BOOST_CHECK_EQUAL(t->col, 1); // 300/256 = 1
    BOOST_CHECK_EQUAL(t->row, 0); // 100/256 = 0
}

BOOST_AUTO_TEST_CASE(tile_at_pixel_out_of_bounds)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    BOOST_CHECK(tm.tileAtPixel(9999, 100) == nullptr);
    BOOST_CHECK(tm.tileAtPixel(100, 9999) == nullptr);
    BOOST_CHECK(tm.tileAtPixel(-1, 100) == nullptr);
}

BOOST_AUTO_TEST_CASE(tile_at_pixel_empty_grid)
{
    core::TileManager tm;
    BOOST_CHECK(tm.tileAtPixel(0, 0) == nullptr);
}

// ── visibleTiles ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(visible_tiles_culling)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    // Viewport covering top-left quarter
    auto tiles = tm.visibleTiles(QRect(0, 0, 200, 200));
    BOOST_CHECK_EQUAL(tiles.size(), 1);
    BOOST_CHECK_EQUAL(tiles[0]->col, 0);
    BOOST_CHECK_EQUAL(tiles[0]->row, 0);
}

BOOST_AUTO_TEST_CASE(visible_tiles_returns_all_intersecting)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    // Viewport spanning all 4 tiles
    auto tiles = tm.visibleTiles(QRect(0, 0, 512, 512));
    BOOST_CHECK_EQUAL(tiles.size(), 4);
}

BOOST_AUTO_TEST_CASE(visible_tiles_skips_empty)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    tm.at(0, 0).state = core::Tile::State::Empty;
    auto tiles = tm.visibleTiles(QRect(0, 0, 512, 512));
    // tile (0,0) is Empty, should not be returned
    for (auto* t : tiles)
        BOOST_CHECK(!(t->col == 0 && t->row == 0));
}

BOOST_AUTO_TEST_CASE(visible_tiles_empty_when_empty_grid)
{
    core::TileManager tm;
    auto tiles = tm.visibleTiles(QRect(0, 0, 512, 512));
    BOOST_CHECK(tiles.empty());
}

// ── markDirty / markAllDirty ──────────────────────────────────

BOOST_AUTO_TEST_CASE(mark_dirty_overlapping_tiles)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    // Mark tiles clean first
    for (int r = 0; r < tm.rows(); ++r)
        for (int c = 0; c < tm.cols(); ++c)
            tm.at(c, r).state = core::Tile::State::GPUReady;

    // Dirty rect overlapping 2 bottom tiles
    tm.markDirty(QRect(0, 300, 512, 200));
    BOOST_CHECK(tm.at(0, 1).state == core::Tile::State::Dirty);
    BOOST_CHECK(tm.at(1, 1).state == core::Tile::State::Dirty);
    // Top tiles should still be GPUReady
    BOOST_CHECK(tm.at(0, 0).state == core::Tile::State::GPUReady);
    BOOST_CHECK(tm.at(1, 0).state == core::Tile::State::GPUReady);
}

BOOST_AUTO_TEST_CASE(mark_all_dirty)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    // Mark all GPUReady
    for (int r = 0; r < tm.rows(); ++r)
        for (int c = 0; c < tm.cols(); ++c)
            tm.at(c, r).state = core::Tile::State::GPUReady;

    tm.markAllDirty();
    for (int r = 0; r < tm.rows(); ++r)
        for (int c = 0; c < tm.cols(); ++c)
            BOOST_CHECK(tm.at(c, r).state == core::Tile::State::Dirty);
}

BOOST_AUTO_TEST_CASE(mark_dirty_empty_rect_no_op)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    for (int r = 0; r < tm.rows(); ++r)
        for (int c = 0; c < tm.cols(); ++c)
            tm.at(c, r).state = core::Tile::State::GPUReady;

    tm.markDirty(QRect()); // empty rect
    // All should remain GPUReady
    for (int r = 0; r < tm.rows(); ++r)
        for (int c = 0; c < tm.cols(); ++c)
            BOOST_CHECK(tm.at(c, r).state == core::Tile::State::GPUReady);
}

BOOST_AUTO_TEST_CASE(mark_dirty_invalidates_cpu_cache)
{
    core::TileManager tm;
    tm.init(256, 256, 256);
    auto& t = tm.at(0, 0);
    t.cpuCache = QImage(64, 64, QImage::Format_RGBA8888);
    t.state = core::Tile::State::Cached;
    tm.markDirty(QRect(0, 0, 256, 256));
    BOOST_CHECK(t.cpuCache.isNull());
    BOOST_CHECK(t.state == core::Tile::State::Dirty);
}

// ── dirtyTiles ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(dirty_tiles_after_mark)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    // After init all are Dirty
    auto dirty = tm.dirtyTiles();
    BOOST_CHECK_EQUAL(dirty.size(), 4);
}

BOOST_AUTO_TEST_CASE(dirty_tiles_empty_when_clean)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    for (int r = 0; r < tm.rows(); ++r)
        for (int c = 0; c < tm.cols(); ++c)
            tm.at(c, r).state = core::Tile::State::GPUReady;

    BOOST_CHECK(tm.dirtyTiles().empty());
}

// ── resize ────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(resize_preserves_cache_overlap)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    // Set one tile as cached
    tm.at(0, 0).state = core::Tile::State::Cached;
    tm.at(0, 0).cpuCache = QImage(64, 64, QImage::Format_RGBA8888);

    tm.resize(1024, 1024); // still has (0,0) tile
    BOOST_CHECK(!tm.at(0, 0).cpuCache.isNull());
    BOOST_CHECK(tm.at(0, 0).state == core::Tile::State::Cached);
}

BOOST_AUTO_TEST_CASE(resize_invalid_clears)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    tm.resize(0, 0);
    BOOST_CHECK_EQUAL(tm.cols(), 0);
    BOOST_CHECK_EQUAL(tm.totalTiles(), 0);
}

// ── clear ─────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(clear_resets_all)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    tm.clear();
    BOOST_CHECK_EQUAL(tm.cols(), 0);
    BOOST_CHECK_EQUAL(tm.rows(), 0);
    BOOST_CHECK_EQUAL(tm.totalTiles(), 0);
    BOOST_CHECK_EQUAL(tm.imageWidth(), 0);
    BOOST_CHECK_EQUAL(tm.imageHeight(), 0);
}

// ── evictLRU / cpuCacheCount ──────────────────────────────────

BOOST_AUTO_TEST_CASE(evict_lru_removes_oldest)
{
    core::TileManager tm;
    tm.init(512, 1, 256); // 2 tiles: (0,0) and (1,0)
    // Set both as cached
    tm.at(0, 0).state = core::Tile::State::Cached;
    tm.at(0, 0).cpuCache = QImage(16, 16, QImage::Format_RGBA8888);
    tm.at(0, 0).lastAccess = 100;

    tm.at(1, 0).state = core::Tile::State::Cached;
    tm.at(1, 0).cpuCache = QImage(16, 16, QImage::Format_RGBA8888);
    tm.at(1, 0).lastAccess = 200;

    tm.evictLRU(1); // keep 1 most recent
    BOOST_CHECK(tm.at(0, 0).cpuCache.isNull());  // oldest evicted
    BOOST_CHECK(tm.at(0, 0).state == core::Tile::State::GPUReady);
    BOOST_CHECK(!tm.at(1, 0).cpuCache.isNull()); // newest kept
    BOOST_CHECK(tm.at(1, 0).state == core::Tile::State::Cached);
}

BOOST_AUTO_TEST_CASE(evict_lru_zero_max)
{
    core::TileManager tm;
    tm.init(256, 256, 256);
    auto& t = tm.at(0, 0);
    t.state = core::Tile::State::Cached;
    t.cpuCache = QImage(16, 16, QImage::Format_RGBA8888);

    tm.evictLRU(0);
    BOOST_CHECK(t.cpuCache.isNull());
    BOOST_CHECK(t.state == core::Tile::State::GPUReady);
}

BOOST_AUTO_TEST_CASE(evict_lru_negative_no_op)
{
    core::TileManager tm;
    tm.init(256, 256, 256);
    auto& t = tm.at(0, 0);
    t.state = core::Tile::State::Cached;
    t.cpuCache = QImage(16, 16, QImage::Format_RGBA8888);

    tm.evictLRU(-1); // negative -> returns early
    BOOST_CHECK(!t.cpuCache.isNull());
}

BOOST_AUTO_TEST_CASE(cpu_cache_count)
{
    core::TileManager tm;
    tm.init(512, 512, 256);
    BOOST_CHECK_EQUAL(tm.cpuCacheCount(), 0);

    tm.at(0, 0).state = core::Tile::State::Cached;
    tm.at(0, 0).cpuCache = QImage(16, 16, QImage::Format_RGBA8888);
    BOOST_CHECK_EQUAL(tm.cpuCacheCount(), 1);

    tm.at(1, 0).state = core::Tile::State::Cached;
    tm.at(1, 0).cpuCache = QImage(16, 16, QImage::Format_RGBA8888);
    BOOST_CHECK_EQUAL(tm.cpuCacheCount(), 2);
}

// ── Edge cases ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(edge_1x1_image)
{
    core::TileManager tm;
    tm.init(1, 1, 256);
    BOOST_CHECK_EQUAL(tm.totalTiles(), 1);
    BOOST_CHECK(tm.at(0, 0).bounds == QRect(0, 0, 1, 1));
}

BOOST_AUTO_TEST_CASE(edge_image_smaller_than_tile)
{
    core::TileManager tm;
    tm.init(100, 100, 256);
    BOOST_CHECK_EQUAL(tm.totalTiles(), 1);
    BOOST_CHECK(tm.at(0, 0).bounds == QRect(0, 0, 100, 100));
}

BOOST_AUTO_TEST_CASE(edge_exact_tile_multiple)
{
    core::TileManager tm;
    tm.init(256, 256, 256);
    BOOST_CHECK_EQUAL(tm.cols(), 1);
    BOOST_CHECK_EQUAL(tm.rows(), 1);
    BOOST_CHECK(tm.at(0, 0).bounds == QRect(0, 0, 256, 256));
}

BOOST_AUTO_TEST_SUITE_END()
