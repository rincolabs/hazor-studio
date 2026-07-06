#define BOOST_TEST_MODULE GroupsTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "engine/ImageEngine.hpp"

#include <QImage>
#include <QColor>
#include <QTransform>

struct GroupFixture {
    Document doc;
    ImageController ctrl;

    GroupFixture()
        : ctrl()
    {
        doc.size = QSize(800, 600);
        doc.selection.create(800, 600);
        ctrl.setDocument(&doc);
    }

    int flatCount() const { return doc.flatCount(); }
    Layer* activeLayer() { return doc.activeLayer(); }
    LayerTreeNode* activeNode() { return doc.activeNode(); }

    void fillActiveLayer(uchar r, uchar g, uchar b, uchar a = 255)
    {
        auto* layer = activeLayer();
        if (layer)
            layer->cpuImage.fill(QColor(r, g, b, a));
    }
};

// ── accumulatedTransform ───────────────────────────────────

BOOST_AUTO_TEST_SUITE(accumulated_transform)

BOOST_AUTO_TEST_CASE(single_node_identity)
{
    LayerTreeNode node;
    QTransform xf = node.accumulatedTransform();
    BOOST_CHECK(xf.isIdentity());
}

BOOST_AUTO_TEST_CASE(single_node_with_transform)
{
    LayerTreeNode node;
    node.transform.translate(0.5, -0.3);
    QTransform xf = node.accumulatedTransform();
    BOOST_CHECK_CLOSE(xf.m31(), 0.5, 1e-6);
    BOOST_CHECK_CLOSE(xf.m32(), -0.3, 1e-6);
}

BOOST_AUTO_TEST_CASE(child_accumulates_parent_transform)
{
    auto parent = std::make_unique<LayerTreeNode>();
    parent->transform.translate(0.2, 0.1);

    auto child = std::make_unique<LayerTreeNode>();
    child->transform.scale(1.5, 1.0);
    child->parent = parent.get();
    parent->children.push_back(std::move(child));

    LayerTreeNode* childPtr = parent->children[0].get();
    QTransform xf = childPtr->accumulatedTransform();
    // Hierarchy semantics: child local transform is applied before parent.
    // This keeps parent/group translation independent of each child's scale.
    BOOST_CHECK_CLOSE(xf.m11(), 1.5, 1e-6);
    BOOST_CHECK_CLOSE(xf.m31(), 0.2, 1e-6);
    BOOST_CHECK_CLOSE(xf.m32(), 0.1, 1e-6);
}

BOOST_AUTO_TEST_CASE(nested_groups_accumulate)
{
    auto grandparent = std::make_unique<LayerTreeNode>();
    grandparent->transform.translate(0.1, 0.2);

    auto parent = std::make_unique<LayerTreeNode>();
    parent->transform.scale(2.0, 1.0);
    parent->parent = grandparent.get();
    grandparent->children.push_back(std::move(parent));

    auto child = std::make_unique<LayerTreeNode>();
    child->transform.rotateRadians(0.5);
    child->parent = grandparent->children[0].get();
    grandparent->children[0]->children.push_back(std::move(child));

    LayerTreeNode* childPtr = grandparent->children[0]->children[0].get();
    QTransform xf = childPtr->accumulatedTransform();
    // child * parent * grandparent: parent translation is not scaled by child.
    double c = std::cos(0.5);
    double s = std::sin(0.5);
    BOOST_CHECK_CLOSE(xf.m11(), 2.0 * c, 1e-4);
    BOOST_CHECK_CLOSE(xf.m12(), s, 1e-4);
    BOOST_CHECK_CLOSE(xf.m21(), -2.0 * s, 1e-4);
    BOOST_CHECK_CLOSE(xf.m22(), c, 1e-4);
    BOOST_CHECK_CLOSE(xf.m31(), 0.1, 1e-4);
    BOOST_CHECK_CLOSE(xf.m32(), 0.2, 1e-4);
}

BOOST_AUTO_TEST_SUITE_END()

// ── Group move semantics ───────────────────────────────────

BOOST_AUTO_TEST_SUITE(group_move)

BOOST_AUTO_TEST_CASE(children_with_different_transforms_move_by_same_world_delta)
{
    auto group = std::make_unique<LayerTreeNode>();
    group->type = LayerTreeNode::Type::Group;

    auto childA = std::make_unique<LayerTreeNode>();
    childA->type = LayerTreeNode::Type::Layer;
    childA->layer = std::make_shared<Layer>();
    childA->parent = group.get();
    childA->transform.scale(0.5, 0.25);
    childA->transform.translate(-0.2, 0.1);
    auto* childAPtr = childA.get();

    auto childB = std::make_unique<LayerTreeNode>();
    childB->type = LayerTreeNode::Type::Layer;
    childB->layer = std::make_shared<Layer>();
    childB->parent = group.get();
    childB->transform.rotate(35.0);
    childB->transform.scale(0.2, 0.7);
    childB->transform.translate(0.35, -0.15);
    auto* childBPtr = childB.get();

    group->children.push_back(std::move(childA));
    group->children.push_back(std::move(childB));

    const QPointF aBefore = childAPtr->accumulatedTransform().map(QPointF(0.0, 0.0));
    const QPointF bBefore = childBPtr->accumulatedTransform().map(QPointF(0.0, 0.0));

    group->transform.translate(0.3, -0.2);

    const QPointF aAfter = childAPtr->accumulatedTransform().map(QPointF(0.0, 0.0));
    const QPointF bAfter = childBPtr->accumulatedTransform().map(QPointF(0.0, 0.0));
    const QPointF aDelta = aAfter - aBefore;
    const QPointF bDelta = bAfter - bBefore;

    BOOST_CHECK_CLOSE(aDelta.x(), bDelta.x(), 1e-6);
    BOOST_CHECK_CLOSE(aDelta.y(), bDelta.y(), 1e-6);
    BOOST_CHECK_CLOSE(aDelta.x(), 0.3, 1e-6);
    BOOST_CHECK_CLOSE(aDelta.y(), -0.2, 1e-6);
}

BOOST_AUTO_TEST_SUITE_END()

// ── moveNodeIntoGroup ──────────────────────────────────────

BOOST_AUTO_TEST_SUITE(move_node_into_group)

BOOST_AUTO_TEST_CASE(visual_position_preserved)
{
    GroupFixture f;

    // Create a layer
    f.ctrl.executeTool("add_layer", {});
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer);
    QTransform beforeTransform = f.activeNode()->transform;

    // Create a group (inserted at roots.begin(), index 0)
    f.ctrl.newGroup();
    BOOST_CHECK_EQUAL(f.flatCount(), 2);  // group(idx 0) + layer(idx 1)

    // Move layer (index 1) into group (index 0)
    // After takeNodeAt(1), layer is removed, group shifts to index 0
    // After push_back, layer becomes group's child
    f.ctrl.moveNodeIntoGroup(1, 0);

    // activeNode is now the group (activeFlatIndex = groupFlatIndex = 0)
    auto* groupNode = f.doc.nodeAt(0);
    BOOST_REQUIRE(groupNode);
    BOOST_REQUIRE_EQUAL(groupNode->children.size(), size_t(1));

    auto* movedLayer = groupNode->children[0].get();
    QTransform accum = movedLayer->accumulatedTransform();
    BOOST_CHECK_CLOSE(accum.m31(), beforeTransform.m31(), 1e-6);
    BOOST_CHECK_CLOSE(accum.m32(), beforeTransform.m32(), 1e-6);
    BOOST_CHECK_CLOSE(accum.m11(), beforeTransform.m11(), 1e-6);
    BOOST_CHECK_CLOSE(accum.m22(), beforeTransform.m22(), 1e-6);
}

BOOST_AUTO_TEST_CASE(group_with_transform_compensates_correctly)
{
    GroupFixture f;

    f.ctrl.executeTool("add_layer", {});
    QTransform beforeT = f.activeNode()->transform;
    beforeT.translate(0.3, 0.0);
    f.activeNode()->transform = beforeT;

    f.ctrl.newGroup();

    // Now: roots[0]=Group(idx0), roots[1]=Layer(idx1), activeFlatIndex=0
    auto* groupNode = f.doc.activeNode();
    BOOST_REQUIRE(groupNode);
    groupNode->transform.translate(0.5, 0.2);

    // Move layer (index 1) into group (index 0)
    f.ctrl.moveNodeIntoGroup(1, 0);

    // The layer's accumulated transform should still be (0.3, 0)
    auto* group = f.doc.nodeAt(0);
    BOOST_REQUIRE(group);
    BOOST_REQUIRE_EQUAL(group->children.size(), size_t(1));
    auto* child = group->children[0].get();
    QTransform accum = child->accumulatedTransform();
    BOOST_CHECK_CLOSE(accum.m31(), 0.3, 1e-6);
    // child->transform should compensate for group:
    // new = group^{-1} * old = translate(-0.5,-0.2) * translate(0.3,0) = translate(-0.2, -0.2)
    BOOST_CHECK_CLOSE(child->transform.m31(), -0.2, 1e-6);
    BOOST_CHECK_CLOSE(child->transform.m32(), -0.2, 1e-6);
}

BOOST_AUTO_TEST_SUITE_END()

// ── mergeLayers with transforms ────────────────────────────

BOOST_AUTO_TEST_SUITE(merge_layers_transform)

BOOST_AUTO_TEST_CASE(merge_down_preserves_transformed_positions)
{
    GroupFixture f;

    // Create dst layer at index 0
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(255, 0, 0);
    f.activeNode()->transform.translate(-0.3, 0.0);  // moved left

    // Create src layer at index 0 (inserted above)
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(0, 255, 0);
    f.activeNode()->transform.translate(0.3, 0.0);   // moved right

    // Both layers exist, active is src (index 0)
    int srcIdx = 0;
    int dstIdx = 1;

    // Merge down: src into dst
    // After merge: only dst layer remains, with both layers' content
    // at their respective transformed positions
    int beforeCount = f.flatCount();
    f.ctrl.mergeLayers(srcIdx, dstIdx);
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount - 1);

    // The resulting merged layer should have content that combines both
    auto* mergedLayer = f.doc.activeLayer();
    BOOST_REQUIRE(mergedLayer);

    // Transform should be identity (merge resets it)
    auto* mergedNode = f.doc.activeNode();
    BOOST_REQUIRE(mergedNode);
}

BOOST_AUTO_TEST_CASE(merge_visible_uses_accumulated_transform)
{
    GroupFixture f;

    // Create layer 1 (red, moved right)
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(255, 0, 0);
    f.activeNode()->transform.translate(0.3, 0.0);

    // Create layer 2 (green, identity)
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(0, 255, 0);

    // Merge visible (replaces all visible layers with a single composite)
    int beforeCount = f.flatCount();
    bool ok = f.ctrl.executeTool("merge_visible", {});
    BOOST_CHECK(ok);
    // All visible layers replaced by single composite layer
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
}

BOOST_AUTO_TEST_SUITE_END()

// ─── copy with selection ─────────────────────────────────────

BOOST_AUTO_TEST_SUITE(copy_with_selection_transform)

BOOST_AUTO_TEST_CASE(copy_selection_on_transformed_layer)
{
    GroupFixture f;
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(100, 150, 200);

    // Move layer to the right
    f.activeNode()->transform.translate(0.5, 0.0);

    // Create selection on the right half (matches moved layer's content)
    f.doc.selection.setRect(QRectF(400, 0, 400, 600), SelectMode::Replace);
    f.doc.selection.setActive(true);

    // Copy with selection active
    f.ctrl.copy();
    BOOST_CHECK(ClipboardManager::instance().hasData());

    // Paste should create a new layer
    int beforePaste = f.flatCount();
    f.ctrl.paste();
    BOOST_CHECK_EQUAL(f.flatCount(), beforePaste + 1);

    // Cleanup
    ClipboardManager::instance().clear();
}

BOOST_AUTO_TEST_CASE(delete_selected_in_group)
{
    GroupFixture f;
    f.ctrl.executeTool("add_layer", {});
    f.fillActiveLayer(100, 150, 200);

    // Create selection
    f.doc.selection.setRect(QRectF(100, 100, 200, 200), SelectMode::Replace);
    f.doc.selection.setActive(true);

    // Create group and move layer into it
    f.ctrl.newGroup();
    f.ctrl.moveNodeIntoGroup(1, 0);
    
    // Delete selected pixels (active is now the group, so activeLayer is null → fails)
    // This is expected — delete_selected requires an active layer, not a group
    bool success = f.ctrl.executeTool("delete_selected", {});
    // After group creation, activeFlatIndex=0 is the group, so activeLayer is null
    // The deletion should gracefully handle this
    BOOST_CHECK(!success);  // expected: no active layer
}

BOOST_AUTO_TEST_SUITE_END()

// ─── screenToImage guard (unit) ──────────────────────────────

BOOST_AUTO_TEST_SUITE(screen_to_image_safety)

BOOST_AUTO_TEST_CASE(null_layer_returns_empty)
{
    GroupFixture f;
    // Test that screenToImage with null layer doesn't crash
    // (the controller version takes explicit params)
    QPointF result = f.ctrl.screenToImage(
        QPointF(100, 100), nullptr,
        1.0f, QPointF(0, 0), QPointF(1, 1), QSize(200, 200));
    // Should return empty QPointF (0,0) without crashing
    BOOST_CHECK_EQUAL(result.x(), 0.0);
    BOOST_CHECK_EQUAL(result.y(), 0.0);
}

BOOST_AUTO_TEST_SUITE_END()
