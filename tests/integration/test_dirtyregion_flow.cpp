#define BOOST_TEST_MODULE DirtyRegionFlowTest
#include <boost/test/included/unit_test.hpp>

#include <QApplication>

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

struct DirtyFlowFixture {
    Document doc;
    ImageController ctrl;

    DirtyFlowFixture() : ctrl() {
        // Must be >= 256x256 for auto-tiling to enable
        doc.size = QSize(512, 512);
        doc.selection.create(512, 512);
        doc.selection.clear();
        ctrl.setDocument(&doc);
        ctrl.newLayer();
    }

    Layer* active() { return doc.activeLayer(); }

    void clearDirtyTiles() {
        Layer* l = active();
        if (l) {
            for (auto* t : l->tileManager.dirtyTiles())
                t->state = core::Tile::State::GPUReady;
        }
    }
};

BOOST_AUTO_TEST_SUITE(dirty_region_flow)

BOOST_AUTO_TEST_CASE(filter_marks_tiles_dirty)
{
    DirtyFlowFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);
    BOOST_REQUIRE(layer->tiledSystem);

    // Clear initial dirty state from creation
    f.clearDirtyTiles();
    BOOST_CHECK(layer->tileManager.dirtyTiles().empty());

    // Apply a filter
    f.ctrl.executeTool("grayscale", {});
    // After filter, tileManager should have dirty tiles
    auto dirty = layer->tileManager.dirtyTiles();
    BOOST_CHECK(!dirty.empty());
}

BOOST_AUTO_TEST_CASE(undo_marks_tiles_dirty)
{
    DirtyFlowFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);

    f.ctrl.executeTool("grayscale", {});
    f.clearDirtyTiles();
    BOOST_CHECK(layer->tileManager.dirtyTiles().empty());

    f.ctrl.undo();
    auto dirty = layer->tileManager.dirtyTiles();
    BOOST_CHECK(!dirty.empty());
}

BOOST_AUTO_TEST_CASE(redo_marks_tiles_dirty)
{
    DirtyFlowFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);

    f.ctrl.executeTool("grayscale", {});
    f.ctrl.undo();
    f.clearDirtyTiles();
    BOOST_CHECK(layer->tileManager.dirtyTiles().empty());

    f.ctrl.redo();
    auto dirty = layer->tileManager.dirtyTiles();
    BOOST_CHECK(!dirty.empty());
}

BOOST_AUTO_TEST_CASE(multiple_filters_accumulate_dirty)
{
    DirtyFlowFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);

    f.clearDirtyTiles();
    BOOST_CHECK(layer->tileManager.dirtyTiles().empty());

    f.ctrl.executeTool("invert_colors", {});
    BOOST_CHECK(!layer->tileManager.dirtyTiles().empty());
}

BOOST_AUTO_TEST_CASE(filter_marks_tiles_all_dirty)
{
    DirtyFlowFixture f;
    Layer* layer = f.active();
    BOOST_REQUIRE(layer != nullptr);

    f.clearDirtyTiles();

    // grayscale on a tiled system should mark all tiles
    f.ctrl.executeTool("grayscale", {});
    auto dirty = layer->tileManager.dirtyTiles();
    int totalTiles = layer->tileManager.totalTiles();
    BOOST_CHECK_EQUAL(static_cast<int>(dirty.size()), totalTiles);
}

BOOST_AUTO_TEST_CASE(composition_generation_increments_on_filter)
{
    DirtyFlowFixture f;
    uint64_t genBefore = f.doc.compositionGeneration;

    f.ctrl.executeTool("grayscale", {});
    uint64_t genAfter = f.doc.compositionGeneration;

    BOOST_CHECK(genAfter > genBefore);
}

BOOST_AUTO_TEST_SUITE_END()
