#define BOOST_TEST_MODULE ColorSamplerServiceTest
#include <boost/test/included/unit_test.hpp>

#include "engine/ColorSamplerService.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"

#include <QImage>
#include <QColor>

static QImage makeTestImage(int w, int h, uchar r, uchar g, uchar b, uchar a = 255)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(QColor(r, g, b, a));
    return img;
}

static void setPixel(QImage& img, int x, int y, uchar r, uchar g, uchar b, uchar a = 255)
{
    uchar* px = img.scanLine(y) + x * 4;
    px[0] = r; px[1] = g; px[2] = b; px[3] = a;
}

static QImage makeGradientImage(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            setPixel(img, x, y, x % 256, y % 256, (x + y) % 256);
        }
    }
    return img;
}

BOOST_AUTO_TEST_SUITE(color_sampler_service)

// ── sampleLayer: Point ────────────────────────────────────

BOOST_AUTO_TEST_CASE(sample_layer_point_rgba)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeTestImage(100, 100, 200, 100, 50, 255);

    QColor c = ColorSamplerService::sampleLayer(layer.get(), QPointF(50, 50), SampleSize::Point);
    BOOST_REQUIRE(c.isValid());
    BOOST_CHECK(c.red()   == 200);
    BOOST_CHECK(c.green() == 100);
    BOOST_CHECK(c.blue()  == 50);
    BOOST_CHECK(c.alpha() == 255);
}

BOOST_AUTO_TEST_CASE(sample_layer_point_transparent)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeTestImage(50, 50, 100, 150, 200, 80);

    QColor c = ColorSamplerService::sampleLayer(layer.get(), QPointF(25, 25), SampleSize::Point);
    BOOST_REQUIRE(c.isValid());
    BOOST_CHECK(c.alpha() == 80);
    BOOST_CHECK(c.red()   == 100);
}

BOOST_AUTO_TEST_CASE(sample_layer_point_out_of_bounds)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeTestImage(10, 10, 255, 0, 0);

    QColor c = ColorSamplerService::sampleLayer(layer.get(), QPointF(50, 50), SampleSize::Point);
    BOOST_CHECK(!c.isValid());
}

BOOST_AUTO_TEST_CASE(sample_layer_null_layer)
{
    QColor c = ColorSamplerService::sampleLayer(nullptr, QPointF(0, 0), SampleSize::Point);
    BOOST_CHECK(!c.isValid());
}

BOOST_AUTO_TEST_CASE(sample_layer_empty_image)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = QImage();

    QColor c = ColorSamplerService::sampleLayer(layer.get(), QPointF(0, 0), SampleSize::Point);
    BOOST_CHECK(!c.isValid());
}

// ── sampleLayer: Average sizes ────────────────────────────

BOOST_AUTO_TEST_CASE(sample_layer_point_vs_average)
{
    // On a sharp edge, point and 3x3 average should differ
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = QImage(20, 20, QImage::Format_RGBA8888);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (x < 10)
                setPixel(layer->cpuImage, x, y, 255, 0, 0);
            else
                setPixel(layer->cpuImage, x, y, 0, 0, 255);
        }
    }

    QColor point = ColorSamplerService::sampleLayer(layer.get(), QPointF(10, 10), SampleSize::Point);
    QColor avg3  = ColorSamplerService::sampleLayer(layer.get(), QPointF(10, 10), SampleSize::Size3x3);
    BOOST_REQUIRE(point.isValid());
    BOOST_REQUIRE(avg3.isValid());
    // Point = blue (0,0,255) at x=10, 3x3 includes red pixels → different
    bool same = (point.red() == avg3.red() && point.green() == avg3.green()
                 && point.blue() == avg3.blue());
    BOOST_CHECK(!same);
}

BOOST_AUTO_TEST_CASE(sample_layer_3x3_average_smooths)
{
    // Create a sharp transition image: left half red, right half blue
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = QImage(20, 20, QImage::Format_RGBA8888);
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 20; ++x) {
            if (x < 10)
                setPixel(layer->cpuImage, x, y, 255, 0, 0);
            else
                setPixel(layer->cpuImage, x, y, 0, 0, 255);
        }
    }

    // Point at x=10 should be blue (x>=10)
    QColor point = ColorSamplerService::sampleLayer(layer.get(), QPointF(10, 10), SampleSize::Point);
    QColor avg3   = ColorSamplerService::sampleLayer(layer.get(), QPointF(10, 10), SampleSize::Size3x3);
    BOOST_REQUIRE(point.isValid());
    BOOST_REQUIRE(avg3.isValid());
    // Point = pure blue (0,0,255), 3x3 includes red pixels → red > 0
    BOOST_CHECK_MESSAGE(point.red() == 0, "point.red() = " << point.red() << " (expected 0)");
    BOOST_CHECK_MESSAGE(avg3.red() > 0, "avg3.red() = " << avg3.red() << " (expected > 0)");
    BOOST_CHECK_MESSAGE(avg3.blue() < 255, "avg3.blue() = " << avg3.blue() << " (expected < 255)");
    BOOST_CHECK_MESSAGE(avg3.blue() > 0, "avg3.blue() = " << avg3.blue() << " (expected > 0)");
}

BOOST_AUTO_TEST_CASE(sample_layer_5x5_vs_3x3_differs)
{
    // Use sharp transition to ensure different window sizes give different results
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = QImage(30, 30, QImage::Format_RGBA8888);
    for (int y = 0; y < 30; ++y) {
        for (int x = 0; x < 30; ++x) {
            if (x < 15)
                setPixel(layer->cpuImage, x, y, 255, 0, 0);
            else
                setPixel(layer->cpuImage, x, y, 0, 0, 255);
        }
    }

    QColor avg3 = ColorSamplerService::sampleLayer(layer.get(), QPointF(15, 15), SampleSize::Size3x3);
    QColor avg5 = ColorSamplerService::sampleLayer(layer.get(), QPointF(15, 15), SampleSize::Size5x5);
    BOOST_REQUIRE(avg3.isValid());
    BOOST_REQUIRE(avg5.isValid());
    // 3x3 and 5x5 centered on edge should give different averages
    bool same = (avg3.red() == avg5.red() && avg3.green() == avg5.green()
                 && avg3.blue() == avg5.blue());
    BOOST_CHECK(!same);
}

BOOST_AUTO_TEST_CASE(sample_layer_11x11_smooths_more)
{
    // With edge transition, 11x11 should be more blended than 3x3
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = QImage(40, 40, QImage::Format_RGBA8888);
    for (int y = 0; y < 40; ++y) {
        for (int x = 0; x < 40; ++x) {
            if (x < 20)
                setPixel(layer->cpuImage, x, y, 255, 0, 0);
            else
                setPixel(layer->cpuImage, x, y, 0, 0, 255);
        }
    }

    QColor avg3  = ColorSamplerService::sampleLayer(layer.get(), QPointF(20, 20), SampleSize::Size3x3);
    QColor avg11 = ColorSamplerService::sampleLayer(layer.get(), QPointF(20, 20), SampleSize::Size11x11);
    BOOST_REQUIRE(avg3.isValid());
    BOOST_REQUIRE(avg11.isValid());
    // 11x11 includes more of both sides → red and blue should be closer to each other
    bool same = (avg3.red() == avg11.red() && avg3.green() == avg11.green()
                 && avg3.blue() == avg11.blue());
    BOOST_CHECK(!same);
}

BOOST_AUTO_TEST_CASE(sample_layer_average_near_edge)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeGradientImage(10, 10);

    // Sample at corner with 5x5 (radius 2) — should clamp to image bounds
    QColor c = ColorSamplerService::sampleLayer(layer.get(), QPointF(0, 0), SampleSize::Size5x5);
    BOOST_REQUIRE(c.isValid());
}

// ── sampleComposite ───────────────────────────────────────

BOOST_AUTO_TEST_CASE(sample_composite_basic)
{
    Document doc;
    doc.size = QSize(100, 100);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->layer = std::make_shared<Layer>();
    node->layer->cpuImage = makeTestImage(100, 100, 255, 0, 0, 255);
    node->layer->owner = node.get();
    node->visible = true;
    node->opacity = 1.0f;
    node->name = "Red Layer";

    // Set transform to cover full canvas
    float hw = 1.0f;
    float hh = 1.0f;
    node->transform = QTransform(hw, 0, 0, hh, 0, 0);

    doc.roots.push_back(std::move(node));
    doc.activeFlatIndex = 0;

    QColor c = ColorSamplerService::sampleComposite(&doc, QPointF(50, 50), SampleSize::Point);
    BOOST_REQUIRE(c.isValid());
    BOOST_CHECK(c.red()   == 255);
    BOOST_CHECK(c.green() == 0);
    BOOST_CHECK(c.blue()  == 0);
}

BOOST_AUTO_TEST_CASE(sample_composite_single_layer)
{
    Document doc;
    doc.size = QSize(100, 100);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->layer = std::make_shared<Layer>();
    node->layer->cpuImage = makeTestImage(100, 100, 200, 100, 50, 255);
    node->visible = true;
    node->opacity = 1.0f;
    node->transform = QTransform(1, 0, 0, 1, 0, 0);
    node->name = "Test";
    node->layer->owner = node.get();

    doc.roots.push_back(std::move(node));
    doc.activeFlatIndex = 0;

    QColor c = ColorSamplerService::sampleComposite(&doc, QPointF(50, 50), SampleSize::Point);
    BOOST_REQUIRE_MESSAGE(c.isValid(), "Single layer composite should be valid");
    BOOST_CHECK_EQUAL(c.red(),   200);
    BOOST_CHECK_EQUAL(c.green(), 100);
    BOOST_CHECK_EQUAL(c.blue(),  50);
}

BOOST_AUTO_TEST_CASE(sample_composite_two_layers)
{
    Document doc;
    doc.size = QSize(100, 100);

    // Bottom (first in roots): blue — set transform scale to cover full canvas NDC
    auto nodeB = std::make_unique<LayerTreeNode>();
    nodeB->type = LayerTreeNode::Type::Layer;
    nodeB->layer = std::make_shared<Layer>();
    nodeB->layer->cpuImage = makeTestImage(100, 100, 0, 0, 255, 255);
    nodeB->visible = true;
    nodeB->opacity = 1.0f;
    nodeB->transform = QTransform(1, 0, 0, 1, 0, 0);
    nodeB->name = "Blue";
    nodeB->layer->owner = nodeB.get();

    // Top (last in roots): red, 50% opacity
    auto nodeT = std::make_unique<LayerTreeNode>();
    nodeT->type = LayerTreeNode::Type::Layer;
    nodeT->layer = std::make_shared<Layer>();
    nodeT->layer->cpuImage = makeTestImage(100, 100, 255, 0, 0, 255);
    nodeT->visible = true;
    nodeT->opacity = 0.5f;
    nodeT->transform = QTransform(1, 0, 0, 1, 0, 0);
    nodeT->name = "Red 50%";
    nodeT->layer->owner = nodeT.get();

    // flat[0] = visual top (from roots.begin()). With push_back:
    // first push → roots[0] → flat[0] = visual top
    // second push → roots[1] → flat[1] = visual bottom
    // So push red (top layer) first, blue (bottom layer) second
    doc.roots.push_back(std::move(nodeT));  // red → flat[0] = visual top
    doc.roots.push_back(std::move(nodeB));  // blue → flat[1] = visual bottom
    doc.activeFlatIndex = 0;

    // Red (255,0,0) at 50% over blue (0,0,255):
    //   R: 255*0.5 + 0*(1-0.5) = 127.5 → 127 or 128
    //   G: 0
    //   B: 0*0.5   + 255*(1-0.5) = 127.5 → 127 or 128
    QColor c = ColorSamplerService::sampleComposite(&doc, QPointF(50, 50), SampleSize::Point);
    BOOST_REQUIRE_MESSAGE(c.isValid(),
        "Composite two layers should be valid, got alpha=" << c.alpha());
    BOOST_CHECK_MESSAGE(c.red() == 127 || c.red() == 128,
        "Red expected 127 or 128, got " << c.red());
    BOOST_CHECK_MESSAGE(c.green() == 0,
        "Green expected 0, got " << c.green());
    BOOST_CHECK_MESSAGE(c.blue() == 127 || c.blue() == 128,
        "Blue expected 127 or 128, got " << c.blue());
}

BOOST_AUTO_TEST_CASE(sample_composite_empty_document)
{
    Document doc;
    doc.size = QSize(0, 0);
    QColor c = ColorSamplerService::sampleComposite(&doc, QPointF(0, 0));
    BOOST_CHECK(!c.isValid());
}

BOOST_AUTO_TEST_CASE(sample_composite_invisible_layer)
{
    Document doc;
    doc.size = QSize(50, 50);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->layer = std::make_shared<Layer>();
    node->layer->cpuImage = makeTestImage(50, 50, 255, 0, 0, 255);
    node->layer->owner = node.get();
    node->visible = false;
    node->transform = QTransform(1, 0, 0, 1, 0, 0);

    doc.roots.push_back(std::move(node));
    doc.activeFlatIndex = 0;

    // Invisible layer should not contribute → transparent
    QColor c = ColorSamplerService::sampleComposite(&doc, QPointF(25, 25), SampleSize::Point);
    BOOST_REQUIRE(c.isValid());
    BOOST_CHECK(c.alpha() == 0);
}

// ── sampleCompositeFramebuffer ────────────────────────────

BOOST_AUTO_TEST_CASE(sample_framebuffer_point)
{
    QImage fb(100, 100, QImage::Format_RGBA8888);
    fb.fill(QColor(100, 150, 200, 255));

    QColor c = ColorSamplerService::sampleCompositeFramebuffer(fb, QPointF(50, 50), SampleSize::Point);
    BOOST_REQUIRE(c.isValid());
    BOOST_CHECK(c.red()   == 100);
    BOOST_CHECK(c.green() == 150);
    BOOST_CHECK(c.blue()  == 200);
}

BOOST_AUTO_TEST_CASE(sample_framebuffer_3x3)
{
    QImage fb(20, 20, QImage::Format_RGBA8888);
    fb.fill(QColor(120, 130, 140, 255));
    // Paint a single different pixel
    fb.setPixelColor(10, 10, QColor(255, 0, 0, 255));

    QColor point = ColorSamplerService::sampleCompositeFramebuffer(fb, QPointF(10, 10), SampleSize::Point);
    QColor avg3  = ColorSamplerService::sampleCompositeFramebuffer(fb, QPointF(10, 10), SampleSize::Size3x3);
    BOOST_REQUIRE(point.isValid());
    BOOST_REQUIRE(avg3.isValid());
    // Point should be the outlier (255,0,0), 3x3 should be close to background
    BOOST_CHECK(point.red() == 255);
    BOOST_CHECK(avg3.red() < 200);  // averaged with surrounding 120s
}

BOOST_AUTO_TEST_CASE(sample_framebuffer_out_of_bounds)
{
    QImage fb(10, 10, QImage::Format_RGBA8888);
    QColor c = ColorSamplerService::sampleCompositeFramebuffer(fb, QPointF(50, 50));
    BOOST_CHECK(!c.isValid());
}

// ── Edge cases ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(sample_null_framebuffer)
{
    QColor c = ColorSamplerService::sampleCompositeFramebuffer(QImage(), QPointF(0, 0));
    BOOST_CHECK(!c.isValid());
}

BOOST_AUTO_TEST_CASE(sample_negative_coordinates)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeTestImage(10, 10, 100, 100, 100);

    QColor c = ColorSamplerService::sampleLayer(layer.get(), QPointF(-5, -5));
    BOOST_CHECK(!c.isValid());
}

BOOST_AUTO_TEST_CASE(sample_fractional_rounds_correctly)
{
    // 5.2 rounds to 5, 5.7 rounds to 6, so they should differ on a gradient
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeGradientImage(10, 10);

    QColor c1 = ColorSamplerService::sampleLayer(layer.get(), QPointF(5.2, 5.3));
    QColor c2 = ColorSamplerService::sampleLayer(layer.get(), QPointF(5.0, 5.0));
    BOOST_REQUIRE(c1.isValid());
    BOOST_REQUIRE(c2.isValid());
    // Same integer rounding → same color
    BOOST_CHECK(c1 == c2);
}

BOOST_AUTO_TEST_CASE(sample_fractional_rounds_to_different_pixel)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeGradientImage(10, 10);

    QColor c1 = ColorSamplerService::sampleLayer(layer.get(), QPointF(5.49, 5.49));
    QColor c2 = ColorSamplerService::sampleLayer(layer.get(), QPointF(5.50, 5.50));
    BOOST_REQUIRE(c1.isValid());
    BOOST_REQUIRE(c2.isValid());
    // 5.49 rounds to 5, 5.50 rounds to 6 → different pixels on gradient
    bool same = (c1.red() == c2.red() && c1.green() == c2.green()
                 && c1.blue() == c2.blue());
    BOOST_CHECK(!same);
}

BOOST_AUTO_TEST_CASE(sample_partial_average_at_corner)
{
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeTestImage(5, 5, 50, 100, 150, 255);

    // 11x11 sample at (2,2) on a 5x5 image — radius 5, clamps to 0..4
    // All pixels are the same color (50,100,150), so average = (50,100,150)
    QColor c = ColorSamplerService::sampleLayer(layer.get(), QPointF(2, 2), SampleSize::Size11x11);
    BOOST_REQUIRE_MESSAGE(c.isValid(), "11x11 at corner should be valid");
    BOOST_CHECK_MESSAGE(c.red() == 50, "Red: expected 50, got " << c.red());
    BOOST_CHECK_MESSAGE(c.green() == 100, "Green: expected 100, got " << c.green());
    BOOST_CHECK_MESSAGE(c.blue() == 150, "Blue: expected 150, got " << c.blue());
    BOOST_CHECK_MESSAGE(c.alpha() == 255, "Alpha: expected 255, got " << c.alpha());
}

// ── SampleSize enum ───────────────────────────────────────

BOOST_AUTO_TEST_CASE(sample_size_default_is_point)
{
    // Verify that default SampleSize parameter is Point (radius 0)
    auto layer = std::make_shared<Layer>();
    layer->cpuImage = makeTestImage(10, 10, 80, 90, 100, 200);

    QColor withDefault = ColorSamplerService::sampleLayer(layer.get(), QPointF(5, 5));
    QColor withPoint   = ColorSamplerService::sampleLayer(layer.get(), QPointF(5, 5), SampleSize::Point);
    BOOST_CHECK(withDefault == withPoint);
}

BOOST_AUTO_TEST_SUITE_END()
