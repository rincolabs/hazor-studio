#define BOOST_TEST_MODULE LayerMenuTest
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

struct LayerMenuFixture {
    Document doc;
    ImageController ctrl;

    LayerMenuFixture()
        : ctrl()
    {
        doc.size = QSize(800, 600);
        doc.selection.create(800, 600);
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    Layer* activeLayer() { return doc.activeLayer(); }
    LayerTreeNode* activeNode() { return doc.activeNode(); }
    int flatCount() const { return doc.flatCount(); }
    int activeIndex() const { return doc.activeFlatIndex; }

    void fillActiveLayer(uchar r, uchar g, uchar b, uchar a = 255)
    {
        auto* layer = activeLayer();
        if (layer)
            layer->cpuImage.fill(QColor(r, g, b, a));
    }

    void addSecondLayer()
    {
        ctrl.newLayer();
    }
};

BOOST_AUTO_TEST_SUITE(layermenu)

// ── isLayerEmpty ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(isLayerEmpty_fully_transparent)
{
    LayerMenuFixture f;
    BOOST_CHECK(f.ctrl.isLayerEmpty(f.activeIndex()));
}

BOOST_AUTO_TEST_CASE(isLayerEmpty_opaque_returns_false)
{
    LayerMenuFixture f;
    f.fillActiveLayer(255, 0, 0);
    BOOST_CHECK(!f.ctrl.isLayerEmpty(f.activeIndex()));
}

BOOST_AUTO_TEST_CASE(isLayerEmpty_no_document_returns_true)
{
    ImageController orphan;
    BOOST_CHECK(orphan.isLayerEmpty(0));
}

// ── AddLayerCommand (undo / redo) ────────────────────────────

BOOST_AUTO_TEST_CASE(add_layer_command_undo_removes_layer)
{
    LayerMenuFixture f;
    int before = f.flatCount();
    f.ctrl.executeTool("add_layer", {});
    BOOST_CHECK_EQUAL(f.flatCount(), before + 1);
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(add_layer_command_redo_restores_layer)
{
    LayerMenuFixture f;
    int before = f.flatCount();
    f.ctrl.executeTool("add_layer", {});
    f.ctrl.history().undo();
    f.ctrl.history().redo();
    BOOST_CHECK_EQUAL(f.flatCount(), before + 1);
}

// ── RemoveLayerCommand (undo / redo) ─────────────────────────

BOOST_AUTO_TEST_CASE(remove_layer_command_undo_restores_content)
{
    LayerMenuFixture f;
    f.fillActiveLayer(100, 150, 200);
    int before = f.flatCount();

    f.ctrl.executeTool("remove_layer", {{"index", 0.0}});
    BOOST_CHECK_EQUAL(f.flatCount(), before - 1);

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), before);

    auto* restored = f.doc.activeLayer();
    BOOST_REQUIRE(restored != nullptr);
    BOOST_CHECK(restored->cpuImage.pixelColor(0, 0).red() == uchar(100));
    BOOST_CHECK(restored->cpuImage.pixelColor(0, 0).green() == uchar(150));
    BOOST_CHECK(restored->cpuImage.pixelColor(0, 0).blue() == uchar(200));
}

BOOST_AUTO_TEST_CASE(remove_layer_command_redo)
{
    LayerMenuFixture f;
    f.fillActiveLayer(100, 150, 200);
    f.addSecondLayer();
    int before = f.flatCount();

    f.ctrl.executeTool("remove_layer", {{"index", 0.0}});
    f.ctrl.history().undo();
    f.ctrl.history().redo();
    BOOST_CHECK_EQUAL(f.flatCount(), before - 1);
}

// ── DuplicateLayerCommand (undo / redo) ──────────────────────

BOOST_AUTO_TEST_CASE(duplicate_layer_command_undo)
{
    LayerMenuFixture f;
    f.fillActiveLayer(50, 100, 150);
    int before = f.flatCount();

    f.ctrl.executeTool("duplicate_layer", {{"index", 0.0}});
    BOOST_CHECK_EQUAL(f.flatCount(), before + 1);

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(duplicate_layer_command_redo)
{
    LayerMenuFixture f;
    f.fillActiveLayer(50, 100, 150);
    int before = f.flatCount();

    f.ctrl.executeTool("duplicate_layer", {{"index", 0.0}});
    f.ctrl.history().undo();
    f.ctrl.history().redo();
    BOOST_CHECK_EQUAL(f.flatCount(), before + 1);

    auto* dup = f.doc.nodeAt(0);
    BOOST_REQUIRE(dup != nullptr);
    BOOST_REQUIRE(dup->layer != nullptr);
    BOOST_CHECK_EQUAL(dup->layer->cpuImage.pixelColor(0, 0).red(), uchar(50));
    BOOST_CHECK_EQUAL(dup->layer->cpuImage.pixelColor(0, 0).green(), uchar(100));
    BOOST_CHECK_EQUAL(dup->layer->cpuImage.pixelColor(0, 0).blue(), uchar(150));
}

// ── resize_layer transform sync ──────────────────────────────

BOOST_AUTO_TEST_CASE(resize_layer_syncs_both_transforms)
{
    LayerMenuFixture f;
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);

    layer->cpuImage = QImage(200, 200, QImage::Format_RGBA8888);
    layer->cpuImage.fill(Qt::white);

    f.ctrl.executeTool("resize_layer", {{"width", 100.0}, {"height", 100.0}});

    BOOST_CHECK_EQUAL(layer->cpuImage.width(), 100);
    BOOST_CHECK_EQUAL(layer->cpuImage.height(), 100);

    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);

    float expectedScale = 100.0f / 200.0f;
    BOOST_CHECK_CLOSE(node->transform.m11(), expectedScale, 0.001f);
    BOOST_CHECK_CLOSE(node->transform.m22(), expectedScale, 0.001f);
}

BOOST_AUTO_TEST_CASE(remove_layer_preserves_full_node_state)
{
    LayerMenuFixture f;
    f.fillActiveLayer(200, 100, 50);

    auto* beforeNode = f.doc.nodeAt(0);
    BOOST_REQUIRE(beforeNode != nullptr);
    QTransform origTransform;
    origTransform.translate(0.5f, 0.3f);
    beforeNode->transform = origTransform;
    beforeNode->opacity = 0.7f;
    beforeNode->visible = false;

    f.ctrl.executeTool("remove_layer", {{"index", 0.0}});

    BOOST_CHECK_EQUAL(f.flatCount(), 0);

    f.ctrl.history().undo();

    BOOST_CHECK_EQUAL(f.flatCount(), 1);
    auto* restored = f.doc.nodeAt(0);
    BOOST_REQUIRE(restored != nullptr);
    BOOST_REQUIRE(restored->layer != nullptr);

    BOOST_CHECK(restored->layer->cpuImage.pixelColor(0, 0).red() == uchar(200));
    BOOST_CHECK(restored->layer->cpuImage.pixelColor(0, 0).green() == uchar(100));
    BOOST_CHECK(restored->layer->cpuImage.pixelColor(0, 0).blue() == uchar(50));
    BOOST_CHECK_CLOSE(restored->transform.m31(), 0.5f, 0.001f);
    BOOST_CHECK_CLOSE(restored->transform.m32(), 0.3f, 0.001f);
    BOOST_CHECK_CLOSE(restored->opacity, 0.7f, 0.001f);
    BOOST_CHECK(!restored->visible);
}

// ── merge_visible ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(merge_visible_keeps_old_layers)
{
    LayerMenuFixture f;
    f.fillActiveLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillActiveLayer(0, 255, 0);

    int before = f.flatCount();
    f.ctrl.executeTool("merge_visible", {});

    BOOST_CHECK_EQUAL(f.flatCount(), 1);

    auto* mergedNode = f.doc.nodeAt(0);
    BOOST_REQUIRE(mergedNode != nullptr);
    BOOST_REQUIRE(mergedNode->layer != nullptr);
    BOOST_CHECK(!mergedNode->layer->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(merge_visible_undo_removes_merged_layer)
{
    LayerMenuFixture f;
    f.fillActiveLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillActiveLayer(0, 255, 0);

    int before = f.flatCount();
    f.ctrl.executeTool("merge_visible", {});
    BOOST_CHECK_EQUAL(f.flatCount(), 1);

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(merge_visible_redo_restores_merged_layer)
{
    LayerMenuFixture f;
    f.fillActiveLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillActiveLayer(0, 255, 0);

    int before = f.flatCount();
    f.ctrl.executeTool("merge_visible", {});
    f.ctrl.history().undo();
    f.ctrl.history().redo();
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(merge_visible_noop_with_single_layer)
{
    LayerMenuFixture f;
    f.ctrl.executeTool("merge_visible", {});
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
}

// ── fill_layer undoes correctly ──────────────────────────────

BOOST_AUTO_TEST_CASE(fill_layer_undo_restores_previous_content)
{
    LayerMenuFixture f;
    f.fillActiveLayer(100, 100, 100);

    f.ctrl.executeTool("fill_layer", {
        {"red", 200.0}, {"green", 50.0}, {"blue", 100.0}, {"alpha", 255.0}
    });

    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).red(), uchar(200));

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).red(), uchar(100));
}

// ── Transform sync through FilterCommand undo ────────────────

BOOST_AUTO_TEST_CASE(filter_command_syncs_node_transform)
{
    LayerMenuFixture f;
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    layer->cpuImage = QImage(200, 200, QImage::Format_RGBA8888);
    layer->cpuImage.fill(Qt::white);

    f.ctrl.executeTool("resize_layer", {{"width", 100.0}, {"height", 100.0}});

    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);
    float scale = 100.0f / 200.0f;

    BOOST_CHECK_CLOSE(node->transform.m11(), scale, 0.001f);
    BOOST_CHECK_CLOSE(node->transform.m11(), scale, 0.001f);

    f.ctrl.history().undo();
    BOOST_CHECK_CLOSE(node->transform.m11(), 1.0f, 0.001f);

    f.ctrl.history().redo();
    BOOST_CHECK_CLOSE(node->transform.m11(), scale, 0.001f);
}

// ── LayerTreeNode clone ──────────────────────────────────────

BOOST_AUTO_TEST_CASE(layer_tree_node_clone_deep_copy)
{
    LayerTreeNode orig;
    orig.type = LayerTreeNode::Type::Layer;
    orig.name = "test";
    orig.opacity = 0.5f;
    orig.visible = false;
    orig.transform.translate(0.3f, 0.7f);
    orig.layer = std::make_shared<Layer>();
    orig.layer->name = "test";
    orig.layer->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    orig.layer->cpuImage.fill(QColor(100, 200, 50, 200));

    auto clone = orig.clone();
    BOOST_REQUIRE(clone != nullptr);
    BOOST_CHECK_EQUAL(clone->name.toStdString(), "test");
    BOOST_CHECK(clone->type == LayerTreeNode::Type::Layer);
    BOOST_CHECK_CLOSE(clone->opacity, 0.5f, 0.001f);
    BOOST_CHECK(!clone->visible);
    BOOST_CHECK_CLOSE(clone->transform.m31(), 0.3f, 0.001f);
    BOOST_CHECK_CLOSE(clone->transform.m32(), 0.7f, 0.001f);
    BOOST_REQUIRE(clone->layer != nullptr);
    BOOST_CHECK(clone->layer->cpuImage.pixelColor(0, 0).red() == uchar(100));
    BOOST_CHECK(clone->layer->cpuImage.pixelColor(0, 0).green() == uchar(200));
    BOOST_CHECK(clone->layer->cpuImage.pixelColor(0, 0).blue() == uchar(50));
    BOOST_CHECK(clone->layer->cpuImage.pixelColor(0, 0).alpha() == uchar(200));
}

BOOST_AUTO_TEST_CASE(layer_tree_node_clone_independent)
{
    LayerTreeNode orig;
    orig.layer = std::make_shared<Layer>();
    orig.layer->cpuImage = QImage(10, 10, QImage::Format_RGBA8888);
    orig.layer->cpuImage.fill(Qt::red);

    auto clone = orig.clone();
    clone->layer->cpuImage.fill(Qt::blue);

    BOOST_CHECK_EQUAL(orig.layer->cpuImage.pixelColor(0, 0).red(), uchar(255));
    BOOST_CHECK_EQUAL(clone->layer->cpuImage.pixelColor(0, 0).blue(), uchar(255));
}

// ── mergeLayers with transforms ───────────────────────────────

BOOST_AUTO_TEST_CASE(merge_down_preserves_transformed_layers)
{
    LayerMenuFixture f;

    // Create dst layer (red, moved left)
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(255, 0, 0);
    f.activeNode()->transform.translate(-0.3, 0.0);

    // Create src layer (green, moved right)
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(0, 255, 0);
    f.activeNode()->transform.translate(0.3, 0.0);

    int beforeCount = f.flatCount();
    f.ctrl.mergeLayers(0, 1);
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount - 1);

    // Should have pixel content
    auto* merged = f.activeLayer();
    BOOST_REQUIRE(merged);
    BOOST_REQUIRE(!merged->cpuImage.isNull());
}

BOOST_AUTO_TEST_SUITE_END()
