#define BOOST_TEST_MODULE LayerMaskTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "controller/Commands.hpp"
#include "engine/ImageEngine.hpp"
#include "tools/ToolCall.hpp"
#include "tools/ToolCatalog.hpp"

#include <QImage>
#include <QColor>
#include <QTransform>
#include <QApplication>

struct QtAppFixture {
    QtAppFixture() {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = { const_cast<char*>("test") };
            static QApplication app(argc, argv);
        }
    }
};
BOOST_GLOBAL_FIXTURE(QtAppFixture);

struct MaskFixture {
    Document doc;
    ImageController ctrl;

    MaskFixture() : ctrl() {
        doc.size = QSize(200, 150);
        doc.selection.create(200, 150);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    Layer* active() { return doc.activeLayer(); }
    int activeIdx() const { return doc.activeFlatIndex; }

    void fillLayer(uchar r, uchar g, uchar b, uchar a = 255) {
        auto* l = active();
        if (l) l->cpuImage.fill(QColor(r, g, b, a));
    }

    void addMask() { ctrl.addLayerMask(activeIdx()); }
    bool hasMask() { return ctrl.hasLayerMask(activeIdx()); }

    LayerTreeNode* nodeAt(int idx) { return doc.nodeAt(idx); }
    int flatCount() const { return doc.flatCount(); }
};

using Params = std::unordered_map<std::string, JsonValue>;

// Helper: check that every pixel in maskImage equals expected value
static void checkMaskUniform(Layer* l, uchar expected) {
    for (int y = 0; y < l->maskImage.height(); ++y) {
        const uchar* row = l->maskImage.constScanLine(y);
        for (int x = 0; x < l->maskImage.width(); ++x)
            BOOST_CHECK_EQUAL(row[x], expected);
    }
}

// Helper: set a rectangular region of maskImage to value
static void setMaskRect(Layer* l, int x1, int y1, int x2, int y2, uchar val) {
    x1 = std::max(0, x1); y1 = std::max(0, y1);
    x2 = std::min(l->maskImage.width() - 1, x2);
    y2 = std::min(l->maskImage.height() - 1, y2);
    for (int y = y1; y <= y2; ++y) {
        uchar* row = l->maskImage.scanLine(y);
        for (int x = x1; x <= x2; ++x)
            row[x] = val;
    }
}

BOOST_AUTO_TEST_SUITE(layermask)

// ═══════════════════════════════════════════════════════════════════
// Category A — Basic Mask CRUD
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(add_mask_creates_white_mask)
{
    MaskFixture f;
    f.addMask();
    BOOST_REQUIRE(f.hasMask());
    auto* l = f.active();
    BOOST_REQUIRE(l);
    BOOST_REQUIRE(!l->maskImage.isNull());
    checkMaskUniform(l, 255);
}

BOOST_AUTO_TEST_CASE(add_mask_leaves_mask_enabled)
{
    MaskFixture f;
    f.addMask();
    BOOST_CHECK(f.ctrl.isLayerMaskEnabled(f.activeIdx()));
    auto* l = f.active();
    BOOST_REQUIRE(l);
    BOOST_CHECK(l->maskVisible);
}

BOOST_AUTO_TEST_CASE(remove_mask_clears_state)
{
    MaskFixture f;
    f.addMask();
    BOOST_REQUIRE(f.hasMask());
    f.ctrl.removeLayerMask(f.activeIdx());
    BOOST_CHECK(!f.hasMask());
    auto* l = f.active();
    BOOST_REQUIRE(l);
    BOOST_CHECK(l->maskImage.isNull());
}

BOOST_AUTO_TEST_CASE(toggle_mask_add_then_remove)
{
    MaskFixture f;
    int idx = f.activeIdx();
    f.ctrl.toggleLayerMask(idx);
    BOOST_CHECK(f.hasMask());
    f.ctrl.toggleLayerMask(idx);
    BOOST_CHECK(!f.hasMask());
}

BOOST_AUTO_TEST_CASE(add_mask_on_invalid_index)
{
    MaskFixture f;
    // Should not crash
    f.ctrl.addLayerMask(-1);
    f.ctrl.addLayerMask(99);
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(remove_mask_on_invalid_index)
{
    MaskFixture f;
    f.ctrl.removeLayerMask(-1);
    f.ctrl.removeLayerMask(99);
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(add_mask_dimensions_match_layer)
{
    MaskFixture f;
    auto* l = f.active();
    BOOST_REQUIRE(l);
    QSize imgSz = l->cpuImage.size();
    f.addMask();
    BOOST_CHECK(l->maskImage.size() == imgSz);
}

BOOST_AUTO_TEST_CASE(add_mask_sets_thumb_dirty)
{
    MaskFixture f;
    auto* l = f.active();
    BOOST_REQUIRE(l);
    l->maskThumbDirty = false;
    f.addMask();
    BOOST_CHECK(l->maskThumbDirty);
}

// ═══════════════════════════════════════════════════════════════════
// Category B — Mask Visibility
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(disable_then_enable_mask)
{
    MaskFixture f;
    int idx = f.activeIdx();
    f.addMask();
    f.ctrl.setLayerMaskEnabled(idx, false);
    BOOST_CHECK(!f.ctrl.isLayerMaskEnabled(idx));
    auto* l = f.active();
    BOOST_REQUIRE(l);
    BOOST_CHECK(!l->maskVisible);
    f.ctrl.setLayerMaskEnabled(idx, true);
    BOOST_CHECK(f.ctrl.isLayerMaskEnabled(idx));
    BOOST_CHECK(l->maskVisible);
}

BOOST_AUTO_TEST_CASE(enable_mask_without_mask)
{
    MaskFixture f;
    int idx = f.activeIdx();
    BOOST_CHECK(!f.ctrl.hasLayerMask(idx));
    f.ctrl.setLayerMaskEnabled(idx, false);
    f.ctrl.setLayerMaskEnabled(idx, true);
    BOOST_CHECK(!f.ctrl.isLayerMaskEnabled(idx));
}

BOOST_AUTO_TEST_CASE(disable_on_invalid_index)
{
    MaskFixture f;
    f.ctrl.setLayerMaskEnabled(-1, false);
    f.ctrl.setLayerMaskEnabled(999, true);
    BOOST_CHECK(true);
}

// ═══════════════════════════════════════════════════════════════════
// Category C — Apply Mask (destructive)
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(apply_mask_full_white_no_change)
{
    MaskFixture f;
    f.fillLayer(255, 0, 0);
    f.addMask(); // all-white mask — reveal all
    auto before = f.active()->cpuImage.copy();
    int idx = f.activeIdx();
    f.ctrl.applyLayerMask(idx);
    BOOST_CHECK(before == f.active()->cpuImage);
    BOOST_CHECK(!f.hasMask());
}

BOOST_AUTO_TEST_CASE(apply_mask_black_zeroes_alpha)
{
    MaskFixture f;
    f.fillLayer(255, 0, 0);
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    // Paint black rectangle on mask (hide)
    setMaskRect(l, 10, 10, 50, 50, 0);
    f.ctrl.applyLayerMask(idx);
    // Pixels outside mask rect: still opaque
    BOOST_CHECK_EQUAL(l->cpuImage.pixelColor(5, 5).alpha(), 255);
    BOOST_CHECK_EQUAL(l->cpuImage.pixelColor(5, 5).red(), 255);
    // Pixels inside mask rect: now transparent
    BOOST_CHECK_EQUAL(l->cpuImage.pixelColor(25, 25).alpha(), 0);
    // Mask is removed
    BOOST_CHECK(!f.hasMask());
}

BOOST_AUTO_TEST_CASE(apply_mask_semi_transparent)
{
    MaskFixture f;
    f.fillLayer(100, 150, 200);
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    // Set a region to 50% gray on mask
    setMaskRect(l, 0, 0, l->maskImage.width() - 1, l->maskImage.height() - 1, 128);
    f.ctrl.applyLayerMask(idx);
    // The alpha should be approximately 128/255 of original (255 → ~128)
    auto c = l->cpuImage.pixelColor(50, 50);
    BOOST_CHECK_CLOSE(static_cast<float>(c.alpha()), 128.0f, 5.0f);
    // RGB unchanged
    BOOST_CHECK_EQUAL(c.red(), 100);
    BOOST_CHECK_EQUAL(c.green(), 150);
    BOOST_CHECK_EQUAL(c.blue(), 200);
}

BOOST_AUTO_TEST_CASE(apply_mask_without_mask)
{
    MaskFixture f;
    BOOST_CHECK(!f.hasMask());
    f.ctrl.applyLayerMask(f.activeIdx());
    BOOST_CHECK(true);
}

// ═══════════════════════════════════════════════════════════════════
// Category D — Mask Invert
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(invert_white_becomes_black)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    f.ctrl.invertLayerMask(idx);
    auto* l = f.active();
    checkMaskUniform(l, 0);
}

BOOST_AUTO_TEST_CASE(invert_black_becomes_white)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    l->maskImage.fill(0);
    f.ctrl.invertLayerMask(idx);
    checkMaskUniform(l, 255);
}

BOOST_AUTO_TEST_CASE(invert_partial)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    setMaskRect(l, 10, 10, 50, 50, 0);
    setMaskRect(l, 60, 60, 80, 80, 128);
    f.ctrl.invertLayerMask(idx);
    BOOST_CHECK_EQUAL(l->maskImage.constScanLine(30)[30], 255); // 0 → 255
    BOOST_CHECK_EQUAL(l->maskImage.constScanLine(70)[70], 127); // 128 → 127
    BOOST_CHECK_EQUAL(l->maskImage.constScanLine(0)[0], 0);     // 255 → 0
}

BOOST_AUTO_TEST_CASE(invert_without_mask_no_crash)
{
    MaskFixture f;
    BOOST_CHECK(!f.hasMask());
    f.ctrl.invertLayerMask(f.activeIdx());
    BOOST_CHECK(true);
}

// ═══════════════════════════════════════════════════════════════════
// Category E — Mask Density
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(density_half)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    f.ctrl.setMaskDensity(idx, 0.5f);
    auto* l = f.active();
    BOOST_CHECK_CLOSE(l->maskDensity, 0.5f, 0.001f);
}

BOOST_AUTO_TEST_CASE(density_clamped_high)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    f.ctrl.setMaskDensity(idx, 1.5f);
    auto* l = f.active();
    BOOST_CHECK_CLOSE(l->maskDensity, 1.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(density_clamped_low)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    f.ctrl.setMaskDensity(idx, -0.5f);
    auto* l = f.active();
    BOOST_CHECK_CLOSE(l->maskDensity, 0.0f, 0.001f);
}

BOOST_AUTO_TEST_CASE(density_without_mask)
{
    MaskFixture f;
    f.ctrl.setMaskDensity(f.activeIdx(), 0.3f);
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(density_default_is_one)
{
    MaskFixture f;
    auto* l = f.active();
    BOOST_CHECK_CLOSE(l->maskDensity, 1.0f, 0.001f);
}

// ═══════════════════════════════════════════════════════════════════
// Category F — Mask Feather
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(feather_blurs_mask)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    // Create hard edge: left half black, right half white
    int midX = l->maskImage.width() / 2;
    setMaskRect(l, 0, 0, midX, l->maskImage.height() - 1, 0);
    setMaskRect(l, midX, 0, l->maskImage.width() - 1, l->maskImage.height() - 1, 255);

    f.ctrl.setMaskFeather(idx, 5.0f);

    // Pixels far from the edge should still be 0 or 255
    BOOST_CHECK_EQUAL(l->maskImage.constScanLine(50)[0], 0);
    BOOST_CHECK_EQUAL(l->maskImage.constScanLine(50)[l->maskImage.width() - 1], 255);
    // Pixels near the edge should be intermediate (blurred)
    uchar edgePixel = l->maskImage.constScanLine(50)[midX];
    BOOST_CHECK(edgePixel > 0 && edgePixel < 255);
}

BOOST_AUTO_TEST_CASE(feather_zero_no_change)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    setMaskRect(l, 10, 10, 50, 50, 0);
    QImage before = l->maskImage.copy();
    f.ctrl.setMaskFeather(idx, 0.0f);
    BOOST_CHECK(before == l->maskImage);
}

BOOST_AUTO_TEST_CASE(feather_without_mask_no_crash)
{
    MaskFixture f;
    f.ctrl.setMaskFeather(f.activeIdx(), 5.0f);
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(feather_large_radius)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    f.ctrl.setMaskFeather(idx, 250.0f);
    auto* l = f.active();
    BOOST_CHECK_CLOSE(l->maskFeather, 250.0f, 0.5f);
}

// ═══════════════════════════════════════════════════════════════════
// Category G — Mask Edit State
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(editing_toggle_basic)
{
    MaskFixture f;
    f.addMask();
    BOOST_CHECK(!f.ctrl.isEditingMask());
    f.ctrl.setEditingMask(true);
    BOOST_CHECK(f.ctrl.isEditingMask());
    f.ctrl.setEditingMask(false);
    BOOST_CHECK(!f.ctrl.isEditingMask());
}

BOOST_AUTO_TEST_CASE(editing_without_mask)
{
    MaskFixture f;
    f.ctrl.setEditingMask(true);
    BOOST_CHECK(f.ctrl.isEditingMask());
}

// ═══════════════════════════════════════════════════════════════════
// Category H — Selection ↔ Mask
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(create_mask_from_selection)
{
    MaskFixture f;
    f.fillLayer(255, 0, 0);
    int idx = f.activeIdx();
    // Partial selection: left half (0..99)
    f.doc.selection.create(200, 150);
    f.doc.selection.setRect(QRectF(0, 0, 100, 150), SelectMode::Replace, false);
    BOOST_CHECK(f.doc.selection.active());

    f.ctrl.createMaskFromSelection(idx);
    auto* l = f.active();
    BOOST_REQUIRE(l);
    BOOST_CHECK(f.ctrl.hasLayerMask(idx));
    BOOST_REQUIRE(!l->maskImage.isNull());
    // Inside selection (x=50): mask should reveal (white=255)
    BOOST_CHECK_EQUAL(l->maskImage.constScanLine(75)[50], 255);
    // Outside selection (x=150): mask should hide (black=0)
    uchar outVal = l->maskImage.constScanLine(75)[150];
    BOOST_CHECK_EQUAL(outVal, 0);
}

BOOST_AUTO_TEST_CASE(create_mask_without_selection_no_crash)
{
    MaskFixture f;
    f.doc.selection.clear();
    f.doc.selection.setActive(false);
    f.ctrl.createMaskFromSelection(f.activeIdx());
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(load_mask_to_selection)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    // Paint a region of mask to 0 (black = hidden)
    setMaskRect(l, 30, 30, 70, 70, 0);
    f.ctrl.loadMaskToSelection(idx);
    BOOST_CHECK(f.doc.selection.active());
    // Pixels at 0,0 should be selected (mask = 255 white everywhere outside the rect)
    BOOST_CHECK(f.doc.selection.isSelected(0, 0));
    // Pixels at 50,50 should NOT be selected (mask = 0 black in this region)
    BOOST_CHECK(!f.doc.selection.isSelected(50, 50));
}

BOOST_AUTO_TEST_CASE(load_mask_without_mask_no_crash)
{
    MaskFixture f;
    BOOST_CHECK(!f.hasMask());
    f.ctrl.loadMaskToSelection(f.activeIdx());
    BOOST_CHECK(true);
}

// ═══════════════════════════════════════════════════════════════════
// Category I — Copy/Paste/Clear Mask
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(copy_mask_stores_copy)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    setMaskRect(l, 10, 10, 50, 50, 0);
    BOOST_CHECK(!f.ctrl.hasCopiedMask());
    f.ctrl.copyLayerMask(idx);
    BOOST_CHECK(f.ctrl.hasCopiedMask());
}

BOOST_AUTO_TEST_CASE(paste_mask_to_another_layer)
{
    MaskFixture f;
    f.addMask();
    int srcIdx = f.activeIdx();
    auto* srcL = f.active();
    setMaskRect(srcL, 5, 5, 20, 20, 0);
    f.ctrl.copyLayerMask(srcIdx);

    // Create a second layer
    f.ctrl.newLayer();
    int dstIdx = f.activeIdx();
    BOOST_CHECK(!f.ctrl.hasLayerMask(dstIdx));
    f.ctrl.pasteLayerMask(dstIdx);
    BOOST_CHECK(f.ctrl.hasLayerMask(dstIdx));
    auto* dstL = f.active();
    BOOST_REQUIRE(dstL);
    BOOST_REQUIRE(!dstL->maskImage.isNull());
    // Pasted mask should match original
    BOOST_CHECK_EQUAL(dstL->maskImage.width(), srcL->maskImage.width());
    BOOST_CHECK_EQUAL(dstL->maskImage.height(), srcL->maskImage.height());
    BOOST_CHECK_EQUAL(dstL->maskImage.constScanLine(10)[10], 0);
    BOOST_CHECK_EQUAL(dstL->maskImage.constScanLine(0)[0], 255);
}

BOOST_AUTO_TEST_CASE(paste_without_copied_mask_no_crash)
{
    MaskFixture f;
    f.ctrl.pasteLayerMask(f.activeIdx());
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(clear_mask_resets_to_white)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    setMaskRect(l, 0, 0, l->maskImage.width() - 1, l->maskImage.height() - 1, 0);
    f.ctrl.clearLayerMask(idx);
    BOOST_CHECK(f.hasMask());
    checkMaskUniform(l, 255);
}

BOOST_AUTO_TEST_CASE(clear_mask_without_mask_no_crash)
{
    MaskFixture f;
    f.ctrl.clearLayerMask(f.activeIdx());
    BOOST_CHECK(true);
}

// ═══════════════════════════════════════════════════════════════════
// Category J — Undo/Redo (MaskEditCommand)
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(undo_invert_restores_mask)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    QImage before = l->maskImage.copy();
    f.ctrl.invertLayerMask(idx);
    BOOST_CHECK(f.ctrl.history().canUndo());
    f.ctrl.undo();
    BOOST_CHECK(before == l->maskImage);
}

BOOST_AUTO_TEST_CASE(redo_invert_reapplies)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    QImage before = l->maskImage.copy();
    f.ctrl.invertLayerMask(idx);
    f.ctrl.undo();
    BOOST_CHECK(before == l->maskImage);
    f.ctrl.redo();
    // After redo, mask should be all 0
    checkMaskUniform(l, 0);
}

BOOST_AUTO_TEST_CASE(undo_clear_restores_mask)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    setMaskRect(l, 0, 0, 50, 50, 0);
    QImage before = l->maskImage.copy();
    f.ctrl.clearLayerMask(idx);
    f.ctrl.undo();
    BOOST_CHECK(before == l->maskImage);
}

BOOST_AUTO_TEST_CASE(multiple_undos_correct_order)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    QImage original = l->maskImage.copy();
    // Change 1: invert
    f.ctrl.invertLayerMask(idx);
    QImage afterInvert = l->maskImage.copy();
    // Change 2: clear
    f.ctrl.clearLayerMask(idx);
    // Undo clear
    f.ctrl.undo();
    BOOST_CHECK(afterInvert == l->maskImage);
    // Undo invert
    f.ctrl.undo();
    BOOST_CHECK(original == l->maskImage);
}

BOOST_AUTO_TEST_CASE(multiple_redos)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    auto* l = f.active();
    QImage original = l->maskImage.copy();
    // invert
    f.ctrl.invertLayerMask(idx);
    QImage afterInvert = l->maskImage.copy();
    // clear
    f.ctrl.clearLayerMask(idx);
    QImage afterClear = l->maskImage.copy();
    // undo twice
    f.ctrl.undo();
    BOOST_CHECK(afterInvert == l->maskImage);
    f.ctrl.undo();
    BOOST_CHECK(original == l->maskImage);
    // redo twice
    f.ctrl.redo();
    BOOST_CHECK(afterInvert == l->maskImage);
    f.ctrl.redo();
    BOOST_CHECK(afterClear == l->maskImage);
}

// ═══════════════════════════════════════════════════════════════════
// Category K — AI Tools (via executeTool)
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(ai_add_mask)
{
    MaskFixture f;
    f.ctrl.executeTool("layer_mask_add", {{"index", 0.0}});
    BOOST_CHECK(f.hasMask());
}

BOOST_AUTO_TEST_CASE(ai_remove_mask)
{
    MaskFixture f;
    f.addMask();
    f.ctrl.executeTool("layer_mask_remove", {{"index", 0.0}});
    BOOST_CHECK(!f.hasMask());
}

BOOST_AUTO_TEST_CASE(ai_toggle_mask)
{
    MaskFixture f;
    f.ctrl.executeTool("layer_mask_toggle", {{"index", 0.0}});
    BOOST_CHECK(f.hasMask());
    f.ctrl.executeTool("layer_mask_toggle", {{"index", 0.0}});
    BOOST_CHECK(!f.hasMask());
}

BOOST_AUTO_TEST_CASE(ai_apply_mask)
{
    MaskFixture f;
    f.fillLayer(100, 100, 100);
    f.addMask();
    f.ctrl.executeTool("layer_mask_apply", {{"index", 0.0}});
    BOOST_CHECK(!f.hasMask());
    BOOST_CHECK(f.active()->maskImage.isNull());
}

BOOST_AUTO_TEST_CASE(ai_disable_enable_mask)
{
    MaskFixture f;
    f.addMask();
    int idx = f.activeIdx();
    f.ctrl.executeTool("layer_mask_disable", {{"index", static_cast<double>(idx)}});
    BOOST_CHECK(!f.ctrl.isLayerMaskEnabled(idx));
    f.ctrl.executeTool("layer_mask_enable", {{"index", static_cast<double>(idx)}});
    BOOST_CHECK(f.ctrl.isLayerMaskEnabled(idx));
}

BOOST_AUTO_TEST_CASE(ai_invert)
{
    MaskFixture f;
    f.addMask();
    f.ctrl.executeTool("mask_invert", {{"index", 0.0}});
    checkMaskUniform(f.active(), 0);
}

BOOST_AUTO_TEST_CASE(ai_density)
{
    MaskFixture f;
    f.addMask();
    f.ctrl.executeTool("mask_density", {{"index", 0.0}, {"density", 0.3}});
    BOOST_CHECK_CLOSE(f.active()->maskDensity, 0.3f, 0.001f);
}

BOOST_AUTO_TEST_CASE(ai_feather)
{
    MaskFixture f;
    f.addMask();
    auto* l = f.active();
    setMaskRect(l, 0, 0, l->maskImage.width() / 2, l->maskImage.height() - 1, 0);
    setMaskRect(l, l->maskImage.width() / 2, 0,
                l->maskImage.width() - 1, l->maskImage.height() - 1, 255);
    f.ctrl.executeTool("mask_feather", {{"index", 0.0}, {"radius", 5.0}});
    // Edge pixel should be softened
    int mid = l->maskImage.width() / 2;
    uchar edge = l->maskImage.constScanLine(l->maskImage.height() / 2)[mid];
    BOOST_CHECK(edge > 0 && edge < 255);
}

BOOST_AUTO_TEST_CASE(ai_selection_to_mask)
{
    MaskFixture f;
    f.fillLayer(255, 0, 0);
    f.doc.selection.create(200, 150);
    f.doc.selection.setRect(QRectF(0, 0, 100, 150), SelectMode::Replace, false);
    f.ctrl.executeTool("selection_to_mask", {{"index", 0.0}});
    BOOST_CHECK(f.hasMask());
}

BOOST_AUTO_TEST_CASE(ai_mask_copy)
{
    MaskFixture f;
    f.addMask();
    BOOST_CHECK(!f.ctrl.hasCopiedMask());
    f.ctrl.executeTool("mask_copy", {{"index", 0.0}});
    BOOST_CHECK(f.ctrl.hasCopiedMask());
}

BOOST_AUTO_TEST_CASE(ai_mask_paste)
{
    MaskFixture f;
    f.addMask();
    auto* src = f.active();
    setMaskRect(src, 5, 5, 20, 20, 0);
    f.ctrl.executeTool("mask_copy", {{"index", 0.0}});
    f.ctrl.newLayer();
    // newLayer inserts at index 0, shifting old layer to 1.
    // Active layer is now index 0 (the new empty one).
    int dstIdx = f.activeIdx();
    f.ctrl.executeTool("mask_paste", {{"index", static_cast<double>(dstIdx)}});
    auto* dst = f.active();
    BOOST_REQUIRE(f.ctrl.hasLayerMask(dstIdx));
    BOOST_REQUIRE(dst);
    BOOST_REQUIRE(!dst->maskImage.isNull());
    BOOST_CHECK_EQUAL(dst->maskImage.constScanLine(10)[10], 0);
}

BOOST_AUTO_TEST_CASE(ai_mask_clear)
{
    MaskFixture f;
    f.addMask();
    setMaskRect(f.active(), 0, 0, f.active()->maskImage.width() - 1,
                f.active()->maskImage.height() - 1, 0);
    f.ctrl.executeTool("mask_clear", {{"index", 0.0}});
    checkMaskUniform(f.active(), 255);
}

BOOST_AUTO_TEST_CASE(ai_mask_tools_count_in_catalog)
{
    auto& tools = ToolCatalog::allTools();
    int maskCount = 0;
    for (auto& t : tools) {
        if (t.name.find("layer_mask_") == 0 || t.name.find("mask_") == 0
            || t.name == "selection_to_mask")
            ++maskCount;
    }
    BOOST_CHECK_EQUAL(maskCount, 13);
}

// ═══════════════════════════════════════════════════════════════════
// Category L — Edge Cases
// ═══════════════════════════════════════════════════════════════════

BOOST_AUTO_TEST_CASE(edge_mask_on_group_node)
{
    MaskFixture f;
    f.ctrl.newGroup();
    // Find the group node in the flat list
    auto flat = f.doc.flatten();
    int groupIdx = -1;
    for (int i = 0; i < static_cast<int>(flat.size()); ++i) {
        if (flat[i]->type == LayerTreeNode::Type::Group) {
            groupIdx = i;
            break;
        }
    }
    BOOST_REQUIRE(groupIdx >= 0);
    f.ctrl.addLayerMask(groupIdx);
    BOOST_CHECK(!f.ctrl.hasLayerMask(groupIdx));
}

BOOST_AUTO_TEST_CASE(edge_undo_redo_empty_history)
{
    MaskFixture f;
    // Undo/redo with empty history should be safe
    f.ctrl.undo();
    f.ctrl.redo();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(edge_add_remove_mask_many_times)
{
    MaskFixture f;
    for (int i = 0; i < 10; ++i) {
        f.ctrl.addLayerMask(0);
        BOOST_CHECK(f.hasMask());
        f.ctrl.removeLayerMask(0);
        BOOST_CHECK(!f.hasMask());
    }
}

BOOST_AUTO_TEST_CASE(edge_density_does_not_affect_mask_image)
{
    MaskFixture f;
    f.addMask();
    auto* l = f.active();
    setMaskRect(l, 10, 10, 20, 20, 0);
    QImage before = l->maskImage.copy();
    f.ctrl.setMaskDensity(f.activeIdx(), 0.3f);
    // maskImage should be unchanged (density is non-destructive)
    BOOST_CHECK(before == l->maskImage);
}

BOOST_AUTO_TEST_CASE(edge_paste_preserves_dimensions)
{
    MaskFixture f;
    f.addMask();
    f.ctrl.copyLayerMask(f.activeIdx());

    // Create a layer with different-sized image
    f.ctrl.newLayer();
    auto* l = f.active();
    l->cpuImage = QImage(50, 50, QImage::Format_RGBA8888);
    l->cpuImage.fill(Qt::red);

    f.ctrl.pasteLayerMask(f.activeIdx());
    // Pasted mask should be the original size (200x150), not the layer size (50x50)
    BOOST_CHECK_EQUAL(l->maskImage.width(), 200);
    BOOST_CHECK_EQUAL(l->maskImage.height(), 150);
}

// ═══════════════════════════════════════════════════════════════════
// Category M — Delete selection with layer transform
// (Regression: layer->owner must be set, otherwise makeLayerMask
//  ignores layer transform and deletes wrong pixels)
// ═══════════════════════════════════════════════════════════════════

struct TransformDeleteFixture {
    Document doc;
    ImageController ctrl;

    TransformDeleteFixture() : ctrl() {
        doc.size = QSize(200, 150);
        doc.selection.create(200, 150);
        doc.selection.clear();
        ctrl.setDocument(&doc);
    }

    LayerTreeNode* makeLayer(bool setOwner) {
        auto node = std::make_unique<LayerTreeNode>();
        node->type = LayerTreeNode::Type::Layer;
        node->name = "test";
        node->layer = std::make_shared<Layer>();
        node->layer->name = "test";
        node->layer->cpuImage = QImage(200, 150, QImage::Format_RGBA8888);
        node->layer->cpuImage.fill(QColor(255, 0, 0)); // solid red
        if (setOwner)
            node->layer->owner = node.get();
        auto* ptr = node.get();
        doc.roots.push_back(std::move(node));
        doc.activeFlatIndex = 0;
        return ptr;
    }
};

BOOST_AUTO_TEST_CASE(delete_selection_with_transform_owner_set)
{
    // When owner IS set, makeLayerMask correctly warps the document-space
    // selection to layer-pixel space using the layer's transform.
    TransformDeleteFixture f;
    auto* node = f.makeLayer(true);
    auto* layer = f.doc.activeLayer();
    BOOST_REQUIRE(layer);

    // Translate layer 20px to the right in pixel space.
    // NDC shift: 20px / (200/2) = 0.2 in X
    node->transform = QTransform::fromTranslate(0.2f, 0.0f);

    // Create selection covering document rect (60,0)-(99,149).
    // With translate(0.2,0), layer pixel (lx,ly) appears at doc (lx+20,ly).
    // So doc rect (60,0)-(99,149) maps to layer rect (40,0)-(79,149).
    f.doc.selection.image().fill(0);
    for (int y = 0; y < 150; ++y)
        for (int x = 60; x < 100; ++x)
            f.doc.selection.image().bits()[y * 200 + x] = 255;
    f.doc.selection.setActive(true);

    f.ctrl.executeTool("delete_selected", {});

    // Layer pixel (39,0) → doc (59,0) → OUTSIDE selection → preserved
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(39, 0).alpha(), 255);
    // Layer pixel (40,0) → doc (60,0) → INSIDE selection → cleared
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(40, 0).alpha(), 0);
    // Layer pixel (79,0) → doc (99,0) → INSIDE selection → cleared
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(79, 0).alpha(), 0);
    // Layer pixel (80,0) → doc (100,0) → OUTSIDE selection → preserved
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(80, 0).alpha(), 255);
}

BOOST_AUTO_TEST_CASE(delete_selection_with_transform_no_owner)
{
    // Without owner, makeLayerMask uses identity transform.
    // The selection at doc (60,0)-(99,149) is applied directly to
    // layer pixels (60,0)-(79,149), IGNORING the layer's 20px shift.
    // This is the bug: layer pixel (40,0) that should be cleared
    // stays red, while layer pixel (80,0) is wrongly cleared.
    TransformDeleteFixture f;
    auto* node = f.makeLayer(false); // owner = nullptr
    auto* layer = f.doc.activeLayer();
    BOOST_REQUIRE(layer);

    // Same transform and selection as owner-set test
    node->transform = QTransform::fromTranslate(0.2f, 0.0f);

    f.doc.selection.image().fill(0);
    for (int y = 0; y < 150; ++y)
        for (int x = 60; x < 100; ++x)
            f.doc.selection.image().bits()[y * 200 + x] = 255;
    f.doc.selection.setActive(true);

    f.ctrl.executeTool("delete_selected", {});

    // Without owner: identity → doc rect (60,0)-(99,149) maps directly
    // to layer rect (60,0)-(79,149).

    // Layer pixel (40,0) → doc (40,0) → OUTSIDE → wrongly preserved
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(40, 0).alpha(), 255);
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(40, 0).red(), 255);

    // Layer pixel (80,0) → doc (80,0) → INSIDE → wrongly cleared
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(80, 0).alpha(), 0);

    // Layer pixel (60,0) → doc (60,0) → INSIDE → correctly cleared
    // (the 60-79 range happens to overlap between both mappings)
    BOOST_CHECK_EQUAL(layer->cpuImage.pixelColor(60, 0).alpha(), 0);
}

BOOST_AUTO_TEST_SUITE_END()
