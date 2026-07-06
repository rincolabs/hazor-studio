#define BOOST_TEST_MODULE CommandHistoryTest
#include <boost/test/included/unit_test.hpp>

#include "controller/CommandHistory.hpp"
#include "controller/Commands.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "tools/ToolCall.hpp"

#include <QImage>
#include <QColor>
#include <QTransform>

static QImage makeImage(int w, int h, uchar r, uchar g, uchar b, uchar a = 255)
{
    QImage img(w, h, QImage::Format_RGBA8888);
    img.fill(QColor(r, g, b, a));
    return img;
}

static QImage redSquare()   { return makeImage(10, 10, 255, 0, 0); }
static QImage blueSquare()  { return makeImage(10, 10, 0, 0, 255); }
static QImage greenSquare() { return makeImage(10, 10, 0, 255, 0); }

struct HistoryFixture {
    Document doc;
    LayerTreeNode* node;

    HistoryFixture() {
        doc.size = QSize(100, 100);
        doc.selection.create(100, 100);
        auto n = std::make_unique<LayerTreeNode>();
        n->type = LayerTreeNode::Type::Layer;
        n->name = "Layer 1";
        n->layer = std::make_shared<Layer>();
        n->layer->name = "Layer 1";
        n->layer->cpuImage = redSquare();
        n->layer->owner = n.get();
        node = n.get();
        doc.roots.push_back(std::move(n));
        doc.activeFlatIndex = 0;
    }
};

BOOST_AUTO_TEST_SUITE(commandhistory_core)

BOOST_AUTO_TEST_CASE(initial_state_empty)
{
    CommandHistory hist;
    BOOST_CHECK(!hist.canUndo());
    BOOST_CHECK(!hist.canRedo());
    BOOST_CHECK_EQUAL(hist.size(), 0);
    BOOST_CHECK_EQUAL(hist.currentIndex(), -1);
}

BOOST_AUTO_TEST_CASE(push_enables_undo)
{
    Document doc;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);
    auto n = std::make_unique<LayerTreeNode>();
    n->type = LayerTreeNode::Type::Layer;
    n->layer = std::make_shared<Layer>();
    n->layer->cpuImage = redSquare();
    n->layer->owner = n.get();
    doc.roots.push_back(std::move(n));
    doc.activeFlatIndex = 0;

    QImage before = doc.activeLayer()->cpuImage.copy();
    CommandHistory hist;
    hist.push(std::make_unique<FilterCommand>(
        &doc, 0, before, QTransform(), blueSquare(), QTransform(), "test"));

    BOOST_CHECK(hist.canUndo());
    BOOST_CHECK(!hist.canRedo());
    BOOST_CHECK_EQUAL(hist.size(), 1);
    BOOST_CHECK_EQUAL(hist.currentIndex(), 0);
}

BOOST_AUTO_TEST_CASE(undo_restores_canRedo)
{
    Document doc;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);
    auto n = std::make_unique<LayerTreeNode>();
    n->type = LayerTreeNode::Type::Layer;
    n->layer = std::make_shared<Layer>();
    n->layer->cpuImage = redSquare();
    n->layer->owner = n.get();
    doc.roots.push_back(std::move(n));
    doc.activeFlatIndex = 0;

    QImage before = doc.activeLayer()->cpuImage.copy();
    CommandHistory hist;
    hist.push(std::make_unique<FilterCommand>(
        &doc, 0, before, QTransform(), blueSquare(), QTransform(), "test"));

    hist.undo();

    BOOST_CHECK(!hist.canUndo());
    BOOST_CHECK(hist.canRedo());
}

BOOST_AUTO_TEST_CASE(redo_restores_canUndo)
{
    Document doc;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);
    auto n = std::make_unique<LayerTreeNode>();
    n->type = LayerTreeNode::Type::Layer;
    n->layer = std::make_shared<Layer>();
    n->layer->cpuImage = redSquare();
    n->layer->owner = n.get();
    doc.roots.push_back(std::move(n));
    doc.activeFlatIndex = 0;

    QImage before = doc.activeLayer()->cpuImage.copy();
    CommandHistory hist;
    hist.push(std::make_unique<FilterCommand>(
        &doc, 0, before, QTransform(), blueSquare(), QTransform(), "test"));
    hist.undo();
    hist.redo();

    BOOST_CHECK(hist.canUndo());
    BOOST_CHECK(!hist.canRedo());
}

BOOST_AUTO_TEST_CASE(undo_then_redo_preserves_state)
{
    HistoryFixture fix;
    CommandHistory hist;
    QImage before = fix.node->layer->cpuImage.copy();

    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0,
        before, QTransform(),
        blueSquare(), QTransform(),
        "blue_fill"));

    fix.node->layer->cpuImage = blueSquare();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).blue() == 255);

    hist.undo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).red() == 255);

    hist.redo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).blue() == 255);
}

BOOST_AUTO_TEST_CASE(multiple_undo_sequential)
{
    HistoryFixture fix;
    CommandHistory hist;

    QImage before1 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0, before1, QTransform(), blueSquare(), QTransform(), "to_blue"));
    fix.node->layer->cpuImage = blueSquare();

    QImage before2 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0, before2, QTransform(), greenSquare(), QTransform(), "to_green"));
    fix.node->layer->cpuImage = greenSquare();

    BOOST_CHECK_EQUAL(hist.size(), 2);
    BOOST_CHECK_EQUAL(hist.currentIndex(), 1);
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).green() == 255);

    hist.undo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).blue() == 255);

    hist.undo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).red() == 255);

    BOOST_CHECK(!hist.canUndo());
    BOOST_CHECK(hist.canRedo());
}

BOOST_AUTO_TEST_CASE(redo_sequential)
{
    HistoryFixture fix;
    CommandHistory hist;

    QImage before1 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0, before1, QTransform(), blueSquare(), QTransform(), "to_blue"));
    fix.node->layer->cpuImage = blueSquare();

    QImage before2 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0, before2, QTransform(), greenSquare(), QTransform(), "to_green"));
    fix.node->layer->cpuImage = greenSquare();

    hist.undo();
    hist.undo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).red() == 255);

    hist.redo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).blue() == 255);

    hist.redo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).green() == 255);
}

BOOST_AUTO_TEST_CASE(push_after_undo_truncates_redo_queue)
{
    HistoryFixture fix;
    CommandHistory hist;

    QImage before1 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0, before1, QTransform(), blueSquare(), QTransform(), "to_blue"));
    fix.node->layer->cpuImage = blueSquare();

    QImage before2 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0, before2, QTransform(), greenSquare(), QTransform(), "to_green"));
    fix.node->layer->cpuImage = greenSquare();

    BOOST_CHECK_EQUAL(hist.size(), 2);

    hist.undo();
    BOOST_CHECK_EQUAL(hist.size(), 2);

    QImage before3 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(
        &fix.doc, 0, before3, QTransform(),
        makeImage(10, 10, 255, 255, 0), QTransform(), "to_yellow"));
    fix.node->layer->cpuImage = makeImage(10, 10, 255, 255, 0);

    BOOST_CHECK_EQUAL(hist.size(), 2);
    BOOST_CHECK(!hist.canRedo());
    BOOST_CHECK(hist.canUndo());

    hist.undo();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).blue() == 255);
}

BOOST_AUTO_TEST_CASE(clear_resets_everything)
{
    Document doc;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);
    auto n = std::make_unique<LayerTreeNode>();
    n->type = LayerTreeNode::Type::Layer;
    n->layer = std::make_shared<Layer>();
    n->layer->cpuImage = redSquare();
    n->layer->owner = n.get();
    doc.roots.push_back(std::move(n));
    doc.activeFlatIndex = 0;

    QImage before = doc.activeLayer()->cpuImage.copy();
    CommandHistory hist;
    hist.push(std::make_unique<FilterCommand>(
        &doc, 0, before, QTransform(), blueSquare(), QTransform(), "resize"));
    hist.push(std::make_unique<FilterCommand>(
        &doc, 0, blueSquare(), QTransform(), greenSquare(), QTransform(), "resize2"));

    BOOST_CHECK_EQUAL(hist.size(), 2);
    hist.clear();

    BOOST_CHECK_EQUAL(hist.size(), 0);
    BOOST_CHECK_EQUAL(hist.currentIndex(), -1);
    BOOST_CHECK(!hist.canUndo());
    BOOST_CHECK(!hist.canRedo());
}

BOOST_AUTO_TEST_SUITE_END()

// ── Concrete Command Tests ──────────────────────────────────────

BOOST_AUTO_TEST_SUITE(commandhistory_concrete)

BOOST_AUTO_TEST_CASE(filtercommand_execute_applies_image)
{
    HistoryFixture fix;
    QImage before = fix.node->layer->cpuImage.copy();

    FilterCommand cmd(&fix.doc, 0, before, QTransform(),
                      blueSquare(), QTransform(), "test");
    cmd.execute();

    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).blue() == 255);
}

BOOST_AUTO_TEST_CASE(filtercommand_undo_restores_image)
{
    HistoryFixture fix;
    QImage before = fix.node->layer->cpuImage.copy();

    FilterCommand cmd(&fix.doc, 0, before, QTransform(),
                      blueSquare(), QTransform(), "test");
    cmd.execute();
    cmd.undo();

    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).red() == 255);
}

BOOST_AUTO_TEST_CASE(filtercommand_preserves_transform)
{
    HistoryFixture fix;
    QTransform beforeXf;
    beforeXf.translate(0.5f, 0.3f);
    fix.node->transform = beforeXf;

    QTransform afterXf;
    afterXf.scale(2.0f, 2.0f);

    FilterCommand cmd(&fix.doc, 0,
                      fix.node->layer->cpuImage.copy(), beforeXf,
                      blueSquare(), afterXf, "test");

    cmd.execute();
    BOOST_CHECK_CLOSE(fix.node->transform.m22(), 2.0f, 0.001f);
    BOOST_CHECK_CLOSE(fix.node->transform.m31(), 0.0f, 0.001f);

    cmd.undo();
    BOOST_CHECK_CLOSE(fix.node->transform.m31(), 0.5f, 0.001f);
    BOOST_CHECK_CLOSE(fix.node->transform.m32(), 0.3f, 0.001f);
}

BOOST_AUTO_TEST_CASE(filtercommand_null_doc_safe)
{
    FilterCommand cmd(nullptr, 0, redSquare(), QTransform(),
                      blueSquare(), QTransform(), "test");
    cmd.execute();
    cmd.undo();
    BOOST_CHECK(true); // should not crash
}

BOOST_AUTO_TEST_CASE(filtercommand_negative_index_safe)
{
    HistoryFixture fix;
    FilterCommand cmd(&fix.doc, -1, redSquare(), QTransform(),
                      blueSquare(), QTransform(), "test");
    cmd.execute();
    cmd.undo();
    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(selectioncommand_execute_sets_mask)
{
    Document doc;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);

    QImage beforeMask(100, 100, QImage::Format_Grayscale8);
    beforeMask.fill(0);

    QImage afterMask(100, 100, QImage::Format_Grayscale8);
    afterMask.fill(255);

    SelectionCommand cmd(&doc, beforeMask, afterMask, false, true, "test");
    cmd.execute();

    BOOST_CHECK(doc.selection.active());
    BOOST_CHECK_EQUAL(doc.selection.constBits()[0], 255);
}

BOOST_AUTO_TEST_CASE(selectioncommand_undo_restores_mask)
{
    Document doc;
    doc.size = QSize(100, 100);
    doc.selection.create(100, 100);

    QImage beforeMask(100, 100, QImage::Format_Grayscale8);
    beforeMask.fill(0);

    QImage afterMask(100, 100, QImage::Format_Grayscale8);
    afterMask.fill(255);

    SelectionCommand cmd(&doc, beforeMask, afterMask, false, true, "test");
    cmd.execute();
    cmd.undo();

    BOOST_CHECK(!doc.selection.active());
    BOOST_CHECK_EQUAL(doc.selection.constBits()[0], 0);
}

BOOST_AUTO_TEST_CASE(documentsizecommand_changes_size)
{
    Document doc;
    doc.size = QSize(100, 100);

    DocumentSizeCommand cmd(&doc, QSize(100, 100), QSize(200, 150), "resize");
    cmd.execute();

    BOOST_CHECK_EQUAL(doc.size.width(), 200);
    BOOST_CHECK_EQUAL(doc.size.height(), 150);

    cmd.undo();
    BOOST_CHECK_EQUAL(doc.size.width(), 100);
    BOOST_CHECK_EQUAL(doc.size.height(), 100);
}

BOOST_AUTO_TEST_CASE(compositecommand_executes_in_order)
{
    HistoryFixture fix;

    auto comp = std::make_unique<CompositeCommand>("composite");
    comp->add(std::make_unique<FilterCommand>(
        &fix.doc, 0,
        fix.node->layer->cpuImage.copy(), QTransform(),
        blueSquare(), QTransform(), "step1"));

    comp->add(std::make_unique<FilterCommand>(
        &fix.doc, 0,
        blueSquare(), QTransform(),
        greenSquare(), QTransform(), "step2"));

    comp->execute();
    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).green() == 255);
}

BOOST_AUTO_TEST_CASE(compositecommand_undo_reverses_order)
{
    HistoryFixture fix;
    QImage original = fix.node->layer->cpuImage.copy();

    auto comp = std::make_unique<CompositeCommand>("composite");
    comp->add(std::make_unique<FilterCommand>(
        &fix.doc, 0,
        original, QTransform(),
        blueSquare(), QTransform(), "step1"));

    comp->add(std::make_unique<FilterCommand>(
        &fix.doc, 0,
        blueSquare(), QTransform(),
        greenSquare(), QTransform(), "step2"));

    comp->execute();
    comp->undo();

    BOOST_CHECK(fix.node->layer->cpuImage.pixelColor(0, 0).red() == 255);
}

BOOST_AUTO_TEST_CASE(addlayercommand_execute_inserts_node)
{
    HistoryFixture fix;
    int before = fix.doc.flatCount();
    int idx = 0;

    auto newNode = std::make_unique<LayerTreeNode>();
    newNode->type = LayerTreeNode::Type::Layer;
    newNode->name = "New";
    newNode->layer = std::make_shared<Layer>();
    newNode->layer->cpuImage = blueSquare();
    newNode->layer->owner = newNode.get();

    AddLayerCommand cmd(&fix.doc, idx, std::move(newNode), "add");
    cmd.execute();

    BOOST_CHECK_EQUAL(fix.doc.flatCount(), before + 1);
    BOOST_CHECK(fix.doc.nodeAt(0) != nullptr);
    BOOST_CHECK_EQUAL(fix.doc.activeFlatIndex, 0);
}

BOOST_AUTO_TEST_CASE(addlayercommand_undo_removes_node)
{
    HistoryFixture fix;
    int before = fix.doc.flatCount();

    auto newNode = std::make_unique<LayerTreeNode>();
    newNode->type = LayerTreeNode::Type::Layer;
    newNode->layer = std::make_shared<Layer>();
    newNode->layer->cpuImage = blueSquare();
    newNode->layer->owner = newNode.get();

    AddLayerCommand cmd(&fix.doc, 0, std::move(newNode), "add");
    cmd.execute();
    cmd.undo();

    BOOST_CHECK_EQUAL(fix.doc.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(removelayercommand_execute_takes_node)
{
    HistoryFixture fix;
    fix.node->layer->cpuImage = redSquare();

    QImage before = fix.node->layer->cpuImage.copy();
    auto clone = fix.node->clone();

    RemoveLayerCommand cmd(&fix.doc, 0, std::move(clone), "remove");
    cmd.execute();

    BOOST_CHECK_EQUAL(fix.doc.flatCount(), 0);
}

BOOST_AUTO_TEST_CASE(removelayercommand_undo_restores_node)
{
    HistoryFixture fix;
    fix.node->layer->cpuImage = redSquare();

    auto clone = fix.node->clone();
    RemoveLayerCommand cmd(&fix.doc, 0, std::move(clone), "remove");
    cmd.execute();
    BOOST_CHECK_EQUAL(fix.doc.flatCount(), 0);

    cmd.undo();
    BOOST_CHECK_EQUAL(fix.doc.flatCount(), 1);
    auto* restored = fix.doc.nodeAt(0);
    BOOST_REQUIRE(restored);
    BOOST_REQUIRE(restored->layer);
    BOOST_CHECK_EQUAL(restored->layer->cpuImage.pixelColor(0, 0).red(), uchar(255));
}

BOOST_AUTO_TEST_CASE(duplicatelayercommand_execute_adds_copy)
{
    HistoryFixture fix;
    fix.node->layer->cpuImage = redSquare();
    int before = fix.doc.flatCount();

    auto clone = fix.node->clone();
    DuplicateLayerCommand cmd(&fix.doc, 0, std::move(clone), "duplicate");
    cmd.execute();

    BOOST_CHECK_EQUAL(fix.doc.flatCount(), before + 1);
}

BOOST_AUTO_TEST_CASE(duplicatelayercommand_undo_takes_copy)
{
    HistoryFixture fix;
    fix.node->layer->cpuImage = redSquare();
    int before = fix.doc.flatCount();

    auto clone = fix.node->clone();
    DuplicateLayerCommand cmd(&fix.doc, 0, std::move(clone), "duplicate");
    cmd.execute();
    cmd.undo();

    BOOST_CHECK_EQUAL(fix.doc.flatCount(), before);
}

BOOST_AUTO_TEST_CASE(reordercommand_swaps_nodes)
{
    HistoryFixture fix;
    auto n2 = std::make_unique<LayerTreeNode>();
    n2->type = LayerTreeNode::Type::Layer;
    n2->name = "Layer2";
    n2->layer = std::make_shared<Layer>();
    n2->layer->cpuImage = blueSquare();
    n2->layer->owner = n2.get();
    fix.doc.roots.push_back(std::move(n2));

    BOOST_CHECK_EQUAL(fix.doc.nodeAt(0)->name.toStdString(), "Layer 1");
    BOOST_CHECK_EQUAL(fix.doc.nodeAt(1)->name.toStdString(), "Layer2");

    ReorderCommand cmd(&fix.doc, 0, 1, "reorder");
    cmd.execute();

    BOOST_CHECK_EQUAL(fix.doc.nodeAt(0)->name.toStdString(), "Layer2");
    BOOST_CHECK_EQUAL(fix.doc.nodeAt(1)->name.toStdString(), "Layer 1");

    cmd.undo();
    BOOST_CHECK_EQUAL(fix.doc.nodeAt(0)->name.toStdString(), "Layer 1");
    BOOST_CHECK_EQUAL(fix.doc.nodeAt(1)->name.toStdString(), "Layer2");
}

BOOST_AUTO_TEST_CASE(movetogroupcommand_moves_and_undoes)
{
    HistoryFixture fix;

    auto group = std::make_unique<LayerTreeNode>();
    group->type = LayerTreeNode::Type::Group;
    group->name = "Group 1";
    fix.doc.roots.push_back(std::move(group));

    auto layer2 = std::make_unique<LayerTreeNode>();
    layer2->type = LayerTreeNode::Type::Layer;
    layer2->name = "Layer2";
    layer2->layer = std::make_shared<Layer>();
    layer2->layer->cpuImage = blueSquare();
    layer2->layer->owner = layer2.get();
    fix.doc.roots.push_back(std::move(layer2));

    int beforeFlat = fix.doc.flatCount();

    MoveToGroupCommand cmd(&fix.doc, 1, 0, "move_to_group");
    cmd.execute();

    cmd.undo();
    BOOST_CHECK_EQUAL(fix.doc.flatCount(), beforeFlat);
}

// ── commandAt() tests ───────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(command_at_returns_first)
{
    HistoryFixture fix;
    CommandHistory hist;

    QImage b1 = fix.node->layer->cpuImage.copy();
    QImage b2 = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b1, QTransform(), blueSquare(), QTransform(), "first"));
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b2, QTransform(), greenSquare(), QTransform(), "second"));
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b2, QTransform(), redSquare(), QTransform(), "third"));

    const Command* cmd = hist.commandAt(0);
    BOOST_REQUIRE(cmd != nullptr);
    BOOST_CHECK_EQUAL(cmd->name().toStdString(), "first");
}

BOOST_AUTO_TEST_CASE(command_at_returns_last)
{
    HistoryFixture fix;
    CommandHistory hist;

    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "a"));
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "b"));
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "z"));

    const Command* cmd = hist.commandAt(hist.size() - 1);
    BOOST_REQUIRE(cmd != nullptr);
    BOOST_CHECK_EQUAL(cmd->name().toStdString(), "z");
}

BOOST_AUTO_TEST_CASE(command_at_negative_index_returns_null)
{
    HistoryFixture fix;
    CommandHistory hist;
    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));

    BOOST_CHECK(hist.commandAt(-1) == nullptr);
}

BOOST_AUTO_TEST_CASE(command_at_equal_size_returns_null)
{
    HistoryFixture fix;
    CommandHistory hist;
    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));

    BOOST_CHECK(hist.commandAt(hist.size()) == nullptr);
}

BOOST_AUTO_TEST_CASE(command_at_after_clear_returns_null)
{
    HistoryFixture fix;
    CommandHistory hist;
    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));
    hist.clear();

    BOOST_CHECK(hist.commandAt(0) == nullptr);
}

BOOST_AUTO_TEST_CASE(command_at_name_matches)
{
    HistoryFixture fix;
    CommandHistory hist;

    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "test_filter"));

    const Command* cmd = hist.commandAt(0);
    BOOST_REQUIRE(cmd != nullptr);
    BOOST_CHECK_EQUAL(cmd->name().toStdString(), "test_filter");
}

// ── ChangeCallback tests ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(callback_fired_on_push)
{
    HistoryFixture fix;
    CommandHistory hist;
    int count = 0;
    hist.setChangeCallback([&]() { ++count; });

    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));

    BOOST_CHECK_EQUAL(count, 1);
}

BOOST_AUTO_TEST_CASE(callback_fired_on_undo)
{
    HistoryFixture fix;
    CommandHistory hist;
    int count = 0;
    hist.setChangeCallback([&]() { ++count; });

    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));
    hist.undo();

    BOOST_CHECK_EQUAL(count, 2);
}

BOOST_AUTO_TEST_CASE(callback_fired_on_redo)
{
    HistoryFixture fix;
    CommandHistory hist;
    int count = 0;
    hist.setChangeCallback([&]() { ++count; });

    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));
    hist.undo();
    hist.redo();

    BOOST_CHECK_EQUAL(count, 3);
}

BOOST_AUTO_TEST_CASE(callback_fired_on_clear)
{
    HistoryFixture fix;
    CommandHistory hist;
    int count = 0;
    hist.setChangeCallback([&]() { ++count; });

    QImage b = fix.node->layer->cpuImage.copy();
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));
    hist.clear();

    BOOST_CHECK_EQUAL(count, 2);
}

BOOST_AUTO_TEST_CASE(callback_not_fired_when_undo_has_nothing_to_undo)
{
    CommandHistory hist;
    int count = 0;
    hist.setChangeCallback([&]() { ++count; });

    hist.undo();
    BOOST_CHECK_EQUAL(count, 0);
}

BOOST_AUTO_TEST_CASE(callback_not_fired_when_redo_has_nothing_to_redo)
{
    CommandHistory hist;
    int count = 0;
    hist.setChangeCallback([&]() { ++count; });

    hist.redo();
    BOOST_CHECK_EQUAL(count, 0);
}

BOOST_AUTO_TEST_CASE(callback_not_fired_when_not_set)
{
    HistoryFixture fix;
    CommandHistory hist;

    QImage b = fix.node->layer->cpuImage.copy();
    // Should not crash
    hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "x"));
    hist.undo();
    hist.redo();
    hist.clear();

    BOOST_CHECK(true);
}

BOOST_AUTO_TEST_CASE(callback_fires_on_each_undo_during_jump)
{
    HistoryFixture fix;
    CommandHistory hist;
    int count = 0;
    hist.setChangeCallback([&]() { ++count; });

    QImage b = fix.node->layer->cpuImage.copy();
    for (int i = 0; i < 5; ++i) {
        hist.push(std::make_unique<FilterCommand>(&fix.doc, 0, b, QTransform(), blueSquare(), QTransform(), "step"));
    }

    // Undo all the way back manually (simulates jumpTo)
    while (hist.canUndo())
        hist.undo();

    // 5 pushes + 5 undos = 10
    BOOST_CHECK_EQUAL(count, 10);
}

BOOST_AUTO_TEST_SUITE_END()
