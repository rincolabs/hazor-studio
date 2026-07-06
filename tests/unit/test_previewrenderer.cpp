#define BOOST_TEST_MODULE PreviewRendererTest
#include <boost/test/included/unit_test.hpp>

#include "processing/PreviewRenderer.hpp"
#include <QApplication>
#include <QSignalSpy>
#include <QImage>
#include <QColor>
#include <thread>
#include <chrono>

// Global QApplication fixture (required for signal dispatch)
struct QtAppFixture {
    QtAppFixture() {
        static int argc = 1;
        static char* argv[] = {const_cast<char*>("test")};
        static QApplication app(argc, argv);
    }
};
BOOST_GLOBAL_FIXTURE(QtAppFixture);

BOOST_AUTO_TEST_SUITE(preview_renderer)

BOOST_AUTO_TEST_CASE(initial_not_busy)
{
    processing::PreviewRenderer pr;
    BOOST_CHECK(!pr.isBusy());
}

BOOST_AUTO_TEST_CASE(generate_preview_emits_signal)
{
    processing::PreviewRenderer pr;
    QSignalSpy spy(&pr, SIGNAL(previewReady(QImage)));

    QImage source(2000, 2000, QImage::Format_RGBA8888);
    source.fill(Qt::white);

    pr.generatePreview(source, "grayscale", {}, QSize(512, 512));

    // Wait for async result (up to 5s)
    bool received = spy.wait(5000);
    BOOST_CHECK(received);
    if (received) {
        QImage preview = spy.at(0).at(0).value<QImage>();
        BOOST_CHECK(!preview.isNull());
    }
}

BOOST_AUTO_TEST_CASE(preview_respects_target_size)
{
    processing::PreviewRenderer pr;
    QSignalSpy spy(&pr, SIGNAL(previewReady(QImage)));

    QImage source(2000, 1500, QImage::Format_RGBA8888);
    source.fill(Qt::white);

    pr.generatePreview(source, "grayscale", {}, QSize(512, 512));

    BOOST_REQUIRE(spy.wait(5000));
    QImage preview = spy.at(0).at(0).value<QImage>();
    BOOST_CHECK(!preview.isNull());
    BOOST_CHECK_LE(preview.width(), 512);
    BOOST_CHECK_LE(preview.height(), 512);
}

BOOST_AUTO_TEST_CASE(preview_preserves_aspect_ratio)
{
    processing::PreviewRenderer pr;
    QSignalSpy spy(&pr, SIGNAL(previewReady(QImage)));

    // Wide source 2:1
    QImage source(800, 400, QImage::Format_RGBA8888);
    source.fill(Qt::white);

    pr.generatePreview(source, "grayscale", {}, QSize(512, 512));

    BOOST_REQUIRE(spy.wait(5000));
    QImage preview = spy.at(0).at(0).value<QImage>();
    BOOST_CHECK(!preview.isNull());
    // Aspect ratio should be preserved (2:1)
    float ratio = static_cast<float>(preview.width()) / preview.height();
    BOOST_CHECK_CLOSE(ratio, 2.0f, 5.0f);
}

BOOST_AUTO_TEST_CASE(cancel_sets_not_busy)
{
    processing::PreviewRenderer pr;
    QImage source(4000, 4000, QImage::Format_RGBA8888);
    source.fill(Qt::white);

    pr.generatePreview(source, "gaussian_blur", {{"radius", 20}}, QSize(512, 512));
    pr.cancel();
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    BOOST_CHECK(!pr.isBusy());
}

BOOST_AUTO_TEST_CASE(null_source_emits_empty)
{
    processing::PreviewRenderer pr;
    QSignalSpy spy(&pr, SIGNAL(previewReady(QImage)));

    pr.generatePreview(QImage(), "grayscale", {}, QSize(512, 512));

    // null source emits previewReady({}) synchronously
    BOOST_CHECK_EQUAL(spy.count(), 1);
    QImage preview = spy.at(0).at(0).value<QImage>();
    BOOST_CHECK(preview.isNull());
}

BOOST_AUTO_TEST_CASE(small_source_not_downscaled)
{
    processing::PreviewRenderer pr;
    QSignalSpy spy(&pr, SIGNAL(previewReady(QImage)));

    QImage source(256, 256, QImage::Format_RGBA8888);
    source.fill(Qt::white);

    pr.generatePreview(source, "grayscale", {}, QSize(512, 512));

    // Small source (<= target size) is not downscaled
    BOOST_REQUIRE(spy.wait(5000));
    QImage preview = spy.at(0).at(0).value<QImage>();
    BOOST_CHECK(!preview.isNull());
    BOOST_CHECK(preview.width() <= 256);
}

BOOST_AUTO_TEST_CASE(unknown_tool_fallback)
{
    processing::PreviewRenderer pr;
    QSignalSpy spy(&pr, SIGNAL(previewReady(QImage)));

    QImage source(128, 128, QImage::Format_RGBA8888);
    source.fill(Qt::white);

    // Unknown tool -> falls back to FilterProcessor::processFull
    pr.generatePreview(source, "nonexistent_tool", {}, QSize(512, 512));

    BOOST_REQUIRE(spy.wait(5000));
    QImage preview = spy.at(0).at(0).value<QImage>();
    BOOST_CHECK(!preview.isNull());
}

BOOST_AUTO_TEST_CASE(blur_filter_alters_image)
{
    processing::PreviewRenderer pr;
    QSignalSpy spy(&pr, SIGNAL(previewReady(QImage)));

    QImage source(256, 256, QImage::Format_RGBA8888);
    source.fill(Qt::white);
    // Add a distinct pixel
    source.setPixelColor(128, 128, QColor(255, 0, 0));

    pr.generatePreview(source, "gaussian_blur", {{"radius", 10.0}}, QSize(512, 512));

    BOOST_REQUIRE(spy.wait(5000));
    QImage preview = spy.at(0).at(0).value<QImage>();
    BOOST_CHECK(!preview.isNull());
}

BOOST_AUTO_TEST_SUITE_END()
