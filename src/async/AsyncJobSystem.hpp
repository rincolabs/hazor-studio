#pragma once

#include <QObject>
#include <QString>
#include <QThreadPool>
#include <QMutex>
#include <memory>
#include <vector>
#include <atomic>
#include <cstdint>

#include "AsyncJob.hpp"
#include "core/PerformanceConfig.hpp"
#include <opencv2/core.hpp>

class AsyncJobSystem : public QObject {
    Q_OBJECT

public:
    explicit AsyncJobSystem(QObject* parent = nullptr);
    static AsyncJobSystem* instance() { return s_instance; }
    static void create();
    static void destroy();

    ~AsyncJobSystem() override;

    uint64_t enqueue(std::shared_ptr<AsyncJob> job);
    void cancelForLayer(const Layer* layer);
    void cancelAll();
    int pendingCount() const;

signals:
    void jobCompleted(uint64_t jobId);
    void progressiveBatch(uint64_t jobId, QVector<QRect> tileRects);
    void jobProgressChanged(uint64_t jobId, int progress);
    void jobStatusChanged(uint64_t jobId, QString status);

private:
    void executeWorker(std::shared_ptr<AsyncJob> job);
    void executeProgressiveWorker(std::shared_ptr<AsyncJob> job);
    void applyImageEngine(const std::string& toolName,
                          const QVariantMap& params,
                          cv::Mat& mat);

    static AsyncJobSystem* s_instance;

    QThreadPool m_pool;
    std::vector<std::shared_ptr<AsyncJob>> m_jobs;
    mutable QMutex m_mutex;
    std::atomic<uint64_t> m_nextJobId{1};
};
