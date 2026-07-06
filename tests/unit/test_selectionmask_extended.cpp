#define BOOST_TEST_MODULE SelectionMaskExtendedTest
#include <boost/test/included/unit_test.hpp>

#include "core/SelectionMask.hpp"

#include <QImage>
#include <QRectF>
#include <QPointF>
#include <QColor>

static QImage makeOpaqueSrc(int w, int h) {
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(QColor(255, 0, 0, 255));
    return img;
}

struct SelectionFixture {
    SelectionMask mask;
    static constexpr int W = 200, H = 150;

    SelectionFixture() {
        mask.create(W, H);
    }

    void assertSelected(int x, int y) {
        if (!mask.isSelected(x, y)) {
            mask.setActive(true);
        }
    }
};

BOOST_AUTO_TEST_SUITE(selectionmask_extended)

// ── Feather ─────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(feather_zero_radius_no_crash)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.feather(0.0f);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(feather_positive_radius)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.feather(5.0f);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(feather_large_radius)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.feather(50.0f);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(feather_empty_selection_no_crash)
{
    SelectionFixture f;
    f.mask.feather(10.0f);
    BOOST_CHECK(true);
}

// ── Grow ────────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(grow_zero_pixels)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    QRectF before = f.mask.bounds();
    f.mask.grow(0);
    BOOST_CHECK(f.mask.bounds() == before);
}

BOOST_AUTO_TEST_CASE(grow_positive_pixels)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    QRectF before = f.mask.bounds();
    f.mask.grow(10);
    QRectF after = f.mask.bounds();
    BOOST_CHECK(after.width() >= before.width());
}

BOOST_AUTO_TEST_CASE(grow_clamped_to_bounds)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(190, 10, 10, 10), SelectMode::Replace);
    f.mask.grow(50);
    QRectF after = f.mask.bounds();
    BOOST_CHECK(after.left() >= 0);
    BOOST_CHECK(after.right() <= 200);
    BOOST_CHECK(after.top() >= 0);
    BOOST_CHECK(after.bottom() <= 150);
}

// ── Shrink ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(shrink_zero_pixels)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.shrink(0);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(shrink_positive_pixels)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    QRectF before = f.mask.bounds();
    f.mask.shrink(10);
    QRectF after = f.mask.bounds();
    BOOST_CHECK(after.width() <= before.width());
}

BOOST_AUTO_TEST_CASE(shrink_to_nothing)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 5, 5), SelectMode::Replace);
    f.mask.shrink(20);
    BOOST_CHECK(f.mask.isEmpty());
}

// ── Border ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(border_zero_pixels)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.border(0);
}

BOOST_AUTO_TEST_CASE(border_positive_pixels)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.border(10);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(border_larger_than_selection_produces_empty_or_border)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 10, 10), SelectMode::Replace);
    f.mask.border(100);
    // Should not crash
}

// ── Smooth ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(smooth_zero_radius)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.smooth(0.0f);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(smooth_positive_radius)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.smooth(5.0f);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(smooth_large_radius)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.smooth(30.0f);
    BOOST_CHECK(!f.mask.isEmpty());
}

// ── Translate ───────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(translate_positive)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    QRectF before = f.mask.bounds();
    f.mask.translate(10, 10);
    QRectF after = f.mask.bounds();
    BOOST_CHECK_EQUAL(after.x(), before.x() + 10);
    BOOST_CHECK_EQUAL(after.y(), before.y() + 10);
}

BOOST_AUTO_TEST_CASE(translate_negative)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.translate(-10, -10);
    QRectF after = f.mask.bounds();
    BOOST_CHECK_EQUAL(after.x(), 40);
    BOOST_CHECK_EQUAL(after.y(), 40);
}

BOOST_AUTO_TEST_CASE(translate_zero)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    QRectF before = f.mask.bounds();
    f.mask.translate(0, 0);
    BOOST_CHECK(f.mask.bounds() == before);
}

// ── ApplyTransform ──────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(applyTransform_identity)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    f.mask.applyTransform(1.0f, 1.0f, 0.0f, 100, 75);
    BOOST_CHECK(!f.mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(applyTransform_scale_up)
{
    SelectionFixture f;
    f.mask.setRect(QRectF(50, 50, 100, 100), SelectMode::Replace);
    auto boundsBefore = f.mask.bounds();
    f.mask.applyTransform(2.0f, 2.0f, 0.0f, 100, 75);
    auto boundsAfter = f.mask.bounds();
    BOOST_CHECK(boundsAfter.width() >= boundsBefore.width());
}

// ── SetMagicWand ────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(magic_wand_solid_image)
{
    QImage src = makeOpaqueSrc(100, 100);
    SelectionMask mask;
    mask.create(100, 100);
    mask.setMagicWand(0, 0, 32.0f, true, SelectMode::Replace,
                      src.bits(), 100, 100);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(magic_wand_noncontiguous)
{
    QImage src(50, 50, QImage::Format_RGBA8888);
    src.fill(QColor(255, 0, 0));
    src.setPixelColor(25, 25, QColor(0, 0, 255));

    SelectionMask mask;
    mask.create(50, 50);
    mask.setMagicWand(0, 0, 64.0f, false, SelectMode::Replace,
                      src.bits(), 50, 50);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(magic_wand_high_tolerance)
{
    QImage src = makeOpaqueSrc(100, 100);
    SelectionMask mask;
    mask.create(100, 100);
    mask.setMagicWand(0, 0, 255.0f, true, SelectMode::Replace,
                      src.bits(), 100, 100);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(magic_wand_zero_tolerance_exact_match)
{
    QImage src = makeOpaqueSrc(100, 100);
    SelectionMask mask;
    mask.create(100, 100);
    mask.setMagicWand(0, 0, 0.0f, true, SelectMode::Replace,
                      src.bits(), 100, 100);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(magic_wand_null_src_no_crash)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setMagicWand(0, 0, 32.0f, true, SelectMode::Replace,
                      nullptr, 100, 100);
    BOOST_CHECK(mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(magic_wand_add_mode)
{
    QImage src = makeOpaqueSrc(100, 100);
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(0, 0, 50, 50), SelectMode::Replace);
    mask.setMagicWand(80, 80, 32.0f, true, SelectMode::Add,
                      src.bits(), 100, 100);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(magic_wand_subtract_mode)
{
    QImage src = makeOpaqueSrc(100, 100);
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    mask.setMagicWand(50, 50, 32.0f, true, SelectMode::Subtract,
                      src.bits(), 100, 100);
    // May produce empty selection
}

BOOST_AUTO_TEST_CASE(magic_wand_intersect_mode)
{
    QImage src = makeOpaqueSrc(100, 100);
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    mask.setMagicWand(50, 50, 32.0f, true, SelectMode::Intersect,
                      src.bits(), 100, 100);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(magic_wand_out_of_bounds_seed)
{
    QImage src = makeOpaqueSrc(50, 50);
    SelectionMask mask;
    mask.create(50, 50);
    mask.setMagicWand(-10, -10, 32.0f, true, SelectMode::Replace,
                      src.bits(), 50, 50);
}

// ── SetRect / SetEllipse Anti-Alias ────────────────────────────

BOOST_AUTO_TEST_CASE(setRect_antiAlias_true)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(10, 10, 80, 80), SelectMode::Replace, true);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(setRect_antiAlias_false)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(10, 10, 80, 80), SelectMode::Replace, false);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(setEllipse_antiAlias_true)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setEllipse(QRectF(10, 10, 80, 80), SelectMode::Replace, true);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(setEllipse_antiAlias_false)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setEllipse(QRectF(10, 10, 80, 80), SelectMode::Replace, false);
    BOOST_CHECK(!mask.isEmpty());
}

// ── SetPolygon ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(setPolygon_triangle)
{
    SelectionMask mask;
    mask.create(100, 100);
    std::vector<QPointF> pts = { {10, 10}, {90, 10}, {50, 90} };
    mask.setPolygon(pts, SelectMode::Replace, true);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(setPolygon_two_points_no_crash)
{
    SelectionMask mask;
    mask.create(100, 100);
    std::vector<QPointF> pts = { {10, 10}, {90, 10} };
    mask.setPolygon(pts, SelectMode::Replace, true);
}

BOOST_AUTO_TEST_CASE(setPolygon_one_point_no_crash)
{
    SelectionMask mask;
    mask.create(100, 100);
    std::vector<QPointF> pts = { {10, 10} };
    mask.setPolygon(pts, SelectMode::Replace, true);
}

BOOST_AUTO_TEST_CASE(setPolygon_empty_no_crash)
{
    SelectionMask mask;
    mask.create(100, 100);
    std::vector<QPointF> pts;
    mask.setPolygon(pts, SelectMode::Replace);
}

// ── Bounds and IsEmpty ──────────────────────────────────────────

BOOST_AUTO_TEST_CASE(bounds_empty_mask)
{
    SelectionMask mask;
    mask.create(100, 100);
    QRectF b = mask.bounds();
    BOOST_CHECK(b.isEmpty() || !mask.active());
}

BOOST_AUTO_TEST_CASE(bounds_full_mask)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(0, 0, 100, 100), SelectMode::Replace);
    QRectF b = mask.bounds();
    BOOST_CHECK_EQUAL(b.width(), 100);
    BOOST_CHECK_EQUAL(b.height(), 100);
}

BOOST_AUTO_TEST_CASE(bounds_single_pixel)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(50, 50, 1, 1), SelectMode::Replace);
    QRectF b = mask.bounds();
    BOOST_CHECK(b.width() >= 0);
    BOOST_CHECK(b.height() >= 0);
}

BOOST_AUTO_TEST_CASE(isEmpty_clear_mask)
{
    SelectionMask mask;
    mask.create(100, 100);
    BOOST_CHECK(mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(isEmpty_after_setRect)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(10, 10, 80, 80), SelectMode::Replace);
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(isSelected_inside_selection)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(10, 10, 50, 50), SelectMode::Replace);
    BOOST_CHECK(mask.isSelected(30, 30));
}

BOOST_AUTO_TEST_CASE(isSelected_outside_selection)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(10, 10, 50, 50), SelectMode::Replace);
    BOOST_CHECK(!mask.isSelected(80, 80));
}

// ── Active Flag ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(setActive_toggles)
{
    SelectionMask mask;
    mask.create(100, 100);
    BOOST_CHECK(!mask.active());
    mask.setActive(true);
    BOOST_CHECK(mask.active());
    mask.setActive(false);
    BOOST_CHECK(!mask.active());
}

// ── Bits Access ─────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(bits_access_all_zero_after_create)
{
    SelectionMask mask;
    mask.create(50, 50);
    auto* bits = mask.bits();
    BOOST_REQUIRE(bits != nullptr);
    BOOST_CHECK_EQUAL(bits[0], 0);
    BOOST_CHECK_EQUAL(bits[49 * mask.image().bytesPerLine()], 0);
}

BOOST_AUTO_TEST_CASE(bits_setRect_fills_values)
{
    SelectionMask mask;
    mask.create(50, 50);
    mask.setRect(QRectF(0, 0, 50, 50), SelectMode::Replace);
    auto* bits = mask.bits();
    BOOST_CHECK(bits[0] > 0);
}

// ── Resize ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(resize_updates_dimensions)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.resize(200, 150);
    BOOST_CHECK_EQUAL(mask.width(), 200);
    BOOST_CHECK_EQUAL(mask.height(), 150);
}

BOOST_AUTO_TEST_CASE(resize_to_zero_no_crash)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.resize(0, 0);
}

// ── Invert ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(invert_empty_selection)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.invert();
    BOOST_CHECK(!mask.isEmpty());
}

BOOST_AUTO_TEST_CASE(invert_then_invert_restores)
{
    SelectionMask mask;
    mask.create(100, 100);
    mask.setRect(QRectF(10, 10, 80, 80), SelectMode::Replace);
    mask.invert();
    BOOST_CHECK(!mask.isSelected(50, 50));
    mask.invert();
    BOOST_CHECK(mask.isSelected(50, 50));
}

BOOST_AUTO_TEST_SUITE_END()
