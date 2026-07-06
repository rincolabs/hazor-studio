#define BOOST_TEST_MODULE ImportImageTest
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
#include <QFile>
#include <QTransform>

static QImage makeTestImage(int w, int h, QColor color = QColor(255, 0, 0, 255))
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(color);
    return img;
}

struct ImportImageFixture {
    Document doc;
    ImageController ctrl;

    ImportImageFixture()
    {
        doc.size = QSize(800, 600);
        doc.selection.create(800, 600);
        ctrl.setDocument(&doc);
    }

    int flatCount() const { return doc.flatCount(); }
    LayerTreeNode* activeNode() { return doc.activeNode(); }
    Layer* activeLayer() { return doc.activeLayer(); }
};

BOOST_AUTO_TEST_SUITE(import_image)

// ══════════════════════════════════════════════════════════════
// GROUP A: Happy Path
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(import_valid_image_creates_node)
{
    ImportImageFixture f;
    QImage img = makeTestImage(200, 150);

    bool ok = f.ctrl.importImage(img, "Test Layer");
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
    BOOST_CHECK(f.activeNode() != nullptr);
    BOOST_CHECK(f.activeNode()->type == LayerTreeNode::Type::Layer);
}

BOOST_AUTO_TEST_CASE(import_image_layer_name)
{
    ImportImageFixture f;
    QImage img = makeTestImage(100, 100);

    f.ctrl.importImage(img, "MyCustomName");
    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);
    BOOST_CHECK_EQUAL(node->name.toStdString(), "MyCustomName");
}

BOOST_AUTO_TEST_CASE(import_image_layer_visible_and_opaque)
{
    ImportImageFixture f;
    QImage img = makeTestImage(100, 100);

    f.ctrl.importImage(img, "Test");
    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);
    BOOST_CHECK(node->visible);
    BOOST_CHECK_EQUAL(node->opacity, 1.0f);
    BOOST_CHECK(node->blendMode == BlendMode::Normal);
}

BOOST_AUTO_TEST_CASE(import_image_content_preserved)
{
    ImportImageFixture f;
    QImage img = makeTestImage(10, 10, QColor(0, 255, 0, 255));

    f.ctrl.importImage(img, "Test");
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    BOOST_CHECK_EQUAL(layer->cpuImage.width(), 10);
    BOOST_CHECK_EQUAL(layer->cpuImage.height(), 10);
    QColor px = layer->cpuImage.pixelColor(5, 5);
    BOOST_CHECK_EQUAL(px.red(), 0);
    BOOST_CHECK_EQUAL(px.green(), 255);
    BOOST_CHECK_EQUAL(px.blue(), 0);
    BOOST_CHECK_EQUAL(px.alpha(), 255);
}

BOOST_AUTO_TEST_CASE(import_image_layer_centered_at_origin)
{
    ImportImageFixture f;
    QImage img = makeTestImage(200, 200, QColor(0, 0, 255, 255));

    f.ctrl.importImage(img, "Test");
    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);

    // Transform should center image at canvas NDC origin (0,0)
    QTransform t = node->transform;
    // The scale should map image half-size to NDC [-1,1]
    // doc=800x600, image=200x200 → halfW = 200/800 = 0.25f, halfH = 200/600 = ~0.333f
    float expectedHalfW = 200.0f / 800.0f;
    float expectedHalfH = 200.0f / 600.0f;
    BOOST_CHECK_CLOSE(static_cast<float>(t.m11()), expectedHalfW, 0.01f);
    BOOST_CHECK_CLOSE(static_cast<float>(t.m22()), expectedHalfH, 0.01f);
    BOOST_CHECK_SMALL(static_cast<float>(t.m31()), 0.0001f);
    BOOST_CHECK_SMALL(static_cast<float>(t.m32()), 0.0001f);
}

BOOST_AUTO_TEST_CASE(import_image_undoable)
{
    ImportImageFixture f;
    QImage img = makeTestImage(100, 100);

    f.ctrl.importImage(img, "Test");
    BOOST_CHECK_EQUAL(f.flatCount(), 1);

    // Check undo reduces flatCount to 0
    BOOST_CHECK(f.ctrl.history().canUndo());
    f.ctrl.undo();
    BOOST_CHECK_EQUAL(f.flatCount(), 0);

    // Redo brings it back
    BOOST_CHECK(f.ctrl.history().canRedo());
    f.ctrl.redo();
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
}

BOOST_AUTO_TEST_CASE(import_multiple_images_creates_multiple_layers)
{
    ImportImageFixture f;
    QImage img1 = makeTestImage(100, 100, QColor(255, 0, 0, 255));
    QImage img2 = makeTestImage(100, 100, QColor(0, 255, 0, 255));

    f.ctrl.importImage(img1, "Layer1");
    f.ctrl.importImage(img2, "Layer2");
    BOOST_CHECK_EQUAL(f.flatCount(), 2);
}

// ══════════════════════════════════════════════════════════════
// GROUP B: Edge & Error Cases
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(import_null_image_returns_false)
{
    ImportImageFixture f;
    QImage nullImg;
    BOOST_ASSERT(nullImg.isNull());

    bool ok = f.ctrl.importImage(nullImg, "Test");
    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(f.flatCount(), 0);
}

BOOST_AUTO_TEST_CASE(import_null_document_returns_false)
{
    ImageController ctrlNoDoc;
    QImage img = makeTestImage(100, 100);

    bool ok = ctrlNoDoc.importImage(img, "Test");
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(import_1x1_image_succeeds)
{
    ImportImageFixture f;
    QImage img = makeTestImage(1, 1, QColor(255, 255, 0, 255));

    bool ok = f.ctrl.importImage(img, "Tiny");
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    BOOST_CHECK_EQUAL(layer->cpuImage.width(), 1);
    BOOST_CHECK_EQUAL(layer->cpuImage.height(), 1);
}

BOOST_AUTO_TEST_CASE(import_large_image_succeeds)
{
    ImportImageFixture f;
    QImage img = makeTestImage(4000, 3000, QColor(255, 0, 255, 255));

    bool ok = f.ctrl.importImage(img, "Large");
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(f.flatCount(), 1);
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    BOOST_CHECK_EQUAL(layer->cpuImage.width(), 4000);
    BOOST_CHECK_EQUAL(layer->cpuImage.height(), 3000);
}

BOOST_AUTO_TEST_CASE(import_image_into_doc_with_existing_layers)
{
    ImportImageFixture f;
    f.ctrl.newLayer();
    BOOST_CHECK_EQUAL(f.flatCount(), 1);

    QImage img = makeTestImage(100, 100);
    bool ok = f.ctrl.importImage(img, "Imported");
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(f.flatCount(), 2);
}

BOOST_AUTO_TEST_CASE(import_image_format_conversion_rgb32)
{
    ImportImageFixture f;
    QImage img(50, 50, QImage::Format_RGB32);
    img.fill(QColor(0, 0, 255));
    BOOST_CHECK(img.format() == QImage::Format_RGB32);

    bool ok = f.ctrl.importImage(img, "RGB32");
    BOOST_CHECK(ok);
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    // Should be auto-converted to RGBA8888
    BOOST_CHECK_EQUAL(layer->cpuImage.format(), QImage::Format_RGBA8888);
}

BOOST_AUTO_TEST_CASE(import_image_format_conversion_argb32_pm)
{
    ImportImageFixture f;
    QImage img(50, 50, QImage::Format_ARGB32_Premultiplied);
    img.fill(QColor(100, 200, 50));
    BOOST_CHECK(img.format() != QImage::Format_RGBA8888);

    bool ok = f.ctrl.importImage(img, "ARGB32PM");
    BOOST_CHECK(ok);
    auto* layer = f.activeLayer();
    BOOST_REQUIRE(layer != nullptr);
    BOOST_CHECK_EQUAL(layer->cpuImage.format(), QImage::Format_RGBA8888);
}

BOOST_AUTO_TEST_CASE(import_image_reset_transform_preserved)
{
    ImportImageFixture f;
    QImage img = makeTestImage(100, 100);

    f.ctrl.importImage(img, "Test");
    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);
    BOOST_REQUIRE(node->layer != nullptr);

    // resetTransform should be set and equal to the node transform
    BOOST_CHECK(node->layer->hasResetTransform);
    BOOST_CHECK(node->layer->resetTransform == node->transform);
}

// ══════════════════════════════════════════════════════════════
// GROUP C: importImage positioning with non-square images
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(import_wide_rectangular_image_positioned_correctly)
{
    ImportImageFixture f;
    QImage img = makeTestImage(400, 100, QColor(255, 0, 0, 255));

    f.ctrl.importImage(img, "Wide");
    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);

    QTransform t = node->transform;
    float expectedHalfW = 400.0f / 800.0f;
    float expectedHalfH = 100.0f / 600.0f;
    BOOST_CHECK_CLOSE(static_cast<float>(t.m11()), expectedHalfW, 0.01f);
    BOOST_CHECK_CLOSE(static_cast<float>(t.m22()), expectedHalfH, 0.01f);
    // Center at origin
    BOOST_CHECK_SMALL(static_cast<float>(t.m31()), 0.0001f);
    BOOST_CHECK_SMALL(static_cast<float>(t.m32()), 0.0001f);
}

BOOST_AUTO_TEST_CASE(import_tall_rectangular_image_positioned_correctly)
{
    ImportImageFixture f;
    QImage img = makeTestImage(100, 400, QColor(255, 0, 0, 255));

    f.ctrl.importImage(img, "Tall");
    auto* node = f.activeNode();
    BOOST_REQUIRE(node != nullptr);

    QTransform t = node->transform;
    float expectedHalfW = 100.0f / 800.0f;
    float expectedHalfH = 400.0f / 600.0f;
    BOOST_CHECK_CLOSE(static_cast<float>(t.m11()), expectedHalfW, 0.01f);
    BOOST_CHECK_CLOSE(static_cast<float>(t.m22()), expectedHalfH, 0.01f);
}

// ══════════════════════════════════════════════════════════════
// GROUP D: activeLayerChanged signal emission
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(import_image_emits_active_layer_changed)
{
    ImportImageFixture f;
    int signalCount = 0;
    int lastActive = -1;

    QObject::connect(&f.ctrl, &ImageController::activeLayerChanged,
                     [&](int idx) { signalCount++; lastActive = idx; });

    QImage img = makeTestImage(100, 100);
    f.ctrl.importImage(img, "Test");

    BOOST_CHECK_EQUAL(signalCount, 1);
    BOOST_CHECK_EQUAL(lastActive, 0);
}

BOOST_AUTO_TEST_CASE(import_image_emits_layer_changed)
{
    ImportImageFixture f;
    int signalCount = 0;

    QObject::connect(&f.ctrl, &ImageController::layerChanged,
                     [&](int) { signalCount++; });

    QImage img = makeTestImage(100, 100);
    f.ctrl.importImage(img, "Test");

    BOOST_CHECK_EQUAL(signalCount, 1);
}

BOOST_AUTO_TEST_CASE(import_image_emits_image_changed)
{
    ImportImageFixture f;
    int signalCount = 0;

    QObject::connect(&f.ctrl, &ImageController::imageChanged,
                     [&]() { signalCount++; });

    QImage img = makeTestImage(100, 100);
    f.ctrl.importImage(img, "Test");

    BOOST_CHECK_EQUAL(signalCount, 1);
}

// ══════════════════════════════════════════════════════════════
// GROUP E: Regression — importExternalImages still works
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(import_external_images_still_works)
{
    ImportImageFixture f;

    QImage saved = makeTestImage(200, 150, QColor(0, 255, 255, 255));
    QString path = "/tmp/test_import_image.png";
    saved.save(path);

    QStringList paths = { path };
    bool ok = f.ctrl.importExternalImages(paths, QPointF(0.0, 0.0));
    BOOST_CHECK(ok);
    BOOST_CHECK_EQUAL(f.flatCount(), 1);

    // Clean up
    QFile::remove(path);
}

BOOST_AUTO_TEST_CASE(import_external_images_empty_list_returns_false)
{
    ImportImageFixture f;
    bool ok = f.ctrl.importExternalImages({}, QPointF(0.0, 0.0));
    BOOST_CHECK(!ok);
    BOOST_CHECK_EQUAL(f.flatCount(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
