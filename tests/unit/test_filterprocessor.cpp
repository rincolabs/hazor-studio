#define BOOST_TEST_MODULE FilterProcessorTest
#include <boost/test/included/unit_test.hpp>

#include "processing/FilterProcessor.hpp"
#include "core/TileManager.hpp"
#include "engine/ImageEngine.hpp"
#include <QImage>
#include <QColor>

using namespace processing;

static QImage makeOpaqueRgba(int w, int h, uchar r, uchar g, uchar b)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(QColor(r, g, b, 255));
    return img;
}

static QImage makePattern(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(Qt::green);
    // Draw a red rectangle in the center
    for (int y = h/4; y < 3*h/4; ++y) {
        uchar* row = img.scanLine(y);
        for (int x = w/4; x < 3*w/4; ++x) {
            row[x*4 + 0] = 0;   // B
            row[x*4 + 1] = 0;   // G
            row[x*4 + 2] = 255; // R
            row[x*4 + 3] = 255; // A
        }
    }
    return img;
}

BOOST_AUTO_TEST_SUITE(filterprocessor)

BOOST_AUTO_TEST_CASE(isTileable_returns_true_for_brightness)
{
    BOOST_CHECK(FilterProcessor::isTileable("adjust_brightness"));
}

BOOST_AUTO_TEST_CASE(isTileable_returns_false_for_crop)
{
    BOOST_CHECK(!FilterProcessor::isTileable("crop"));
}

BOOST_AUTO_TEST_CASE(isTileable_returns_false_for_rotate)
{
    BOOST_CHECK(!FilterProcessor::isTileable("rotate"));
}

BOOST_AUTO_TEST_CASE(kernelRadius_zero_for_point_filters)
{
    BOOST_CHECK_EQUAL(FilterProcessor::kernelRadius("adjust_brightness", {}), 0);
    BOOST_CHECK_EQUAL(FilterProcessor::kernelRadius("grayscale", {}), 0);
    BOOST_CHECK_EQUAL(FilterProcessor::kernelRadius("invert_colors", {}), 0);
}

BOOST_AUTO_TEST_CASE(kernelRadius_positive_for_blur)
{
    QVariantMap p;
    p["radius"] = 3.0;
    BOOST_CHECK(FilterProcessor::kernelRadius("gaussian_blur", p) > 0);
}

BOOST_AUTO_TEST_CASE(processFull_inverts_colors)
{
    QImage input = makeOpaqueRgba(10, 10, 200, 100, 50);
    QImage result = FilterProcessor::processFull(input, "invert_colors", {});
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK_EQUAL(result.pixelColor(0, 0).red(),   55);   // 255-200
    BOOST_CHECK_EQUAL(result.pixelColor(0, 0).green(), 155);  // 255-100
    BOOST_CHECK_EQUAL(result.pixelColor(0, 0).blue(),  205);  // 255-50
    BOOST_CHECK_EQUAL(result.pixelColor(0, 0).alpha(), 255);
}

BOOST_AUTO_TEST_CASE(processFull_brightness_increases_value)
{
    QImage input = makeOpaqueRgba(10, 10, 100, 100, 100);
    QVariantMap p;
    p["value"] = 0.5;
    QImage result = FilterProcessor::processFull(input, "adjust_brightness", p);
    BOOST_REQUIRE(!result.isNull());
    BOOST_CHECK(result.pixelColor(0, 0).red() > 100);
}

BOOST_AUTO_TEST_CASE(processTiles_processes_only_specified_tiles)
{
    QImage img = makePattern(64, 64);
    int ts = 32;

    core::TileManager tm;
    tm.init(64, 64, ts);

    // Mark all tiles dirty so visibleTiles returns them
    tm.markAllDirty();

    // Get only the first tile (col=0, row=0)
    auto tile = tm.at(0, 0);
    tile.markDirty();

    QVariantMap p;
    p["value"] = 0.5;

    int processed = FilterProcessor::processTiles(img, {&tile}, "adjust_brightness", p);
    BOOST_CHECK_EQUAL(processed, 1);
}

BOOST_AUTO_TEST_CASE(processFull_grayscale)
{
    QImage input = makeOpaqueRgba(8, 8, 200, 100, 50);
    QImage result = FilterProcessor::processFull(input, "grayscale", {});
    BOOST_REQUIRE(!result.isNull());
    auto c = result.pixelColor(0, 0);
    // In grayscale, R=G=B (luminosity)
    BOOST_CHECK(c.red() == c.green());
    BOOST_CHECK(c.green() == c.blue());
    BOOST_CHECK_EQUAL(c.alpha(), 255);
}

BOOST_AUTO_TEST_CASE(processFull_posterize)
{
    QImage input = makeOpaqueRgba(8, 8, 200, 200, 200);
    QVariantMap p;
    p["levels"] = 4;
    QImage result = FilterProcessor::processFull(input, "posterize", p);
    BOOST_REQUIRE(!result.isNull());
    auto c = result.pixelColor(0, 0);
    // With 4 levels, 200 is posterized to 191 (step=63.75, round(200/63.75)=3, 3*63.75=191)
    BOOST_CHECK(c.red() > 150);
    BOOST_CHECK(c.red() < 220);
}

BOOST_AUTO_TEST_CASE(processFull_threshold)
{
    QImage input = makeOpaqueRgba(8, 8, 200, 200, 200);
    QVariantMap p;
    p["value"] = 128.0;
    QImage result = FilterProcessor::processFull(input, "threshold", p);
    BOOST_REQUIRE(!result.isNull());
    auto c = result.pixelColor(0, 0);
    // Value 200 > threshold 128 → white
    BOOST_CHECK(c.red() > 200);
    BOOST_CHECK(c.green() > 200);
    BOOST_CHECK(c.blue() > 200);
}

// ── Fase 11.1: Separable blur ─────────────────────────────────

BOOST_AUTO_TEST_CASE(processFull_gaussianBlur_via_separable_path)
{
    // Create a gradient-like image (white left, black right) so blur changes pixels
    QImage input(64, 64, QImage::Format_RGBA8888);
    for (int y = 0; y < 64; ++y) {
        for (int x = 0; x < 64; ++x) {
            uchar v = static_cast<uchar>((x * 255) / 63);
            uchar* p = input.scanLine(y) + x * 4;
            p[0] = v; p[1] = v; p[2] = v; p[3] = 255;
        }
    }

    QVariantMap p;
    p["radius"] = 5.0; // >2.0 → triggers separable path

    QImage result = FilterProcessor::processFull(input, "gaussian_blur", p);
    BOOST_REQUIRE(!result.isNull());

    // Pixel (32,0) should be blurred (mixed with neighbors)
    auto res = result.pixelColor(32, 0);
    BOOST_CHECK(res.red() > 0 && res.red() < 255);
    // Alpha should be preserved
    BOOST_CHECK_EQUAL(res.alpha(), 255);
}

BOOST_AUTO_TEST_CASE(processTiles_gaussianBlur_with_separable_path)
{
    QImage img = makePattern(64, 64);
    int ts = 32;

    core::TileManager tm;
    tm.init(64, 64, ts);
    tm.markAllDirty();

    // Get all tiles
    std::vector<core::Tile*> tiles;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c)
            tiles.push_back(&tm.at(r, c));

    QVariantMap p;
    p["radius"] = 5.0;

    // The point filter path (radius==0) is separate from neighborhood (radius>0)
    // Kernel radius for blur with radius 5: ksize=11, return ksize/2+1=6
    BOOST_CHECK(FilterProcessor::kernelRadius("gaussian_blur", p) > 0);

    int processed = FilterProcessor::processTiles(img, tiles, "gaussian_blur", p);
    BOOST_CHECK_EQUAL(processed, 4); // all 4 tiles should be processed
}

// ── Fase 11.3: Chain processing ────────────────────────────────

BOOST_AUTO_TEST_CASE(processBatch_applies_chain_in_order)
{
    QImage input = makeOpaqueRgba(16, 16, 200, 100, 50);

    // Chain: grayscale then invert
    std::vector<std::pair<std::string, QVariantMap>> chain;
    chain.emplace_back("grayscale", QVariantMap());
    chain.emplace_back("invert_colors", QVariantMap());

    QImage result = FilterProcessor::processBatch(input, chain);
    BOOST_REQUIRE(!result.isNull());

    // Apply individually to verify: grayscale then invert
    QImage grayStep = FilterProcessor::processFull(input, "grayscale", {});
    QImage expected = FilterProcessor::processFull(grayStep, "invert_colors", {});

    BOOST_REQUIRE(!expected.isNull());
    auto r = result.pixelColor(0, 0);
    auto e = expected.pixelColor(0, 0);
    BOOST_CHECK_EQUAL(r.red(), e.red());
    BOOST_CHECK_EQUAL(r.green(), e.green());
    BOOST_CHECK_EQUAL(r.blue(), e.blue());
    BOOST_CHECK_EQUAL(r.alpha(), e.alpha());
}

BOOST_AUTO_TEST_CASE(processBatch_empty_chain_returns_empty)
{
    QImage input = makeOpaqueRgba(8, 8, 100, 100, 100);
    std::vector<std::pair<std::string, QVariantMap>> chain;
    QImage result = FilterProcessor::processBatch(input, chain);
    BOOST_CHECK(result.isNull());
}

BOOST_AUTO_TEST_CASE(processBatch_single_tool_equals_processFull)
{
    QImage input = makeOpaqueRgba(8, 8, 200, 100, 50);
    QVariantMap p;
    p["value"] = 0.3;

    std::vector<std::pair<std::string, QVariantMap>> chain;
    chain.emplace_back("adjust_brightness", p);

    QImage batchResult = FilterProcessor::processBatch(input, chain);
    QImage fullResult = FilterProcessor::processFull(input, "adjust_brightness", p);

    BOOST_REQUIRE(!batchResult.isNull());
    BOOST_REQUIRE(!fullResult.isNull());
    BOOST_CHECK_EQUAL(batchResult.pixelColor(0, 0).red(),
                      fullResult.pixelColor(0, 0).red());
}

BOOST_AUTO_TEST_CASE(processBatchTiles_applies_chain_per_tile)
{
    QImage img = makePattern(64, 64);
    int ts = 32;

    core::TileManager tm;
    tm.init(64, 64, ts);
    tm.markAllDirty();

    std::vector<core::Tile*> tiles;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 2; ++c)
            tiles.push_back(&tm.at(r, c));

    std::vector<std::pair<std::string, QVariantMap>> chain;
    chain.emplace_back("adjust_brightness", QVariantMap{{"value", 0.3}});
    chain.emplace_back("invert_colors", QVariantMap());

    int processed = FilterProcessor::processBatchTiles(img, tiles, chain);
    BOOST_CHECK_EQUAL(processed, 4);
}

BOOST_AUTO_TEST_SUITE_END()
