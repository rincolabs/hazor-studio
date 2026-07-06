#pragma once

#include <QSettings>
#include <QThread>
#include <QString>

struct PerformanceConfig {
    // Tile system
    int tileSize = 256;
    // Auto-enable tiling only if image exceeds this area (pixels)
    int autoTileMinArea = 512 * 512;

    // Async job system
    int maxAsyncThreads = 0; // 0 = auto (idealThreadCount - 1)
    int progressiveBatchSize = 4;

    // LOD thresholds (pixels per screen pixel)
    float lodHalfThreshold = 4.0f;
    float lodQuarterThreshold = 8.0f;
    float lodEighthThreshold = 16.0f;

    // Fetch effective thread count (0 = auto)
    int effectiveMaxThreads() const {
        if (maxAsyncThreads > 0) return maxAsyncThreads;
        return std::max(1, QThread::idealThreadCount() - 1);
    }

    // Save to QSettings
    void save(QSettings& s) const {
        s.beginGroup("Performance");
        s.setValue("tileSize", tileSize);
        s.setValue("autoTileMinArea", autoTileMinArea);
        s.setValue("maxAsyncThreads", maxAsyncThreads);
        s.setValue("progressiveBatchSize", progressiveBatchSize);
        s.setValue("lodHalfThreshold", static_cast<double>(lodHalfThreshold));
        s.setValue("lodQuarterThreshold", static_cast<double>(lodQuarterThreshold));
        s.setValue("lodEighthThreshold", static_cast<double>(lodEighthThreshold));
        s.endGroup();
    }

    // Load from QSettings
    void load(const QSettings& s) {
        QSettings& mutable_s = const_cast<QSettings&>(s);
        mutable_s.beginGroup("Performance");
        tileSize = mutable_s.value("tileSize", tileSize).toInt();
        autoTileMinArea = mutable_s.value("autoTileMinArea", autoTileMinArea).toInt();
        maxAsyncThreads = mutable_s.value("maxAsyncThreads", maxAsyncThreads).toInt();
        progressiveBatchSize = mutable_s.value("progressiveBatchSize", progressiveBatchSize).toInt();
        lodHalfThreshold = static_cast<float>(
            mutable_s.value("lodHalfThreshold", static_cast<double>(lodHalfThreshold)).toDouble());
        lodQuarterThreshold = static_cast<float>(
            mutable_s.value("lodQuarterThreshold", static_cast<double>(lodQuarterThreshold)).toDouble());
        lodEighthThreshold = static_cast<float>(
            mutable_s.value("lodEighthThreshold", static_cast<double>(lodEighthThreshold)).toDouble());
        mutable_s.endGroup();
    }

    // Convenience: load from default app settings
    static PerformanceConfig fromDefaultSettings() {
        PerformanceConfig cfg;
        QSettings s;
        cfg.load(s);
        return cfg;
    }
};
