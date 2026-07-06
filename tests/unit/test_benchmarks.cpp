#define BOOST_TEST_MODULE PerformanceBenchmarks
#include <boost/test/included/unit_test.hpp>

#include "processing/FilterProcessor.hpp"
#include "engine/ImageEngine.hpp"
#include "core/TileManager.hpp"

#include <QImage>
#include <QElapsedTimer>
#include <QDebug>

using namespace processing;

// ── Helpers ────────────────────────────────────────────────────

static QImage makeGradient(int w, int h)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            uchar r = static_cast<uchar>((x * 255) / (w - 1));
            uchar g = static_cast<uchar>((y * 255) / (h - 1));
            uchar b = static_cast<uchar>(((x + y) * 255) / (w + h - 2));
            uchar* p = img.scanLine(y) + x * 4;
            p[0] = r; p[1] = g; p[2] = b; p[3] = 255;
        }
    }
    return img;
}

// Print timing info for manual inspection
#define BENCH(name, expr, expectedMs) do { \
    QElapsedTimer __t; __t.start(); \
    expr; \
    qreal __elapsed = __t.nsecsElapsed() / 1000000.0; \
    qDebug().noquote() \
        << QString("  %1: %2 ms (threshold: %3 ms)") \
               .arg(name, -50) \
               .arg(__elapsed, 8, 'f', 2) \
               .arg(expectedMs); \
    BOOST_CHECK_LE(__elapsed, expectedMs); \
} while(0)

BOOST_AUTO_TEST_SUITE(benchmarks)

// ── Bridge conversion timing (QImage↔cv::Mat) ────────────────

BOOST_AUTO_TEST_CASE(bridge_conversion_4k)
{
    int size = 4096;
    QImage img = makeGradient(size, size);
    BOOST_REQUIRE(!img.isNull());

    cv::Mat mat;
    BENCH("toCvMatFast(4096x4096, RGBA→BGRA)",
          mat = ImageEngine::toCvMatFast(img),
          300.0);

    mat = ImageEngine::toCvMatFast(img);

    QImage result;
    BENCH("toQImageFast(4096x4096, BGRA→RGBA)",
          result = ImageEngine::toQImageFast(mat),
          300.0);

    QImage rt;
    BENCH("Round-trip RGBA→BGRA→RGBA (4096x4096)",
          rt = ImageEngine::toQImageFast(ImageEngine::toCvMatFast(img)),
          500.0);
}

// ── Gaussian blur timing (separable path, radius 20) ─────────

BOOST_AUTO_TEST_CASE(gaussian_blur_4k_separable)
{
    int size = 4096;
    QImage img = makeGradient(size, size);
    BOOST_REQUIRE(!img.isNull());

    QVariantMap p;
    p["radius"] = 20.0;

    QImage result;
    BENCH("gaussian_blur(radius=20, 4096x4096, separable)",
          result = FilterProcessor::processFull(img, "gaussian_blur", p),
          5000.0);
}

// ── Separable vs regular blur (comparison) ────────────────────

BOOST_AUTO_TEST_CASE(separable_faster_than_regular)
{
    int size = 2048;
    QImage img = makeGradient(size, size);
    BOOST_REQUIRE(!img.isNull());

    QVariantMap largeBlur{{"radius", 15.0}};
    QVariantMap smallBlur{{"radius", 1.0}};

    QElapsedTimer t;

    t.start();
    QImage reg = FilterProcessor::processFull(img, "gaussian_blur", smallBlur);
    qint64 regularNs = t.nsecsElapsed();
    BOOST_REQUIRE(!reg.isNull());

    t.start();
    QImage sep = FilterProcessor::processFull(img, "gaussian_blur", largeBlur);
    qint64 separableNs = t.nsecsElapsed();
    BOOST_REQUIRE(!sep.isNull());

    qDebug().noquote()
        << QString("  Regular (radius=1): %1 ms, Separable (radius=15): %2 ms")
               .arg(regularNs / 1000000.0, 8, 'f', 2)
               .arg(separableNs / 1000000.0, 8, 'f', 2);
}

// ── Chain batching vs individual (3 filters) ─────────────────

BOOST_AUTO_TEST_CASE(chain_batching_faster_than_individual)
{
    int size = 1024;
    QImage img = makeGradient(size, size);
    BOOST_REQUIRE(!img.isNull());

    std::vector<std::pair<std::string, QVariantMap>> chain;
    chain.emplace_back("adjust_brightness", QVariantMap{{"value", 0.3}});
    chain.emplace_back("invert_colors", QVariantMap());
    chain.emplace_back("adjust_contrast", QVariantMap{{"value", 0.2}});

    QElapsedTimer t;

    t.start();
    QImage s1 = FilterProcessor::processFull(img, "adjust_brightness",
                                              QVariantMap{{"value", 0.3}});
    QImage s2 = FilterProcessor::processFull(s1, "invert_colors", {});
    QImage indResult = FilterProcessor::processFull(s2, "adjust_contrast",
                                                     QVariantMap{{"value", 0.2}});
    qint64 individualNs = t.nsecsElapsed();
    BOOST_REQUIRE(!indResult.isNull());

    t.start();
    QImage batchResult = FilterProcessor::processBatch(img, chain);
    qint64 batchNs = t.nsecsElapsed();
    BOOST_REQUIRE(!batchResult.isNull());

    double speedup = static_cast<double>(individualNs) / batchNs;
    qDebug().noquote()
        << QString("  Individual: %1 ms, Batch: %2 ms (%3x)")
               .arg(individualNs / 1000000.0, 8, 'f', 2)
               .arg(batchNs / 1000000.0, 8, 'f', 2)
               .arg(speedup, 5, 'f', 2);

    BOOST_CHECK_GE(speedup, 1.35);
}

// ── Tile processing timing (256 tiles, gaussian blur) ────────

BOOST_AUTO_TEST_CASE(tile_processing_4k_blur)
{
    int size = 4096;
    QImage img = makeGradient(size, size);
    BOOST_REQUIRE(!img.isNull());

    core::TileManager tm;
    tm.init(size, size, 256);
    tm.markAllDirty();

    std::vector<core::Tile*> allTiles;
    for (int r = 0; r < size / 256; ++r)
        for (int c = 0; c < size / 256; ++c)
            allTiles.push_back(&tm.at(r, c));

    BOOST_REQUIRE_EQUAL(allTiles.size(), 256);

    QVariantMap p{{"radius", 5.0}};

    int processed = 0;
    BENCH("processTiles(256 tiles, 4096x4096, gaussian_blur radius=5)",
          processed = FilterProcessor::processTiles(img, allTiles, "gaussian_blur", p),
          10000.0);

    BOOST_CHECK_EQUAL(processed, 256);
}

BOOST_AUTO_TEST_SUITE_END()
