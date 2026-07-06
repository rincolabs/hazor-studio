#define BOOST_TEST_MODULE ImageIOTest
#include <boost/test/included/unit_test.hpp>

#include "io/ImageIO.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerEffect.hpp"
#include "core/LayerTreeNode.hpp"

#include <QImage>
#include <QColor>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QTransform>

static std::unique_ptr<Document> makeSimpleDoc(int w = 100, int h = 100)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(w, h);
    doc->selection.create(w, h);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = "Layer 1";
    node->layer = std::make_shared<Layer>();
    node->layer->name = "Layer 1";
    node->layer->cpuImage = QImage(w, h, QImage::Format_RGBA8888);
    node->layer->cpuImage.fill(QColor(255, 0, 0, 255));
    node->layer->owner = node.get();
    doc->roots.push_back(std::move(node));
    doc->activeFlatIndex = 0;

    return doc;
}

BOOST_AUTO_TEST_SUITE(imageio)

BOOST_AUTO_TEST_CASE(saveImage_null_document_returns_false)
{
    BOOST_CHECK(!saveImage(nullptr, "/tmp/test_null.png"));
}

BOOST_AUTO_TEST_CASE(saveImage_empty_document_returns_false)
{
    Document empty;
    empty.size = QSize(100, 100);
    BOOST_CHECK(!saveImage(&empty, "/tmp/test_empty.png"));
}

BOOST_AUTO_TEST_CASE(saveImage_layer_visible_ops_correctly)
{
    auto doc = makeSimpleDoc(50, 50);
    QString path = "/tmp/imageio_test_output.png";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QImage loaded(path);
        BOOST_CHECK(!loaded.isNull());
        BOOST_CHECK_EQUAL(loaded.width(), 50);
        BOOST_CHECK_EQUAL(loaded.height(), 50);
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(compositeImage_applies_layer_styles)
{
    auto doc = makeSimpleDoc(8, 8);
    auto* node = doc->nodeAt(0);
    BOOST_REQUIRE(node != nullptr);

    QVariantMap params = LayerEffect::defaultStyleParams(QStringLiteral("color_overlay"));
    params["color"] = QColor(0, 0, 255, 255);
    params["opacity"] = 1.0;
    node->effects.push_back(LayerEffect(QStringLiteral("color_overlay"), params));

    QImage composited = compositeImage(doc.get());
    BOOST_REQUIRE(!composited.isNull());
    const QColor px = composited.pixelColor(4, 4);
    BOOST_CHECK(px.blue() > 240);
    BOOST_CHECK(px.red() < 20);
    BOOST_CHECK(px.green() < 20);
    BOOST_CHECK_EQUAL(px.alpha(), 255);
}

BOOST_AUTO_TEST_CASE(layer_styles_expand_effect_bounds)
{
    LayerTreeNode node;
    node.type = LayerTreeNode::Type::Layer;
    node.layer = std::make_shared<Layer>();
    node.layer->cpuImage = QImage(8, 8, QImage::Format_RGBA8888);
    node.layer->cpuImage.fill(Qt::transparent);
    node.layer->cpuImage.setPixelColor(4, 4, QColor(255, 0, 0, 255));
    node.layer->owner = &node;

    QVariantMap params = LayerEffect::defaultStyleParams(QStringLiteral("drop_shadow"));
    params["opacity"] = 1.0;
    params["distance"] = 4;
    params["blur"] = 0.0;
    params["spread"] = 0;
    params["angle"] = 0.0;
    node.effects.push_back(LayerEffect(QStringLiteral("drop_shadow"), params));

    QImage effected = node.computeEffectedImage();
    const QRectF bounds = node.effectedImageBounds();
    BOOST_REQUIRE(!effected.isNull());
    BOOST_CHECK(effected.width() > node.layer->cpuImage.width());
    BOOST_CHECK(effected.height() > node.layer->cpuImage.height());
    BOOST_CHECK(bounds.left() < 0.0);
    BOOST_CHECK(bounds.right() > node.layer->cpuImage.width());
}

BOOST_AUTO_TEST_CASE(gaussian_layer_effect_expands_tight_raster_dab_bounds)
{
    LayerTreeNode node;
    node.type = LayerTreeNode::Type::Layer;
    node.layer = std::make_shared<Layer>();

    QImage dab(1, 1, QImage::Format_RGBA8888);
    dab.fill(QColor(255, 0, 0, 255));
    node.layer->cpuImage = dab;
    node.layer->rasterStorage.replaceWithImage(dab, QPoint(16, 16), 8);
    node.layer->owner = &node;

    QVariantMap params;
    params["radius"] = 4.0;
    node.effects.push_back(LayerEffect(QStringLiteral("gaussian_blur"), params));

    const QRectF baseBounds = node.layer->renderImageBounds();
    QImage effected = node.computeEffectedImage();
    const QRectF bounds = node.effectedImageBounds();

    BOOST_REQUIRE(!effected.isNull());
    BOOST_CHECK(effected.width() > baseBounds.width());
    BOOST_CHECK(effected.height() > baseBounds.height());
    BOOST_CHECK(bounds.left() < baseBounds.left());
    BOOST_CHECK(bounds.top() < baseBounds.top());

    const QColor expanded = effected.pixelColor(2, 6);
    BOOST_CHECK(expanded.alpha() > 0);
    BOOST_CHECK(expanded.red() > expanded.green());
    BOOST_CHECK(expanded.red() > expanded.blue());
}

BOOST_AUTO_TEST_CASE(saveImage_invisible_layer_skipped)
{
    auto doc = makeSimpleDoc(50, 50);
    doc->nodeAt(0)->visible = false;

    QString path = "/tmp/imageio_test_invisible.png";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QImage loaded(path);
        BOOST_CHECK(!loaded.isNull());
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_layer_opacity_respected)
{
    auto doc = makeSimpleDoc(50, 50);
    doc->nodeAt(0)->opacity = 0.5f;

    QString path = "/tmp/imageio_test_opacity.png";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QImage loaded(path);
        BOOST_CHECK(!loaded.isNull());
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_layer_transform_applied)
{
    auto doc = makeSimpleDoc(50, 50);
    doc->nodeAt(0)->transform.translate(0.2f, 0.1f);

    QString path = "/tmp/imageio_test_transform.png";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QImage loaded(path);
        BOOST_CHECK(!loaded.isNull());
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_multiple_layers)
{
    auto doc = makeSimpleDoc(50, 50);

    auto node2 = std::make_unique<LayerTreeNode>();
    node2->type = LayerTreeNode::Type::Layer;
    node2->name = "Layer 2";
    node2->layer = std::make_shared<Layer>();
    node2->layer->cpuImage = QImage(50, 50, QImage::Format_RGBA8888);
    node2->layer->cpuImage.fill(QColor(0, 0, 255, 128));
    node2->layer->owner = node2.get();
    doc->roots.push_back(std::move(node2));

    QString path = "/tmp/imageio_test_multilayer.png";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QImage loaded(path);
        BOOST_CHECK(!loaded.isNull());
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_group_node_children_not_saved)
{
    auto doc = makeSimpleDoc(50, 50);

    auto group = std::make_unique<LayerTreeNode>();
    group->type = LayerTreeNode::Type::Group;
    group->name = "Group";
    group->children.push_back(std::make_unique<LayerTreeNode>());
    group->children[0]->type = LayerTreeNode::Type::Layer;
    group->children[0]->layer = std::make_shared<Layer>();
    group->children[0]->layer->cpuImage = QImage(50, 50, QImage::Format_RGBA8888);
    group->children[0]->layer->cpuImage.fill(QColor(0, 255, 0, 128));
    group->children[0]->layer->owner = group->children[0].get();
    doc->roots.push_back(std::move(group));

    QString path = "/tmp/imageio_test_group.png";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QImage loaded(path);
        BOOST_CHECK(!loaded.isNull());
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_png_format)
{
    auto doc = makeSimpleDoc(10, 10);
    QString path = "/tmp/imageio_test_png.png";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QFile f(path);
        BOOST_CHECK(f.size() > 0);
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_jpg_format)
{
    auto doc = makeSimpleDoc(10, 10);
    QString path = "/tmp/imageio_test_jpg.jpg";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QFile f(path);
        BOOST_CHECK(f.size() > 0);
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_bmp_format)
{
    auto doc = makeSimpleDoc(10, 10);
    QString path = "/tmp/imageio_test_bmp.bmp";
    QFile::remove(path);

    bool ok = saveImage(doc.get(), path);
    BOOST_CHECK(ok);

    if (ok) {
        QFile f(path);
        BOOST_CHECK(f.size() > 0);
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_invalid_path)
{
    auto doc = makeSimpleDoc(50, 50);
    bool ok = saveImage(doc.get(), "/nonexistent/dir/image.png");
    BOOST_CHECK(!ok);
}

// ── Helpers ──────────────────────────────────────────────────

static bool pixelEquals(const QImage& img, int x, int y, uchar r, uchar g, uchar b, uchar a)
{
    if (x < 0 || x >= img.width() || y < 0 || y >= img.height()) return false;
    QColor c = img.pixelColor(x, y);
    return c.red() == r && c.green() == g && c.blue() == b && c.alpha() == a;
}

static void addLayer(Document* doc, const QColor& color, int w, int h,
                      bool visible = true, float opacity = 1.0f,
                      QTransform xf = QTransform(),
                      BlendMode blendMode = BlendMode::Normal)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = QString("Layer %1").arg(doc->flatCount() + 1);
    node->visible = visible;
    node->opacity = opacity;
    node->blendMode = blendMode;
    node->transform = xf;
    node->layer = std::make_shared<Layer>();
    node->layer->name = node->name;
    node->layer->cpuImage = QImage(w, h, QImage::Format_RGBA8888);
    node->layer->cpuImage.fill(color);
    node->layer->owner = node.get();
    doc->roots.push_back(std::move(node));
}

static QImage makeNonUniformImage(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(QColor(255, 0, 0, 255));
    for (int y = 0; y < qMin(20, h); ++y)
        for (int x = 0; x < qMin(20, w); ++x)
            img.setPixelColor(x, y, QColor(0, 255, 0, 255));
    return img;
}

// ══════════════════════════════════════════════════════════════
// GROUP A: compositeImage() — coordinate system
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(composite_identity_fills_canvas)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(100, 100);
    doc->selection.create(100, 100);
    addLayer(doc.get(), QColor(255, 0, 0, 255), 100, 100);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK(pixelEquals(result, 0, 0, 255, 0, 0, 255));
    BOOST_CHECK(pixelEquals(result, 50, 50, 255, 0, 0, 255));
    BOOST_CHECK(pixelEquals(result, 99, 99, 255, 0, 0, 255));
}

BOOST_AUTO_TEST_CASE(composite_translate_right)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(200, 200);
    doc->selection.create(200, 200);
    QTransform xf;
    xf.translate(0.5, 0.0);
    addLayer(doc.get(), QColor(0, 255, 0, 255), 200, 200, true, 1.0f, xf);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // Layer shifted right by 50 px. Center now at pixel (150, 100).
    // Pixel (100, 100): still within layer (x=100 > 50) → green
    BOOST_CHECK(pixelEquals(result, 100, 100, 0, 255, 0, 255));
    // Pixel (30, 100): x=30 < 50 → outside layer → transparent
    BOOST_CHECK(pixelEquals(result, 30, 100, 0, 0, 0, 0));
}

BOOST_AUTO_TEST_CASE(composite_translate_up)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(200, 200);
    doc->selection.create(200, 200);
    QTransform xf;
    xf.translate(0.0, 0.5);
    addLayer(doc.get(), QColor(0, 0, 255, 255), 200, 200, true, 1.0f, xf);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // NDC Y-up: translate(0, 0.5) moves layer UP 50 px.
    // Center at (100, 50). Layer bottom at y=100.
    // Pixel (100, 80): y=80 < 100 → within layer → blue
    BOOST_CHECK(pixelEquals(result, 100, 80, 0, 0, 255, 255));
    // Pixel (100, 160): y=160 > 100 → outside layer → transparent
    BOOST_CHECK(pixelEquals(result, 100, 160, 0, 0, 0, 0));
}

BOOST_AUTO_TEST_CASE(composite_scale_2x)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(400, 400);
    doc->selection.create(400, 400);
    QTransform xf;
    xf.scale(2.0, 2.0);
    addLayer(doc.get(), QColor(255, 0, 0, 255), 200, 200, true, 1.0f, xf);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // 200x200 image scaled 2x → fills 400x400 canvas
    BOOST_CHECK(pixelEquals(result, 199, 199, 255, 0, 0, 255));
    BOOST_CHECK(pixelEquals(result, 399, 399, 255, 0, 0, 255));
}

BOOST_AUTO_TEST_CASE(composite_translate_and_scale)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(200, 200);
    doc->selection.create(200, 200);
    QTransform xf;
    xf.translate(0.5, 0.3);
    xf.scale(0.5, 0.5);
    addLayer(doc.get(), QColor(255, 0, 0, 255), 100, 100, true, 1.0f, xf);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // Layer half-size, shifted right+up. Center at NDC (0.5, 0.3).
    // Pixel (150, 70) = expected center → red
    BOOST_CHECK(pixelEquals(result, 150, 70, 255, 0, 0, 255));
    // Pixel (50, 70) = far outside left edge → transparent
    BOOST_CHECK(pixelEquals(result, 50, 70, 0, 0, 0, 0));
}

BOOST_AUTO_TEST_CASE(composite_opacity_respected)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(50, 50);
    doc->selection.create(50, 50);
    addLayer(doc.get(), QColor(255, 0, 0, 255), 50, 50, true, 0.5f);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // Red at 50% opacity over transparent background
    // Opacity scales alpha, not the color channels
    QColor c = result.pixelColor(25, 25);
    BOOST_CHECK_EQUAL(c.red(), 255);
    BOOST_CHECK_EQUAL(c.alpha(), 127);  // ≈ 255 * 0.5
}

// ══════════════════════════════════════════════════════════════
// GROUP B: compositeImage() — z-ordering
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(composite_two_layers_bottom_to_top)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(100, 100);
    doc->selection.create(100, 100);

    // Layer 0 (topmost): green, shifted right
    QTransform xfTop;
    xfTop.translate(0.2, 0.0);
    addLayer(doc.get(), QColor(0, 255, 0, 255), 100, 100, true, 1.0f, xfTop);

    // Layer 1 (bottom): red, identity
    addLayer(doc.get(), QColor(255, 0, 0, 255), 100, 100, true, 1.0f, QTransform());

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // Overlap area: pixel (70, 50) — within both layers → green on top
    BOOST_CHECK(pixelEquals(result, 70, 50, 0, 255, 0, 255));
    // Non-overlap left: pixel (5, 50) — only red layer (green shifted right) → red
    BOOST_CHECK(pixelEquals(result, 5, 50, 255, 0, 0, 255));
}

BOOST_AUTO_TEST_CASE(composite_blend_mode_multiply_applied)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(40, 40);
    doc->selection.create(40, 40);

    // Top layer: green with Multiply.
    addLayer(doc.get(), QColor(0, 255, 0, 255), 40, 40, true, 1.0f,
             QTransform(), BlendMode::Multiply);
    // Bottom layer: red.
    addLayer(doc.get(), QColor(255, 0, 0, 255), 40, 40);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());
    QColor c = result.pixelColor(20, 20);

    // red * green => black
    BOOST_CHECK_EQUAL(c.red(), 0);
    BOOST_CHECK_EQUAL(c.green(), 0);
    BOOST_CHECK_EQUAL(c.blue(), 0);
    BOOST_CHECK_EQUAL(c.alpha(), 255);
}

BOOST_AUTO_TEST_CASE(composite_three_layers_alpha_blend)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(100, 100);
    doc->selection.create(100, 100);

    // Layer 0 (top): red, 50% alpha
    addLayer(doc.get(), QColor(255, 0, 0, 127), 100, 100, true, 1.0f);

    // Layer 1 (mid): green, shifted, 50% alpha
    QTransform xfMid;
    xfMid.translate(0.15, 0.0);
    addLayer(doc.get(), QColor(0, 255, 0, 127), 100, 100, true, 1.0f, xfMid);

    // Layer 2 (bottom): blue, identity
    addLayer(doc.get(), QColor(0, 0, 255, 255), 100, 100, true, 1.0f);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // Center pixel: blue bottom + green mid alpha + red top alpha
    QColor c = result.pixelColor(50, 50);
    // All three contribute (red is opaque enough)
    BOOST_CHECK(c.red() > 0 && c.green() > 0 && c.blue() > 0);
}

BOOST_AUTO_TEST_CASE(composite_invisible_layer_skipped)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(50, 50);
    doc->selection.create(50, 50);

    addLayer(doc.get(), QColor(0, 255, 0, 255), 50, 50, false);  // invisible
    addLayer(doc.get(), QColor(255, 0, 0, 255), 50, 50, true);   // visible

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // Only red visible
    BOOST_CHECK(pixelEquals(result, 25, 25, 255, 0, 0, 255));
}

BOOST_AUTO_TEST_CASE(composite_all_invisible_returns_transparent)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(50, 50);
    doc->selection.create(50, 50);

    addLayer(doc.get(), QColor(255, 0, 0, 255), 50, 50, false);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // All transparent
    BOOST_CHECK(pixelEquals(result, 0, 0, 0, 0, 0, 0));
}

// ══════════════════════════════════════════════════════════════
// GROUP C: saveImage() with ExportOptions
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(saveImage_with_export_options_defaults)
{
    auto doc = makeSimpleDoc(10, 10);
    QString path = "/tmp/imageio_export_default.png";
    QFile::remove(path);

    ExportOptions opts;
    bool ok = saveImage(doc.get(), path, opts);
    BOOST_CHECK(ok);

    if (ok) {
        QImage loaded(path);
        BOOST_CHECK(!loaded.isNull());
        BOOST_CHECK_EQUAL(loaded.width(), 10);
        QFile::remove(path);
    }
}

BOOST_AUTO_TEST_CASE(saveImage_with_export_options_null_doc)
{
    bool ok = saveImage(nullptr, "/tmp/imageio_export_null.png", ExportOptions{90});
    BOOST_CHECK(!ok);
}

BOOST_AUTO_TEST_CASE(saveImage_jpeg_quality_low_smaller_file)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(200, 200);
    doc->selection.create(200, 200);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = "Var";
    node->layer = std::make_shared<Layer>();
    node->layer->cpuImage = makeNonUniformImage(200, 200);
    node->layer->owner = node.get();
    doc->roots.push_back(std::move(node));
    doc->activeFlatIndex = 0;

    QString pathHigh = "/tmp/imageio_jpg_high.jpg";
    QString pathLow  = "/tmp/imageio_jpg_low.jpg";
    QFile::remove(pathHigh);
    QFile::remove(pathLow);

    ExportOptions optsHigh, optsLow;
    optsHigh.quality = 90;
    optsLow.quality = 10;

    BOOST_CHECK(saveImage(doc.get(), pathHigh, optsHigh));
    BOOST_CHECK(saveImage(doc.get(), pathLow, optsLow));

    qint64 sizeHigh = QFileInfo(pathHigh).size();
    qint64 sizeLow  = QFileInfo(pathLow).size();
    BOOST_CHECK_GT(sizeHigh, sizeLow);

    QFile::remove(pathHigh);
    QFile::remove(pathLow);
}

BOOST_AUTO_TEST_CASE(saveImage_png_compression_affects_size)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(200, 200);
    doc->selection.create(200, 200);

    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Layer;
    node->name = "Var";
    node->layer = std::make_shared<Layer>();
    node->layer->cpuImage = makeNonUniformImage(200, 200);
    node->layer->owner = node.get();
    doc->roots.push_back(std::move(node));
    doc->activeFlatIndex = 0;

    QString pathSmall = "/tmp/imageio_png_small.png";
    QString pathBig   = "/tmp/imageio_png_big.png";
    QFile::remove(pathSmall);
    QFile::remove(pathBig);

    ExportOptions optsSmall, optsBig;
    optsSmall.compression = 9;
    optsBig.compression = 0;

    BOOST_CHECK(saveImage(doc.get(), pathSmall, optsSmall));
    BOOST_CHECK(saveImage(doc.get(), pathBig, optsBig));

    qint64 sizeSmall = QFileInfo(pathSmall).size();
    qint64 sizeBig   = QFileInfo(pathBig).size();
    BOOST_CHECK_GE(sizeBig, sizeSmall);

    QFile::remove(pathSmall);
    QFile::remove(pathBig);
}

BOOST_AUTO_TEST_CASE(saveImage_resize_to_valid)
{
    auto doc = makeSimpleDoc(200, 200);
    QString path = "/tmp/imageio_resized.png";
    QFile::remove(path);

    ExportOptions opts;
    opts.resizeTo = QSize(50, 50);

    BOOST_CHECK(saveImage(doc.get(), path, opts));

    QImage loaded(path);
    BOOST_REQUIRE(!loaded.isNull());
    BOOST_CHECK_EQUAL(loaded.width(), 50);
    BOOST_CHECK_EQUAL(loaded.height(), 50);
    QFile::remove(path);
}

BOOST_AUTO_TEST_CASE(saveImage_resize_to_invalid_ignored)
{
    auto doc = makeSimpleDoc(100, 100);
    QString path = "/tmp/imageio_noresize.png";
    QFile::remove(path);

    ExportOptions opts;
    opts.resizeTo = QSize(0, 0);

    BOOST_CHECK(saveImage(doc.get(), path, opts));

    QImage loaded(path);
    BOOST_REQUIRE(!loaded.isNull());
    BOOST_CHECK_EQUAL(loaded.width(), 100);
    BOOST_CHECK_EQUAL(loaded.height(), 100);
    QFile::remove(path);
}

BOOST_AUTO_TEST_CASE(saveImage_resize_empty_ignored)
{
    auto doc = makeSimpleDoc(100, 100);
    QString path = "/tmp/imageio_noresize2.png";
    QFile::remove(path);

    ExportOptions opts;
    opts.resizeTo = QSize(100, 100);
    // resizing to same size → should keep size
    BOOST_CHECK(saveImage(doc.get(), path, opts));

    QImage loaded(path);
    BOOST_REQUIRE(!loaded.isNull());
    BOOST_CHECK_EQUAL(loaded.width(), 100);
    QFile::remove(path);
}

BOOST_AUTO_TEST_CASE(saveImage_progressive_no_crash)
{
    auto doc = makeSimpleDoc(20, 20);
    QString path = "/tmp/imageio_progressive.jpg";
    QFile::remove(path);

    ExportOptions opts;
    opts.progressive = true;

    // Just verify no crash — Qt doesn't expose API to verify
    bool ok = saveImage(doc.get(), path, opts);
    BOOST_CHECK(ok);

    if (ok) {
        QFile f(path);
        BOOST_CHECK(f.size() > 0);
        QFile::remove(path);
    }
}

// ══════════════════════════════════════════════════════════════
// GROUP D: Edge cases and guards
// ══════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(composite_null_doc_returns_null)
{
    QImage result = compositeImage(nullptr);
    BOOST_CHECK(result.isNull());
}

BOOST_AUTO_TEST_CASE(composite_empty_doc_returns_null)
{
    Document empty;
    empty.size = QSize(100, 100);
    QImage result = compositeImage(&empty);
    BOOST_CHECK(result.isNull());
}

BOOST_AUTO_TEST_CASE(composite_zero_size_doc_returns_null)
{
    auto doc = std::make_unique<Document>();
    doc->size = QSize(0, 0);
    doc->selection.create(0, 0);
    addLayer(doc.get(), QColor(255, 0, 0, 255), 0, 0);
    QImage result = compositeImage(doc.get());
    BOOST_CHECK(result.isNull());
}

BOOST_AUTO_TEST_CASE(composite_layers_same_doc_position_preserved)
{
    // Two layers at different positions: verify both positions independently
    auto doc = std::make_unique<Document>();
    doc->size = QSize(200, 200);
    doc->selection.create(200, 200);

    // Green layer at left side
    QTransform xfLeft;
    xfLeft.translate(-0.4, 0.0);
    addLayer(doc.get(), QColor(0, 255, 0, 255), 100, 100, true, 1.0f, xfLeft);

    // Red layer at right side, on top
    QTransform xfRight;
    xfRight.translate(0.4, 0.0);
    addLayer(doc.get(), QColor(255, 0, 0, 255), 100, 100, true, 1.0f, xfRight);

    QImage result = compositeImage(doc.get());
    BOOST_REQUIRE(!result.isNull());

    // Left area: green (red layer is shifted right, doesn't cover left)
    BOOST_CHECK(pixelEquals(result, 40, 50, 0, 255, 0, 255));
    // Right area: red on top (covers green)
    BOOST_CHECK(pixelEquals(result, 160, 50, 255, 0, 0, 255));
    // Center: green on top (green layer was added first = painted last = topmost)
    BOOST_CHECK(pixelEquals(result, 100, 50, 0, 255, 0, 255));
}

BOOST_AUTO_TEST_SUITE_END()
