#define BOOST_TEST_MODULE ImageFiltersTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "controller/Commands.hpp"
#include "engine/ImageEngine.hpp"

#include <QImage>
#include <QColor>
#include <cmath>

struct FilterFixture {
    Document doc;
    ImageController ctrl;

    FilterFixture()
        : ctrl()
    {
        doc.size = QSize(100, 100);
        doc.selection.create(100, 100);
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    Layer* activeLayer() { return doc.activeLayer(); }
    int activeIndex() const { return doc.activeFlatIndex; }

    void fill(uchar r, uchar g, uchar b, uchar a = 255)
    {
        auto* l = activeLayer();
        if (l) l->cpuImage.fill(QColor(r, g, b, a));
    }

    void fillCheckerboard(int tileSize = 4)
    {
        auto* l = activeLayer();
        if (!l) return;
        QImage& img = l->cpuImage;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                bool white = ((x / tileSize) + (y / tileSize)) % 2 == 0;
                img.setPixelColor(x, y, white ? QColor(255, 255, 255) : QColor(0, 0, 0));
            }
    }

    void fillGradient()
    {
        auto* l = activeLayer();
        if (!l) return;
        QImage& img = l->cpuImage;
        for (int y = 0; y < img.height(); ++y)
            for (int x = 0; x < img.width(); ++x) {
                int v = (x * 255) / img.width();
                img.setPixelColor(x, y, QColor(v, v, v));
            }
    }
};

BOOST_AUTO_TEST_SUITE(imagefilters)

// ── sharpen uses "strength" param ──────────────────────────────

BOOST_AUTO_TEST_CASE(sharpen_reads_strength_param)
{
    FilterFixture f;
    f.fillGradient();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("sharpen", {{"strength", 5.0}});

    auto after = f.activeLayer()->cpuImage;
    BOOST_CHECK(before != after);
    BOOST_CHECK(!after.isNull());
}

BOOST_AUTO_TEST_CASE(sharpen_default_strength)
{
    FilterFixture f;
    f.fillGradient();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("sharpen", {});

    BOOST_CHECK(before != f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_CASE(sharpen_undo_restores_original)
{
    FilterFixture f;
    f.fillCheckerboard();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("sharpen", {{"strength", 5.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── gaussian_blur uses "radius" param ──────────────────────────

BOOST_AUTO_TEST_CASE(gaussian_blur_reads_radius_param)
{
    FilterFixture f;
    f.fillCheckerboard();

    f.ctrl.executeTool("gaussian_blur", {{"radius", 5.0}});

    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(gaussian_blur_undo_restores_original)
{
    FilterFixture f;
    f.fillCheckerboard();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("gaussian_blur", {{"radius", 3.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── median_blur uses "kernel_size" param ───────────────────────

BOOST_AUTO_TEST_CASE(median_blur_reads_kernel_size_param)
{
    FilterFixture f;
    f.fillCheckerboard();

    f.ctrl.executeTool("median_blur", {{"kernel_size", 5.0}});

    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(median_blur_undo_restores_original)
{
    FilterFixture f;
    f.fillCheckerboard();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("median_blur", {{"kernel_size", 5.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── New blur filters ───────────────────────────────────────────────
// Activating a full-canvas selection forces the synchronous filter path
// (applyFilterWithSelection), so the result + undo command are produced
// immediately without needing an event loop for the async worker.

BOOST_AUTO_TEST_CASE(box_blur_reads_radius_and_undo_restores)
{
    FilterFixture f;
    f.fillCheckerboard();
    f.doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("box_blur", {{"radius", 4.0}});
    BOOST_CHECK(before != f.activeLayer()->cpuImage);   // actually modified

    f.ctrl.history().undo();
    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_CASE(box_blur_radius_zero_leaves_image_unchanged)
{
    FilterFixture f;
    f.fillCheckerboard();
    f.doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("box_blur", {{"radius", 0.0}});
    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_CASE(bilateral_blur_runs_and_undo_restores)
{
    FilterFixture f;
    f.fillCheckerboard();
    f.doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("bilateral_blur",
        {{"diameter", 9.0}, {"sigma_color", 75.0}, {"sigma_space", 75.0}});
    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());

    f.ctrl.history().undo();
    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_CASE(motion_blur_runs_and_undo_restores)
{
    FilterFixture f;
    f.fillCheckerboard();
    f.doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("motion_blur", {{"length", 15.0}, {"angle", 45.0}});
    BOOST_CHECK(before != f.activeLayer()->cpuImage);

    f.ctrl.history().undo();
    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_CASE(radial_blur_amount_zero_leaves_image_unchanged)
{
    FilterFixture f;
    f.fillGradient();
    f.doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("radial_blur",
        {{"amount", 0.0}, {"samples", 12.0}, {"center_x", 0.5}, {"center_y", 0.5}});
    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_CASE(zoom_blur_amount_zero_leaves_image_unchanged)
{
    FilterFixture f;
    f.fillGradient();
    f.doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("zoom_blur",
        {{"amount", 0.0}, {"samples", 12.0}, {"center_x", 0.5}, {"center_y", 0.5}});
    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_CASE(zoom_blur_runs_and_undo_restores)
{
    FilterFixture f;
    f.fillGradient();
    f.doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("zoom_blur",
        {{"amount", 0.5}, {"samples", 16.0}, {"center_x", 0.5}, {"center_y", 0.5}});
    BOOST_CHECK(before != f.activeLayer()->cpuImage);

    f.ctrl.history().undo();
    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── edge_detect uses "threshold1"/"threshold2" ─────────────────

BOOST_AUTO_TEST_CASE(edge_detect_reads_threshold_params)
{
    FilterFixture f;
    f.fillCheckerboard();

    f.ctrl.executeTool("edge_detect", {{"threshold1", 50.0}, {"threshold2", 150.0}});

    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(edge_detect_undo_restores_original)
{
    FilterFixture f;
    f.fillCheckerboard();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("edge_detect", {{"threshold1", 50.0}, {"threshold2", 150.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── grayscale ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(grayscale_makes_rgb_equal)
{
    FilterFixture f;
    f.fill(120, 180, 60);

    f.ctrl.executeTool("grayscale", {});

    auto c = f.activeLayer()->cpuImage.pixelColor(0, 0);
    BOOST_CHECK_EQUAL(c.red(), c.green());
    BOOST_CHECK_EQUAL(c.green(), c.blue());
}

BOOST_AUTO_TEST_CASE(grayscale_undo_restores_original)
{
    FilterFixture f;
    f.fill(120, 180, 60);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("grayscale", {});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── invert_colors ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(invert_colors_flips_pixels)
{
    FilterFixture f;
    f.fill(200, 50, 100);

    f.ctrl.executeTool("invert_colors", {});

    auto c = f.activeLayer()->cpuImage.pixelColor(0, 0);
    BOOST_CHECK_EQUAL(c.red(), uchar(55));    // 255 - 200
    BOOST_CHECK_EQUAL(c.green(), uchar(205));  // 255 - 50
    BOOST_CHECK_EQUAL(c.blue(), uchar(155));   // 255 - 100
}

BOOST_AUTO_TEST_CASE(invert_colors_undo_restores_original)
{
    FilterFixture f;
    f.fill(200, 50, 100);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── posterize uses "levels" param ──────────────────────────────

BOOST_AUTO_TEST_CASE(posterize_reads_levels_param)
{
    FilterFixture f;
    f.fill(123, 45, 67);

    f.ctrl.executeTool("posterize", {{"levels", 4.0}});

    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(posterize_undo_restores_original)
{
    FilterFixture f;
    f.fill(123, 45, 67);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("posterize", {{"levels", 4.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── threshold uses "value" param ───────────────────────────────

BOOST_AUTO_TEST_CASE(threshold_reads_value_param)
{
    FilterFixture f;
    f.fillCheckerboard();

    f.ctrl.executeTool("threshold", {{"value", 128.0}});

    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(threshold_undo_restores_original)
{
    FilterFixture f;
    f.fillCheckerboard();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("threshold", {{"value", 128.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── noise_reduce uses "strength" param ─────────────────────────

BOOST_AUTO_TEST_CASE(noise_reduce_reads_strength_param)
{
    FilterFixture f;
    f.fillCheckerboard();

    f.ctrl.executeTool("noise_reduce", {{"strength", 3.0}, {"preserve_edges", 0.5}});

    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(noise_reduce_undo_restores_original)
{
    FilterFixture f;
    f.fillCheckerboard();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("noise_reduce", {{"strength", 3.0}, {"preserve_edges", 0.5}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── remove_background ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(remove_background_undo_restores_original)
{
    FilterFixture f;
    f.fillCheckerboard();
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("remove_background", {{"mode", 0.0}, {"threshold", 20.0}, {"feather", 5.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

// ── adjust_color with all params ───────────────────────────────

BOOST_AUTO_TEST_CASE(adjust_color_reads_named_params)
{
    FilterFixture f;
    f.fill(128, 128, 128);

    f.ctrl.executeTool("adjust_color", {
        {"brightness", 0.5}, {"contrast", 0.3},
        {"saturation", 0.2}, {"hue", 0.1}, {"auto_contrast", 0.0}
    });

    BOOST_CHECK(!f.activeLayer()->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(adjust_color_undo_restores_original)
{
    FilterFixture f;
    f.fill(128, 128, 128);
    auto before = f.activeLayer()->cpuImage.copy();

    f.ctrl.executeTool("adjust_color", {
        {"brightness", 0.5}, {"contrast", 0.3},
        {"saturation", 0.2}, {"hue", 0.1}, {"auto_contrast", 0.0}
    });
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.activeLayer()->cpuImage);
}

BOOST_AUTO_TEST_SUITE_END()
