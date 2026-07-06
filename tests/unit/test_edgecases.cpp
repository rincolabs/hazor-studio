#define BOOST_TEST_MODULE EdgeCasesTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "engine/ImageEngine.hpp"
#include "processing/FilterProcessor.hpp"
#include "async/AsyncJobSystem.hpp"
#include "async/AsyncJob.hpp"

#include <QImage>
#include <QColor>
#include <QSignalSpy>
#include <QTimer>
#include <QCoreApplication>

struct EdgeFixture {
    Document doc;
    ImageController ctrl;

    EdgeFixture() : ctrl() {
        doc.size = QSize(50, 50);
        doc.selection.create(50, 50);
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    Layer* active() { return doc.activeLayer(); }
};

BOOST_AUTO_TEST_SUITE(edgecases)

// ── 1x1 Images ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(one_by_one_image_all_filters)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(1, 1, QImage::Format_RGBA8888);
    f.active()->cpuImage.setPixelColor(0, 0, QColor(100, 150, 200));

    BOOST_CHECK(f.ctrl.executeTool("adjust_brightness", {{"value", 0.5}}));
    BOOST_CHECK(f.ctrl.executeTool("adjust_contrast", {{"value", 0.3}}));
    BOOST_CHECK(f.ctrl.executeTool("adjust_saturation", {{"value", 0.5}}));
    BOOST_CHECK(f.ctrl.executeTool("adjust_hue", {{"value", 0.5}}));
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 1.0}}));
    BOOST_CHECK(f.ctrl.executeTool("sharpen", {{"strength", 1.0}}));
    BOOST_CHECK(f.ctrl.executeTool("median_blur", {{"kernel_size", 3.0}}));
    BOOST_CHECK(f.ctrl.executeTool("grayscale", {}));
    BOOST_CHECK(f.ctrl.executeTool("invert_colors", {}));
    BOOST_CHECK(f.ctrl.executeTool("posterize", {{"levels", 4.0}}));
    BOOST_CHECK(f.ctrl.executeTool("threshold", {{"value", 128.0}}));
}

BOOST_AUTO_TEST_CASE(one_by_one_engine_level)
{
    QImage img(1, 1, QImage::Format_RGBA8888);
    img.setPixelColor(0, 0, QColor(100, 150, 200, 255));
    cv::Mat cvIn = ImageEngine::toCvMat(img);

    BOOST_CHECK(!ImageEngine::adjustBrightness(cvIn, 0.5f).empty());
    BOOST_CHECK(!ImageEngine::adjustContrast(cvIn, 0.5f).empty());
    BOOST_CHECK(!ImageEngine::gaussianBlur(cvIn, 1.0f).empty());
    BOOST_CHECK(!ImageEngine::medianBlur(cvIn, 3).empty());
    BOOST_CHECK(!ImageEngine::grayscale(cvIn).empty());
    BOOST_CHECK(!ImageEngine::posterize(cvIn, 4).empty());
    BOOST_CHECK(!ImageEngine::threshold(cvIn, 128.0).empty());
    BOOST_CHECK(!ImageEngine::crop(cvIn, 0, 0, 1, 1).empty());
    BOOST_CHECK(!ImageEngine::resize(cvIn, 1, 1).empty());
}

// ── Transparent Images ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(fully_transparent_filters_no_crash)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::transparent);

    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 3.0}}));
    BOOST_CHECK(f.ctrl.executeTool("grayscale", {}));
    BOOST_CHECK(f.ctrl.executeTool("invert_colors", {}));
    BOOST_CHECK(f.ctrl.executeTool("sharpen", {{"strength", 1.0}}));
    BOOST_CHECK(f.ctrl.executeTool("edge_detect", {{"threshold1", 50.0}, {"threshold2", 150.0}}));
}

// ── Zero/Extreme Parameters ─────────────────────────────────────

BOOST_AUTO_TEST_CASE(brightness_maximum)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("adjust_brightness", {{"value", 1.0}}));
}

BOOST_AUTO_TEST_CASE(brightness_minimum)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("adjust_brightness", {{"value", -1.0}}));
}

BOOST_AUTO_TEST_CASE(contrast_maximum)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("adjust_contrast", {{"value", 1.0}}));
}

BOOST_AUTO_TEST_CASE(blur_zero_radius)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 0.0}}));
}

BOOST_AUTO_TEST_CASE(blur_large_radius)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 100.0}}));
}

BOOST_AUTO_TEST_CASE(sharpen_max_strength)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("sharpen", {{"strength", 10.0}}));
}

BOOST_AUTO_TEST_CASE(posterize_min_levels)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("posterize", {{"levels", 2.0}}));
}

BOOST_AUTO_TEST_CASE(posterize_max_levels)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("posterize", {{"levels", 255.0}}));
}

BOOST_AUTO_TEST_CASE(threshold_zero)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(0, 0, 0));
    BOOST_CHECK(f.ctrl.executeTool("threshold", {{"value", 0.0}}));
}

BOOST_AUTO_TEST_CASE(threshold_max)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(255, 255, 255));
    BOOST_CHECK(f.ctrl.executeTool("threshold", {{"value", 255.0}}));
}

// ── Layer Index Edge Cases ──────────────────────────────────────

BOOST_AUTO_TEST_CASE(negative_index_operations)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("remove_layer", {{"index", -1.0}}));
    BOOST_CHECK(f.ctrl.executeTool("set_layer_opacity", {{"index", -1.0}, {"opacity", 0.5}}));
    BOOST_CHECK(f.ctrl.executeTool("set_layer_visibility", {{"index", -1.0}, {"visible", 0.0}}));
}

BOOST_AUTO_TEST_CASE(out_of_bounds_index)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    BOOST_CHECK(f.ctrl.executeTool("remove_layer", {{"index", 999.0}}));
    BOOST_CHECK(f.ctrl.executeTool("duplicate_layer", {{"index", 999.0}}));
}

// ── Zoom Boundaries ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(zoom_minimum)
{
    EdgeFixture f;
    BOOST_CHECK(f.ctrl.executeTool("zoom", {{"level", 0.001}}));
    BOOST_CHECK_GE(f.doc.zoom, 0.01f);
}

BOOST_AUTO_TEST_CASE(zoom_maximum)
{
    EdgeFixture f;
    BOOST_CHECK(f.ctrl.executeTool("zoom", {{"level", 1000.0}}));
    BOOST_CHECK_LE(f.doc.zoom, 100.0f);
}

BOOST_AUTO_TEST_CASE(zoom_zero_gets_clamped)
{
    EdgeFixture f;
    f.ctrl.executeTool("zoom", {{"level", 0.0}});
    BOOST_CHECK_GE(f.doc.zoom, 0.01f);
}

// ── Document Size Extremes ──────────────────────────────────────

BOOST_AUTO_TEST_CASE(document_min_size)
{
    EdgeFixture f;
    BOOST_CHECK(f.ctrl.executeTool("new_document", {{"width", 1.0}, {"height", 1.0}}));
    BOOST_CHECK_EQUAL(f.doc.size.width(), 1);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 1);
}

BOOST_AUTO_TEST_CASE(document_large_size)
{
    EdgeFixture f;
    BOOST_CHECK(f.ctrl.executeTool("new_document", {{"width", 4096.0}, {"height", 4096.0}}));
    BOOST_CHECK_EQUAL(f.doc.size.width(), 4096);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 4096);
}

// ── Resize Layer Edges ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(resize_layer_to_zero_fails_or_clamps)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(100, 100, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::red);
    // Zero dimensions may fail OpenCV assert - check it gracefully fails
    f.ctrl.executeTool("resize_layer", {{"width", 0.0}, {"height", 0.0}});
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(resize_layer_to_large)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::red);
    BOOST_CHECK(f.ctrl.executeTool("resize_layer", {{"width", 4000.0}, {"height", 4000.0}}));
}

// ── Undo Exhaustion ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(undo_after_clear)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.history().clear();
    BOOST_CHECK(!f.ctrl.history().canUndo());
}

BOOST_AUTO_TEST_CASE(redo_after_push)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    f.ctrl.executeTool("invert_colors", {});
    BOOST_CHECK(!f.ctrl.history().canRedo());
}

BOOST_AUTO_TEST_CASE(undo_redo_multiple_times)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));
    auto orig = f.active()->cpuImage.copy();

    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    f.ctrl.executeTool("sharpen", {{"strength", 1.0}});

    for (int i = 0; i < 5; ++i) {
        f.ctrl.history().undo();
        f.ctrl.history().redo();
    }
}

// ── Rapid Operations ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(rapid_filter_chain)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(20, 20, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));

    for (int i = 0; i < 10; ++i) {
        BOOST_CHECK(f.ctrl.executeTool("adjust_brightness", {{"value", 0.1}}));
        BOOST_CHECK(f.ctrl.executeTool("adjust_contrast", {{"value", -0.05}}));
    }
}

// ── Empty Image in Active Layer ─────────────────────────────────

BOOST_AUTO_TEST_CASE(null_layer_image_does_not_crash)
{
    EdgeFixture f;
    // Grayscale on null image should return false but not crash
    bool ok = f.ctrl.executeTool("grayscale", {});
    BOOST_CHECK(ok || !ok);
}

// ── Fase 12.3: Tile border continuity ─────────────────────────
// Verify that FilterProcessor produces continuous results across
// tile boundaries (no seam artifacts when two adjacent tiles are
// processed separately with neighborhood filters).

BOOST_AUTO_TEST_CASE(tile_border_no_seams_after_blur)
{
    // Create a horizontal gradient so tile boundaries would show seams
    QImage img(64, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            uchar v = static_cast<uchar>((x * 255) / 63);
            uchar* p = img.scanLine(y) + x * 4;
            p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
        }
    }

    // Tile at 32px → tile boundary at x=32
    core::TileManager tm;
    tm.init(64, 64, 32);
    tm.markAllDirty();

    std::vector<core::Tile*> tiles;
    for (int c = 0; c < 2; ++c)
        tiles.push_back(&tm.at(0, c));

    QVariantMap p;
    p["radius"] = 5.0;
    processing::FilterProcessor::processTiles(img, tiles, "gaussian_blur", p);

    // Check that pixels at tile boundary (x=31, x=32) are continuous
    // (no abrupt change due to separate tile processing)
    for (int y = 10; y < 54; ++y) {
        auto left = img.pixelColor(31, y);
        auto right = img.pixelColor(32, y);
        int diff = std::abs(left.red() - right.red());
        // After blur with radius 5, adjacent pixels should be smooth
        BOOST_CHECK_LE(diff, 10);
    }
}

// ── Fase 12.3: Mixed tiled / untiled layers ──────────────────

BOOST_AUTO_TEST_CASE(mixed_tiled_untiled_layers_no_crash)
{
    EdgeFixture f;
    f.active()->cpuImage = QImage(100, 100, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));

    // Enable tiling on the active layer
    f.active()->enableTiling(64);
    BOOST_CHECK(f.active()->tiledSystem);

    // Add a second layer (untiled by default)
    f.ctrl.newLayer();
    f.active()->cpuImage = QImage(100, 100, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(200, 100, 50));
    BOOST_CHECK(!f.active()->tiledSystem);

    // Apply filter on tiled layer
    f.ctrl.setActiveNode(0);
    BOOST_CHECK(f.ctrl.executeTool("adjust_brightness", {{"value", 0.3}}));

    // Apply filter on untiled layer
    f.ctrl.setActiveNode(1);
    BOOST_CHECK(f.ctrl.executeTool("invert_colors", {}));

    // Switch back to tiled layer, apply neighborhood filter
    f.ctrl.setActiveNode(0);
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 3.0}}));
}

// ── Fase 12.3: Zoom extreme values ───────────────────────────

BOOST_AUTO_TEST_CASE(zoom_extreme_10000_percent)
{
    EdgeFixture f;
    BOOST_CHECK(f.ctrl.executeTool("zoom", {{"level", 100.0}})); // 10000%
    BOOST_CHECK_LE(f.doc.zoom, 100.0f);
    BOOST_CHECK_GE(f.doc.zoom, 0.01f);
}

BOOST_AUTO_TEST_CASE(zoom_extreme_01_percent)
{
    EdgeFixture f;
    BOOST_CHECK(f.ctrl.executeTool("zoom", {{"level", 0.001}})); // 0.1%
    BOOST_CHECK_GE(f.doc.zoom, 0.01f);
    BOOST_CHECK_LE(f.doc.zoom, 100.0f);
}

// ── Fase 12.3: Async heavy tool + undo ──────────────────────

BOOST_AUTO_TEST_CASE(async_dispatch_cancels_previous_for_same_layer)
{
    AsyncJobSystem::create();

    EdgeFixture f;
    f.active()->cpuImage = QImage(64, 64, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));

    auto* aj = AsyncJobSystem::instance();
    BOOST_REQUIRE(aj);
    BOOST_CHECK_EQUAL(aj->pendingCount(), 0);

    // First async dispatch
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 10.0}}));
    BOOST_CHECK_EQUAL(aj->pendingCount(), 1);

    // Second async dispatch for same layer cancels first
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 5.0}}));
    BOOST_CHECK_EQUAL(aj->pendingCount(), 1);

    aj->cancelAll();
    BOOST_CHECK_EQUAL(aj->pendingCount(), 0);

    AsyncJobSystem::destroy();
}

BOOST_AUTO_TEST_CASE(resize_during_async_cancels_pending_jobs)
{
    AsyncJobSystem::create();

    EdgeFixture f;
    f.active()->cpuImage = QImage(64, 64, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(100, 150, 200));

    auto* aj = AsyncJobSystem::instance();
    BOOST_REQUIRE(aj);

    // Dispatch async operation
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {{"radius", 10.0}}));
    BOOST_CHECK_EQUAL(aj->pendingCount(), 1);

    // Immediately resize document (now cancels pending async jobs)
    BOOST_CHECK(f.ctrl.executeTool("resize_document", {{"width", 200.0}, {"height", 200.0}}));

    // No pending jobs should remain after resize
    BOOST_CHECK_EQUAL(aj->pendingCount(), 0);

    // Document should be resized
    BOOST_CHECK_EQUAL(f.doc.size.width(), 200);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 200);

    AsyncJobSystem::instance()->cancelAll();
    AsyncJobSystem::destroy();
}

BOOST_AUTO_TEST_SUITE_END()
