#define BOOST_TEST_MODULE DataModelTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"

#include <QImage>
#include <QColor>
#include <QTransform>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <memory>

static std::unique_ptr<LayerTreeNode> makeLayerNode(const QString& name,
    int w = 10, int h = 10, uchar r = 255, uchar g = 0, uchar b = 0)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = name;
    node->layer = std::make_shared<Layer>();
    node->layer->name = name;
    node->layer->cpuImage = QImage(w, h, QImage::Format_RGBA8888);
    node->layer->cpuImage.fill(QColor(r, g, b));
    node->layer->owner = node.get();
    return node;
}

static std::unique_ptr<LayerTreeNode> makeGroupNode(const QString& name)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Group;
    node->name = name;
    return node;
}

struct DataModelFixture {
    Document doc;
    LayerTreeNode* rootLayer;

    DataModelFixture() {
        doc.size = QSize(800, 600);
        doc.selection.create(800, 600);
        auto n = makeLayerNode("RootLayer", 200, 200);
        rootLayer = n.get();
        doc.roots.push_back(std::move(n));
        doc.activeFlatIndex = 0;
    }
};

BOOST_AUTO_TEST_SUITE(document)

BOOST_AUTO_TEST_CASE(flatCount_empty_document)
{
    Document empty;
    BOOST_CHECK_EQUAL(empty.flatCount(), 0);
}

BOOST_AUTO_TEST_CASE(flatCount_single_layer)
{
    DataModelFixture f;
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(flatCount_multiple_layers)
{
    DataModelFixture f;
    f.doc.roots.push_back(makeLayerNode("Layer2"));
    f.doc.roots.push_back(makeLayerNode("Layer3"));
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 3);
}

BOOST_AUTO_TEST_CASE(flatCount_groups_expand_children)
{
    DataModelFixture f;
    auto group = makeGroupNode("Group");
    group->children.push_back(makeLayerNode("Child1"));
    group->children.push_back(makeLayerNode("Child2"));
    f.doc.roots.push_back(std::move(group));

    BOOST_CHECK_EQUAL(f.doc.flatCount(), 4);
}

BOOST_AUTO_TEST_CASE(flatCount_nested_groups)
{
    DataModelFixture f;
    auto outerGroup = makeGroupNode("Outer");
    auto innerGroup = makeGroupNode("Inner");
    innerGroup->children.push_back(makeLayerNode("Deep"));
    outerGroup->children.push_back(std::move(innerGroup));
    f.doc.roots.push_back(std::move(outerGroup));

    BOOST_CHECK_EQUAL(f.doc.flatCount(), 4);
}

BOOST_AUTO_TEST_CASE(flatten_order_depth_first)
{
    auto root = makeLayerNode("A");
    auto group = makeGroupNode("G");
    auto child1 = makeLayerNode("B");
    auto child2 = makeLayerNode("C");
    group->children.push_back(std::move(child1));
    group->children.push_back(std::move(child2));
    Document d;
    d.roots.push_back(std::move(root));
    d.roots.push_back(std::move(group));

    auto flat = d.flatten();
    BOOST_CHECK_EQUAL(flat.size(), 4);
    BOOST_CHECK_EQUAL(flat[0]->name.toStdString(), "A");
    BOOST_CHECK_EQUAL(flat[1]->name.toStdString(), "G");
    BOOST_CHECK_EQUAL(flat[2]->name.toStdString(), "B");
    BOOST_CHECK_EQUAL(flat[3]->name.toStdString(), "C");
}

BOOST_AUTO_TEST_CASE(nodeAt_valid_index)
{
    DataModelFixture f;
    auto* node = f.doc.nodeAt(0);
    BOOST_REQUIRE(node != nullptr);
    BOOST_CHECK_EQUAL(node->name.toStdString(), "RootLayer");
}

BOOST_AUTO_TEST_CASE(nodeAt_out_of_bounds)
{
    DataModelFixture f;
    BOOST_CHECK(f.doc.nodeAt(-1) == nullptr);
    BOOST_CHECK(f.doc.nodeAt(999) == nullptr);
}

BOOST_AUTO_TEST_CASE(nodeAt_empty_document)
{
    Document empty;
    BOOST_CHECK(empty.nodeAt(0) == nullptr);
}

BOOST_AUTO_TEST_CASE(activeNode_returns_correct)
{
    DataModelFixture f;
    BOOST_CHECK_EQUAL(f.doc.activeNode()->name.toStdString(), "RootLayer");
}

BOOST_AUTO_TEST_CASE(activeNode_negative_index_returns_null)
{
    DataModelFixture f;
    f.doc.activeFlatIndex = -1;
    BOOST_CHECK(f.doc.activeNode() == nullptr);
}

BOOST_AUTO_TEST_CASE(activeLayer_returns_layer)
{
    DataModelFixture f;
    auto* layer = f.doc.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(0, 0).red(), uchar(255));
}

BOOST_AUTO_TEST_CASE(activeLayer_group_node_returns_null)
{
    DataModelFixture f;
    f.doc.roots.push_back(makeGroupNode("G"));
    f.doc.activeFlatIndex = 1;
    BOOST_CHECK(f.doc.activeLayer() == nullptr);
}

BOOST_AUTO_TEST_CASE(layerCount_counts_only_layers)
{
    DataModelFixture f;
    BOOST_CHECK_EQUAL(f.doc.layerCount(), 1);

    auto group = makeGroupNode("G");
    group->children.push_back(makeLayerNode("ChildLayer"));
    f.doc.roots.push_back(std::move(group));

    BOOST_CHECK_EQUAL(f.doc.layerCount(), 2);
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 3);
}

BOOST_AUTO_TEST_CASE(layerAtFlat_returns_layer_for_layer_node)
{
    DataModelFixture f;
    auto* layer = f.doc.layerAtFlat(0);
    BOOST_REQUIRE(layer != nullptr);
    BOOST_CHECK_EQUAL(layer->cpuImage.width(), 200);
}

BOOST_AUTO_TEST_CASE(layerAtFlat_returns_null_for_group)
{
    DataModelFixture f;
    f.doc.roots.push_back(makeGroupNode("G"));
    BOOST_CHECK(f.doc.layerAtFlat(1) == nullptr);
}

BOOST_AUTO_TEST_CASE(layerAtFlat_out_of_bounds_returns_null)
{
    DataModelFixture f;
    BOOST_CHECK(f.doc.layerAtFlat(-1) == nullptr);
    BOOST_CHECK(f.doc.layerAtFlat(999) == nullptr);
}

BOOST_AUTO_TEST_CASE(takeNodeAt_removes_and_returns)
{
    DataModelFixture f;
    auto owned = f.doc.takeNodeAt(0);
    BOOST_CHECK(owned != nullptr);
    BOOST_CHECK_EQUAL(owned->name.toStdString(), "RootLayer");
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 0);
}

BOOST_AUTO_TEST_CASE(takeNodeAt_out_of_bounds_returns_null)
{
    DataModelFixture f;
    auto owned = f.doc.takeNodeAt(999);
    BOOST_CHECK(owned == nullptr);
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(insertNodeAt_beginning)
{
    DataModelFixture f;
    auto newNode = makeLayerNode("New", 50, 50, 0, 0, 255);
    int idx = f.doc.insertNodeAt(0, std::move(newNode));

    BOOST_CHECK_EQUAL(idx, 0);
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 2);
    BOOST_CHECK_EQUAL(f.doc.nodeAt(0)->name.toStdString(), "New");
    BOOST_CHECK_EQUAL(f.doc.nodeAt(1)->name.toStdString(), "RootLayer");
}

BOOST_AUTO_TEST_CASE(insertNodeAt_end)
{
    DataModelFixture f;
    auto newNode = makeLayerNode("New", 50, 50, 0, 0, 255);
    int idx = f.doc.insertNodeAt(1, std::move(newNode));
    BOOST_CHECK_EQUAL(idx, 1);
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 2);
}

BOOST_AUTO_TEST_CASE(insertNodeAt_out_of_bounds_appends_to_roots)
{
    DataModelFixture f;
    auto newNode = makeLayerNode("New", 50, 50, 0, 0, 255);
    int idx = f.doc.insertNodeAt(999, std::move(newNode));
    BOOST_CHECK_EQUAL(idx, 1);
    BOOST_CHECK_EQUAL(f.doc.nodeAt(1)->name.toStdString(), "New");
}

BOOST_AUTO_TEST_CASE(insertNodeAt_negative_appends)
{
    DataModelFixture f;
    auto newNode = makeLayerNode("New", 50, 50, 0, 0, 255);
    int idx = f.doc.insertNodeAt(-1, std::move(newNode));
    BOOST_CHECK_EQUAL(idx, 1);
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 2);
}

BOOST_AUTO_TEST_CASE(take_then_insert_roundtrip)
{
    DataModelFixture f;
    auto taken = f.doc.takeNodeAt(0);
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 0);

    int idx = f.doc.insertNodeAt(0, std::move(taken));
    BOOST_CHECK_EQUAL(idx, 0);
    BOOST_CHECK_EQUAL(f.doc.flatCount(), 1);
    BOOST_CHECK_EQUAL(f.doc.nodeAt(0)->name.toStdString(), "RootLayer");
}

BOOST_AUTO_TEST_CASE(default_document_values)
{
    Document d;
    BOOST_CHECK_EQUAL(d.name.toStdString(), "My new document");
    BOOST_CHECK_EQUAL(d.activeFlatIndex, 0);
    BOOST_CHECK_CLOSE(d.zoom, 1.0f, 0.001f);
    BOOST_CHECK_EQUAL(d.panOffset.x(), 0.0);
    BOOST_CHECK_EQUAL(d.panOffset.y(), 0.0);
    BOOST_CHECK(d.roots.empty());
    BOOST_CHECK(!d.selection.active());
    BOOST_CHECK(d.channels.empty());
}

BOOST_AUTO_TEST_CASE(save_and_load_selection_channel)
{
    Document d;
    d.size = QSize(100, 100);
    d.selection.create(100, 100);
    d.selection.setActive(true);
    d.selection.setRect(QRectF(10, 10, 30, 30), SelectMode::Replace);

    d.saveSelectionToChannel("TestChannel");
    BOOST_CHECK_EQUAL(d.channels.size(), 1);
    BOOST_CHECK_EQUAL(d.channels[0].name.toStdString(), "TestChannel");
    BOOST_CHECK(d.channels[0].visible);
    BOOST_CHECK(!d.channels[0].mask.isNull());

    d.selection.clear();
    d.selection.setActive(false);

    d.loadChannelToSelection(0, SelectMode::Replace);
    BOOST_CHECK(d.selection.active());
    BOOST_CHECK(!d.selection.isEmpty());
}

BOOST_AUTO_TEST_CASE(delete_channel)
{
    Document d;
    d.size = QSize(100, 100);
    d.selection.create(100, 100);
    d.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    d.saveSelectionToChannel("Ch1");
    BOOST_CHECK_EQUAL(d.channels.size(), 1);

    d.deleteChannel(0);
    BOOST_CHECK(d.channels.empty());
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(layertreenode)

BOOST_AUTO_TEST_CASE(accumulatedTransform_identity)
{
    auto node = makeLayerNode("N");
    QTransform xf = node->accumulatedTransform();
    BOOST_CHECK(xf.isIdentity());
}

BOOST_AUTO_TEST_CASE(accumulatedTransform_with_translation)
{
    auto node = makeLayerNode("N");
    node->transform.translate(0.5f, 0.3f);
    QTransform xf = node->accumulatedTransform();
    BOOST_CHECK_CLOSE(xf.m31(), 0.5f, 0.001f);
    BOOST_CHECK_CLOSE(xf.m32(), 0.3f, 0.001f);
}

BOOST_AUTO_TEST_CASE(accumulatedTransform_with_scale)
{
    auto node = makeLayerNode("N");
    node->transform.scale(2.0f, 1.5f);
    QTransform xf = node->accumulatedTransform();
    BOOST_CHECK_CLOSE(xf.m11(), 2.0f, 0.001f);
    BOOST_CHECK_CLOSE(xf.m22(), 1.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(accumulatedTransform_with_rotation)
{
    auto node = makeLayerNode("N");
    node->transform.rotate(90);
    QTransform xf = node->accumulatedTransform();
    BOOST_CHECK_CLOSE(double(xf.m11()), 0.0, 0.001);
    BOOST_CHECK_CLOSE(double(xf.m22()), 0.0, 0.001);
}

BOOST_AUTO_TEST_CASE(accumulatedTransform_child_inherits_parent)
{
    auto parent = makeLayerNode("Parent");
    parent->transform.translate(10.0f, 0.0f);

    auto child = makeLayerNode("Child");
    child->transform.translate(5.0f, 0.0f);
    child->parent = parent.get();

    parent->children.push_back(std::move(child));

    QTransform xf = parent->children[0]->accumulatedTransform();
    BOOST_CHECK_CLOSE(xf.m31(), 15.0f, 0.001f);
    BOOST_CHECK_CLOSE(xf.m32(), 0.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(accumulatedTransform_nested_groups)
{
    auto root = makeLayerNode("Root");
    root->transform.scale(2.0f, 2.0f);

    auto group = makeGroupNode("Group");
    group->transform.translate(1.0f, 0.0f);
    group->parent = root.get();

    auto child = makeLayerNode("Deep");
    child->transform.translate(3.0f, 0.0f);
    child->parent = group.get();

    group->children.push_back(std::move(child));
    root->children.push_back(std::move(group));

    QTransform xf = root->children[0]->children[0]->accumulatedTransform();
    BOOST_CHECK_CLOSE(xf.m31(), 8.0f, 0.001f);
    BOOST_CHECK_CLOSE(xf.m11(), 2.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(clone_deep_copy_basic)
{
    auto orig = makeLayerNode("Original", 64, 64, 100, 200, 50);
    orig->opacity = 0.7f;
    orig->visible = false;
    orig->blendMode = BlendMode::Multiply;
    orig->lockFlags = LockTransparent | LockPosition;
    orig->transform.translate(0.3f, -0.6f);

    auto cloned = orig->clone();
    BOOST_REQUIRE(cloned != nullptr);
    BOOST_CHECK_EQUAL(cloned->name.toStdString(), "Original");
    BOOST_CHECK(cloned->type == LayerTreeNode::Type::Layer);
    BOOST_CHECK_CLOSE(cloned->opacity, 0.7f, 0.001f);
    BOOST_CHECK(!cloned->visible);
    BOOST_CHECK(cloned->blendMode == BlendMode::Multiply);
    BOOST_CHECK_EQUAL(cloned->lockFlags, LockTransparent | LockPosition);
    BOOST_CHECK_CLOSE(cloned->transform.m31(), 0.3f, 0.001f);
    BOOST_CHECK_CLOSE(cloned->transform.m32(), -0.6f, 0.001f);

    BOOST_REQUIRE(cloned->layer != nullptr);
    BOOST_CHECK_EQUAL(cloned->layer->cpuImage.width(), 64);
    BOOST_CHECK_EQUAL(cloned->layer->cpuImage.height(), 64);
    BOOST_CHECK_EQUAL(cloned->layer->cpuImage.pixelColor(0, 0).red(), uchar(100));
    BOOST_CHECK_EQUAL(cloned->layer->cpuImage.pixelColor(0, 0).green(), uchar(200));
    BOOST_CHECK_EQUAL(cloned->layer->cpuImage.pixelColor(0, 0).blue(), uchar(50));
}

BOOST_AUTO_TEST_CASE(clone_is_independent)
{
    auto orig = makeLayerNode("Original");
    auto cloned = orig->clone();

    cloned->layer->cpuImage.fill(Qt::blue);
    cloned->opacity = 0.2f;

    BOOST_CHECK_EQUAL(orig->layer->cpuImage.pixelColor(0, 0).red(), uchar(255));
    BOOST_CHECK_CLOSE(orig->opacity, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(clone_group_with_children)
{
    auto group = makeGroupNode("Parent");
    group->children.push_back(makeLayerNode("Child1", 10, 10, 255, 0, 0));
    group->children.push_back(makeLayerNode("Child2", 10, 10, 0, 255, 0));

    auto cloned = group->clone();
    BOOST_CHECK_EQUAL(cloned->name.toStdString(), "Parent");
    BOOST_CHECK(cloned->type == LayerTreeNode::Type::Group);
    BOOST_CHECK_EQUAL(cloned->children.size(), 2);
    BOOST_CHECK_EQUAL(cloned->children[0]->name.toStdString(), "Child1");
    BOOST_CHECK_EQUAL(cloned->children[1]->name.toStdString(), "Child2");

    BOOST_CHECK(cloned->children[0]->parent == cloned.get());
    BOOST_CHECK(cloned->children[1]->parent == cloned.get());
}

BOOST_AUTO_TEST_CASE(clone_deep_group_independent)
{
    auto group = makeGroupNode("Parent");
    group->children.push_back(makeLayerNode("Child", 10, 10, 255, 0, 0));

    auto cloned = group->clone();
    cloned->children[0]->layer->cpuImage.fill(Qt::blue);

    BOOST_CHECK_EQUAL(group->children[0]->layer->cpuImage.pixelColor(0, 0).red(), uchar(255));
    BOOST_CHECK_EQUAL(cloned->children[0]->layer->cpuImage.pixelColor(0, 0).blue(), uchar(255));
}

BOOST_AUTO_TEST_CASE(flatten_single_layer)
{
    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    roots.push_back(makeLayerNode("A"));
    std::vector<LayerTreeNode*> out;
    LayerTreeNode::flatten(roots, out);
    BOOST_CHECK_EQUAL(out.size(), 1);
    BOOST_CHECK_EQUAL(out[0]->name.toStdString(), "A");
}

BOOST_AUTO_TEST_CASE(flatten_nested_groups)
{
    auto group = makeGroupNode("G");
    group->children.push_back(makeLayerNode("Inner"));
    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    roots.push_back(std::move(group));
    roots.push_back(makeLayerNode("B"));

    std::vector<LayerTreeNode*> out;
    LayerTreeNode::flatten(roots, out);
    BOOST_CHECK_EQUAL(out.size(), 3);
    BOOST_CHECK_EQUAL(out[0]->name.toStdString(), "G");
    BOOST_CHECK_EQUAL(out[1]->name.toStdString(), "Inner");
    BOOST_CHECK_EQUAL(out[2]->name.toStdString(), "B");
}

BOOST_AUTO_TEST_CASE(findByFlatIndex_valid)
{
    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    roots.push_back(makeLayerNode("A"));
    BOOST_CHECK(LayerTreeNode::findByFlatIndex(roots, 0) != nullptr);
    BOOST_CHECK(LayerTreeNode::findByFlatIndex(roots, -1) == nullptr);
    BOOST_CHECK(LayerTreeNode::findByFlatIndex(roots, 1) == nullptr);
}

BOOST_AUTO_TEST_CASE(isLeaf_returns_correct)
{
    auto layer = makeLayerNode("L");
    BOOST_CHECK(layer->isLeaf());

    auto group = makeGroupNode("G");
    BOOST_CHECK(!group->isLeaf());
}

BOOST_AUTO_TEST_CASE(layerType_raster_default)
{
    auto node = makeLayerNode("R");
    BOOST_CHECK(node->layerType() == LayerType::Raster);
}

BOOST_AUTO_TEST_CASE(default_node_values)
{
    LayerTreeNode node;
    BOOST_CHECK(node.type == LayerTreeNode::Type::Layer);
    BOOST_CHECK_CLOSE(node.opacity, 1.0f, 0.001f);
    BOOST_CHECK(node.visible);
    BOOST_CHECK(node.blendMode == BlendMode::Normal);
    BOOST_CHECK_EQUAL(node.lockFlags, LockNone);
    BOOST_CHECK(!node.collapsed);
    BOOST_CHECK(!node.clipped);
    BOOST_CHECK(node.layer == nullptr);
    BOOST_CHECK(node.parent == nullptr);
}

BOOST_AUTO_TEST_SUITE_END()

BOOST_AUTO_TEST_SUITE(layer)

BOOST_AUTO_TEST_CASE(default_layer_values)
{
    Layer l;
    BOOST_CHECK_EQUAL(l.textureId, 0);
    BOOST_CHECK_EQUAL(l.fbo, 0);
    BOOST_CHECK(l.cpuImage.isNull());
    BOOST_CHECK(l.maskImage.isNull());
    BOOST_CHECK_EQUAL(l.maskTextureId, 0);
    BOOST_CHECK(l.maskVisible);
    BOOST_CHECK(l.owner == nullptr);
    BOOST_CHECK(l.textureOutdated);
    BOOST_CHECK(!l.isTextLayer());
}

BOOST_AUTO_TEST_CASE(isTextLayer_with_textData)
{
    Layer l;
    l.textData = std::make_shared<TextLayerData>();
    BOOST_CHECK(l.isTextLayer());
}

BOOST_AUTO_TEST_CASE(cpuImage_lifecycle)
{
    Layer l;
    BOOST_CHECK(l.cpuImage.isNull());

    l.cpuImage = QImage(100, 100, QImage::Format_RGBA8888);
    BOOST_CHECK(!l.cpuImage.isNull());
    BOOST_CHECK_EQUAL(l.cpuImage.width(), 100);

    l.cpuImage = QImage();
    BOOST_CHECK(l.cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(maskImage_independent)
{
    Layer l;
    l.maskImage = QImage(10, 10, QImage::Format_Grayscale8);
    l.maskImage.fill(Qt::black);
    l.maskVisible = false;

    BOOST_CHECK(!l.maskImage.isNull());
    BOOST_CHECK(!l.maskVisible);
}

// ── selectionMaskForLayer tests ─────────────────────────────────────

BOOST_AUTO_TEST_CASE(selection_mask_for_layer_dims_match_layer)
{
    DataModelFixture f;
    // Fill entire selection mask with 255
    f.doc.selection.image().fill(255);
    f.doc.selection.setActive(true);

    auto* node = f.rootLayer;
    cv::Mat mask = f.doc.selectionMaskForLayer(
        node->layer->cpuImage.width(),
        node->layer->cpuImage.height(),
        node->accumulatedTransform());

    // Warped mask should match layer size (200x200)
    BOOST_CHECK_EQUAL(mask.rows, 200);
    BOOST_CHECK_EQUAL(mask.cols, 200);
    // With identity transform and full-selection mask, all pixels should be 255
    BOOST_CHECK_EQUAL(mask.at<uchar>(0, 0), 255);
    BOOST_CHECK_EQUAL(mask.at<uchar>(199, 199), 255);
}

BOOST_AUTO_TEST_CASE(selection_mask_for_layer_identity_transform)
{
    DataModelFixture f;
    // Fill selection with white in top-left quarter only
    f.doc.selection.image().fill(0);
    for (int y = 0; y < 300; ++y) {
        uchar* row = f.doc.selection.image().scanLine(y);
        for (int x = 0; x < 400; ++x)
            row[x] = 255;
    }
    f.doc.selection.setActive(true);

    auto* node = f.rootLayer; // 200x200 layer, identity transform
    cv::Mat mask = f.doc.selectionMaskForLayer(
        node->layer->cpuImage.width(),
        node->layer->cpuImage.height(),
        node->accumulatedTransform());

    // Layer pixel (50,50) maps to doc (200, 150) — inside selection → 255
    BOOST_CHECK_EQUAL(mask.at<uchar>(50, 50), 255);
    // Layer pixel (150,150) maps to doc (600, 450) — outside selection → 0
    BOOST_CHECK_EQUAL(mask.at<uchar>(150, 150), 0);
}

BOOST_AUTO_TEST_CASE(selection_mask_for_layer_scaled_transform)
{
    DataModelFixture f;
    // Fill entire selection with 255
    f.doc.selection.image().fill(255);
    f.doc.selection.setActive(true);

    auto* node = f.rootLayer;
    // Scale layer by 2x in NDC
    node->transform.scale(2.0, 2.0);

    int lw = node->layer->cpuImage.width();   // 200
    int lh = node->layer->cpuImage.height();  // 200
    cv::Mat mask = f.doc.selectionMaskForLayer(lw, lh, node->accumulatedTransform());

    // Should match layer dimensions
    BOOST_CHECK_EQUAL(mask.rows, 200);
    BOOST_CHECK_EQUAL(mask.cols, 200);

    // With 2x NDC scale and identity pan: pixel (50,50) maps to doc (0,0) → inside selection → 255
    BOOST_CHECK_EQUAL(mask.at<uchar>(50, 50), 255);
    // Pixel (0,0) maps to doc (-400,-300) → outside → 0
    BOOST_CHECK_EQUAL(mask.at<uchar>(0, 0), 0);
}

BOOST_AUTO_TEST_CASE(selection_mask_for_layer_empty_when_selection_inactive)
{
    DataModelFixture f;
    f.doc.selection.setActive(false);

    auto* node = f.rootLayer;
    cv::Mat mask = f.doc.selectionMaskForLayer(
        node->layer->cpuImage.width(),
        node->layer->cpuImage.height(),
        node->accumulatedTransform());

    // The method doesn't check active state itself — callers check.
    // But with an empty mask (not yet filled with 255), values are 0.
    BOOST_CHECK_EQUAL(mask.rows, 200);
    BOOST_CHECK_EQUAL(mask.cols, 200);
    BOOST_CHECK_EQUAL(mask.at<uchar>(0, 0), 0);
}

BOOST_AUTO_TEST_CASE(selection_mask_for_layer_zero_sized_layer)
{
    DataModelFixture f;
    f.doc.selection.image().fill(255);
    f.doc.selection.setActive(true);

    // Call with zero dimensions
    cv::Mat mask = f.doc.selectionMaskForLayer(0, 0, QTransform());
    BOOST_CHECK(mask.empty());
}

BOOST_AUTO_TEST_CASE(selection_mask_for_layer_preserves_rect_selection)
{
    DataModelFixture f;
    // Create a small rectangular selection in doc space
    f.doc.selection.image().fill(0);
    for (int y = 100; y < 200; ++y) {
        uchar* row = f.doc.selection.image().scanLine(y);
        for (int x = 100; x < 300; ++x)
            row[x] = 255;
    }
    f.doc.selection.setActive(true);

    auto* node = f.rootLayer;
    cv::Mat mask = f.doc.selectionMaskForLayer(
        node->layer->cpuImage.width(),
        node->layer->cpuImage.height(),
        node->accumulatedTransform());

    // Layer pixel (25,50) maps to doc (100,150) → inside selection → 255
    BOOST_CHECK_EQUAL(mask.at<uchar>(50, 25), 255);
    // Layer pixel (0, 0) maps to doc (0,0) → outside → 0
    BOOST_CHECK_EQUAL(mask.at<uchar>(0, 0), 0);
}

BOOST_AUTO_TEST_SUITE_END()
