#define BOOST_TEST_MODULE BoxSelectTest
#include <boost/test/included/unit_test.hpp>

#include "core/BoxSelection.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"

#include <QImage>
#include <QColor>
#include <QTransform>

struct Fixture {
    Document doc;

    Fixture()
    {
        doc.size = QSize(800, 600);
    }

    int addLayer(const QTransform& xf = QTransform())
    {
        auto node = std::make_unique<LayerTreeNode>();
        node->type = LayerTreeNode::Type::Layer;
        node->name = QString("Layer %1").arg(doc.flatCount() + 1);
        node->layer = std::make_shared<Layer>();
        node->layer->name = node->name;
        node->layer->cpuImage = QImage(100, 100, QImage::Format_RGBA8888);
        node->layer->cpuImage.fill(QColor(128, 128, 128));
        node->layer->owner = node.get();
        node->transform = xf;
        node->visible = true;

        int idx = doc.insertNodeAt(doc.flatCount(), std::move(node));
        return idx;
    }

    int addInvisibleLayer(const QTransform& xf = QTransform())
    {
        int idx = addLayer(xf);
        auto* node = doc.nodeAt(idx);
        if (node) node->visible = false;
        return idx;
    }

    int addGroupNode()
    {
        auto group = std::make_unique<LayerTreeNode>();
        group->type = LayerTreeNode::Type::Group;
        group->name = "Group";
        group->visible = true;

        int idx = doc.insertNodeAt(doc.flatCount(), std::move(group));
        return idx;
    }
};

BOOST_AUTO_TEST_SUITE(boxselect)

BOOST_AUTO_TEST_CASE(single_layer_fully_inside_rect)
{
    Fixture f;
    int idx = f.addLayer(QTransform());
    QRectF rect(-2.0, -2.0, 4.0, 4.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(idx) == 1);
}

BOOST_AUTO_TEST_CASE(single_layer_partially_intersecting)
{
    Fixture f;
    int idx = f.addLayer(QTransform());
    QRectF rect(0.0, 0.0, 2.0, 2.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(idx) == 1);
}

BOOST_AUTO_TEST_CASE(single_layer_outside_rect)
{
    Fixture f;
    f.addLayer(QTransform());
    QRectF rect(10.0, 10.0, 2.0, 2.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 0);
}

BOOST_AUTO_TEST_CASE(multiple_layers_partial_intersection)
{
    Fixture f;
    int layer0 = f.addLayer(QTransform::fromTranslate(-1.5, 0.0));
    int layer1 = f.addLayer(QTransform::fromTranslate(0.0, 0.0));
    int layer2 = f.addLayer(QTransform::fromTranslate(1.5, 0.0));

    QRectF rect(-1.0, -1.5, 1.5, 3.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 2);
    BOOST_CHECK(result.count(layer0) == 1);
    BOOST_CHECK(result.count(layer1) == 1);
    BOOST_CHECK(result.count(layer2) == 0);
}

BOOST_AUTO_TEST_CASE(no_layers_returns_empty)
{
    Fixture f;
    QRectF rect(-2.0, -2.0, 4.0, 4.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(no_layers_intersected)
{
    Fixture f;
    f.addLayer(QTransform::fromTranslate(10.0, 10.0));
    f.addLayer(QTransform::fromTranslate(10.0, -10.0));
    QRectF rect(-1.0, -1.0, 2.0, 2.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(invisible_layers_ignored)
{
    Fixture f;
    int visibleL = f.addLayer(QTransform());
    int invisibleL = f.addInvisibleLayer(QTransform());
    QRectF rect(-2.0, -2.0, 4.0, 4.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(visibleL) == 1);
    BOOST_CHECK(result.count(invisibleL) == 0);
}

BOOST_AUTO_TEST_CASE(group_nodes_ignored)
{
    Fixture f;
    int groupIdx = f.addGroupNode();
    int layerIdx = f.addLayer(QTransform());
    QRectF rect(-2.0, -2.0, 4.0, 4.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(layerIdx) == 1);
    BOOST_CHECK(result.count(groupIdx) == 0);
}

BOOST_AUTO_TEST_CASE(null_document_returns_empty)
{
    auto result = BoxSelection::findLayersInRect(nullptr, QRectF(-1, -1, 2, 2));
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(inverted_drag_x)
{
    Fixture f;
    int idx = f.addLayer(QTransform());
    QRectF rect(0.5, -1.0, -1.5, 2.0);
    QRectF normalized = rect.normalized();

    auto result = BoxSelection::findLayersInRect(&f.doc, normalized);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(idx) == 1);
}

BOOST_AUTO_TEST_CASE(inverted_drag_y)
{
    Fixture f;
    int idx = f.addLayer(QTransform());
    QRectF rect(-1.0, 0.5, 2.0, -1.5);
    QRectF normalized = rect.normalized();

    auto result = BoxSelection::findLayersInRect(&f.doc, normalized);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(idx) == 1);
}

BOOST_AUTO_TEST_CASE(inverted_drag_both)
{
    Fixture f;
    int idx = f.addLayer(QTransform());
    QRectF rect(0.5, 0.5, -1.5, -1.5);
    QRectF normalized = rect.normalized();

    auto result = BoxSelection::findLayersInRect(&f.doc, normalized);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(idx) == 1);
}

BOOST_AUTO_TEST_CASE(translated_layer_outside_rect)
{
    Fixture f;
    f.addLayer(QTransform::fromTranslate(5.0, 0.0));
    QRectF rect(-1.0, -1.0, 2.0, 2.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(scaled_layer_intersecting)
{
    Fixture f;
    QTransform xf;
    xf.scale(0.5, 0.5);
    int idx = f.addLayer(xf);
    QRectF rect(-0.6, -0.6, 0.8, 0.8);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(idx) == 1);
}

BOOST_AUTO_TEST_CASE(rotated_layer_intersecting)
{
    Fixture f;
    QTransform xf;
    xf.rotate(45.0);
    int idx = f.addLayer(xf);
    QRectF rect(-0.5, -0.5, 2.0, 2.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(idx) == 1);
}

BOOST_AUTO_TEST_CASE(empty_rect_returns_empty)
{
    Fixture f;
    f.addLayer(QTransform());
    QRectF rect(0.0, 0.0, 0.0, 0.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK(result.empty());
}

BOOST_AUTO_TEST_CASE(position_locked_layer_ignored)
{
    Fixture f;
    int lockedIdx = f.addLayer(QTransform());
    auto* lockedNode = f.doc.nodeAt(lockedIdx);
    if (lockedNode) lockedNode->lockFlags = LockPosition;

    int unlockedIdx = f.addLayer(QTransform::fromTranslate(0.5, 0.0));
    QRectF rect(-2.0, -2.0, 4.0, 4.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(unlockedIdx) == 1);
    BOOST_CHECK(result.count(lockedIdx) == 0);
}

BOOST_AUTO_TEST_CASE(all_locked_layer_ignored)
{
    Fixture f;
    int lockedIdx = f.addLayer(QTransform());
    auto* lockedNode = f.doc.nodeAt(lockedIdx);
    if (lockedNode) lockedNode->lockFlags = LockAll;

    int unlockedIdx = f.addLayer(QTransform::fromTranslate(0.5, 0.0));
    QRectF rect(-2.0, -2.0, 4.0, 4.0);

    auto result = BoxSelection::findLayersInRect(&f.doc, rect);
    BOOST_CHECK_EQUAL(result.size(), 1);
    BOOST_CHECK(result.count(unlockedIdx) == 1);
    BOOST_CHECK(result.count(lockedIdx) == 0);
}

BOOST_AUTO_TEST_SUITE_END()
