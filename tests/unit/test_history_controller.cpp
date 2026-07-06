#define BOOST_TEST_MODULE HistoryControllerTest
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

#include <QImage>
#include <QColor>
#include <QTransform>
#include <QApplication>
#include <QObject>

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

struct HistoryControllerFixture {
    Document doc;
    ImageController ctrl;
    int signalCount;

    HistoryControllerFixture() : ctrl(), signalCount(0) {
        doc.size = QSize(200, 150);
        doc.selection.create(200, 150);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
        QObject::connect(&ctrl, &ImageController::historyChanged, [this]() { ++signalCount; });
        signalCount = 0;
    }

    Layer* active() { return doc.activeLayer(); }

    void fillLayer(uchar r, uchar g, uchar b, uchar a = 255) {
        auto* l = active();
        if (l) l->cpuImage.fill(QColor(r, g, b, a));
    }

    // Helper: push a filter command manually (bypassing executeTool)
    void pushFilterCmd(const QString& name) {
        auto* layer = active();
        auto* node = doc.activeNode();
        QImage before = layer->cpuImage.copy();
        QTransform t = node ? node->transform : QTransform();
        // Invert image as the "after" state
        cv::Mat mat = ImageEngine::toCvMat(layer->cpuImage);
        cv::bitwise_not(mat, mat);
        layer->cpuImage = ImageEngine::toQImage(mat);
        QImage after = layer->cpuImage.copy();
        ctrl.history().push(std::make_unique<FilterCommand>(
            &doc, doc.activeFlatIndex, std::move(before), t,
            std::move(after), t, name));
    }
};

using Params = std::unordered_map<std::string, JsonValue>;

BOOST_AUTO_TEST_SUITE(history_controller)

// ── historyStateNames() ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(history_state_names_after_operations)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);

    int beforeCount = f.signalCount;
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    Q_UNUSED(beforeCount);

    QStringList names = f.ctrl.historyStateNames();
    // At least: AddLayer (from newLayer) + invert_colors + grayscale
    BOOST_CHECK(names.size() >= 3);
    BOOST_CHECK(names.last() == "grayscale");
    BOOST_CHECK(names[names.size() - 2] == "invert_colors");
}

BOOST_AUTO_TEST_CASE(history_state_names_empty_after_clear)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.clearHistory();

    QStringList names = f.ctrl.historyStateNames();
    BOOST_CHECK(names.isEmpty());
}

// ── undo() wrapper ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(undo_wrapper_restores_image)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);
    auto before = f.active()->cpuImage.copy();

    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.undo();

    BOOST_CHECK(before == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(undo_wrapper_noop_when_cant_undo)
{
    HistoryControllerFixture f;
    // Clear history so there's nothing to undo
    f.ctrl.clearHistory();

    int sigCount = f.signalCount;
    f.ctrl.undo(); // should be no-op (can't undo, nothing happens)
    BOOST_CHECK_EQUAL(f.signalCount, sigCount); // no new signal
}

// ── redo() wrapper ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(redo_wrapper_restores_image)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);
    auto before = f.active()->cpuImage.copy();

    f.ctrl.executeTool("invert_colors", {});
    auto afterUndo = f.active()->cpuImage.copy();
    f.ctrl.undo(); // back to before
    f.ctrl.redo();

    BOOST_CHECK(afterUndo == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(redo_wrapper_noop_when_cant_redo)
{
    HistoryControllerFixture f;
    auto before = f.active()->cpuImage.copy();

    f.ctrl.redo(); // nothing to redo
    BOOST_CHECK(before == f.active()->cpuImage);
}

// ── jumpToHistoryState() ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(jump_to_same_index_is_noop)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});

    auto img = f.active()->cpuImage.copy();
    int idx = f.ctrl.history().currentIndex();
    f.ctrl.jumpToHistoryState(idx);

    BOOST_CHECK(img == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(jump_to_past_state_restores_correctly)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);

    auto state0 = f.active()->cpuImage.copy(); // after fillLayer

    f.ctrl.executeTool("invert_colors", {});
    auto state1 = f.active()->cpuImage.copy();

    f.ctrl.executeTool("grayscale", {});
    // state2 = after grayscale (current)

    f.ctrl.jumpToHistoryState(f.ctrl.history().currentIndex() - 1);
    BOOST_CHECK(state1 == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(jump_to_future_state_restores_correctly)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);

    f.ctrl.executeTool("invert_colors", {});
    auto state1 = f.active()->cpuImage.copy();

    f.ctrl.executeTool("grayscale", {});
    auto state2 = f.active()->cpuImage.copy();

    f.ctrl.jumpToHistoryState(f.ctrl.history().currentIndex() - 1);
    // now at state1
    BOOST_CHECK(state1 == f.active()->cpuImage);

    f.ctrl.jumpToHistoryState(f.ctrl.history().currentIndex() + 1);
    BOOST_CHECK(state2 == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(jump_to_earliest_state)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);

    auto state0 = f.active()->cpuImage.copy();
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    f.ctrl.executeTool("flip_horizontal", {});

    // Jump to earliest (first state after AddLayer)
    f.ctrl.jumpToHistoryState(0);
    // The first state should be AddLayer (from newLayer), but since we filled after,
    // we need to check that we're before invert_colors
    BOOST_CHECK(state0 == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(jump_to_latest_state)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);

    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    auto state2 = f.active()->cpuImage.copy();

    // Save latest index before jumping
    int latestIdx = f.ctrl.history().currentIndex();
    // Go back to start
    f.ctrl.jumpToHistoryState(0);
    // Go to latest
    f.ctrl.jumpToHistoryState(latestIdx);

    BOOST_CHECK(state2 == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(jump_to_negative_index_does_nothing)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});

    auto img = f.active()->cpuImage.copy();
    f.ctrl.jumpToHistoryState(-1);

    BOOST_CHECK(img == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(jump_to_out_of_bounds_does_nothing)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});

    auto img = f.active()->cpuImage.copy();
    f.ctrl.jumpToHistoryState(999);

    BOOST_CHECK(img == f.active()->cpuImage);
}

BOOST_AUTO_TEST_CASE(jump_past_then_forward_then_back)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);

    auto state0 = f.active()->cpuImage.copy();
    f.ctrl.executeTool("invert_colors", {});
    auto state1 = f.active()->cpuImage.copy();
    f.ctrl.executeTool("grayscale", {});
    auto state2 = f.active()->cpuImage.copy();
    f.ctrl.executeTool("flip_horizontal", {});
    auto state3 = f.active()->cpuImage.copy();

    // state3 -> state1
    f.ctrl.jumpToHistoryState(1);
    BOOST_CHECK(state1 == f.active()->cpuImage);

    // state1 -> state3
    f.ctrl.jumpToHistoryState(3);
    BOOST_CHECK(state3 == f.active()->cpuImage);

    // state3 -> state0
    f.ctrl.jumpToHistoryState(0);
    BOOST_CHECK(state0 == f.active()->cpuImage);
}

// ── clearHistory() ──────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(clear_history_removes_all)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});

    f.ctrl.clearHistory();

    BOOST_CHECK(!f.ctrl.history().canUndo());
    BOOST_CHECK(!f.ctrl.history().canRedo());
    BOOST_CHECK_EQUAL(f.ctrl.history().size(), 0);
    BOOST_CHECK_EQUAL(f.ctrl.history().currentIndex(), -1);
    BOOST_CHECK(f.ctrl.historyStateNames().isEmpty());
}

BOOST_AUTO_TEST_CASE(clear_history_resets_signal_count)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});
    f.signalCount = 0;

    f.ctrl.clearHistory();
    BOOST_CHECK_EQUAL(f.signalCount, 1);
}

// ── historyChanged signal ───────────────────────────────────────────

BOOST_AUTO_TEST_CASE(history_changed_on_push)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("invert_colors", {});
    BOOST_CHECK_EQUAL(f.signalCount, 1);
}

BOOST_AUTO_TEST_CASE(history_changed_on_undo)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});
    f.signalCount = 0;

    f.ctrl.undo();
    BOOST_CHECK_EQUAL(f.signalCount, 1);
}

BOOST_AUTO_TEST_CASE(history_changed_on_redo)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.undo();
    f.signalCount = 0;

    f.ctrl.redo();
    BOOST_CHECK_EQUAL(f.signalCount, 1);
}

BOOST_AUTO_TEST_CASE(history_changed_on_jump)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    f.signalCount = 0;

    f.ctrl.jumpToHistoryState(0);
    // jump calls undo twice → 2 signal emissions
    BOOST_CHECK_EQUAL(f.signalCount, 2);
}

BOOST_AUTO_TEST_CASE(history_changed_on_clear)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});
    f.signalCount = 0;

    f.ctrl.clearHistory();
    BOOST_CHECK_EQUAL(f.signalCount, 1);
}

BOOST_AUTO_TEST_CASE(history_changed_not_emitted_on_failed_undo)
{
    HistoryControllerFixture f;
    f.ctrl.clearHistory(); // remove all commands
    f.signalCount = 0;     // reset after clear's signal

    f.ctrl.undo();
    BOOST_CHECK_EQUAL(f.signalCount, 0);
}

BOOST_AUTO_TEST_CASE(history_changed_not_emitted_on_failed_redo)
{
    HistoryControllerFixture f;
    f.signalCount = 0;

    f.ctrl.redo();
    BOOST_CHECK_EQUAL(f.signalCount, 0);
}

// ── Edge cases ──────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(jump_on_empty_history)
{
    HistoryControllerFixture f;
    // No operations performed

    // Should not crash or affect signal count
    f.ctrl.jumpToHistoryState(0);
    BOOST_CHECK_EQUAL(f.signalCount, 0);
}

BOOST_AUTO_TEST_CASE(controller_without_document)
{
    ImageController orphan;
    // No document set

    BOOST_CHECK(orphan.historyStateNames().isEmpty());
    // Should not crash
    orphan.undo();
    orphan.redo();
    orphan.jumpToHistoryState(0);
    orphan.clearHistory();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(undo_and_redo_preserve_signal_state)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);
    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});

    int expected = f.signalCount;
    f.ctrl.undo();
    f.ctrl.undo();
    expected += 2;

    f.ctrl.redo();
    f.ctrl.redo();
    expected += 2;

    BOOST_CHECK_EQUAL(f.signalCount, expected);
}

BOOST_AUTO_TEST_CASE(sequential_push_update_names)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("invert_colors", {});
    QStringList names1 = f.ctrl.historyStateNames();
    int len1 = names1.size();
    BOOST_CHECK(names1.last() == "invert_colors");

    f.ctrl.executeTool("grayscale", {});
    QStringList names2 = f.ctrl.historyStateNames();
    BOOST_CHECK_EQUAL(names2.size(), len1 + 1);
    BOOST_CHECK(names2.last() == "grayscale");
}

BOOST_AUTO_TEST_CASE(jump_forward_after_new_push_truncates_future)
{
    HistoryControllerFixture f;
    f.fillLayer(100, 100, 100);

    f.ctrl.executeTool("invert_colors", {});
    f.ctrl.executeTool("grayscale", {});
    f.ctrl.undo(); // back to state0 (after invert_colors)

    // Push a new tool — should truncate "grayscale" from future
    f.ctrl.executeTool("flip_horizontal", {});

    QStringList names = f.ctrl.historyStateNames();
    BOOST_CHECK(names.last() == "flip_horizontal");
    BOOST_CHECK(names.size() >= 2);
    BOOST_CHECK(!f.ctrl.history().canRedo()); // no future states
}

// ── jumpTo via Manual Push (simulating ImageController.jumpToHistoryState) ──

BOOST_AUTO_TEST_CASE(jump_to_with_single_undo_step)
{
    HistoryControllerFixture f;
    f.fillLayer(200, 50, 100);

    f.pushFilterCmd("tool_a");
    auto stateA = f.active()->cpuImage.copy();

    f.pushFilterCmd("tool_b");
    auto stateB = f.active()->cpuImage.copy();

    f.pushFilterCmd("tool_c");
    auto stateC = f.active()->cpuImage.copy();

    // Jump from stateC back to stateB
    int target = f.ctrl.history().currentIndex() - 1;
    f.ctrl.jumpToHistoryState(target);
    BOOST_CHECK(stateB == f.active()->cpuImage);

    // Jump from stateB forward to stateC
    f.ctrl.jumpToHistoryState(target + 1);
    BOOST_CHECK(stateC == f.active()->cpuImage);

    // Jump from stateC directly to stateA (2 undos)
    f.ctrl.jumpToHistoryState(target - 1);
    BOOST_CHECK(stateA == f.active()->cpuImage);
}

BOOST_AUTO_TEST_SUITE_END()
