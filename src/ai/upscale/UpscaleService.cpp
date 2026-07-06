#include "ai/upscale/UpscaleService.hpp"

#include <QMetaObject>
#include <QtConcurrent>

UpscaleService::UpscaleService(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<UpscaleJobResult>("UpscaleJobResult");
    qRegisterMetaType<UpscaleError>("UpscaleError");
    m_pool.setMaxThreadCount(1);
}

UpscaleService::~UpscaleService()
{
    {
        QMutexLocker lock(&m_mutex);
        for (auto it = m_jobs.begin(); it != m_jobs.end(); ++it) {
            if (it.value().cancel)
                it.value().cancel->store(true);
        }
    }
    m_pool.waitForDone();
}

UpscaleBackendStatus UpscaleService::probe()
{
    return m_backend.probe();
}

UpscaleJobId UpscaleService::upscale(const UpscaleInput& input, const UpscaleOptions& options)
{
    const UpscaleJobId jobId = m_nextId.fetch_add(1);
    JobState state;
    state.cancel = std::make_shared<std::atomic_bool>(false);
    {
        QMutexLocker lock(&m_mutex);
        m_jobs.insert(jobId, state);
    }

    emit jobStarted(jobId);

    (void) QtConcurrent::run(&m_pool, [this, input, options, jobId, cancel = state.cancel]() {
        auto progress = [this, jobId](int percent, const QString& message) {
            QMetaObject::invokeMethod(this, [this, jobId, percent, message]() {
                emit jobProgress(jobId, percent, message);
            }, Qt::QueuedConnection);
        };

        UpscaleJobResult result = m_backend.upscale(input, options, progress, *cancel);
        QMetaObject::invokeMethod(this, [this, jobId, result]() {
            {
                QMutexLocker lock(&m_mutex);
                m_jobs.remove(jobId);
            }
            if (result.ok || result.cancelled)
                emit jobFinished(jobId, result);
            else
                emit jobFailed(jobId, result.error);
        }, Qt::QueuedConnection);
    });

    return jobId;
}

void UpscaleService::cancel(UpscaleJobId jobId)
{
    QMutexLocker lock(&m_mutex);
    auto it = m_jobs.find(jobId);
    if (it != m_jobs.end() && it.value().cancel)
        it.value().cancel->store(true);
}
