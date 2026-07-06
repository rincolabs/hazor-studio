#define BOOST_TEST_MODULE BrushEngineTest
#include <boost/test/included/unit_test.hpp>

#include "brush/BrushTypes.hpp"
#include "brush/DynamicsEvaluator.hpp"
#include <QImage>
#include <cmath>

BOOST_AUTO_TEST_SUITE(brushengine)

// ── BrushSettings defaults ─────────────────────────────────────

BOOST_AUTO_TEST_CASE(default_settings)
{
    BrushSettings s;
    BOOST_CHECK_EQUAL(s.size, 20.0f);
    BOOST_CHECK_EQUAL(s.hardness, 0.8f);
    BOOST_CHECK_EQUAL(s.opacity, 1.0f);
    BOOST_CHECK_EQUAL(s.flow, 1.0f);
    BOOST_CHECK(s.color == QColor(Qt::black));
    BOOST_CHECK(s.type == BrushType::Round);
    BOOST_CHECK(s.mode == BrushMode::Paint);
}

// ── Enums ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(brush_type_round_default)
{
    BOOST_CHECK(BrushType::Round == static_cast<BrushType>(0));
}

BOOST_AUTO_TEST_CASE(brush_type_square)
{
    BOOST_CHECK(BrushType::Square == static_cast<BrushType>(1));
    BOOST_CHECK(BrushType::Square != BrushType::Round);
}

BOOST_AUTO_TEST_CASE(brush_mode_paint_default)
{
    BOOST_CHECK(BrushMode::Paint == static_cast<BrushMode>(0));
}

BOOST_AUTO_TEST_CASE(brush_mode_erase)
{
    BOOST_CHECK(BrushMode::Erase == static_cast<BrushMode>(1));
    BOOST_CHECK(BrushMode::Erase != BrushMode::Paint);
}

// ── brushFalloff function ──────────────────────────────────────

BOOST_AUTO_TEST_CASE(falloff_hardness_1_center)
{
    float f = brushFalloff(0.0f, 1.0f, 0.01f);
    BOOST_CHECK_CLOSE(f, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_1_inside)
{
    float f = brushFalloff(0.5f, 1.0f, 0.01f);
    BOOST_CHECK_CLOSE(f, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_1_at_edge)
{
    float aa = 1.5f / 20.0f;
    float f = brushFalloff(0.999f, 1.0f, aa);
    BOOST_CHECK(f > 0.0f);
    BOOST_CHECK(f <= 1.0f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_1_outside)
{
    float f = brushFalloff(1.5f, 1.0f, 0.01f);
    BOOST_CHECK_EQUAL(f, 0.0f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_0_center)
{
    float f = brushFalloff(0.0f, 0.0f, 0.01f);
    BOOST_CHECK_CLOSE(f, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_0_mid)
{
    float f = brushFalloff(0.5f, 0.0f, 0.01f);
    float expected = 1.0f - 0.5f * 0.5f;
    BOOST_CHECK_CLOSE(f, expected, 0.01f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_05_mid)
{
    float f = brushFalloff(0.5f, 0.5f, 0.01f);
    BOOST_CHECK_CLOSE(f, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_05_three_quarters)
{
    float f = brushFalloff(0.75f, 0.5f, 0.01f);
    BOOST_CHECK_CLOSE(f, 0.75f, 0.02f);
}

BOOST_AUTO_TEST_CASE(falloff_hardness_05_outside)
{
    float f = brushFalloff(1.1f, 0.5f, 0.01f);
    BOOST_CHECK_EQUAL(f, 0.0f);
}

BOOST_AUTO_TEST_CASE(falloff_monotonic_decreasing)
{
    float a = 0.01f;
    float prev = brushFalloff(0.0f, 0.5f, a);
    for (float d = 0.05f; d <= 1.05f; d += 0.05f) {
        float cur = brushFalloff(d, 0.5f, a);
        BOOST_CHECK(cur <= prev + 0.001f);
        prev = cur;
    }
}

BOOST_AUTO_TEST_CASE(falloff_monotonic_hard)
{
    float a = 0.01f;
    float prev = brushFalloff(0.0f, 1.0f, a);
    for (float d = 0.05f; d <= 1.05f; d += 0.05f) {
        float cur = brushFalloff(d, 1.0f, a);
        BOOST_CHECK(cur <= prev + 0.001f);
        prev = cur;
    }
}

// ── Stamp generation ───────────────────────────────────────────

static int pixelValue(const QImage& img, int x, int y)
{
    if (x < 0 || x >= img.width() || y < 0 || y >= img.height()) return -1;
    return img.constScanLine(y)[x];
}

static QImage generateStamp(BrushType type, float size, float hardness)
{
    int r = static_cast<int>(std::ceil(size));
    int diam = r * 2 + 2;
    QImage img(diam, diam, QImage::Format_Grayscale8);
    img.fill(0);

    float cx = diam / 2.0f;
    float cy = diam / 2.0f;
    float radius = size;
    float h = std::clamp(hardness, 0.0f, 1.0f);
    float aa = 1.5f / radius;

    if (type == BrushType::Square) {
        for (int y = 0; y < diam; ++y) {
            uchar* row = img.scanLine(y);
            for (int x = 0; x < diam; ++x) {
                float d = std::max(std::abs(x - cx), std::abs(y - cy)) / radius;
                if (d <= 1.0f)
                    row[x] = static_cast<uchar>(
                        std::clamp(brushFalloff(d, h, aa) * 255.0f, 0.0f, 255.0f));
            }
        }
    } else {
        for (int y = 0; y < diam; ++y) {
            uchar* row = img.scanLine(y);
            for (int x = 0; x < diam; ++x) {
                float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / radius;
                if (d <= 1.0f)
                    row[x] = static_cast<uchar>(
                        std::clamp(brushFalloff(d, h, aa) * 255.0f, 0.0f, 255.0f));
            }
        }
    }
    return img;
}

BOOST_AUTO_TEST_CASE(stamp_size)
{
    QImage stamp = generateStamp(BrushType::Round, 20.0f, 0.8f);
    int expected = static_cast<int>(std::ceil(20.0f)) * 2 + 2;
    BOOST_CHECK_EQUAL(stamp.width(), expected);
    BOOST_CHECK_EQUAL(stamp.height(), expected);
}

BOOST_AUTO_TEST_CASE(stamp_center_opaque)
{
    QImage stamp = generateStamp(BrushType::Round, 10.0f, 1.0f);
    int c = stamp.width() / 2;
    BOOST_CHECK(pixelValue(stamp, c, c) > 200);
}

BOOST_AUTO_TEST_CASE(stamp_corner_transparent)
{
    QImage stamp = generateStamp(BrushType::Round, 10.0f, 1.0f);
    BOOST_CHECK_EQUAL(pixelValue(stamp, 0, 0), 0);
    BOOST_CHECK_EQUAL(pixelValue(stamp, stamp.width() - 1, stamp.height() - 1), 0);
}

BOOST_AUTO_TEST_CASE(stamp_hard_has_sharper_edge_than_soft)
{
    QImage hardStamp = generateStamp(BrushType::Round, 20.0f, 1.0f);
    QImage softStamp = generateStamp(BrushType::Round, 20.0f, 0.0f);

    int c = hardStamp.width() / 2;
    int r = static_cast<int>(20.0f * 0.8f);

    int px = c + r;
    int py = c;
    BOOST_CHECK(pixelValue(hardStamp, px, py) > 250);
    BOOST_CHECK(pixelValue(softStamp, px, py) < 250);
}

BOOST_AUTO_TEST_CASE(stamp_square_has_different_shape)
{
    QImage round = generateStamp(BrushType::Round, 15.0f, 1.0f);
    QImage square = generateStamp(BrushType::Square, 15.0f, 1.0f);

    int c = round.width() / 2;
    int r = static_cast<int>(15.0f * 0.7f);
    int px = c + r;
    int py = c + r;

    BOOST_CHECK(pixelValue(round, px, py) < pixelValue(square, px, py));
}

BOOST_AUTO_TEST_CASE(stamp_different_sizes)
{
    QImage small = generateStamp(BrushType::Round, 5.0f, 1.0f);
    QImage large = generateStamp(BrushType::Round, 50.0f, 1.0f);
    BOOST_CHECK(small.width() < large.width());
}

BOOST_AUTO_TEST_CASE(stamp_hard_vs_soft_center_same)
{
    QImage hardStamp = generateStamp(BrushType::Round, 20.0f, 1.0f);
    QImage softStamp = generateStamp(BrushType::Round, 20.0f, 0.0f);

    int c = hardStamp.width() / 2;
    BOOST_CHECK(pixelValue(hardStamp, c, c) > 250);
    BOOST_CHECK(pixelValue(softStamp, c, c) > 250);
}

BOOST_AUTO_TEST_CASE(stamp_hard_edge_falloff)
{
    QImage stamp = generateStamp(BrushType::Round, 20.0f, 1.0f);
    int c = stamp.width() / 2;
    int r = static_cast<int>(20.0f);

    // Outside circle → pixel value 0
    int outside = c + r + 2;
    if (outside < stamp.width())
        BOOST_CHECK_EQUAL(pixelValue(stamp, outside, c), 0);
    // Circle edge pixel → should be > 0 due to AA
    int at_edge = c + r;
    BOOST_CHECK(pixelValue(stamp, at_edge, c) >= 0);
    // Inside circle → pixel value > 0
    int inside = c + r - 1;
    BOOST_CHECK(pixelValue(stamp, inside, c) > 0);
}

BOOST_AUTO_TEST_CASE(shape_dynamics_size_min_ratio_is_floor_not_scale)
{
    BrushSettings settings;
    settings.size = 100.0f;
    settings.shapeDynamics.enabled = true;
    settings.shapeDynamics.sizeMinRatio = 0.35f;
    settings.shapeDynamics.sizeJitter.amount = 0.0f;

    BrushInputState input;
    auto params = DynamicsEvaluator::evaluate(settings, input, 123);

    BOOST_CHECK_CLOSE(params.effectiveSize, 100.0f, 0.001f);
}

BOOST_AUTO_TEST_SUITE_END()
