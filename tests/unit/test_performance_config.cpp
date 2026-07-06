#define BOOST_TEST_MODULE PerformanceConfigTest
#include <boost/test/included/unit_test.hpp>

#include "core/PerformanceConfig.hpp"
#include <QTemporaryFile>
#include <QSettings>

BOOST_AUTO_TEST_SUITE(performance_config)

BOOST_AUTO_TEST_CASE(default_values)
{
    PerformanceConfig cfg;
    BOOST_CHECK_EQUAL(cfg.tileSize, 256);
    BOOST_CHECK_EQUAL(cfg.autoTileMinArea, 512 * 512);
    BOOST_CHECK_EQUAL(cfg.progressiveBatchSize, 4);
    BOOST_CHECK_CLOSE(cfg.lodHalfThreshold, 4.0f, 0.01f);
    BOOST_CHECK_CLOSE(cfg.lodQuarterThreshold, 8.0f, 0.01f);
    BOOST_CHECK_CLOSE(cfg.lodEighthThreshold, 16.0f, 0.01f);
}

BOOST_AUTO_TEST_CASE(effective_thread_count_uses_auto)
{
    PerformanceConfig cfg;
    cfg.maxAsyncThreads = 0; // auto
    int effective = cfg.effectiveMaxThreads();
    BOOST_CHECK_GE(effective, 1);
}

BOOST_AUTO_TEST_CASE(effective_thread_count_uses_explicit)
{
    PerformanceConfig cfg;
    cfg.maxAsyncThreads = 3;
    BOOST_CHECK_EQUAL(cfg.effectiveMaxThreads(), 3);
}

BOOST_AUTO_TEST_CASE(save_and_load_roundtrip)
{
    PerformanceConfig saved;
    saved.tileSize = 128;
    saved.autoTileMinArea = 256 * 256;
    saved.maxAsyncThreads = 4;
    saved.progressiveBatchSize = 8;
    saved.lodHalfThreshold = 2.0f;
    saved.lodQuarterThreshold = 6.0f;
    saved.lodEighthThreshold = 12.0f;

    {
        QTemporaryFile tmpFile;
        tmpFile.open();
        QSettings s(tmpFile.fileName(), QSettings::IniFormat);
        saved.save(s);
        s.sync();
    }

    // Load into a fresh config
    PerformanceConfig loaded;
    {
        QTemporaryFile tmpFile;
        tmpFile.open();
        QSettings s(tmpFile.fileName(), QSettings::IniFormat);
        saved.save(s);
        s.sync();

        loaded.load(QSettings(tmpFile.fileName(), QSettings::IniFormat));
    }

    BOOST_CHECK_EQUAL(loaded.tileSize, 128);
    BOOST_CHECK_EQUAL(loaded.autoTileMinArea, 256 * 256);
    BOOST_CHECK_EQUAL(loaded.maxAsyncThreads, 4);
    BOOST_CHECK_EQUAL(loaded.progressiveBatchSize, 8);
    BOOST_CHECK_CLOSE(loaded.lodHalfThreshold, 2.0f, 0.01f);
    BOOST_CHECK_CLOSE(loaded.lodQuarterThreshold, 6.0f, 0.01f);
    BOOST_CHECK_CLOSE(loaded.lodEighthThreshold, 12.0f, 0.01f);
}

BOOST_AUTO_TEST_SUITE_END()
