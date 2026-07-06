#define BOOST_TEST_MODULE CopyPasteTest
#include <boost/test/included/unit_test.hpp>

#include "core/Clipboard.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "engine/ImageEngine.hpp"

#include <QImage>
#include <QColor>

struct CopyPasteFixture {
    Document doc;
    ImageController ctrl;

    CopyPasteFixture()
        : ctrl()
    {
        doc.size = QSize(800, 600);
        doc.selection.create(800, 600);
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    ~CopyPasteFixture()
    {
        ClipboardManager::instance().clear();
    }

    LayerTreeNode* activeNode() { return doc.activeNode(); }
    Layer* activeLayer() { return doc.activeLayer(); }
    int flatCount() const { return doc.flatCount(); }

    void fillActiveLayer(uchar r, uchar g, uchar b)
    {
        auto* layer = activeLayer();
        if (layer)
            layer->cpuImage.fill(QColor(r, g, b, 255));
    }

    void addSecondLayer()
    {
        ctrl.newLayer();
    }
};

BOOST_AUTO_TEST_SUITE(copypaste)

BOOST_AUTO_TEST_CASE(copy_layer_sets_clipboard_type_layer)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.ctrl.copy();
    BOOST_CHECK(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Layer);
    BOOST_REQUIRE(ClipboardManager::instance().data().node != nullptr);
    BOOST_CHECK(ClipboardManager::instance().data().node->type == LayerTreeNode::Type::Layer);
}

BOOST_AUTO_TEST_CASE(copy_then_paste_creates_new_node)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    int beforeCount = f.flatCount();
    f.ctrl.copy();
    f.ctrl.paste();
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount + 1);
}

BOOST_AUTO_TEST_CASE(pasted_layer_is_above_active)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(255, 0, 0);
    f.addSecondLayer();
    f.fillActiveLayer(0, 255, 0);

    BOOST_CHECK_EQUAL(f.flatCount(), 2);
    int prevActiveIndex = f.doc.activeFlatIndex;

    f.ctrl.copy();
    f.ctrl.paste();

    BOOST_CHECK_EQUAL(f.flatCount(), 3);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 0);
}

BOOST_AUTO_TEST_CASE(paste_preserves_pixel_data)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(120, 130, 140);
    QImage before = f.activeLayer()->cpuImage.copy();

    f.ctrl.copy();
    f.ctrl.paste();

    auto* pastedLayer = f.activeLayer();
    BOOST_REQUIRE(pastedLayer != nullptr);
    BOOST_REQUIRE(!pastedLayer->cpuImage.isNull());

    BOOST_CHECK_EQUAL(pastedLayer->cpuImage.width(), before.width());
    BOOST_CHECK_EQUAL(pastedLayer->cpuImage.height(), before.height());
    BOOST_CHECK_EQUAL(pastedLayer->cpuImage.pixelColor(0, 0).red(), 120);
    BOOST_CHECK_EQUAL(pastedLayer->cpuImage.pixelColor(0, 0).green(), 130);
    BOOST_CHECK_EQUAL(pastedLayer->cpuImage.pixelColor(0, 0).blue(), 140);
}

BOOST_AUTO_TEST_CASE(paste_deep_copy_is_independent)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(200, 100, 50);
    f.ctrl.copy();
    f.ctrl.paste();

    auto* original = f.doc.layerAtFlat(1);
    auto* pasted = f.activeLayer();
    BOOST_REQUIRE(original != nullptr);
    BOOST_REQUIRE(pasted != nullptr);

    BOOST_CHECK_EQUAL(original->cpuImage.pixelColor(0, 0).red(), 200);
    BOOST_CHECK_EQUAL(pasted->cpuImage.pixelColor(0, 0).red(), 200);

    pasted->cpuImage.fill(Qt::black);
    BOOST_CHECK_EQUAL(original->cpuImage.pixelColor(0, 0).red(), 200);
}

BOOST_AUTO_TEST_CASE(paste_offset_accumulates)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(100, 100, 100);
    f.ctrl.copy();

    float t1_before = f.activeNode()->transform.m31();
    f.ctrl.paste();
    float t1_after = f.activeNode()->transform.m31();

    f.ctrl.paste();
    float t2_after = f.activeNode()->transform.m31();
    f.ctrl.paste();
    float t3_after = f.activeNode()->transform.m31();

    float offset1 = t1_after - t1_before;
    float offset2 = t2_after - t1_after;
    float offset3 = t3_after - t2_after;

    BOOST_CHECK(offset1 > 0.0f);
    BOOST_CHECK(offset2 > 0.0f);
    BOOST_CHECK(offset3 > 0.0f);
}

BOOST_AUTO_TEST_CASE(paste_offset_resets_on_copy)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.ctrl.copy();
    f.ctrl.paste();
    float afterFirstPaste = f.activeNode()->transform.m31();
    float offset1 = afterFirstPaste;

    f.ctrl.paste();
    float afterSecondPaste = f.activeNode()->transform.m31();
    float offset2 = afterSecondPaste;

    f.ctrl.copy();
    f.ctrl.paste();
    float afterCopyReset = f.activeNode()->transform.m31();
    float offset3 = afterCopyReset;

    BOOST_CHECK(std::abs(offset2 - offset1) > 0.0f);
    BOOST_CHECK(std::abs(offset3 - offset1) > 0.0f);
}

BOOST_AUTO_TEST_CASE(copy_with_selection_sets_pixels_type)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(255, 0, 0);
    f.doc.selection.setRect(QRectF(50, 50, 100, 80), SelectMode::Replace);
    f.doc.selection.setActive(true);

    f.ctrl.copy();

    BOOST_CHECK(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Pixels);
    BOOST_REQUIRE(!ClipboardManager::instance().data().pixels.isNull());
    BOOST_CHECK(ClipboardManager::instance().data().pixels.width() <= 105);
    BOOST_CHECK(ClipboardManager::instance().data().pixels.height() <= 85);
    BOOST_CHECK(ClipboardManager::instance().data().pixels.width() >= 95);
    BOOST_CHECK(ClipboardManager::instance().data().pixels.height() >= 75);
}

BOOST_AUTO_TEST_CASE(paste_pixels_creates_raster_layer)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(255, 0, 0);
    f.doc.selection.setRect(QRectF(50, 50, 100, 80), SelectMode::Replace);
    f.doc.selection.setActive(true);
    f.ctrl.copy();

    int beforeCount = f.flatCount();
    f.ctrl.paste();

    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount + 1);
    auto* pasted = f.activeLayer();
    BOOST_REQUIRE(pasted != nullptr);
    BOOST_CHECK(!pasted->cpuImage.isNull());
}

BOOST_AUTO_TEST_CASE(copy_with_no_selection_and_no_active_layer_does_nothing)
{
    Document emptyDoc;
    ImageController ctrl;
    emptyDoc.size = QSize(800, 600);
    emptyDoc.selection.create(800, 600);
    ctrl.setDocument(&emptyDoc);
    ClipboardManager::instance().clear();

    BOOST_CHECK_EQUAL(emptyDoc.flatCount(), 0);
    ctrl.copy();
    BOOST_CHECK(!ClipboardManager::instance().hasData());
}

BOOST_AUTO_TEST_CASE(paste_with_empty_clipboard_does_nothing)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    int beforeCount = f.flatCount();
    f.ctrl.paste();
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount);
}

BOOST_AUTO_TEST_CASE(copy_group_sets_clipboard_type_group)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.ctrl.newGroup();
    BOOST_CHECK(f.activeNode()->type == LayerTreeNode::Type::Group);

    f.ctrl.copy();
    BOOST_CHECK(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Group);
    BOOST_REQUIRE(ClipboardManager::instance().data().node != nullptr);
    BOOST_CHECK(ClipboardManager::instance().data().node->type == LayerTreeNode::Type::Group);
}

BOOST_AUTO_TEST_CASE(paste_group_preserves_children)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.ctrl.newGroup();
    // flat = [group], activeFlatIndex = 0

    int groupIdx = f.doc.activeFlatIndex;

    f.ctrl.newLayer();
    // flat = [layer, group], activeFlatIndex = 0
    // group is now at flat index 1

    f.fillActiveLayer(100, 100, 200);

    f.ctrl.moveNodeIntoGroup(0, 1);
    // layer moves into group, flat = [group, layer]
    // activeFlatIndex was set to groupFlatIndex (1) -> layer

    f.doc.activeFlatIndex = 0; // select the group
    f.ctrl.copy();

    int beforeCount = f.flatCount();
    f.ctrl.paste();

    BOOST_CHECK(f.flatCount() > beforeCount);
    auto* pastedGroup = f.activeNode();
    BOOST_REQUIRE(pastedGroup != nullptr);
    BOOST_CHECK(pastedGroup->type == LayerTreeNode::Type::Group);
    BOOST_CHECK(pastedGroup->children.size() >= 1);
}

BOOST_AUTO_TEST_CASE(cross_document_paste_preserves_pixel_data)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(200, 150, 100);
    f.ctrl.copy();

    Document destDoc;
    destDoc.size = QSize(400, 300);
    destDoc.selection.create(400, 300);
    ImageController destCtrl;
    destCtrl.setDocument(&destDoc);
    destCtrl.newLayer();

    int beforeCount = destDoc.flatCount();
    destCtrl.paste();

    BOOST_CHECK_EQUAL(destDoc.flatCount(), beforeCount + 1);
    auto* pasted = destDoc.activeLayer();
    BOOST_REQUIRE(pasted != nullptr);
    BOOST_CHECK(!pasted->cpuImage.isNull());
    BOOST_CHECK_EQUAL(pasted->cpuImage.pixelColor(0, 0).red(), 200);
    BOOST_CHECK_EQUAL(pasted->cpuImage.pixelColor(0, 0).green(), 150);
    BOOST_CHECK_EQUAL(pasted->cpuImage.pixelColor(0, 0).blue(), 100);
}

BOOST_AUTO_TEST_CASE(composite_layers_alpha_blend)
{
    int w = 50, h = 50;
    cv::Mat red(h, w, CV_8UC4, cv::Scalar(0, 0, 255, 255));
    cv::Mat blue(h, w, CV_8UC4, cv::Scalar(255, 0, 0, 255));

    std::vector<cv::Mat> layers = {red}; // single opaque layer
    std::vector<float> ops = {1.0f};
    std::vector<bool> vis = {true};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(w, h));

    BOOST_REQUIRE(!result.empty());
    BOOST_CHECK_EQUAL(result.channels(), 4);

    cv::Vec4b pixel = result.at<cv::Vec4b>(0, 0);
    BOOST_CHECK_EQUAL((int)pixel[3], 255);
    BOOST_CHECK(pixel[2] > pixel[0]); // R > B
}

BOOST_AUTO_TEST_CASE(composite_layers_with_transparency)
{
    int w = 20, h = 20;
    cv::Mat opaqueRed(h, w, CV_8UC4, cv::Scalar(0, 0, 255, 255));   // B=0, R=255
    cv::Mat semiBlue(h, w, CV_8UC4, cv::Scalar(255, 0, 0, 128));    // B=255, R=0

    // composite: opaque red first, then semi-transparent blue on top
    std::vector<cv::Mat> layers = {opaqueRed, semiBlue};
    std::vector<float> ops = {1.0f, 1.0f};
    std::vector<bool> vis = {true, true};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(w, h));

    BOOST_REQUIRE(!result.empty());
    cv::Vec4b pixel = result.at<cv::Vec4b>(0, 0);

    // blue (B=255) on top of red (R=255) with alpha 0.5
    // result B should be ~128 (mix of red and blue)
    // result R should be ~128
    // both channels should be > 0 and < 255
    BOOST_CHECK((int)pixel[0] > 0);   // some blue shows through
    BOOST_CHECK((int)pixel[2] > 0);   // some red still visible
    BOOST_CHECK((int)pixel[0] < 255); // not fully blue
    BOOST_CHECK((int)pixel[2] < 255); // not fully red
}

BOOST_AUTO_TEST_CASE(composite_layers_additive_not_broken)
{
    int w = 10, h = 10;
    cv::Mat white(h, w, CV_8UC4, cv::Scalar(255, 255, 255, 255));
    cv::Mat white2(h, w, CV_8UC4, cv::Scalar(255, 255, 255, 255));

    std::vector<cv::Mat> layers = {white, white2};
    std::vector<float> ops = {1.0f, 1.0f};
    std::vector<bool> vis = {true, true};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(w, h));

    BOOST_REQUIRE(!result.empty());
    cv::Vec4b pixel = result.at<cv::Vec4b>(0, 0);

    BOOST_CHECK((int)pixel[2] <= 255);
}

BOOST_AUTO_TEST_CASE(composite_layers_partial_visibility)
{
    int w = 10, h = 10;
    cv::Mat red(h, w, CV_8UC4, cv::Scalar(0, 0, 255, 255));

    std::vector<cv::Mat> layers = {red};
    std::vector<float> ops = {1.0f};
    std::vector<bool> vis = {false};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(w, h));

    BOOST_REQUIRE(!result.empty());
    cv::Vec4b pixel = result.at<cv::Vec4b>(0, 0);
    BOOST_CHECK_EQUAL((int)pixel[3], 0);
}

BOOST_AUTO_TEST_CASE(composite_layers_opacity)
{
    int w = 10, h = 10;
    cv::Mat red(h, w, CV_8UC4, cv::Scalar(0, 0, 255, 255));

    std::vector<cv::Mat> layers = {red};
    std::vector<float> ops = {0.5f};
    std::vector<bool> vis = {true};

    cv::Mat result = ImageEngine::compositeLayers(layers, ops, vis, QSize(w, h));

    BOOST_REQUIRE(!result.empty());
    cv::Vec4b pixel = result.at<cv::Vec4b>(0, 0);
    BOOST_CHECK(pixel[3] < 255);
    BOOST_CHECK(pixel[3] > 0);
}

// ── Copy with selection — active layer only ───────────────────

BOOST_AUTO_TEST_CASE(copy_with_selection_uses_active_layer_not_composite)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    // Layer 1 (top): red
    f.fillActiveLayer(255, 0, 0);
    // Layer 2: green
    f.addSecondLayer();
    f.fillActiveLayer(0, 255, 0);
    // Layer 3 (bottom): blue (fixture creates one, addSecondLayer creates another)
    f.addSecondLayer();
    f.fillActiveLayer(0, 0, 255);

    BOOST_REQUIRE_EQUAL(f.flatCount(), 3);

    // Select the middle layer (index 1, green)
    f.doc.activeFlatIndex = 1;
    f.doc.selection.setRect(QRectF(0, 0, 50, 50), SelectMode::Replace);
    f.doc.selection.setActive(true);

    f.ctrl.copy();

    BOOST_REQUIRE(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Pixels);
    BOOST_REQUIRE(!ClipboardManager::instance().data().pixels.isNull());

    // Should be green (from the active layer), not red or blue
    QRgb centerPixel = ClipboardManager::instance().data().pixels.pixelColor(5, 5).rgb();
    int r = qRed(centerPixel);
    int g = qGreen(centerPixel);
    int b = qBlue(centerPixel);
    BOOST_CHECK(r < 50);  // not red
    BOOST_CHECK(g > 200); // is green
    BOOST_CHECK(b < 50);  // not blue
}

BOOST_AUTO_TEST_CASE(copy_without_selection_copies_active_node)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(200, 50, 50);
    f.addSecondLayer();
    f.fillActiveLayer(50, 200, 50);

    // flat = [Layer2(green, idx0), Layer1(red, idx1)]
    // Select the bottom layer (index 1)
    BOOST_REQUIRE_EQUAL(f.flatCount(), 2);
    f.doc.activeFlatIndex = 1;

    f.ctrl.copy();

    BOOST_REQUIRE(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Layer);
    BOOST_REQUIRE(ClipboardManager::instance().data().node != nullptr);
    // Layer at index 1 is the first layer created ("Layer 1")
    BOOST_CHECK(ClipboardManager::instance().data().node->name.contains("Layer 1"));
}

BOOST_AUTO_TEST_CASE(paste_with_deselect_goes_to_top)
{
    CopyPasteFixture f;
    ClipboardManager::instance().clear();

    f.fillActiveLayer(100, 100, 100);
    f.addSecondLayer();
    f.fillActiveLayer(200, 200, 200);
    BOOST_REQUIRE_EQUAL(f.flatCount(), 2);

    // Copy while layer is selected (topmost = Layer2 at index 0)
    f.ctrl.copy();
    // Now deselect
    f.doc.activeFlatIndex = -1;
    f.ctrl.paste();

    // Pasted layer should be at index 0 (topmost), and should be the active layer
    BOOST_CHECK_EQUAL(f.flatCount(), 3);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 0);
    auto* topLayer = f.doc.layerAtFlat(0);
    BOOST_REQUIRE(topLayer != nullptr);
    BOOST_CHECK(topLayer->cpuImage.pixelColor(0, 0).red() == 200);
}

BOOST_AUTO_TEST_CASE(copy_with_selection_and_no_active_layer_composites_all)
{
    Document doc;
    ImageController ctrl;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);
    ctrl.setDocument(&doc);

    // Create two layers with different colors
    ctrl.newLayer();
    auto* l1 = doc.activeLayer();
    BOOST_REQUIRE(l1 != nullptr);
    l1->cpuImage.fill(QColor(255, 0, 0, 255));

    ctrl.newLayer();
    auto* l2 = doc.activeLayer();
    BOOST_REQUIRE(l2 != nullptr);
    l2->cpuImage.fill(QColor(0, 255, 0, 255));

    // Deselect active, create selection
    doc.activeFlatIndex = -1;
    doc.selection.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    doc.selection.setActive(true);

    ClipboardManager::instance().clear();
    ctrl.copy();

    BOOST_REQUIRE(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Pixels);
    BOOST_REQUIRE(!ClipboardManager::instance().data().pixels.isNull());

    // With deselect, we composite all layers (green on top of red)
    // The topmost layer is green (index 0 since it was created last)
    QRgb pixel = ClipboardManager::instance().data().pixels.pixelColor(5, 5).rgb();
    // Top layer = green, so green channel should dominate
    BOOST_CHECK(qGreen(pixel) > 200);
}

// ── copy with selection + layer transform ─────────────────────

BOOST_AUTO_TEST_CASE(copy_selection_respects_layer_transform)
{
    CopyPasteFixture f;

    // Fill active layer with a known color
    f.fillActiveLayer(200, 100, 50);
    f.activeNode()->transform.translate(0.5, 0.0);  // move layer right

    // Create selection on the RIGHT half of the document
    f.doc.selection.setRect(QRectF(400, 0, 400, 600), SelectMode::Replace);
    f.doc.selection.setActive(true);

    ClipboardManager::instance().clear();
    f.ctrl.copy();

    // Should have copied pixel data
    BOOST_REQUIRE(ClipboardManager::instance().hasData());
    BOOST_CHECK(ClipboardManager::instance().data().type == ClipboardType::Pixels);
    BOOST_REQUIRE(!ClipboardManager::instance().data().pixels.isNull());

    // The layer was moved right, so its content is on the right side.
    // The selection is also on the right side. They should overlap.
    // The copied pixels should have the fill color (200, 100, 50).
    QRgb pixel = ClipboardManager::instance().data().pixels.pixelColor(0, 0).rgb();
    BOOST_CHECK(qRed(pixel) > 150);

    ClipboardManager::instance().clear();
}



BOOST_AUTO_TEST_SUITE_END()
