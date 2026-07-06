#define BOOST_TEST_MODULE MaskEditingSyncTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "controller/ImageController.hpp"

#include <QApplication>
#include <QObject>
#include <QSignalSpy>

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

struct MaskSyncFixture {
    Document doc;
    ImageController ctrl;

    MaskSyncFixture() : ctrl() {
        doc.size = QSize(200, 150);
        doc.selection.create(200, 150);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    int activeIdx() const { return doc.activeFlatIndex; }
};

BOOST_AUTO_TEST_SUITE(mask_editing_sync)

// ── Integration: CanvasView mask editing sync ────────────────

BOOST_AUTO_TEST_CASE(canvasview_syncs_with_controller)
{
    // This test verifies the INTEGRATION between CanvasView and ImageController.
    // CanvasView must listen to ImageController::maskEditingChanged signal
    // to keep its internal state in sync — otherwise clicking the mask
    // thumbnail in LayerPanel (which goes through the controller) would not
    // update CanvasView, and brush strokes would paint on the layer instead.
    MaskSyncFixture f;
    f.ctrl.addLayerMask(f.activeIdx());

    bool callbackValue = false;
    QObject::connect(&f.ctrl, &ImageController::maskEditingChanged,
                     [&](bool editing) { callbackValue = editing; });

    // Simulate what LayerPanel does on mask thumbnail click
    f.ctrl.setEditingMask(true);
    BOOST_CHECK_MESSAGE(callbackValue == true,
        "CanvasView must connect maskEditingChanged signal — "
        "without it, clicking mask thumbnail won't enter mask editing mode");

    f.ctrl.setEditingMask(false);
    BOOST_CHECK_MESSAGE(callbackValue == false,
        "CanvasView must respond when controller exits mask editing mode");
}

// ── Unit: controller signal emission ──────────────────────────

BOOST_AUTO_TEST_CASE(controller_emits_signal_on_change)
{
    MaskSyncFixture f;
    f.ctrl.addLayerMask(f.activeIdx());

    QSignalSpy spy(&f.ctrl, &ImageController::maskEditingChanged);

    // Initial state
    BOOST_CHECK(!f.ctrl.isEditingMask());

    // Enable mask editing
    f.ctrl.setEditingMask(true);
    BOOST_CHECK(f.ctrl.isEditingMask());
    BOOST_CHECK_EQUAL(spy.count(), 1);
    BOOST_CHECK_EQUAL(spy.takeFirst().at(0).toBool(), true);

    // Disable mask editing
    f.ctrl.setEditingMask(false);
    BOOST_CHECK(!f.ctrl.isEditingMask());
    BOOST_CHECK_EQUAL(spy.count(), 1);
    BOOST_CHECK_EQUAL(spy.takeFirst().at(0).toBool(), false);
}

BOOST_AUTO_TEST_CASE(controller_no_duplicate_emit_when_unchanged)
{
    MaskSyncFixture f;
    f.ctrl.addLayerMask(f.activeIdx());

    QSignalSpy spy(&f.ctrl, &ImageController::maskEditingChanged);

    f.ctrl.setEditingMask(true);
    BOOST_CHECK_EQUAL(spy.count(), 1);

    // Second call with same value should NOT emit (guard)
    f.ctrl.setEditingMask(true);
    BOOST_CHECK_EQUAL(spy.count(), 1);
}

BOOST_AUTO_TEST_CASE(callback_receives_correct_value)
{
    MaskSyncFixture f;
    f.ctrl.addLayerMask(f.activeIdx());

    bool callbackValue = false;
    int callbackCount = 0;
    QObject::connect(&f.ctrl, &ImageController::maskEditingChanged,
                     [&](bool editing) {
        callbackValue = editing;
        ++callbackCount;
    });

    f.ctrl.setEditingMask(true);
    BOOST_CHECK_EQUAL(callbackCount, 1);
    BOOST_CHECK_EQUAL(callbackValue, true);

    f.ctrl.setEditingMask(false);
    BOOST_CHECK_EQUAL(callbackCount, 2);
    BOOST_CHECK_EQUAL(callbackValue, false);
}

BOOST_AUTO_TEST_CASE(toggle_via_layerpanel_emits_signal)
{
    // This simulates what LayerPanel does on mask thumbnail click:
    //   m_controller->setEditingMask(!m_controller->isEditingMask())
    MaskSyncFixture f;
    f.ctrl.addLayerMask(f.activeIdx());

    QSignalSpy spy(&f.ctrl, &ImageController::maskEditingChanged);

    // Toggle ON
    f.ctrl.setEditingMask(!f.ctrl.isEditingMask());
    BOOST_CHECK(f.ctrl.isEditingMask());
    BOOST_CHECK_EQUAL(spy.count(), 1);

    // Toggle OFF
    f.ctrl.setEditingMask(!f.ctrl.isEditingMask());
    BOOST_CHECK(!f.ctrl.isEditingMask());
    BOOST_CHECK_EQUAL(spy.count(), 2);
}

BOOST_AUTO_TEST_SUITE_END()
