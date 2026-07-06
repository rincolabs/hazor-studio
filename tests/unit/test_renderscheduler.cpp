#define BOOST_TEST_MODULE RenderSchedulerTest
#include <boost/test/included/unit_test.hpp>

#include "core/RenderScheduler.hpp"
#include "core/Tile.hpp"
#include <thread>
#include <chrono>

// Forward decl matching what RenderScheduler uses (core::Layer)
namespace core { class Layer; }

BOOST_AUTO_TEST_SUITE(render_scheduler)

// ── LOD ───────────────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(default_lod_thresholds)
{
    core::RenderScheduler rs;
    // psp = zoom * tileSize. Default thresholds: Half=4, Quarter=8, Eighth=16
    // Full if psp <= 4
    BOOST_CHECK(rs.decideLOD(0.01f, 256) == core::RenderScheduler::LOD::Full);  // psp=2.56 <= 4
    // Half if 4 < psp <= 8
    BOOST_CHECK(rs.decideLOD(0.02f, 256) == core::RenderScheduler::LOD::Half);  // psp=5.12
    // Quarter if 8 < psp <= 16
    BOOST_CHECK(rs.decideLOD(0.04f, 256) == core::RenderScheduler::LOD::Quarter); // psp=10.24
    // Eighth if 16 < psp
    BOOST_CHECK(rs.decideLOD(0.1f, 256) == core::RenderScheduler::LOD::Eighth);  // psp=25.6
}

BOOST_AUTO_TEST_CASE(decide_lod_boundary)
{
    core::RenderScheduler rs;
    // At exact threshold: psp = 4 -> psp > 4 is false -> Full
    BOOST_CHECK(rs.decideLOD(4.0f / 256, 256) == core::RenderScheduler::LOD::Full);
    // Just above threshold: psp = 4.001 -> Half
    BOOST_CHECK(rs.decideLOD(4.001f / 256, 256) == core::RenderScheduler::LOD::Half);
}

BOOST_AUTO_TEST_CASE(decide_lod_custom_tile_size)
{
    core::RenderScheduler rs;
    // tileSize=512: psp=512*0.001=0.512 <= 4 -> Full
    BOOST_CHECK(rs.decideLOD(0.001f, 512) == core::RenderScheduler::LOD::Full);
    // psp=512*0.01=5.12 > 4 -> Half
    BOOST_CHECK(rs.decideLOD(0.01f, 512) == core::RenderScheduler::LOD::Half);
    // psp=512*0.02=10.24 > 8 -> Quarter
    BOOST_CHECK(rs.decideLOD(0.02f, 512) == core::RenderScheduler::LOD::Quarter);
    // psp=512*0.1=51.2 > 16 -> Eighth
    BOOST_CHECK(rs.decideLOD(0.1f, 512) == core::RenderScheduler::LOD::Eighth);
}

BOOST_AUTO_TEST_CASE(set_lod_thresholds)
{
    core::RenderScheduler rs;
    rs.setLodThresholds(2.0f, 4.0f, 8.0f);
    // Custom thresholds: Half=2, Quarter=4, Eighth=8, tileSize=256
    BOOST_CHECK(rs.decideLOD(2.0f / 256, 256) == core::RenderScheduler::LOD::Full);   // psp=2.0
    BOOST_CHECK(rs.decideLOD(2.01f / 256, 256) == core::RenderScheduler::LOD::Half);   // psp=2.01
    BOOST_CHECK(rs.decideLOD(4.01f / 256, 256) == core::RenderScheduler::LOD::Quarter); // psp=4.01
    BOOST_CHECK(rs.decideLOD(8.01f / 256, 256) == core::RenderScheduler::LOD::Eighth);  // psp=8.01
}

// ── Frame budget ─────────────────────────────────────────────

BOOST_AUTO_TEST_CASE(begin_frame_starts_timer)
{
    core::RenderScheduler rs;
    rs.beginFrame();
    float elapsed = rs.elapsedFrameMs();
    BOOST_CHECK(elapsed >= 0.0f);
    BOOST_CHECK(elapsed < 1.0f);
}

BOOST_AUTO_TEST_CASE(has_budget_left_after_begin)
{
    core::RenderScheduler rs;
    rs.setFrameBudgetMs(50.0f);
    rs.beginFrame();
    BOOST_CHECK(rs.hasBudgetLeft());
}

BOOST_AUTO_TEST_CASE(frame_budget_exhaustion)
{
    core::RenderScheduler rs;
    rs.setFrameBudgetMs(1.0f); // 1ms budget
    rs.beginFrame();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    BOOST_CHECK(!rs.hasBudgetLeft());
}

BOOST_AUTO_TEST_CASE(set_frame_budget)
{
    core::RenderScheduler rs;
    rs.setFrameBudgetMs(8.0f);
    rs.beginFrame();
    BOOST_CHECK(rs.hasBudgetLeft());
    rs.setFrameBudgetMs(0.0f);
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
    BOOST_CHECK(!rs.hasBudgetLeft());
}

// ── Schedule / Cancel ─────────────────────────────────────────

BOOST_AUTO_TEST_CASE(schedule_tile_upload_adds_job)
{
    core::RenderScheduler rs;
    core::Tile tile;
    rs.scheduleTileUpload(&tile, nullptr, core::RenderScheduler::Priority::Immediate);
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 1);
}

BOOST_AUTO_TEST_CASE(schedule_tile_upload_unique_ids)
{
    core::RenderScheduler rs;
    core::Tile t1, t2;
    rs.scheduleTileUpload(&t1, nullptr, core::RenderScheduler::Priority::Immediate);
    rs.scheduleTileUpload(&t2, nullptr, core::RenderScheduler::Priority::Visible);
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 2);
}

BOOST_AUTO_TEST_CASE(schedule_multiple_priorities)
{
    core::RenderScheduler rs;
    core::Tile t1, t2, t3;
    rs.scheduleTileUpload(&t1, nullptr, core::RenderScheduler::Priority::Immediate);
    rs.scheduleTileUpload(&t2, nullptr, core::RenderScheduler::Priority::Prefetch);
    rs.scheduleTileUpload(&t3, nullptr, core::RenderScheduler::Priority::Background);
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 3);
}

BOOST_AUTO_TEST_CASE(cancel_all_clears)
{
    core::RenderScheduler rs;
    core::Tile t1, t2;
    rs.scheduleTileUpload(&t1, nullptr, core::RenderScheduler::Priority::Immediate);
    rs.scheduleTileUpload(&t2, nullptr, core::RenderScheduler::Priority::Visible);
    rs.cancelAll();
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 0);
}

BOOST_AUTO_TEST_CASE(pending_job_count_starts_zero)
{
    core::RenderScheduler rs;
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 0);
}

BOOST_AUTO_TEST_CASE(cancel_for_layer_removes_matching)
{
    core::RenderScheduler rs;
    core::Tile t1, t2, t3;
    // We use opaque pointer values to test cancelForLayer
    int dummyA, dummyB;
    core::Layer* layerA = reinterpret_cast<core::Layer*>(&dummyA);
    core::Layer* layerB = reinterpret_cast<core::Layer*>(&dummyB);

    rs.scheduleTileUpload(&t1, layerA, core::RenderScheduler::Priority::Immediate);
    rs.scheduleTileUpload(&t2, layerA, core::RenderScheduler::Priority::Visible);
    rs.scheduleTileUpload(&t3, layerB, core::RenderScheduler::Priority::Prefetch);

    rs.cancelForLayer(layerA);
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 1);
}

BOOST_AUTO_TEST_CASE(cancel_for_layer_does_not_remove_others)
{
    core::RenderScheduler rs;
    core::Tile t1, t2;
    int dummyA, dummyB;
    core::Layer* layerA = reinterpret_cast<core::Layer*>(&dummyA);
    core::Layer* layerB = reinterpret_cast<core::Layer*>(&dummyB);

    rs.scheduleTileUpload(&t1, layerA, core::RenderScheduler::Priority::Immediate);
    rs.scheduleTileUpload(&t2, layerB, core::RenderScheduler::Priority::Visible);

    rs.cancelForLayer(layerA);
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 1);
    // Remaining job should be for layerB
}

BOOST_AUTO_TEST_CASE(cancel_for_nonexistent_layer)
{
    core::RenderScheduler rs;
    core::Tile t1;
    int dummyA, dummyB;
    core::Layer* layerA = reinterpret_cast<core::Layer*>(&dummyA);
    core::Layer* layerB = reinterpret_cast<core::Layer*>(&dummyB);

    rs.scheduleTileUpload(&t1, layerA, core::RenderScheduler::Priority::Immediate);
    rs.cancelForLayer(layerB); // layerB has no jobs
    BOOST_CHECK_EQUAL(rs.pendingJobCount(), 1);
}

BOOST_AUTO_TEST_SUITE_END()
