#include "ai/sd/SdGenerationJob.hpp"

#include "ai/sd/SdGenerationService.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QMetaObject>
#include <QtConcurrent>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

} // namespace

SdGenerationJob::SdGenerationJob(QObject* parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(1);
    // Nothing may be reclaimed while it is running a generation, and nothing
    // may start on a thread the pool is about to retire mid-load.
    m_pool.setExpiryTimeout(-1);
}

SdGenerationJob::~SdGenerationJob()
{
    shutdown();
}

void SdGenerationJob::shutdown()
{
    // The cancel only shortens the wait; waitForDone() is what makes the
    // worker's captures safe. Qt drops the queued result event once the
    // receiver is destroyed, so a result arriving during teardown is harmless.
    SdGenerationService::instance().requestCancel();
    m_pool.waitForDone();
    m_running = false;
    // Safe now: the worker is gone, so no generation can be holding the
    // context the service is about to free.
    SdGenerationService::instance().shutdown();
}

void SdGenerationJob::setState(SdGenerationState state, const QString& detail)
{
    m_state = state;
    emit stateChanged(state, detail);
}

bool SdGenerationJob::start(const SdGenerationRequest& request, QString* refusal)
{
    if (m_running) {
        if (refusal)
            *refusal = tr_("A generation is already running.");
        return false;
    }

    // Checked before a thread is spawned, so a malformed request fails
    // immediately and synchronously instead of after a multi-gigabyte load.
    QString error;
    if (!SdGenerationService::instance().validateRequest(request, &error)) {
        if (refusal)
            *refusal = error;
        return false;
    }

    m_running = true;
    setState(SdGenerationState::Loading, request.load.files.modelId);

    (void) QtConcurrent::run(&m_pool, [this, request]() {
        // Both callbacks fire on this worker thread, so neither touches a Qt
        // object directly: each marshals to the thread that owns the job.
        SdProgressSink sink;
        sink.onState = [this](SdGenerationState state, const QString& detail) {
            QMetaObject::invokeMethod(this, [this, state, detail]() {
                if (m_running)
                    setState(state, detail);
            }, Qt::QueuedConnection);
        };
        sink.onStep = [this](int step, int steps, float seconds) {
            QMetaObject::invokeMethod(this, [this, step, steps, seconds]() {
                if (m_running)
                    emit progress(step, steps, seconds);
            }, Qt::QueuedConnection);
        };

        const SdGenerationResult result = SdGenerationService::instance().run(request, sink);

        // finished() is emitted from the owning thread rather than from here,
        // so SdGenerationResult never has to cross a queued signal connection
        // as a registered metatype.
        QMetaObject::invokeMethod(this, [this, result]() {
            m_running = false;
            setState(result.state, result.error);
            emit finished(result);
        }, Qt::QueuedConnection);
    });

    return true;
}

void SdGenerationJob::cancel()
{
    if (!m_running)
        return;
    qInfo() << "[SD] cancel requested";
    SdGenerationService::instance().requestCancel();
}

} // namespace sdlocal
