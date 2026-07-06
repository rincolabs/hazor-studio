#define BOOST_TEST_MODULE MultiSelectTest
#include <boost/test/included/unit_test.hpp>

#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "controller/ImageController.hpp"
#include "controller/CommandHistory.hpp"
#include "controller/Commands.hpp"
#include "engine/ImageEngine.hpp"

#include <QImage>
#include <QColor>
#include <algorithm>

struct MultiSelectFixture {
    Document doc;
    ImageController ctrl;

    MultiSelectFixture()
        : ctrl()
    {
        doc.size = QSize(800, 600);
        doc.selection.create(800, 600);
        ctrl.setDocument(&doc);
    }

    int flatCount() const { return doc.flatCount(); }

    void addLayers(int count)
    {
        for (int i = 0; i < count; ++i)
            ctrl.newLayer();
    }
};

BOOST_AUTO_TEST_SUITE(multiselect)

// ── Document-level selection ─────────────────────────────────

BOOST_AUTO_TEST_CASE(select_node_replaces_existing)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.doc.selectNode(0);
    f.doc.selectNode(1);

    BOOST_CHECK(!f.doc.isSelected(0));
    BOOST_CHECK(f.doc.isSelected(1));
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 1);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 1);
}

BOOST_AUTO_TEST_CASE(add_to_selection_includes_both)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.doc.selectNode(0);
    f.doc.selectNode(1, true);

    BOOST_CHECK(f.doc.isSelected(0));
    BOOST_CHECK(f.doc.isSelected(1));
    BOOST_CHECK(!f.doc.isSelected(2));
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 2);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 1);
}

BOOST_AUTO_TEST_CASE(toggle_deselects_if_already_selected)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.doc.selectNode(0);
    f.doc.selectNode(1, true);
    f.doc.selectNode(0, true);

    BOOST_CHECK(!f.doc.isSelected(0));
    BOOST_CHECK(f.doc.isSelected(1));
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 1);
}

BOOST_AUTO_TEST_CASE(toggle_last_deselected_sets_active_to_remaining)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.doc.selectNode(0);
    f.doc.selectNode(1, true);
    f.doc.selectNode(1, true);

    BOOST_CHECK(f.doc.isSelected(0));
    BOOST_CHECK(!f.doc.isSelected(1));
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 0);
}

BOOST_AUTO_TEST_CASE(toggle_last_remaining_sets_active_to_minus_one)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.doc.selectNode(0);
    f.doc.selectNode(0, true);

    BOOST_CHECK(f.doc.selectedFlatIndices.empty());
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, -1);
}

BOOST_AUTO_TEST_CASE(select_range_continuous)
{
    MultiSelectFixture f;
    f.addLayers(5);

    f.doc.selectRange(0, 3);

    BOOST_CHECK(f.doc.isSelected(0));
    BOOST_CHECK(f.doc.isSelected(1));
    BOOST_CHECK(f.doc.isSelected(2));
    BOOST_CHECK(f.doc.isSelected(3));
    BOOST_CHECK(!f.doc.isSelected(4));
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 4);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 3);
}

BOOST_AUTO_TEST_CASE(select_range_reversed)
{
    MultiSelectFixture f;
    f.addLayers(5);

    f.doc.selectRange(3, 0);

    BOOST_CHECK(f.doc.isSelected(0));
    BOOST_CHECK(f.doc.isSelected(1));
    BOOST_CHECK(f.doc.isSelected(2));
    BOOST_CHECK(f.doc.isSelected(3));
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 4);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 3);
}

BOOST_AUTO_TEST_CASE(deselect_all_clears)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.doc.selectNode(0);
    f.doc.selectNode(2, true);
    f.doc.deselectAll();

    BOOST_CHECK(f.doc.selectedFlatIndices.empty());
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, -1);
}

BOOST_AUTO_TEST_CASE(selected_list_sorted)
{
    MultiSelectFixture f;
    f.addLayers(5);

    f.doc.selectNode(3);
    f.doc.selectNode(0, true);
    f.doc.selectNode(4, true);

    auto list = f.doc.selectedList();
    BOOST_CHECK_EQUAL(list.size(), 3);
    BOOST_CHECK_EQUAL(list[0], 0);
    BOOST_CHECK_EQUAL(list[1], 3);
    BOOST_CHECK_EQUAL(list[2], 4);
}

BOOST_AUTO_TEST_CASE(active_index_outside_selction_clamps_to_last_selected)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.doc.selectNode(0);
    f.doc.activeFlatIndex = 5;  // manually set out of range
    f.doc.selectNode(3);        // triggers clamping

    BOOST_CHECK(f.doc.isSelected(3));
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 3);
}

// ── Controller-level selection ─────────────────────────────

BOOST_AUTO_TEST_CASE(controller_set_multi_select_add)
{
    MultiSelectFixture f;
    f.addLayers(4);

    f.ctrl.setMultiSelectNode(0, false);
    f.ctrl.setMultiSelectNode(2, true);

    auto indices = f.ctrl.selectedIndices();
    BOOST_CHECK_EQUAL(indices.size(), 2);
    BOOST_CHECK(std::find(indices.begin(), indices.end(), 0) != indices.end());
    BOOST_CHECK(std::find(indices.begin(), indices.end(), 2) != indices.end());
}

BOOST_AUTO_TEST_CASE(controller_select_all_nodes)
{
    MultiSelectFixture f;
    f.addLayers(4);

    f.ctrl.selectAllNodes();

    // 4 addLayers → indices 0-3
    BOOST_CHECK_EQUAL(f.ctrl.selectedIndices().size(), 4);
    BOOST_CHECK_EQUAL(f.ctrl.activeLayerIndex(), 3);
}

BOOST_AUTO_TEST_CASE(controller_remove_selected_nodes)
{
    MultiSelectFixture f;
    f.addLayers(5);
    int before = f.flatCount();

    f.ctrl.setMultiSelectNode(0, false);
    f.ctrl.setMultiSelectNode(2, true);
    f.ctrl.setMultiSelectNode(4, true);

    f.ctrl.removeSelectedNodes();

    // 5 layers → remove 3 → 2 remain
    BOOST_CHECK_EQUAL(f.flatCount(), before - 3);
}

BOOST_AUTO_TEST_CASE(controller_remove_unselected_remain)
{
    MultiSelectFixture f;
    f.addLayers(4);

    f.ctrl.setMultiSelectNode(0, false);
    f.ctrl.setMultiSelectNode(3, true);

    f.ctrl.removeSelectedNodes();

    // original [0, 1, 2, 3], remove {0, 3} → [1, 2]
    BOOST_CHECK_EQUAL(f.flatCount(), 2);
}

BOOST_AUTO_TEST_CASE(controller_set_selected_indices)
{
    MultiSelectFixture f;
    f.addLayers(5);

    f.ctrl.setSelectedIndices({1, 3, 4});

    auto indices = f.ctrl.selectedIndices();
    BOOST_CHECK_EQUAL(indices.size(), 3);
    BOOST_CHECK_EQUAL(f.ctrl.activeLayerIndex(), 4);
}

BOOST_AUTO_TEST_CASE(controller_set_selected_indices_empty)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.ctrl.setSelectedIndices({});

    BOOST_CHECK(f.ctrl.selectedIndices().empty());
    BOOST_CHECK_EQUAL(f.ctrl.activeLayerIndex(), -1);
}

BOOST_AUTO_TEST_CASE(controller_invalid_index_deselects_all)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.ctrl.setMultiSelectNode(-1, false);

    BOOST_CHECK(f.ctrl.selectedIndices().empty());
    BOOST_CHECK_EQUAL(f.ctrl.activeLayerIndex(), -1);
}

BOOST_AUTO_TEST_CASE(controller_select_range)
{
    MultiSelectFixture f;
    f.addLayers(5);

    f.ctrl.selectNodeRange(1, 3);

    auto indices = f.ctrl.selectedIndices();
    BOOST_CHECK_EQUAL(indices.size(), 3);
    BOOST_CHECK_EQUAL(indices[0], 1);
    BOOST_CHECK_EQUAL(indices[1], 2);
    BOOST_CHECK_EQUAL(indices[2], 3);
}

// ── Undo/Redo ────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(undo_remove_restores_layer_count)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.ctrl.setMultiSelectNode(0, false);
    f.ctrl.setMultiSelectNode(2, true);

    int beforeCount = f.flatCount();
    f.ctrl.removeSelectedNodes();
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount - 2);

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount);
}

BOOST_AUTO_TEST_CASE(undo_remove_redo)
{
    MultiSelectFixture f;
    f.addLayers(3);

    f.ctrl.setMultiSelectNode(0, false);
    f.ctrl.setMultiSelectNode(1, true);
    f.ctrl.setMultiSelectNode(2, true);

    int beforeCount = f.flatCount();
    f.ctrl.removeSelectedNodes();
    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount);

    f.ctrl.history().redo();
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount - 3);
}

BOOST_AUTO_TEST_CASE(undo_remove_layer_restores_count)
{
    MultiSelectFixture f;
    f.addLayers(3);

    int beforeCount = f.flatCount();
    // remove_layer removes all selected nodes (active layer is last added)
    f.ctrl.executeTool("remove_layer", {});
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount - 1);

    f.ctrl.history().undo();
    BOOST_CHECK_EQUAL(f.flatCount(), beforeCount);
}

// ── Selection invariant: newLayer / setActiveNode sync selectedFlatIndices ──

BOOST_AUTO_TEST_CASE(new_layer_on_empty_doc_syncs_selected_indices)
{
    MultiSelectFixture f;
    f.addLayers(1);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 0);
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 1);
    BOOST_CHECK(f.doc.isSelected(0));
}

BOOST_AUTO_TEST_CASE(new_layer_after_first_syncs_selected_indices)
{
    MultiSelectFixture f;
    f.addLayers(2);
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 1);
    BOOST_CHECK(f.doc.isSelected(f.doc.activeFlatIndex));
    BOOST_CHECK_EQUAL(*f.doc.selectedFlatIndices.begin(), f.doc.activeFlatIndex);
}

BOOST_AUTO_TEST_CASE(set_active_node_syncs_selected_indices)
{
    MultiSelectFixture f;
    f.addLayers(3);
    f.ctrl.setActiveNode(0);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 0);
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 1);
    BOOST_CHECK(f.doc.isSelected(0));
    BOOST_CHECK(!f.doc.isSelected(1));
    BOOST_CHECK(!f.doc.isSelected(2));
}

BOOST_AUTO_TEST_CASE(set_active_node_invalid_deselects_all)
{
    MultiSelectFixture f;
    f.addLayers(2);
    f.ctrl.setActiveNode(-1);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, -1);
    BOOST_CHECK(f.doc.selectedFlatIndices.empty());
}

// ── Ctrl+click toggle (via setMultiSelectNode with addToSelection=true) ──

BOOST_AUTO_TEST_CASE(ctrl_click_deselects_from_multi_selection)
{
    MultiSelectFixture f;
    f.addLayers(3);
    f.doc.selectNode(0);
    f.doc.selectNode(1, true);
    BOOST_REQUIRE_EQUAL(f.doc.selectedFlatIndices.size(), 2);

    f.ctrl.setMultiSelectNode(1, true);

    BOOST_CHECK(f.doc.isSelected(0));
    BOOST_CHECK(!f.doc.isSelected(1));
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 1);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 0);
}

BOOST_AUTO_TEST_CASE(ctrl_click_on_only_selected_layer_clears_all)
{
    MultiSelectFixture f;
    f.addLayers(2);
    f.doc.selectNode(0);
    BOOST_REQUIRE_EQUAL(f.doc.selectedFlatIndices.size(), 1);

    f.ctrl.setMultiSelectNode(0, true);

    BOOST_CHECK(f.doc.selectedFlatIndices.empty());
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, -1);
}

BOOST_AUTO_TEST_CASE(ctrl_click_on_unselected_adds_it)
{
    MultiSelectFixture f;
    f.addLayers(3);
    f.doc.selectNode(0);
    BOOST_REQUIRE_EQUAL(f.doc.selectedFlatIndices.size(), 1);

    f.ctrl.setMultiSelectNode(1, true);

    BOOST_CHECK(f.doc.isSelected(0));
    BOOST_CHECK(f.doc.isSelected(1));
    BOOST_CHECK_EQUAL(f.doc.selectedFlatIndices.size(), 2);
    BOOST_CHECK_EQUAL(f.doc.activeFlatIndex, 1);
}

BOOST_AUTO_TEST_SUITE_END()
