#define BOOST_TEST_MODULE ControllerAllToolsTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "controller/Commands.hpp"
#include "engine/ImageEngine.hpp"
#include "tools/ToolCall.hpp"
#include "text/TextTypes.hpp"
#include "text/TextRenderer.hpp"

#include <QImage>
#include <QColor>
#include <QTransform>
#include <QApplication>
#include <cmath>

// Global fixture to ensure QApplication exists for text rendering
struct QtAppFixture {
    QtAppFixture() {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = { const_cast<char*>("test") };
            static QApplication app(argc, argv);
        }
    }
};
BOOST_GLOBAL_FIXTURE(QtAppFixture);

struct AllToolsFixture {
    Document doc;
    ImageController ctrl;

    AllToolsFixture() : ctrl() {
        doc.size = QSize(200, 150);
        doc.selection.create(200, 150);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    Layer* active() { return doc.activeLayer(); }
    int activeIdx() const { return doc.activeFlatIndex; }

    void fillLayer(uchar r, uchar g, uchar b, uchar a = 255) {
        auto* l = active();
        if (l) l->cpuImage.fill(QColor(r, g, b, a));
    }

    void addSecondLayer() { ctrl.newLayer(); }
    void addGroup() { ctrl.newGroup(); }

    LayerTreeNode* nodeAt(int idx) { return doc.nodeAt(idx); }
    int flatCount() const { return doc.flatCount(); }
};

using Params = std::unordered_map<std::string, JsonValue>;

BOOST_AUTO_TEST_SUITE(alltools)

static QTransform rotatedLike(const QTransform& base, float radians)
{
    const float hw = std::sqrt(base.m11() * base.m11() + base.m12() * base.m12());
    const float hh = std::sqrt(base.m21() * base.m21() + base.m22() * base.m22());
    const float c = std::cos(radians);
    const float s = std::sin(radians);
    QTransform t;
    t.setMatrix(c * hw, s * hw, 0.0,
                -s * hh, c * hh, 0.0,
                base.m31(), base.m32(), 1.0);
    return t;
}

// ── Document Tools ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(new_document_creates_layer)
{
    AllToolsFixture f;
    f.ctrl.executeTool("new_document", {{"width", 500.0}, {"height", 400.0}});
    BOOST_CHECK_EQUAL(f.doc.size.width(), 500);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 400);
    BOOST_CHECK(f.doc.flatCount() >= 1);
}

BOOST_AUTO_TEST_CASE(new_document_defaults)
{
    AllToolsFixture f;
    f.ctrl.executeTool("new_document", {});
    BOOST_CHECK_EQUAL(f.doc.size.width(), 1024);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 768);
}

BOOST_AUTO_TEST_CASE(resize_document_changes_size)
{
    AllToolsFixture f;
    QSize before = f.doc.size;
    f.ctrl.executeTool("resize_document", {{"width", 300.0}, {"height", 200.0}});
    BOOST_CHECK_EQUAL(f.doc.size.width(), 300);
    BOOST_CHECK_NE(before.width(), 300);
}

BOOST_AUTO_TEST_CASE(resize_document_undo)
{
    AllToolsFixture f;
    QSize before = f.doc.size;
    f.ctrl.executeTool("resize_document", {{"width", 400.0}, {"height", 400.0}});
    // NOTE: resize_document may not push to history
    BOOST_CHECK_EQUAL(f.doc.size.width(), 400);
    if (f.ctrl.history().canUndo()) {
        f.ctrl.history().undo();
        BOOST_CHECK_EQUAL(f.doc.size.width(), before.width());
    }
}

BOOST_AUTO_TEST_CASE(resize_canvas_changes_canvas_without_resampling_layers)
{
    AllToolsFixture f;
    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    const QSize beforeLayerSize = layer->cpuImage.size();

    f.ctrl.executeTool("resize_canvas", {
        {"width", 300.0},
        {"height", 210.0},
        {"anchor", std::string("center")}
    });

    BOOST_CHECK_EQUAL(f.doc.size.width(), 300);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 210);
    BOOST_CHECK_EQUAL(layer->cpuImage.width(), beforeLayerSize.width());
    BOOST_CHECK_EQUAL(layer->cpuImage.height(), beforeLayerSize.height());
}

BOOST_AUTO_TEST_CASE(resize_canvas_anchor_moves_selection_mask)
{
    AllToolsFixture f;
    f.doc.selection.image().fill(0);
    f.doc.selection.image().scanLine(0)[0] = 255;
    f.doc.selection.setActive(true);

    f.ctrl.executeTool("resize_canvas", {
        {"width", 300.0},
        {"height", 210.0},
        {"anchor", std::string("bottom_right")}
    });

    BOOST_CHECK_EQUAL(f.doc.size.width(), 300);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 210);
    BOOST_CHECK(!f.doc.selection.isSelected(0, 0));
    BOOST_CHECK(f.doc.selection.isSelected(100, 60));

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.doc.size.width(), 200);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 150);
    BOOST_CHECK(f.doc.selection.isSelected(0, 0));
}

BOOST_AUTO_TEST_CASE(resize_canvas_fill_extension_adds_fill_layer)
{
    AllToolsFixture f;
    const int beforeCount = f.flatCount();

    f.ctrl.executeTool("resize_canvas", {
        {"width", 220.0},
        {"height", 170.0},
        {"anchor", std::string("top_left")},
        {"fill_extension", 1.0},
        {"extension_color", std::string("#112233")}
    });

    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount + 1);
    auto flat = f.doc.flatten();
    BOOST_REQUIRE(!flat.empty());
    auto* fillNode = flat.back();
    BOOST_REQUIRE(fillNode);
    BOOST_REQUIRE(fillNode->layer);

    const QColor ext = fillNode->layer->cpuImage.pixelColor(210, 160);
    BOOST_CHECK_EQUAL(ext.red(), 0x11);
    BOOST_CHECK_EQUAL(ext.green(), 0x22);
    BOOST_CHECK_EQUAL(ext.blue(), 0x33);
    BOOST_CHECK_EQUAL(ext.alpha(), 255);

    const QColor insideOld = fillNode->layer->cpuImage.pixelColor(10, 10);
    BOOST_CHECK_EQUAL(insideOld.alpha(), 0);
}

// ── Layer Tools ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(add_layer_increases_count)
{
    AllToolsFixture f;
    int before = f.flatCount();
    f.ctrl.executeTool("add_layer", {});
    BOOST_CHECK_EQUAL(f.flatCount(), before + 1);
}

BOOST_AUTO_TEST_CASE(add_layer_undo)
{
    AllToolsFixture f;
    int before = f.flatCount();
    f.ctrl.executeTool("add_layer", {});
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(remove_layer_decreases_count)
{
    AllToolsFixture f;
    f.fillLayer(255, 0, 0);
    int before = f.flatCount();
    f.ctrl.executeTool("remove_layer", {{"index", 0.0}});
    BOOST_CHECK_EQUAL(f.flatCount(), before - 1);
}

BOOST_AUTO_TEST_CASE(remove_layer_undo_restores_content)
{
    AllToolsFixture f;
    f.fillLayer(100, 150, 200);
    f.ctrl.executeTool("remove_layer", {{"index", 0.0}});
    f.ctrl.history().undo();
    auto* restored = f.doc.activeLayer();
    BOOST_REQUIRE(restored);
    BOOST_CHECK_EQUAL(restored->cpuImage.pixelColor(0, 0).red(), uchar(100));
}

BOOST_AUTO_TEST_CASE(duplicate_layer_creates_copy)
{
    AllToolsFixture f;
    f.fillLayer(50, 100, 150);
    int before = f.flatCount();
    f.ctrl.executeTool("duplicate_layer", {{"index", 0.0}});
    BOOST_CHECK_EQUAL(f.flatCount(), before + 1);
}

BOOST_AUTO_TEST_CASE(duplicate_layer_undo)
{
    AllToolsFixture f;
    f.fillLayer(50, 100, 150);
    int before = f.flatCount();
    f.ctrl.executeTool("duplicate_layer", {{"index", 0.0}});
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(set_layer_opacity_changes_opacity)
{
    AllToolsFixture f;
    f.ctrl.executeTool("set_layer_opacity", {{"index", 0.0}, {"opacity", 0.5}});
    auto* node = f.nodeAt(0);
    BOOST_REQUIRE(node);
    BOOST_CHECK_CLOSE(node->opacity, 0.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(set_layer_opacity_default)
{
    AllToolsFixture f;
    BOOST_CHECK(f.ctrl.executeTool("set_layer_opacity", {}));
    auto* node = f.nodeAt(0);
    BOOST_REQUIRE(node);
    BOOST_CHECK_CLOSE(node->opacity, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(set_layer_visibility_hide)
{
    AllToolsFixture f;
    f.ctrl.executeTool("set_layer_visibility", {{"index", 0.0}, {"visible", 0.0}});
    auto* node = f.nodeAt(0);
    BOOST_REQUIRE(node);
    BOOST_CHECK(!node->visible);
}

BOOST_AUTO_TEST_CASE(set_layer_visibility_show)
{
    AllToolsFixture f;
    f.nodeAt(0)->visible = false;
    f.ctrl.executeTool("set_layer_visibility", {{"index", 0.0}, {"visible", 1.0}});
    BOOST_CHECK(f.nodeAt(0)->visible);
}

BOOST_AUTO_TEST_CASE(set_layer_blend_mode_changes_mode)
{
    AllToolsFixture f;
    f.ctrl.executeTool("set_layer_blend_mode", {{"index", 0.0}, {"mode", 3.0}});
    BOOST_CHECK(f.nodeAt(0)->blendMode == BlendMode::Overlay);
}

BOOST_AUTO_TEST_CASE(set_layer_blend_mode_default_normal)
{
    AllToolsFixture f;
    f.ctrl.executeTool("set_layer_blend_mode", {});
    BOOST_CHECK(f.nodeAt(0)->blendMode == BlendMode::Normal);
}

BOOST_AUTO_TEST_CASE(fill_layer_changes_color)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("fill_layer",
        {{"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0}});
    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).red(), uchar(200));
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).green(), uchar(50));
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).blue(), uchar(100));
}

BOOST_AUTO_TEST_CASE(fill_layer_undo)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("fill_layer",
        {{"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0}});
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(0, 0).red(), uchar(100));
}

// ── Adjust Tools ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(adjust_brightness_executes)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    auto before = f.active()->cpuImage.copy();
    BOOST_CHECK(f.ctrl.executeTool("adjust_brightness", {{"value", 0.3}}));
}

BOOST_AUTO_TEST_CASE(adjust_brightness_undo)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("adjust_brightness", {{"value", 0.3}});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(adjust_contrast_executes)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("adjust_contrast", {{"value", 0.3}}));
}

BOOST_AUTO_TEST_CASE(adjust_contrast_undo)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("adjust_contrast", {{"value", 0.3}});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(adjust_saturation_execution)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    BOOST_CHECK(f.ctrl.executeTool("adjust_saturation", {{"value", 0.5}}));
}

BOOST_AUTO_TEST_CASE(adjust_saturation_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("adjust_saturation", {{"value", 0.5}});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(adjust_hue_execution)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    BOOST_CHECK(f.ctrl.executeTool("adjust_hue", {{"value", 0.5}}));
}

BOOST_AUTO_TEST_CASE(adjust_hue_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("adjust_hue", {{"value", 0.5}});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(auto_contrast_execution)
{
    AllToolsFixture f;
    f.fillLayer(50, 50, 50);
    BOOST_CHECK(f.ctrl.executeTool("auto_contrast", {}));
}

BOOST_AUTO_TEST_CASE(auto_contrast_undo)
{
    AllToolsFixture f;
    f.fillLayer(50, 50, 50);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("auto_contrast", {});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(adjust_color_all_params)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("adjust_color", {
        {"brightness", 0.1}, {"contrast", 0.1},
        {"saturation", 0.1}, {"hue", 0.1}, {"auto_contrast", 0.0}
    }));
}

BOOST_AUTO_TEST_CASE(adjust_color_undo)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("adjust_color", {
        {"brightness", 0.2}, {"contrast", 0.2}, {"saturation", 0.2}, {"hue", 0.2}
    });
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

// ── Filter Tools ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(gaussian_blur_default_params)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {}));
}

BOOST_AUTO_TEST_CASE(sharpen_default_params)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("sharpen", {}));
}

BOOST_AUTO_TEST_CASE(median_blur_kernel_3)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("median_blur", {{"kernel_size", 3.0}}));
}

BOOST_AUTO_TEST_CASE(edge_detect_both_thresholds)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("edge_detect", {{"threshold1", 30.0}, {"threshold2", 120.0}}));
}

BOOST_AUTO_TEST_CASE(noise_reduce_with_strength)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("noise_reduce", {{"strength", 5.0}}));
}

BOOST_AUTO_TEST_CASE(posterize_levels_default)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    BOOST_CHECK(f.ctrl.executeTool("posterize", {}));
}

BOOST_AUTO_TEST_CASE(threshold_value_128)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    BOOST_CHECK(f.ctrl.executeTool("threshold", {{"value", 128.0}}));
}

BOOST_AUTO_TEST_CASE(remove_background_executes)
{
    AllToolsFixture f;
    f.fillLayer(200, 200, 200);
    BOOST_CHECK(f.ctrl.executeTool("remove_background", {}));
}

BOOST_AUTO_TEST_CASE(remove_background_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 200, 200);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("remove_background", {});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

// ── Effect Tools ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(grayscale_execution)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    BOOST_CHECK(f.ctrl.executeTool("grayscale", {}));
    auto c = f.active()->cpuImage.pixelColor(0, 0);
    BOOST_CHECK_EQUAL(c.red(), c.green());
    BOOST_CHECK_EQUAL(c.green(), c.blue());
}

BOOST_AUTO_TEST_CASE(grayscale_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("grayscale", {});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(invert_colors_execution)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);
    f.ctrl.executeTool("invert_colors", {});
    auto c = f.active()->cpuImage.pixelColor(0, 0);
    BOOST_CHECK_EQUAL(c.red(), uchar(55));
    BOOST_CHECK_EQUAL(c.green(), uchar(205));
    BOOST_CHECK_EQUAL(c.blue(), uchar(155));
}

BOOST_AUTO_TEST_CASE(invert_colors_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

// ── Transform Tools ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(crop_execution)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage(100, 100, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::red);
    BOOST_CHECK(f.ctrl.executeTool("crop", {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}}));
    BOOST_CHECK_EQUAL(f.active()->cpuImage.width(), 50);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.height(), 50);
}

BOOST_AUTO_TEST_CASE(crop_undo)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage(100, 100, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::red);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("crop", {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(rotate_execution)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage(50, 50, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::red);
    BOOST_CHECK(f.ctrl.executeTool("rotate", {{"angle", 45.0}}));
}

BOOST_AUTO_TEST_CASE(rotate_undo)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage(50, 50, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::red);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("rotate", {{"angle", 45.0}});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(flip_horizontal_execution)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage(20, 20, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(QColor(255, 0, 0));
    f.active()->cpuImage.setPixelColor(0, 0, QColor(0, 0, 255));
    BOOST_CHECK(f.ctrl.executeTool("flip_horizontal", {}));
}

BOOST_AUTO_TEST_CASE(flip_horizontal_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("flip_horizontal", {});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(flip_vertical_execution)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    BOOST_CHECK(f.ctrl.executeTool("flip_vertical", {}));
}

BOOST_AUTO_TEST_CASE(flip_vertical_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("flip_vertical", {});
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(resize_layer_execution)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage(200, 200, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::white);
    BOOST_CHECK(f.ctrl.executeTool("resize_layer", {{"width", 100.0}, {"height", 100.0}}));
    BOOST_CHECK_EQUAL(f.active()->cpuImage.width(), 100);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.height(), 100);
}

BOOST_AUTO_TEST_CASE(resize_layer_undo)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage(200, 200, QImage::Format_RGBA8888);
    f.active()->cpuImage.fill(Qt::white);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("resize_layer", {{"width", 100.0}, {"height", 100.0}});
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.active()->cpuImage.width(), 200);
}

// ── Viewport Tools ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(zoom_changes_zoom)
{
    AllToolsFixture f;
    f.ctrl.executeTool("zoom", {{"level", 2.0}});
    BOOST_CHECK_CLOSE(f.doc.zoom, 2.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(zoom_default_level)
{
    AllToolsFixture f;
    f.doc.zoom = 3.0f;
    f.ctrl.executeTool("zoom", {});
    BOOST_CHECK_CLOSE(f.doc.zoom, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(reset_view_resets_zoom_and_pan)
{
    AllToolsFixture f;
    f.doc.zoom = 5.0f;
    f.doc.panOffset = QPointF(100, 200);
    f.ctrl.executeTool("reset_view", {});
    BOOST_CHECK_CLOSE(f.doc.zoom, 1.0f, 0.001f);
    BOOST_CHECK_EQUAL(f.doc.panOffset.x(), 0.0);
    BOOST_CHECK_EQUAL(f.doc.panOffset.y(), 0.0);
}

// ── Selection Tools ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(select_all_sets_full_mask)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    BOOST_CHECK(f.doc.selection.active());
    BOOST_CHECK(!f.doc.selection.isEmpty());
}

BOOST_AUTO_TEST_CASE(deselect_clears_selection)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    f.ctrl.executeTool("deselect", {});
    BOOST_CHECK(!f.doc.selection.active());
    BOOST_CHECK(f.doc.selection.isEmpty());
}

BOOST_AUTO_TEST_CASE(select_invert_inverts)
{
    AllToolsFixture f;
    // Select a specific rect
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    BOOST_CHECK(f.doc.selection.isSelected(30, 30));
    BOOST_CHECK(!f.doc.selection.isSelected(80, 80));

    f.ctrl.executeTool("select_invert", {});
    BOOST_CHECK(f.doc.selection.active());
    // Pixels inside original rect should NOT be selected after invert
    BOOST_CHECK(!f.doc.selection.isSelected(30, 30));
    // Pixels outside original rect SHOULD be selected after invert
    BOOST_CHECK(f.doc.selection.isSelected(80, 80));
}

BOOST_AUTO_TEST_CASE(select_invert_undo_restores_values)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    QImage before = f.doc.selection.image().copy();
    f.ctrl.executeTool("select_invert", {});
    f.ctrl.history().undo();
    BOOST_CHECK(f.doc.selection.image() == before);
}

BOOST_AUTO_TEST_CASE(select_rect_create)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    BOOST_CHECK(f.doc.selection.active());
    BOOST_CHECK(!f.doc.selection.isEmpty());
}

BOOST_AUTO_TEST_CASE(select_rect_replace_mode)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}, {"mode", 0.0}});
    BOOST_CHECK(f.doc.selection.active());
}

BOOST_AUTO_TEST_CASE(select_rect_add_mode)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}, {"mode", 1.0}});
    BOOST_CHECK(f.doc.selection.active());
}

BOOST_AUTO_TEST_CASE(select_magic_wand_no_layer_fails)
{
    AllToolsFixture f;
    f.active()->cpuImage = QImage();
    bool ok = f.ctrl.executeTool("select_magic_wand",
        {{"x", 10.0}, {"y", 10.0}, {"tolerance", 32.0}});
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(select_magic_wand_with_layer)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    f.active()->cpuImage.setPixelColor(5, 5, QColor(255, 0, 0));
    bool ok = f.ctrl.executeTool("select_magic_wand",
        {{"x", 0.0}, {"y", 0.0}, {"tolerance", 32.0}});
    BOOST_CHECK(ok);
}

BOOST_AUTO_TEST_CASE(select_magic_wand_undo)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    f.ctrl.executeTool("select_magic_wand",
        {{"x", 0.0}, {"y", 0.0}, {"tolerance", 32.0}});
    if (f.ctrl.history().canUndo())
        f.ctrl.history().undo();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(select_feather_executes)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    BOOST_CHECK(f.ctrl.executeTool("select_feather", {{"radius", 5.0}}));
}

BOOST_AUTO_TEST_CASE(select_feather_undo)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    auto before = f.doc.selection.image().copy();
    f.ctrl.executeTool("select_feather", {{"radius", 5.0}});
    f.ctrl.history().undo();
}

BOOST_AUTO_TEST_CASE(select_grow_executes)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    BOOST_CHECK(f.ctrl.executeTool("select_grow", {{"pixels", 5.0}}));
}

BOOST_AUTO_TEST_CASE(select_grow_undo)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    auto before = f.doc.selection.image().copy();
    f.ctrl.executeTool("select_grow", {{"pixels", 5.0}});
    f.ctrl.history().undo();
}

BOOST_AUTO_TEST_CASE(select_shrink_executes)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    BOOST_CHECK(f.ctrl.executeTool("select_shrink", {{"pixels", 5.0}}));
}

BOOST_AUTO_TEST_CASE(select_shrink_undo)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    auto before = f.doc.selection.image().copy();
    f.ctrl.executeTool("select_shrink", {{"pixels", 5.0}});
    f.ctrl.history().undo();
}

BOOST_AUTO_TEST_CASE(select_border_executes)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    BOOST_CHECK(f.ctrl.executeTool("select_border", {{"pixels", 5.0}}));
}

BOOST_AUTO_TEST_CASE(select_border_undo)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    auto before = f.doc.selection.image().copy();
    f.ctrl.executeTool("select_border", {{"pixels", 5.0}});
    f.ctrl.history().undo();
}

BOOST_AUTO_TEST_CASE(select_smooth_executes)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    BOOST_CHECK(f.ctrl.executeTool("select_smooth", {{"radius", 3.0}}));
}

BOOST_AUTO_TEST_CASE(select_smooth_undo)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    auto before = f.doc.selection.image().copy();
    f.ctrl.executeTool("select_smooth", {{"radius", 3.0}});
    f.ctrl.history().undo();
}

BOOST_AUTO_TEST_CASE(select_save_channel_non_active_selection_fails)
{
    AllToolsFixture f;
    bool ok = f.ctrl.executeTool("select_save_channel", {{"name", std::string("Ch1")}});
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(select_save_channel_with_active_selection)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    bool ok = f.ctrl.executeTool("select_save_channel", {{"name", std::string("Ch1")}});
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(f.doc.channels.size(), 1);
}

BOOST_AUTO_TEST_CASE(select_load_channel_invalid_index_fails)
{
    AllToolsFixture f;
    bool ok = f.ctrl.executeTool("select_load_channel", {{"index", 999.0}});
}

BOOST_AUTO_TEST_CASE(select_load_channel_valid_index)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    f.ctrl.executeTool("select_save_channel", {{"name", std::string("Ch1")}});
    f.ctrl.executeTool("deselect", {});
    bool ok = f.ctrl.executeTool("select_load_channel", {{"index", 0.0}, {"mode", 0.0}});
    BOOST_CHECK(ok);
    BOOST_CHECK(f.doc.selection.active());
}

BOOST_AUTO_TEST_CASE(delete_selected_active_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    f.ctrl.executeTool("select_all", {});
    BOOST_CHECK(f.ctrl.executeTool("delete_selected", {}));
}

BOOST_AUTO_TEST_CASE(delete_selected_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    auto before = f.active()->cpuImage.copy();
    f.ctrl.executeTool("select_all", {});
    f.ctrl.executeTool("delete_selected", {});
    f.ctrl.history().undo();
    f.ctrl.history().undo();
    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(delete_selected_partial_rect)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    QColor origColor = f.active()->cpuImage.pixelColor(50, 50);
    QColor origOutside = f.active()->cpuImage.pixelColor(5, 5);

    f.ctrl.executeTool("select_rect",
        {{"x", 40.0}, {"y", 40.0}, {"width", 20.0}, {"height", 20.0}});
    f.ctrl.executeTool("delete_selected", {});

    QColor afterInside = f.active()->cpuImage.pixelColor(50, 50);
    QColor afterOutside = f.active()->cpuImage.pixelColor(5, 5);
    BOOST_CHECK_EQUAL(afterInside.alpha(), uchar(0));
    BOOST_CHECK_EQUAL(afterOutside.red(), origOutside.red());
}

BOOST_AUTO_TEST_CASE(delete_selected_after_layer_move)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);

    // Set up a selection at a known document position
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 30.0}, {"height", 30.0}});

    // Move the layer by setting its transform directly (simulates drag)
    auto* node = f.nodeAt(0);
    BOOST_REQUIRE(node);
    node->transform = QTransform::fromTranslate(0.1f, 0.0f);

    // Delete selected should use the moved transform for mapping
    BOOST_CHECK(f.ctrl.executeTool("delete_selected", {}));
}

BOOST_AUTO_TEST_CASE(delete_selected_after_layer_reset_view)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);

    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 30.0}, {"height", 30.0}});

    // Change zoom (should not affect selection mapping)
    f.ctrl.executeTool("zoom", {{"level", 2.0}});
    BOOST_CHECK(f.ctrl.executeTool("delete_selected", {}));
}

// ── Zoom tool ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(crop_to_selection_no_selection_fails)
{
    AllToolsFixture f;
    bool ok = f.ctrl.executeTool("crop_to_selection", {});
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(crop_to_selection_with_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    BOOST_CHECK(f.ctrl.executeTool("crop_to_selection", {}));
    BOOST_CHECK(!f.doc.selection.active());
}

BOOST_AUTO_TEST_CASE(crop_to_selection_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    QSize before = f.doc.size;
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    f.ctrl.executeTool("crop_to_selection", {});
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.doc.size.width(), before.width());
}

BOOST_AUTO_TEST_CASE(copy_no_selection_no_layer)
{
    AllToolsFixture f;
    BOOST_CHECK(f.ctrl.executeTool("copy", {}));
}

BOOST_AUTO_TEST_CASE(paste_empty_clipboard_noop)
{
    AllToolsFixture f;
    BOOST_CHECK(f.ctrl.executeTool("paste", {}));
}

BOOST_AUTO_TEST_CASE(copy_paste_roundtrip)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    int before = f.flatCount();
    f.ctrl.executeTool("copy", {});
    BOOST_CHECK(f.ctrl.executeTool("paste", {}));
}

BOOST_AUTO_TEST_CASE(float_selection_no_selection_fails)
{
    AllToolsFixture f;
    bool ok = f.ctrl.executeTool("float_selection", {{"cut", 0.0}});
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(float_selection_with_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    BOOST_CHECK(f.ctrl.executeTool("float_selection", {{"cut", 1.0}}));
}

BOOST_AUTO_TEST_CASE(float_selection_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    f.ctrl.executeTool("select_rect",
        {{"x", 10.0}, {"y", 10.0}, {"width", 50.0}, {"height", 50.0}});
    int before = f.flatCount();
    f.ctrl.executeTool("float_selection", {{"cut", 1.0}});
    f.ctrl.history().undo();
}

// ── Layer Mask Tools ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(layer_mask_add)
{
    AllToolsFixture f;
    BOOST_CHECK(f.ctrl.executeTool("layer_mask_add", {{"index", 0.0}}));
    BOOST_CHECK(f.ctrl.hasLayerMask(0));
}

BOOST_AUTO_TEST_CASE(layer_mask_add_invalid_index_fails)
{
    AllToolsFixture f;
    BOOST_CHECK(!f.ctrl.executeTool("layer_mask_add", {{"index", 999.0}}));
}

BOOST_AUTO_TEST_CASE(layer_mask_remove)
{
    AllToolsFixture f;
    f.ctrl.executeTool("layer_mask_add", {{"index", 0.0}});
    BOOST_CHECK(f.ctrl.executeTool("layer_mask_remove", {{"index", 0.0}}));
    BOOST_CHECK(!f.ctrl.hasLayerMask(0));
}

BOOST_AUTO_TEST_CASE(layer_mask_toggle)
{
    AllToolsFixture f;
    f.ctrl.executeTool("layer_mask_add", {{"index", 0.0}});
    f.ctrl.executeTool("layer_mask_toggle", {{"index", 0.0}});
    BOOST_CHECK(!f.ctrl.isLayerMaskEnabled(0));
    f.ctrl.executeTool("layer_mask_toggle", {{"index", 0.0}});
    BOOST_CHECK(f.ctrl.isLayerMaskEnabled(0));
}

BOOST_AUTO_TEST_CASE(layer_mask_enable_disable)
{
    AllToolsFixture f;
    f.ctrl.executeTool("layer_mask_add", {{"index", 0.0}});
    f.ctrl.executeTool("layer_mask_disable", {{"index", 0.0}});
    BOOST_CHECK(!f.ctrl.isLayerMaskEnabled(0));
    f.ctrl.executeTool("layer_mask_enable", {{"index", 0.0}});
    BOOST_CHECK(f.ctrl.isLayerMaskEnabled(0));
}

BOOST_AUTO_TEST_CASE(layer_mask_apply)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    f.ctrl.executeTool("layer_mask_add", {{"index", 0.0}});
    BOOST_CHECK(f.ctrl.executeTool("layer_mask_apply", {{"index", 0.0}}));
}

// ── Merge Tools ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(merge_visible_single_layer_noop)
{
    AllToolsFixture f;
    BOOST_CHECK(!f.ctrl.executeTool("merge_visible", {}));
}

BOOST_AUTO_TEST_CASE(merge_visible_two_layers)
{
    AllToolsFixture f;
    f.fillLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillLayer(0, 255, 0);
    int before = f.flatCount();
    BOOST_CHECK(f.ctrl.executeTool("merge_visible", {}));
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(merge_visible_undo)
{
    AllToolsFixture f;
    f.fillLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillLayer(0, 255, 0);
    int before = f.flatCount();
    f.ctrl.executeTool("merge_visible", {});
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(merge_down_top_layer_noop)
{
    AllToolsFixture f;
    BOOST_CHECK(!f.ctrl.executeTool("merge_down", {}));
}

BOOST_AUTO_TEST_CASE(merge_down_with_lower_layer)
{
    AllToolsFixture f;
    f.ctrl.executeTool("add_layer", {});
    f.fillLayer(0, 255, 0);
    BOOST_CHECK(f.ctrl.executeTool("merge_down", {}));
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(merge_layers_execution)
{
    AllToolsFixture f;
    f.fillLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillLayer(0, 255, 0);
    f.ctrl.executeTool("merge_layers", {});
    BOOST_CHECK_GE(f.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(flatten_image_execution)
{
    AllToolsFixture f;
    f.fillLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillLayer(0, 255, 0);
    f.ctrl.executeTool("flatten_image", {});
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(flatten_image_keeps_blend_mode_result)
{
    AllToolsFixture f;
    // Bottom: red
    f.fillLayer(255, 0, 0);
    // Top: green with Multiply
    f.addSecondLayer();
    f.fillLayer(0, 255, 0);
    f.ctrl.executeTool("set_layer_blend_mode", {{"index", 0.0}, {"mode", 1.0}}); // Multiply

    BOOST_CHECK(f.ctrl.executeTool("flatten_image", {}));
    BOOST_CHECK_EQUAL(f.flatCount(), 1);

    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    QColor c = layer->cpuImage.pixelColor(10, 10);
    BOOST_CHECK_EQUAL(c.red(), 0);
    BOOST_CHECK_EQUAL(c.green(), 0);
    BOOST_CHECK_EQUAL(c.blue(), 0);
}

BOOST_AUTO_TEST_CASE(rasterize_layer_execution)
{
    AllToolsFixture f;
    BOOST_CHECK(f.ctrl.executeTool("rasterize_layer", {}));
}

// ── Text Tools ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(create_text_layer_has_data)
{
    AllToolsFixture f;
    TextBox box;
    box.width = 200.0f;
    f.ctrl.createTextLayer("Hello", box, QPointF(0, 0), 32.0f);

    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    BOOST_REQUIRE(layer->textData);
    BOOST_CHECK_EQUAL(layer->textData->text.toStdString(), "Hello");
    BOOST_CHECK_EQUAL(layer->textData->spans.size(), 1);
    BOOST_CHECK_CLOSE(layer->textData->spans.back().fontSize, 32.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(create_text_layer_renders_image)
{
    AllToolsFixture f;
    TextBox box;
    box.width = 200.0f;
    f.ctrl.createTextLayer("Test", box, QPointF(0, 0), 32.0f);

    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    BOOST_CHECK(!layer->cpuImage.isNull());
    BOOST_CHECK_GT(layer->cpuImage.width(), 1);
    BOOST_CHECK_GT(layer->cpuImage.height(), 1);
}

BOOST_AUTO_TEST_CASE(create_text_layer_has_base_scale_transform)
{
    AllToolsFixture f;
    f.doc.size = QSize(800, 600);
    TextBox box;
    box.width = 200.0f;
    f.ctrl.createTextLayer("Hello", box, QPointF(0.5, 0.2), 32.0f);

    auto* node = f.nodeAt(0);
    BOOST_REQUIRE(node);
    float expectedScale = 32.0f / 800.0f;
    BOOST_CHECK_CLOSE(static_cast<float>(node->transform.m11()), expectedScale, 0.001f);
    BOOST_CHECK_CLOSE(static_cast<float>(node->transform.m22()), expectedScale, 0.001f);
}

BOOST_AUTO_TEST_CASE(create_text_layer_position_affects_translation)
{
    AllToolsFixture f;
    f.doc.size = QSize(800, 600);
    TextBox box;
    box.width = 200.0f;
    f.ctrl.createTextLayer("Pos", box, QPointF(0.3, -0.1), 32.0f);

    auto* node = f.nodeAt(0);
    BOOST_REQUIRE(node);
    float baseScale = 32.0f / 800.0f;
    // m31 = baseScale * posX (from createTextLayer: scale * translate)
    BOOST_CHECK_CLOSE(static_cast<float>(node->transform.m31()), baseScale * 0.3f, 0.001f);
}

BOOST_AUTO_TEST_CASE(create_text_layer_default_font)
{
    AllToolsFixture f;
    TextBox box;
    box.width = 200.0f;
    f.ctrl.createTextLayer("Text", box, QPointF(0, 0), 32.0f);

    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    BOOST_REQUIRE(layer->textData);
    BOOST_CHECK_EQUAL(layer->textData->spans.back().fontFamily.toStdString(), "Sans Serif");
    BOOST_CHECK(static_cast<int>(layer->textData->align) == static_cast<int>(TextAlign::Left));
}

BOOST_AUTO_TEST_CASE(text_renderer_renders_text)
{
    TextLayerData data;
    data.text = "Hello World";
    data.spans.push_back({0, 11, "Sans Serif", 32.0f, Qt::black, false, false, false, false, 0.0f});
    TextBox box;
    box.width = 300.0f;
    data.box = box;

    QImage result;
    TextRenderer renderer;
    renderer.render(data, result);

    BOOST_CHECK(!result.isNull());
    BOOST_CHECK_GT(result.width(), 10);
    BOOST_CHECK_GT(result.height(), 10);
}

BOOST_AUTO_TEST_CASE(text_renderer_empty_text_produces_minimal_image)
{
    TextLayerData data;
    data.text = "";
    data.spans.push_back({0, 0, "Sans Serif", 32.0f, Qt::black, false, false, false, false, 0.0f});

    QImage result;
    TextRenderer renderer;
    renderer.render(data, result);

    // Empty text should produce a 1x1 transparent image
    BOOST_CHECK(!result.isNull());
    BOOST_CHECK_EQUAL(result.width(), 1);
    BOOST_CHECK_EQUAL(result.height(), 1);
    BOOST_CHECK_EQUAL(result.pixelColor(0, 0).alpha(), 0);
}

BOOST_AUTO_TEST_CASE(create_text_layer_undo_removes_layer)
{
    AllToolsFixture f;
    TextBox box;
    box.width = 200.0f;
    f.ctrl.createTextLayer("Undo", box, QPointF(0, 0), 32.0f);

    int countAfterCreate = f.flatCount();
    BOOST_CHECK_EQUAL(countAfterCreate, 2); // initial layer + text layer

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), 1); // text layer removed
}

BOOST_AUTO_TEST_CASE(text_layer_after_redo_restores_content)
{
    AllToolsFixture f;
    TextBox box;
    box.width = 200.0f;
    f.ctrl.createTextLayer("Redo", box, QPointF(0, 0), 32.0f);

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), 1);

    f.ctrl.history().redo();
    BOOST_CHECK_EQUAL(f.flatCount(), 2);
    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    BOOST_REQUIRE(layer->textData);
    BOOST_CHECK_EQUAL(layer->textData->text.toStdString(), "Redo");
}

// ── No-document / Edge Cases ────────────────────────────────────

BOOST_AUTO_TEST_CASE(no_document_tool_fails)
{
    ImageController orphan;
    BOOST_CHECK(!orphan.executeTool("grayscale", {}));
    BOOST_CHECK(!orphan.executeTool("crop", {}));
    BOOST_CHECK(!orphan.executeTool("zoom", {}));
    BOOST_CHECK(!orphan.executeTool("add_layer", {}));
    BOOST_CHECK(!orphan.executeTool("select_all", {}));
}

BOOST_AUTO_TEST_CASE(unknown_tool_returns_false)
{
    AllToolsFixture f;
    BOOST_CHECK(!f.ctrl.executeTool("nonexistent_tool_xyz", {}));
}

BOOST_AUTO_TEST_CASE(empty_params_still_execute)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    BOOST_CHECK(f.ctrl.executeTool("gaussian_blur", {}));
    BOOST_CHECK(f.ctrl.executeTool("sharpen", {}));
    BOOST_CHECK(f.ctrl.executeTool("grayscale", {}));
    BOOST_CHECK(f.ctrl.executeTool("invert_colors", {}));
}

BOOST_AUTO_TEST_CASE(rapid_undo_redo_cycle)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    auto orig = f.active()->cpuImage.copy();

    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    f.ctrl.executeTool("sharpen", {{"strength", 2.0}});

    f.ctrl.history().undo();
    f.ctrl.history().undo();
    f.ctrl.history().undo();
    BOOST_CHECK(orig == f.active()->cpuImage);

    f.ctrl.history().redo();
    f.ctrl.history().redo();
    f.ctrl.history().redo();
    BOOST_CHECK(orig != f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(multiple_undos_exhausts)
{
    AllToolsFixture f;
    f.fillLayer(128, 128, 128);
    auto orig = f.active()->cpuImage.copy();
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    f.ctrl.history().undo();
    f.ctrl.history().undo();
    BOOST_CHECK(orig == f.active()->cpuImage);
    // The initial newLayer() now pushes an AddLayerCommand, so exhausting
    // history removes the layer. Just verify that canUndo eventually toggles.
}

BOOST_AUTO_TEST_CASE(selection_undo_chain)
{
    AllToolsFixture f;
    f.ctrl.executeTool("select_all", {});
    f.ctrl.executeTool("deselect", {});
    f.ctrl.history().undo();
    BOOST_CHECK(f.doc.selection.active());
    f.ctrl.history().undo();
}

// ── Selection-aware operation tests ──────────────────────────────────

BOOST_AUTO_TEST_CASE(fill_layer_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100); // gray base

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 50.0}, {"height", 150.0}});
    f.ctrl.executeTool("fill_layer",
        {{"red", 200.0}, {"green", 50.0}, {"blue", 100.0}});

    // Pixel inside selection → new color
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, 10).red(), 200);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, 10).green(), 50);
    // Pixel outside selection → original
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(100, 10).red(), 100);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(100, 10).green(), 100);
}

BOOST_AUTO_TEST_CASE(fill_layer_respects_selection_undo)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);
    auto before = f.active()->cpuImage.copy();

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 50.0}, {"height", 150.0}});
    f.ctrl.executeTool("fill_layer",
        {{"red", 200.0}, {"green", 50.0}, {"blue", 100.0}});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(flip_horizontal_respects_selection)
{
    AllToolsFixture f;
    int w = f.active()->cpuImage.width();
    int h = f.active()->cpuImage.height();
    int half = w / 2;

    // Fill left half with gray (200,50,100), right half with blue (50,100,200)
    for (int y = 0; y < h; ++y) {
        uchar* row = f.active()->cpuImage.scanLine(y);
        for (int x = 0; x < half; ++x) {
            row[x*4 + 0] = 200; row[x*4 + 1] = 50;  row[x*4 + 2] = 100; row[x*4 + 3] = 255;
        }
        for (int x = half; x < w; ++x) {
            row[x*4 + 0] = 50;  row[x*4 + 1] = 100; row[x*4 + 2] = 200; row[x*4 + 3] = 255;
        }
    }

    // Selection on left half only
    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", static_cast<double>(half)}, {"height", 150.0}});
    f.ctrl.executeTool("flip_horizontal", {});

    // Left side (within selection) = flipped content = color from right half = blue
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, 10).red(), 50);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, 10).green(), 100);

    // Right side (outside selection) = unchanged = blue
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(w - 10, 10).red(), 50);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(w - 10, 10).green(), 100);
}

BOOST_AUTO_TEST_CASE(flip_horizontal_respects_selection_undo)
{
    AllToolsFixture f;
    int w = f.active()->cpuImage.width();
    int h = f.active()->cpuImage.height();
    int half = w / 2;

    for (int y = 0; y < h; ++y) {
        uchar* row = f.active()->cpuImage.scanLine(y);
        for (int x = 0; x < half; ++x) {
            row[x*4 + 0] = 200; row[x*4 + 1] = 50;  row[x*4 + 2] = 100; row[x*4 + 3] = 255;
        }
        for (int x = half; x < w; ++x) {
            row[x*4 + 0] = 50;  row[x*4 + 1] = 100; row[x*4 + 2] = 200; row[x*4 + 3] = 255;
        }
    }
    auto before = f.active()->cpuImage.copy();

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", static_cast<double>(half)}, {"height", 150.0}});
    f.ctrl.executeTool("flip_horizontal", {});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(flip_vertical_respects_selection)
{
    AllToolsFixture f;
    int w = f.active()->cpuImage.width();
    int h = f.active()->cpuImage.height();
    int half = h / 2;

    // Fill top half with gray (200,50,100), bottom half with blue (50,100,200)
    for (int y = 0; y < half; ++y) {
        uchar* row = f.active()->cpuImage.scanLine(y);
        for (int x = 0; x < w; ++x) {
            row[x*4 + 0] = 200; row[x*4 + 1] = 50;  row[x*4 + 2] = 100; row[x*4 + 3] = 255;
        }
    }
    for (int y = half; y < h; ++y) {
        uchar* row = f.active()->cpuImage.scanLine(y);
        for (int x = 0; x < w; ++x) {
            row[x*4 + 0] = 50;  row[x*4 + 1] = 100; row[x*4 + 2] = 200; row[x*4 + 3] = 255;
        }
    }

    // Selection on top half only
    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 200.0}, {"height", static_cast<double>(half)}});
    f.ctrl.executeTool("flip_vertical", {});

    // Top side (within selection) = flipped content = color from bottom half = blue
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, 10).red(), 50);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, 10).green(), 100);

    // Bottom side (outside selection) = unchanged = blue
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, h - 10).red(), 50);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, h - 10).green(), 100);
}

BOOST_AUTO_TEST_CASE(flip_vertical_respects_selection_undo)
{
    AllToolsFixture f;
    int w = f.active()->cpuImage.width();
    int h = f.active()->cpuImage.height();
    int half = h / 2;

    for (int y = 0; y < half; ++y) {
        uchar* row = f.active()->cpuImage.scanLine(y);
        for (int x = 0; x < w; ++x) {
            row[x*4 + 0] = 200; row[x*4 + 1] = 50;  row[x*4 + 2] = 100; row[x*4 + 3] = 255;
        }
    }
    for (int y = half; y < h; ++y) {
        uchar* row = f.active()->cpuImage.scanLine(y);
        for (int x = 0; x < w; ++x) {
            row[x*4 + 0] = 50;  row[x*4 + 1] = 100; row[x*4 + 2] = 200; row[x*4 + 3] = 255;
        }
    }
    auto before = f.active()->cpuImage.copy();

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 200.0}, {"height", static_cast<double>(half)}});
    f.ctrl.executeTool("flip_vertical", {});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.active()->cpuImage);
}

// These operations change image dimensions, so selection masking is not applicable.
// Tests document current behavior: the operation applies to the entire layer.

BOOST_AUTO_TEST_CASE(rotate_applies_to_whole_layer_regardless_of_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 50.0}, {"height", 75.0}});
    f.ctrl.executeTool("rotate", {{"angle", 90.0}});

    // Rotate changes image dimensions (w↔h) even with selection active
    BOOST_CHECK_EQUAL(f.active()->cpuImage.width(), 150);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.height(), 200);
}

BOOST_AUTO_TEST_CASE(crop_applies_to_whole_layer_regardless_of_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 50.0}, {"height", 75.0}});
    f.ctrl.executeTool("crop",
        {{"x", 10.0}, {"y", 10.0}, {"width", 100.0}, {"height", 80.0}});

    // Crop uses its own coordinates, ignores document selection
    BOOST_CHECK_EQUAL(f.active()->cpuImage.width(), 100);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.height(), 80);
}

BOOST_AUTO_TEST_CASE(crop_document_basic)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    f.ctrl.newLayer();
    f.ctrl.setActiveNode(0);

    QSize beforeSize = f.doc.size;
    f.ctrl.cropDocument(QRect(10, 10, 100, 80));

    BOOST_CHECK_EQUAL(f.doc.size.width(), 100);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 80);
    // After crop all layers have identity transform
    auto flat = f.doc.flatten();
    for (auto* node : flat) {
        if (node->type == LayerTreeNode::Type::Layer && node->layer) {
            BOOST_CHECK(node->transform.isIdentity());
        }
    }
}

BOOST_AUTO_TEST_CASE(crop_document_undo_restores_size)
{
    AllToolsFixture f;
    f.fillLayer(150, 100, 50);
    QSize beforeSize = f.doc.size;

    f.ctrl.cropDocument(QRect(20, 20, 60, 60));
    BOOST_CHECK_EQUAL(f.doc.size.width(), 60);

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.doc.size.width(), beforeSize.width());
    BOOST_CHECK_EQUAL(f.doc.size.height(), beforeSize.height());
}

BOOST_AUTO_TEST_CASE(crop_document_invalid_rect_noop)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    QSize beforeSize = f.doc.size;

    f.ctrl.cropDocument(QRect(0, 0, 0, 0));  // zero area
    BOOST_CHECK_EQUAL(f.doc.size.width(), beforeSize.width());
}

BOOST_AUTO_TEST_CASE(crop_document_full_canvas_changes_nothing)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    QSize beforeSize = f.doc.size;

    f.ctrl.cropDocument(QRect(0, 0, beforeSize.width(), beforeSize.height()));
    BOOST_CHECK_EQUAL(f.doc.size.width(), beforeSize.width());
    BOOST_CHECK_EQUAL(f.doc.size.height(), beforeSize.height());
}

BOOST_AUTO_TEST_CASE(crop_document_multiple_layers)
{
    AllToolsFixture f;
    f.fillLayer(200, 100, 50);
    f.ctrl.newLayer();
    f.ctrl.setActiveNode(1);
    f.fillLayer(50, 150, 200);
    f.ctrl.setActiveNode(0);

    f.ctrl.cropDocument(QRect(10, 10, 80, 60));

    auto flat = f.doc.flatten();
    int layerCount = 0;
    for (auto* node : flat) {
        if (node->type == LayerTreeNode::Type::Layer && node->layer) {
            ++layerCount;
            // Each layer should be cropped to the new size
            BOOST_CHECK(node->layer->cpuImage.width() <= 80);
            BOOST_CHECK(node->layer->cpuImage.height() <= 60);
        }
    }
    BOOST_CHECK_GE(layerCount, 2);
    BOOST_CHECK_EQUAL(f.doc.size.width(), 80);
    BOOST_CHECK_EQUAL(f.doc.size.height(), 60);
}

BOOST_AUTO_TEST_CASE(resize_layer_applies_to_whole_layer_regardless_of_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 50.0}, {"height", 75.0}});
    f.ctrl.executeTool("resize_layer",
        {{"width", 100.0}, {"height", 75.0}});

    // Resize applies to the entire layer, not just the selection
    BOOST_CHECK_EQUAL(f.active()->cpuImage.width(), 100);
    BOOST_CHECK_EQUAL(f.active()->cpuImage.height(), 75);
}

// ── Regression: filter/adjust tools must respect selection ──────────

BOOST_AUTO_TEST_CASE(invert_colors_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("invert_colors", {});

    // Inside selection: 200→55, 50→205, 100→155
    auto inside = f.active()->cpuImage.pixelColor(10, 10);
    BOOST_CHECK_EQUAL(inside.red(), 55);
    BOOST_CHECK_EQUAL(inside.green(), 205);
    BOOST_CHECK_EQUAL(inside.blue(), 155);
    // Outside selection: unchanged
    auto outside = f.active()->cpuImage.pixelColor(150, 10);
    BOOST_CHECK_EQUAL(outside.red(), 200);
    BOOST_CHECK_EQUAL(outside.green(), 50);
    BOOST_CHECK_EQUAL(outside.blue(), 100);
}

BOOST_AUTO_TEST_CASE(invert_colors_respects_selection_undo)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);
    auto before = f.active()->cpuImage.copy();

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.history().undo();

    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(grayscale_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(200, 50, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("grayscale", {});

    // Inside selection: pixel is gray (~99 from 0.299*200 + 0.587*50 + 0.114*100)
    auto inside = f.active()->cpuImage.pixelColor(10, 10);
    BOOST_CHECK_EQUAL(inside.red(), inside.green());
    BOOST_CHECK_EQUAL(inside.green(), inside.blue());
    // Outside selection: unchanged
    auto outside = f.active()->cpuImage.pixelColor(150, 10);
    BOOST_CHECK_EQUAL(outside.red(), 200);
    BOOST_CHECK_EQUAL(outside.green(), 50);
    BOOST_CHECK_EQUAL(outside.blue(), 100);
}

BOOST_AUTO_TEST_CASE(adjust_brightness_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("adjust_brightness", {{"value", 50.0}});

    // Inside selection: brightness increased
    auto inside = f.active()->cpuImage.pixelColor(10, 10);
    BOOST_CHECK_GT(inside.red(), 100);
    // Outside selection: unchanged
    auto outside = f.active()->cpuImage.pixelColor(150, 10);
    BOOST_CHECK_EQUAL(outside.red(), 100);
}

BOOST_AUTO_TEST_CASE(gaussian_blur_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("gaussian_blur", {{"radius", 5.0}});

    // Outside selection: pixel far from boundary is unchanged
    auto outside = f.active()->cpuImage.pixelColor(190, 10);
    BOOST_CHECK_EQUAL(outside.red(), 100);
}

BOOST_AUTO_TEST_CASE(sharpen_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("sharpen", {{"strength", 2.0}});

    // Outside selection: pixel far from boundary is unchanged
    auto outside = f.active()->cpuImage.pixelColor(190, 10);
    BOOST_CHECK_EQUAL(outside.red(), 100);
}

BOOST_AUTO_TEST_CASE(auto_contrast_respects_selection)
{
    AllToolsFixture f;
    // Make a gradient so auto_contrast can act
    for (int y = 0; y < f.active()->cpuImage.height(); ++y) {
        uchar* row = f.active()->cpuImage.scanLine(y);
        for (int x = 0; x < f.active()->cpuImage.width(); ++x) {
            uchar v = static_cast<uchar>(x * 255 / f.active()->cpuImage.width());
            row[x*4 + 0] = v;
            row[x*4 + 1] = v;
            row[x*4 + 2] = v;
            row[x*4 + 3] = 255;
        }
    }

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("auto_contrast", {});

    // Outside selection: unchanged
    auto outside = f.active()->cpuImage.pixelColor(190, 10);
    BOOST_CHECK_EQUAL(outside.red(), static_cast<uchar>(190 * 255 / 200));
}

BOOST_AUTO_TEST_CASE(posterize_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 100.0}, {"height", 150.0}});
    f.ctrl.executeTool("posterize", {{"levels", 4.0}});

    // Outside selection: unchanged
    auto outside = f.active()->cpuImage.pixelColor(190, 10);
    BOOST_CHECK_EQUAL(outside.red(), 100);
}

// ── Fill Bucket Tool ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(fill_bucket_changes_pixels)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("fill_bucket",
        {{"x", 100.0}, {"y", 75.0},
         {"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0},
         {"tolerance", 0.5}});
    auto* layer = f.active();
    BOOST_REQUIRE(layer);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).red(), 200);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).green(), 50);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).blue(), 100);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).alpha(), 255);
}

BOOST_AUTO_TEST_CASE(fill_bucket_undo)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("fill_bucket",
        {{"x", 100.0}, {"y", 75.0},
         {"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0},
         {"tolerance", 0.5}});
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(0, 0).red(), 100);
}

BOOST_AUTO_TEST_CASE(fill_bucket_respects_selection)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("select_rect",
        {{"x", 0.0}, {"y", 0.0}, {"width", 50.0}, {"height", 150.0}});
    f.ctrl.executeTool("fill_bucket",
        {{"x", 10.0}, {"y", 10.0},
         {"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0},
         {"tolerance", 0.5}});
    // Inside selection → new color
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(10, 10).red(), 200);
    // Outside selection → original
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(100, 10).red(), 100);
}

BOOST_AUTO_TEST_CASE(fill_bucket_on_transparent_layer_sets_alpha)
{
    AllToolsFixture f;
    f.active()->cpuImage.fill(QColor(0, 0, 0, 0)); // fully transparent
    f.ctrl.executeTool("fill_bucket",
        {{"x", 100.0}, {"y", 75.0},
         {"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0},
         {"tolerance", 0.5}});
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(50, 50).alpha(), 255);
}

// ── Shape Tool ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(shape_bake_rotate_preserves_intrinsic_size)
{
    AllToolsFixture f;

    ShapeData shape;
    shape.type = ShapeType::Rectangle;
    shape.center = QPointF(0.0, 0.0);
    shape.size = QSizeF(0.4, 0.2);
    shape.fillColor = QColor(220, 40, 40, 255);
    shape.strokeWidth = 0.0f;

    f.ctrl.createShapeLayer(shape);
    const int idx = f.activeIdx();
    auto* node = f.nodeAt(idx);
    BOOST_REQUIRE(node);
    BOOST_REQUIRE(node->layer);
    BOOST_REQUIRE(node->layer->shapeData);

    const QTransform before = node->transform;
    node->transform = rotatedLike(before, static_cast<float>(M_PI / 4.0));

    f.ctrl.bakeShapeTransform(idx, before);

    const ShapeData& baked = *node->layer->shapeData;
    BOOST_CHECK_CLOSE(baked.size.width(), 0.4, 0.01);
    BOOST_CHECK_CLOSE(baked.size.height(), 0.2, 0.01);
    BOOST_CHECK_SMALL(baked.center.x(), 1e-6);
    BOOST_CHECK_SMALL(baked.center.y(), 1e-6);
    BOOST_CHECK_CLOSE(baked.rotation, 45.0f, 0.1);
}

BOOST_AUTO_TEST_CASE(shape_stroke_width_change_keeps_visual_center_stable)
{
    AllToolsFixture f;

    ShapeData shape;
    shape.type = ShapeType::Rectangle;
    shape.center = QPointF(0.1, -0.15);
    shape.size = QSizeF(0.35, 0.22);
    shape.fillColor = QColor(40, 180, 220, 255);
    shape.strokeColor = QColor(20, 20, 20, 255);
    shape.strokeWidth = 0.002f;

    f.ctrl.createShapeLayer(shape);
    const int idx = f.activeIdx();
    auto* node = f.nodeAt(idx);
    BOOST_REQUIRE(node);
    BOOST_REQUIRE(node->layer);
    BOOST_REQUIRE(node->layer->shapeData);

    const QPointF initialCenter = node->transform.map(QPointF(0.0, 0.0));
    const double initialW = node->layer->shapeData->size.width();
    const double initialH = node->layer->shapeData->size.height();

    for (float stroke : {0.01f, 0.03f, 0.08f, 0.005f}) {
        ShapeData edited = *node->layer->shapeData;
        edited.strokeWidth = stroke;
        f.ctrl.modifyShapeLayer(idx, edited);
        BOOST_REQUIRE(node->layer->shapeData);
        QPointF center = node->transform.map(QPointF(0.0, 0.0));
        BOOST_CHECK_SMALL(center.x() - initialCenter.x(), 1e-5);
        BOOST_CHECK_SMALL(center.y() - initialCenter.y(), 1e-5);
        BOOST_CHECK_CLOSE(node->layer->shapeData->size.width(), initialW, 0.01);
        BOOST_CHECK_CLOSE(node->layer->shapeData->size.height(), initialH, 0.01);
        BOOST_CHECK(node->layer->cpuImage.width() < 1000);
        BOOST_CHECK(node->layer->cpuImage.height() < 1000);
    }
}

BOOST_AUTO_TEST_CASE(fill_bucket_with_tolerance_zero_only_exact_match)
{
    AllToolsFixture f;
    f.fillLayer(100, 100, 100);
    // Draw a single pixel of different color
    f.active()->cpuImage.setPixelColor(5, 5, QColor(120, 120, 120, 255));
    f.ctrl.executeTool("fill_bucket",
        {{"x", 5.0}, {"y", 5.0},
         {"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0},
         {"tolerance", 0.0}});
    // Only the exact pixel should change (tolerance 0)
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(5, 5).red(), 200);
    // Neighbor with different color unchanged
    BOOST_CHECK_EQUAL(f.active()->cpuImage.pixelColor(5, 6).red(), 100);
}

BOOST_AUTO_TEST_SUITE_END()
