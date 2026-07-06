#define BOOST_TEST_MODULE AsyncProgressiveTest
#include <boost/test/included/unit_test.hpp>

#include <QApplication>
#include <QSignalSpy>
#include <chrono>
#include <thread>

#include "async/AsyncJobSystem.hpp"
#include "async/AsyncJob.hpp"
#include "core/Document.hpp"
#include "controller/ImageController.hpp"

struct QtAppFixture {
    QtAppFixture() {
        static int argc = 1;
        static char* argv[] = { const_cast<char*>("test") };
        static QApplication app(argc, argv);
    }
};
BOOST_GLOBAL_FIXTURE(QtAppFixture);

struct AsyncProgFixture {
    Document doc;
    ImageController ctrl;

    AsyncProgFixture() : ctrl() {
        doc.size = QSize(256, 256);
        doc.selection.create(256, 256);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    ~AsyncProgFixture() {
        if (AsyncJobSystem::instance()) {
            AsyncJobSystem::instance()->cancelAll();
            AsyncJobSystem::destroy();
        }
    }

    Layer* active() { return doc.activeLayer(); }
};

BOOST_AUTO_TEST_SUITE(async_progressive)

BOOST_AUTO_TEST_CASE(enqueue_job_and_check_pending)
{
    AsyncProgFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);
    layer->cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer->cpuImage.fill(QColor(100, 150, 200));

    AsyncJobSystem::create();
    auto* aj = AsyncJobSystem::instance();
    BOOST_REQUIRE(aj);

    // Enqueue a job directly
    auto job = std::make_shared<AsyncJob>();
    job->type = AsyncJobType::FilterApply;
    job->toolName = "gaussian_blur";
    job->params = {{"radius", 5.0}};
    job->sourceImage = layer->cpuImage.copy();
    job->weakLayer = layer;

    uint64_t id = aj->enqueue(job);
    BOOST_CHECK(id > 0);
    BOOST_CHECK_EQUAL(aj->pendingCount(), 1);

    aj->cancelAll();
    AsyncJobSystem::destroy();
}

BOOST_AUTO_TEST_CASE(progressive_job_emits_signal)
{
    AsyncProgFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);
    layer->cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer->cpuImage.fill(QColor(100, 150, 200));

    AsyncJobSystem::create();
    auto* aj = AsyncJobSystem::instance();
    QSignalSpy spy(aj, SIGNAL(progressiveBatch(uint64_t, QVector<QRect>)));

    // Create progressive job with viewport at center
    auto job = std::make_shared<AsyncJob>();
    job->type = AsyncJobType::FilterApplyProgressive;
    job->toolName = "gaussian_blur";
    job->params = {{"radius", 3.0}};
    job->sourceImage = layer->cpuImage.copy();
    job->weakLayer = layer;

    // Set up tile rects (4 tiles covering the 256x256 image with tileSize=128)
    job->tileRects = {
        QRect(0, 0, 128, 128),
        QRect(0, 128, 128, 128),
        QRect(128, 0, 128, 128),
        QRect(128, 128, 128, 128)
    };
    job->viewportCenterX = 128;
    job->viewportCenterY = 128;
    job->kernelRadius = 5;
    job->tileSize = 128;
    job->batchSize = 2;

    aj->enqueue(job);

    // Wait for progressiveBatch signal (up to 10s) or timeout
    bool received = spy.wait(10000);
    BOOST_CHECK(received);

    aj->cancelAll();
    AsyncJobSystem::destroy();
}

BOOST_AUTO_TEST_CASE(progressive_cancel_prevents_batches)
{
    AsyncProgFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);
    layer->cpuImage = QImage(256, 256, QImage::Format_RGBA8888);
    layer->cpuImage.fill(QColor(100, 150, 200));

    AsyncJobSystem::create();
    auto* aj = AsyncJobSystem::instance();
    QSignalSpy spy(aj, SIGNAL(progressiveBatch(uint64_t, QVector<QRect>)));

    auto job = std::make_shared<AsyncJob>();
    job->type = AsyncJobType::FilterApplyProgressive;
    job->toolName = "gaussian_blur";
    job->params = {{"radius", 3.0}};
    job->sourceImage = layer->cpuImage.copy();
    job->weakLayer = layer;
    job->tileRects = {QRect(0, 0, 256, 256)};
    job->viewportCenterX = 128;
    job->viewportCenterY = 128;
    job->kernelRadius = 3;
    job->tileSize = 256;
    job->batchSize = 1;

    aj->enqueue(job);
    // Cancel immediately - pending count should drop to 0
    aj->cancelAll();
    BOOST_CHECK_EQUAL(aj->pendingCount(), 0);
    AsyncJobSystem::destroy();
}

BOOST_AUTO_TEST_CASE(light_op_remains_synchronous)
{
    AsyncProgFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);
    layer->cpuImage = QImage(64, 64, QImage::Format_RGBA8888);
    layer->cpuImage.fill(QColor(100, 150, 200));

    AsyncJobSystem::create();
    auto* aj = AsyncJobSystem::instance();

    // Light ops like brightness should execute synchronously
    BOOST_CHECK(f.ctrl.executeTool("adjust_brightness", {{"value", 0.3}}));
    BOOST_CHECK_EQUAL(aj->pendingCount(), 0);

    AsyncJobSystem::destroy();
}

BOOST_AUTO_TEST_CASE(enqueue_and_cancel_specific_layer)
{
    AsyncProgFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);
    layer->cpuImage = QImage(64, 64, QImage::Format_RGBA8888);
    layer->cpuImage.fill(QColor(100, 150, 200));

    AsyncJobSystem::create();
    auto* aj = AsyncJobSystem::instance();

    auto job = std::make_shared<AsyncJob>();
    job->type = AsyncJobType::FilterApply;
    job->toolName = "gaussian_blur";
    job->params = {{"radius", 5.0}};
    job->sourceImage = layer->cpuImage.copy();
    job->weakLayer = layer;

    aj->enqueue(job);
    BOOST_CHECK_EQUAL(aj->pendingCount(), 1);

    aj->cancelForLayer(layer);
    BOOST_CHECK_EQUAL(aj->pendingCount(), 0);

    AsyncJobSystem::destroy();
}

BOOST_AUTO_TEST_SUITE_END()
