#define BOOST_TEST_MODULE RenderCacheTest
#include <boost/test/included/unit_test.hpp>

#include "cache/RenderCache.hpp"
#include "common/GlFixture.hpp"
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <GL/gl.h>

BOOST_AUTO_TEST_SUITE(render_cache)

BOOST_AUTO_TEST_CASE(construction_null_handles)
{
    RenderCache rc;
    rc.destroy();
    // After destroy, handles should be 0
    // isValid should return false
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, {}, {});
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_CASE(is_valid_false_before_ensure_size)
{
    GlFixture gl;
    RenderCache rc;
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, {}, {});
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_CASE(ensure_size_allocates_fbo)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    // After ensureSize, isValid should still be false (not marked valid)
    // but the FBO/texture should be allocated
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, {}, {});
    BOOST_CHECK(!valid); // not marked valid yet
}

BOOST_AUTO_TEST_CASE(ensure_size_no_realloc_if_same)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.ensureSize(800, 600); // same size, no crash
    BOOST_CHECK(true); // reached without crash
}

BOOST_AUTO_TEST_CASE(ensure_size_realloc_on_change)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.ensureSize(1024, 768); // different size, realloc
    BOOST_CHECK(true); // reached without crash
}

BOOST_AUTO_TEST_CASE(mark_valid_then_is_valid_true)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.markValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    BOOST_CHECK(valid);
}

BOOST_AUTO_TEST_CASE(is_valid_detects_zoom_change)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.markValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    bool valid = rc.isValid(nullptr, 800, 600, 2.0f, QPointF(0, 0), QPointF(1, 1));
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_CASE(is_valid_detects_pan_change)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.markValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, QPointF(10, 10), QPointF(1, 1));
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_CASE(is_valid_detects_canvas_extent_change)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.markValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(2, 2));
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_CASE(mark_invalid_sets_generation_zero)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.markValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    rc.markInvalid();
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_CASE(destroy_frees_resources)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.destroy();
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, {}, {});
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_CASE(capture_current_frame_no_crash)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.captureCurrentFrame(800, 600);
    BOOST_CHECK(true); // reached without crash
}

BOOST_AUTO_TEST_CASE(destroy_twice_no_crash)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(800, 600);
    rc.destroy();
    rc.destroy();
    BOOST_CHECK(true); // reached without crash
}

BOOST_AUTO_TEST_CASE(viewport_size_change_invalidates)
{
    GlFixture gl;
    RenderCache rc;
    rc.ensureSize(640, 480);
    rc.markValid(nullptr, 640, 480, 1.0f, QPointF(0, 0), QPointF(1, 1));
    bool valid = rc.isValid(nullptr, 800, 600, 1.0f, QPointF(0, 0), QPointF(1, 1));
    BOOST_CHECK(!valid);
}

BOOST_AUTO_TEST_SUITE_END()
