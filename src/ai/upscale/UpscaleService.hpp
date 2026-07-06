#pragma once

#include "ai/upscale/RealEsrganProcessBackend.hpp"
#include "ai/upscale/UpscaleTypes.hpp"

#include <QObject>
#include <QHash>
#include <QMutex>
#include <QThreadPool>

#include <atomic>
#include <memory>

using UpscaleJobId = quint64;

class UpscaleService : public QObject {
    Q_OBJECT
public:
    explicit UpscaleService(QObject* parent = nullptr);
    ~UpscaleService() override;

    UpscaleJobId upscale(const UpscaleInput& input, const UpscaleOptions& options);
    void cancel(UpscaleJobId jobId);

    UpscaleBackendStatus probe();

signals:
    void jobStarted(UpscaleJobId jobId);
    void jobProgress(UpscaleJobId jobId, int percent, QString message);
    void jobFinished(UpscaleJobId jobId, UpscaleJobResult result);
    void jobFailed(UpscaleJobId jobId, UpscaleError error);

private:
    struct JobState {
        std::shared_ptr<std::atomic_bool> cancel;
    };

    RealEsrganProcessBackend m_backend;
    QThreadPool m_pool;
    QMutex m_mutex;
    QHash<UpscaleJobId, JobState> m_jobs;
    std::atomic<UpscaleJobId> m_nextId{1};
};
