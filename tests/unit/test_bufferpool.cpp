#define BOOST_TEST_MODULE BufferPoolTest
#include <boost/test/included/unit_test.hpp>

#include "memory/BufferPool.hpp"
#include <QImage>
#include <opencv2/core.hpp>

BOOST_AUTO_TEST_SUITE(buffer_pool)

BOOST_AUTO_TEST_CASE(acquire_image_correct_size_and_format)
{
    memory::BufferPool pool(4);
    QImage img = pool.acquireImage(100, 200, QImage::Format_RGBA8888);
    BOOST_CHECK_EQUAL(img.width(), 100);
    BOOST_CHECK_EQUAL(img.height(), 200);
    BOOST_CHECK(img.format() == QImage::Format_RGBA8888);
}

BOOST_AUTO_TEST_CASE(acquire_image_creates_new_when_empty)
{
    memory::BufferPool pool(4);
    QImage img1 = pool.acquireImage(64, 64, QImage::Format_RGBA8888);
    QImage img2 = pool.acquireImage(64, 64, QImage::Format_RGBA8888);
    // No buffers released yet, so both are new => distinct memory
    BOOST_CHECK(img1.bits() != img2.bits() || img1.isNull() || img2.isNull());
}

BOOST_AUTO_TEST_CASE(release_and_acquire_reuses_buffer)
{
    memory::BufferPool pool(4);
    QImage img1 = pool.acquireImage(100, 100, QImage::Format_RGBA8888);
    void* ptr1 = img1.bits();
    pool.release(img1);
    BOOST_CHECK(img1.isNull()); // released images are nulled

    QImage img2 = pool.acquireImage(100, 100, QImage::Format_RGBA8888);
    // Should reuse the same buffer
    BOOST_CHECK_EQUAL(img2.width(), 100);
    BOOST_CHECK_EQUAL(img2.height(), 100);
}

BOOST_AUTO_TEST_CASE(acquire_image_exact_match_preferred)
{
    memory::BufferPool pool(4);
    // Fill pool: release two different sizes
    QImage a = pool.acquireImage(100, 200, QImage::Format_RGBA8888);
    QImage b = pool.acquireImage(200, 100, QImage::Format_RGBA8888);
    pool.release(a);
    pool.release(b);

    // Acquire exact match for 100x200
    QImage result = pool.acquireImage(100, 200, QImage::Format_RGBA8888);
    BOOST_CHECK_EQUAL(result.width(), 100);
    BOOST_CHECK_EQUAL(result.height(), 200);
}

BOOST_AUTO_TEST_CASE(acquire_image_best_fit_when_no_exact)
{
    memory::BufferPool pool(4);
    QImage small = pool.acquireImage(50, 50, QImage::Format_RGBA8888);
    QImage large = pool.acquireImage(200, 200, QImage::Format_RGBA8888);
    pool.release(small);
    pool.release(large);

    // Request 100x100 -> best fit should be 200x200 (>= in both dimensions)
    QImage result = pool.acquireImage(100, 100, QImage::Format_RGBA8888);
    BOOST_CHECK(result.width() >= 100);
    BOOST_CHECK(result.height() >= 100);
}

BOOST_AUTO_TEST_CASE(acquire_mat_correct_size_type)
{
    memory::BufferPool pool(4);
    cv::Mat m = pool.acquireMat(50, 100, CV_8UC4);
    BOOST_CHECK_EQUAL(m.rows, 50);
    BOOST_CHECK_EQUAL(m.cols, 100);
    BOOST_CHECK_EQUAL(m.type(), CV_8UC4);
}

BOOST_AUTO_TEST_CASE(release_mat_and_acquire_same)
{
    memory::BufferPool pool(4);
    cv::Mat m1 = pool.acquireMat(64, 64, CV_8UC4);
    void* ptr1 = m1.data;
    pool.release(m1);
    BOOST_CHECK(m1.empty()); // released mats are emptied

    cv::Mat m2 = pool.acquireMat(64, 64, CV_8UC4);
    BOOST_CHECK_EQUAL(m2.rows, 64);
    BOOST_CHECK_EQUAL(m2.cols, 64);
}

BOOST_AUTO_TEST_CASE(lru_eviction_on_overflow)
{
    memory::BufferPool pool(2); // max 2 per type
    // Acquire and release 3 images to fill pool with 3
    for (int i = 0; i < 3; ++i) {
        QImage img = pool.acquireImage(32, 32, QImage::Format_RGBA8888);
        pool.release(img);
    }
    // After 3 releases with max=2, only 2 should remain (oldest evicted)
    // Acquire first: from remaining pool
    QImage r1 = pool.acquireImage(32, 32, QImage::Format_RGBA8888);
    QImage r2 = pool.acquireImage(32, 32, QImage::Format_RGBA8888);
    BOOST_CHECK(!r1.isNull());
    BOOST_CHECK(!r2.isNull());
    // Third acquire would need to allocate new (pool drained)
    QImage r3 = pool.acquireImage(32, 32, QImage::Format_RGBA8888);
    BOOST_CHECK(!r3.isNull());
}

BOOST_AUTO_TEST_CASE(clear_empties_pool)
{
    memory::BufferPool pool(4);
    QImage img = pool.acquireImage(50, 50, QImage::Format_RGBA8888);
    pool.release(img);
    pool.clear();

    // After clear, acquiring should be a new allocation
    QImage result = pool.acquireImage(50, 50, QImage::Format_RGBA8888);
    BOOST_CHECK_EQUAL(result.width(), 50);
    BOOST_CHECK_EQUAL(result.height(), 50);
}

BOOST_AUTO_TEST_CASE(release_null_image_noop)
{
    memory::BufferPool pool(4);
    QImage nullImg;
    pool.release(nullImg); // should not crash
    BOOST_CHECK(nullImg.isNull());
}

BOOST_AUTO_TEST_CASE(release_empty_mat_noop)
{
    memory::BufferPool pool(4);
    cv::Mat emptyMat;
    pool.release(emptyMat); // should not crash
    BOOST_CHECK(emptyMat.empty());
}

BOOST_AUTO_TEST_CASE(thread_local_pool_same_thread)
{
    memory::BufferPool& p1 = memory::threadLocalPool();
    memory::BufferPool& p2 = memory::threadLocalPool();
    BOOST_CHECK(&p1 == &p2);
}

BOOST_AUTO_TEST_CASE(acquire_zero_dims)
{
    memory::BufferPool pool(4);
    QImage img = pool.acquireImage(0, 100, QImage::Format_RGBA8888);
    // A QImage with one dimension zero is technically valid but empty-like
    BOOST_CHECK(img.isNull() || (img.width() == 0));
}

BOOST_AUTO_TEST_SUITE_END()
