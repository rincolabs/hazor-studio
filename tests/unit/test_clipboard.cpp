#define BOOST_TEST_MODULE ClipboardTest
#include <boost/test/included/unit_test.hpp>

#include "core/Clipboard.hpp"
#include "core/LayerTreeNode.hpp"
#include <QImage>
#include <QColor>

static std::unique_ptr<LayerTreeNode> makeTestLayer(const QString& name, int w, int h, uchar r, uchar g, uchar b)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = name;
    node->layer = std::make_shared<Layer>();
    node->layer->name = name;
    node->layer->cpuImage = QImage(w, h, QImage::Format_RGBA8888);
    node->layer->cpuImage.fill(QColor(r, g, b, 255));
    return node;
}

static std::unique_ptr<LayerTreeNode> makeTestGroup(const QString& name)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Group;
    node->name = name;
    return node;
}

BOOST_AUTO_TEST_SUITE(clipboard)

BOOST_AUTO_TEST_CASE(initially_empty)
{
    ClipboardManager::instance().clear();
    BOOST_CHECK(!ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::None);
}

BOOST_AUTO_TEST_CASE(set_get_pixels)
{
    ClipboardManager::instance().clear();

    QImage img(50, 40, QImage::Format_RGBA8888);
    img.fill(Qt::red);

    ClipboardData data;
    data.type = ClipboardType::Pixels;
    data.pixels = img.copy();
    data.docPosition = QPointF(10.0f, 20.0f);
    data.sourceDocSize = QSize(1920, 1080);
    data.name = "Pasted Layer";

    ClipboardManager::instance().setData(std::move(data));

    BOOST_CHECK(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Pixels);
    BOOST_CHECK(!ClipboardManager::instance().data().pixels.isNull());
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().pixels.width(), 50);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().pixels.height(), 40);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().docPosition.x(), 10.0f);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().docPosition.y(), 20.0f);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().name.toStdString(), "Pasted Layer");
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().sourceDocSize.width(), 1920);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().sourceDocSize.height(), 1080);
}

BOOST_AUTO_TEST_CASE(set_get_layer)
{
    ClipboardManager::instance().clear();

    auto layer = makeTestLayer("Layer 1", 100, 100, 255, 0, 0);
    layer->transform.translate(0.5f, 0.3f);
    layer->opacity = 0.8f;

    auto* origPtr = layer.get();

    ClipboardData data;
    data.type = ClipboardType::Layer;
    data.node = std::move(layer);
    data.sourceDocSize = QSize(1920, 1080);
    data.name = "Layer 1 (copy)";

    ClipboardManager::instance().setData(std::move(data));

    BOOST_CHECK(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Layer);
    BOOST_REQUIRE(ClipboardManager::instance().data().node != nullptr);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().node->name.toStdString(), "Layer 1");

    auto* clipLayer = ClipboardManager::instance().data().node.get();
    BOOST_CHECK_EQUAL(clipLayer->name.toStdString(), "Layer 1");
    BOOST_REQUIRE(clipLayer->layer != nullptr);
    BOOST_CHECK(!clipLayer->layer->cpuImage.isNull());
    BOOST_CHECK_EQUAL(clipLayer->layer->cpuImage.width(), 100);
    BOOST_CHECK_EQUAL(clipLayer->layer->cpuImage.height(), 100);
    BOOST_CHECK_CLOSE(clipLayer->opacity, 0.8f, 0.001f);
    BOOST_CHECK(!clipLayer->layer->cpuImage.isNull());

    BOOST_CHECK(layer == nullptr);
}

BOOST_AUTO_TEST_CASE(set_get_group)
{
    ClipboardManager::instance().clear();

    auto group = makeTestGroup("Group 1");
    auto child1 = makeTestLayer("Child 1", 50, 50, 0, 255, 0);
    auto child2 = makeTestLayer("Child 2", 60, 60, 0, 0, 255);
    child1->parent = group.get();
    child2->parent = group.get();
    group->children.push_back(std::move(child1));
    group->children.push_back(std::move(child2));

    ClipboardData data;
    data.type = ClipboardType::Group;
    data.node = std::move(group);
    data.sourceDocSize = QSize(1920, 1080);
    data.name = "Group 1 (copy)";

    ClipboardManager::instance().setData(std::move(data));

    BOOST_CHECK(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Group);
    BOOST_REQUIRE(ClipboardManager::instance().data().node != nullptr);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().node->children.size(), 2);
    BOOST_CHECK(ClipboardManager::instance().data().node->children[0]->layer != nullptr);
    BOOST_CHECK(ClipboardManager::instance().data().node->children[1]->layer != nullptr);
}

BOOST_AUTO_TEST_CASE(clear_resets)
{
    ClipboardManager::instance().clear();

    QImage img(10, 10, QImage::Format_RGBA8888);
    img.fill(Qt::blue);

    ClipboardData data;
    data.type = ClipboardType::Pixels;
    data.pixels = img.copy();
    ClipboardManager::instance().setData(std::move(data));
    BOOST_CHECK(ClipboardManager::instance().hasData());

    ClipboardManager::instance().clear();
    BOOST_CHECK(!ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::None);
}

BOOST_AUTO_TEST_CASE(pixels_deep_copy_independent)
{
    ClipboardManager::instance().clear();

    QImage original(20, 20, QImage::Format_RGBA8888);
    original.fill(QColor(100, 150, 200, 255));

    ClipboardData data;
    data.type = ClipboardType::Pixels;
    data.pixels = original.copy();
    data.docPosition = QPointF(5.0f, 5.0f);
    data.sourceDocSize = QSize(100, 100);
    ClipboardManager::instance().setData(std::move(data));

    QImage clipImg = ClipboardManager::instance().data().pixels;
    BOOST_CHECK_EQUAL(clipImg.pixelColor(0, 0).red(), 100);
    BOOST_CHECK_EQUAL(clipImg.pixelColor(0, 0).green(), 150);
    BOOST_CHECK_EQUAL(clipImg.pixelColor(0, 0).blue(), 200);

    clipImg.fill(Qt::black);
    BOOST_CHECK_EQUAL(original.pixelColor(0, 0).red(), 100);
}

BOOST_AUTO_TEST_CASE(singleton_same_instance)
{
    ClipboardManager& inst1 = ClipboardManager::instance();
    ClipboardManager& inst2 = ClipboardManager::instance();
    BOOST_CHECK(&inst1 == &inst2);
}

BOOST_AUTO_TEST_CASE(multiple_set_overwrites)
{
    ClipboardManager::instance().clear();

    ClipboardData d1;
    d1.type = ClipboardType::Pixels;
    d1.pixels = QImage(10, 10, QImage::Format_RGBA8888);
    d1.name = "first";
    ClipboardManager::instance().setData(std::move(d1));

    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().name.toStdString(), "first");

    ClipboardData d2;
    d2.type = ClipboardType::Layer;
    d2.node = makeTestLayer("second", 5, 5, 255, 255, 255);
    d2.name = "second";
    ClipboardManager::instance().setData(std::move(d2));

    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Layer);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().name.toStdString(), "second");
    BOOST_REQUIRE(ClipboardManager::instance().data().node != nullptr);
    BOOST_CHECK_EQUAL(ClipboardManager::instance().data().node->name.toStdString(), "second");
}

BOOST_AUTO_TEST_SUITE_END()
