#define BOOST_TEST_MODULE MipmapCacheTest
#include <boost/test/included/unit_test.hpp>

#include "renderer/MipmapCache.hpp"
#include "core/Layer.hpp"
#include <QImage>
#include <QColor>

BOOST_AUTO_TEST_SUITE(mipmap_cache)

BOOST_AUTO_TEST_CASE(level_null_layer_returns_nullptr)
{
    MipmapCache mc;
    const QImage* lvl = mc.level(nullptr, core::RenderScheduler::LOD::Half);
    BOOST_CHECK(lvl == nullptr);
}

BOOST_AUTO_TEST_CASE(level_null_image_returns_nullptr)
{
    MipmapCache mc;
    Layer layer;
    // cpuImage is null by default
    const QImage* lvl = mc.level(&layer, core::RenderScheduler::LOD::Half);
    BOOST_CHECK(lvl == nullptr);
}

BOOST_AUTO_TEST_CASE(level_lod_full_returns_nullptr)
{
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);
    // LOD Full (index 0) is not managed by MipmapCache
    const QImage* lvl = mc.level(&layer, core::RenderScheduler::LOD::Full);
    BOOST_CHECK(lvl == nullptr);
}

BOOST_AUTO_TEST_CASE(level_half_generates_correct_size)
{
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(1024, 1024, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    const QImage* lvl = mc.level(&layer, core::RenderScheduler::LOD::Half);
    BOOST_REQUIRE(lvl != nullptr);
    BOOST_CHECK_EQUAL(lvl->width(), 512);
    BOOST_CHECK_EQUAL(lvl->height(), 512);
}

BOOST_AUTO_TEST_CASE(level_quarter_generates_correct_size)
{
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(1024, 1024, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    const QImage* lvl = mc.level(&layer, core::RenderScheduler::LOD::Quarter);
    BOOST_REQUIRE(lvl != nullptr);
    BOOST_CHECK_EQUAL(lvl->width(), 256);
    BOOST_CHECK_EQUAL(lvl->height(), 256);
}

BOOST_AUTO_TEST_CASE(level_eighth_generates_correct_size)
{
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(1024, 1024, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    const QImage* lvl = mc.level(&layer, core::RenderScheduler::LOD::Eighth);
    BOOST_REQUIRE(lvl != nullptr);
    BOOST_CHECK_EQUAL(lvl->width(), 128);
    BOOST_CHECK_EQUAL(lvl->height(), 128);
}

BOOST_AUTO_TEST_CASE(level_lazy_generates_only_requested)
{
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(512, 512, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    // Request only Half
    mc.level(&layer, core::RenderScheduler::LOD::Half);

    // Half should be generated, Quarter and Eighth should still be null
    BOOST_CHECK(!layer.lodLevels[1].isNull());
    BOOST_CHECK(layer.lodLevels[2].isNull());
    BOOST_CHECK(layer.lodLevels[3].isNull());
}

BOOST_AUTO_TEST_CASE(upload_level_no_context_returns_zero)
{
    // No GL context made current
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    unsigned int tex = mc.uploadLevel(&layer, core::RenderScheduler::LOD::Half);
    BOOST_CHECK_EQUAL(tex, 0U);
}

BOOST_AUTO_TEST_CASE(no_context_upload_dirty_levels_returns_early)
{
    // No current GL context -> uploadDirtyLevels just marks non-dirty (calls level)
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    mc.uploadDirtyLevels(&layer);
    // level() is called during uploadDirtyLevels, which should update lodDirty
    // If no context, lodDirty remains true
    BOOST_CHECK(layer.lodDirty[1]);
}

BOOST_AUTO_TEST_CASE(invalidate_marks_all_dirty)
{
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    mc.level(&layer, core::RenderScheduler::LOD::Half);
    BOOST_CHECK(!layer.lodLevels[1].isNull());

    mc.invalidate(&layer);
    // All levels should be marked dirty and images freed
    for (int i = 1; i <= 3; ++i) {
        BOOST_CHECK(layer.lodDirty[i]);
        BOOST_CHECK(layer.lodLevels[i].isNull());
    }
}

BOOST_AUTO_TEST_CASE(cleanup_no_context_invalidates)
{
    // Without GL context, cleanup should still call invalidate()
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    mc.level(&layer, core::RenderScheduler::LOD::Half);
    BOOST_CHECK(!layer.lodLevels[1].isNull());

    mc.cleanup(&layer);
    // cleanup calls invalidate -> images freed, dirty set
    BOOST_CHECK(layer.lodDirty[1]);
    BOOST_CHECK(layer.lodLevels[1].isNull());
}

BOOST_AUTO_TEST_CASE(edge_2x2_image)
{
    MipmapCache mc;
    Layer layer;
    layer.cpuImage = QImage(2, 2, QImage::Format_RGBA8888);
    layer.cpuImage.fill(Qt::white);

    // Even smallest images should produce valid 1x1 LOD levels
    const QImage* half = mc.level(&layer, core::RenderScheduler::LOD::Half);
    BOOST_REQUIRE(half != nullptr);
    BOOST_CHECK_EQUAL(half->width(), 1);

    const QImage* quarter = mc.level(&layer, core::RenderScheduler::LOD::Quarter);
    BOOST_REQUIRE(quarter != nullptr);
    BOOST_CHECK_EQUAL(quarter->width(), 1);
}

BOOST_AUTO_TEST_CASE(clear_all_no_crash)
{
    MipmapCache mc;
    mc.clearAll();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_SUITE_END()
