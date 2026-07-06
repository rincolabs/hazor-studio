#include "ai/AiJobRunner.hpp"

#include <QtConcurrent>
#include <QMetaObject>
#include <QDebug>

AiJobRunner* AiJobRunner::instance()
{
    static AiJobRunner* s_instance = new AiJobRunner();
    return s_instance;
}

AiJobRunner::AiJobRunner(QObject* parent)
    : QObject(parent)
{
    qRegisterMetaType<AiJobResult>("AiJobResult");
    qRegisterMetaType<AiInpaintResult>("AiInpaintResult");
    m_pool.setMaxThreadCount(1); // serialise heavy AI jobs
}

AiJobRunner::~AiJobRunner()
{
    cancelAll();
    m_pool.waitForDone();   // ensure no task touches m_pipeline after destruction
}

quint64 AiJobRunner::submit(const AiMaskPipeline::Request& request, int targetLayerIndex)
{
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    quint64 id = m_nextId.fetch_add(1);
    {
        QMutexLocker lock(&m_mutex);
        if (m_currentCancel)
            m_currentCancel->store(true);   // supersede the previous job
        m_currentCancel = cancel;
    }
    m_currentJobId.store(id);

    const AiToolMode mode = request.mode;

    // Runs on the single pool thread. Captures everything by value (the cancel
    // token and request snapshot are cheap to copy: QImage is shared).
    (void) QtConcurrent::run(&m_pool, [this, request, cancel, id, mode, targetLayerIndex]() {
        if (cancel->load()) {
            AiJobResult r;
            r.jobId = id; r.mode = mode; r.cancelled = true;
            r.documentId = request.snapshot.documentId;
            r.sourceLayerIndex = request.snapshot.sourceLayerIndex;
            r.sourceRevision = request.snapshot.sourceRevision;
            r.targetLayerIndex = targetLayerIndex;
            QMetaObject::invokeMethod(this, [this, r]() { emit resultReady(r); },
                                      Qt::QueuedConnection);
            return;
        }

        auto progress = [this, id](const QString& label) {
            QMetaObject::invokeMethod(this, [this, id, label]() {
                emit statusChanged(id, label);
            }, Qt::QueuedConnection);
        };

        AiMaskPipeline::Result pr;
        switch (mode) {
        case AiToolMode::RemoveBackground:
            pr = m_pipeline.runRemoveBackground(request, cancel.get(), progress);
            break;
        case AiToolMode::RefineMask:
            pr = m_pipeline.runRefineExistingMask(request, cancel.get(), progress);
            break;
        case AiToolMode::SelectSubject:
            pr = m_pipeline.runSelectSubject(request, cancel.get(), progress);
            break;
        case AiToolMode::SelectObject:
        default:
            pr = m_pipeline.runObjectSelection(request, cancel.get(), progress);
            break;
        }

        AiJobResult r;
        r.jobId = id;
        r.mode = mode;
        r.mask = pr.mask;
        r.operation = request.options.operation;
        r.error = pr.error;
        r.providerUsed = pr.providerUsed;
        r.fromCache = pr.fromCache;
        r.cancelled = cancel->load();
        r.documentId = request.snapshot.documentId;
        r.sourceLayerIndex = request.snapshot.sourceLayerIndex;
        r.sourceRevision = request.snapshot.sourceRevision;
        r.targetLayerIndex = targetLayerIndex;

        QMetaObject::invokeMethod(this, [this, r]() { emit resultReady(r); },
                                  Qt::QueuedConnection);
    });

    return id;
}

quint64 AiJobRunner::submitInpaint(const AiInpaintRequest& request)
{
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    quint64 id = m_nextId.fetch_add(1);
    {
        QMutexLocker lock(&m_mutex);
        if (m_currentCancel)
            m_currentCancel->store(true);
        m_currentCancel = cancel;
    }
    m_currentJobId.store(id);

    AiInpaintRequest req = request;
    req.jobId = id;

    (void) QtConcurrent::run(&m_pool, [this, req, cancel, id]() mutable {
        if (cancel->load()) {
            AiInpaintResult r;
            r.jobId = id;
            r.documentId = req.documentId;
            r.sourceRevision = req.sourceRevision;
            r.cancelled = true;
            QMetaObject::invokeMethod(this, [this, r]() { emit inpaintResultReady(r); },
                                      Qt::QueuedConnection);
            return;
        }

        req.cancel = cancel.get();
        QMetaObject::invokeMethod(this, [this, id]() {
            emit statusChanged(id, tr("Running AI Remove"));
        }, Qt::QueuedConnection);

        AiInpaintResult r = m_inpaintPipeline.runRemoveObject(req);
        r.jobId = id;
        r.cancelled = r.cancelled || cancel->load();

        QMetaObject::invokeMethod(this, [this, r]() { emit inpaintResultReady(r); },
                                  Qt::QueuedConnection);
    });

    return id;
}

void AiJobRunner::cancelAll()
{
    QMutexLocker lock(&m_mutex);
    if (m_currentCancel)
        m_currentCancel->store(true);
}

void AiJobRunner::resetPipeline()
{
    cancelAll();
    // Run on the pool thread so it serialises after any in-flight job bails.
    (void) QtConcurrent::run(&m_pool, [this]() { m_pipeline.reset(); });
}

void AiJobRunner::invalidateDocument(quint64 documentId)
{
    // The embedding cache is internally mutex-protected, so this is safe to call
    // directly from the UI thread without racing the worker.
    m_pipeline.invalidateDocument(documentId);
}

void AiJobRunner::setEmbeddingCacheEnabled(bool enabled)
{
    m_pipeline.setEmbeddingCacheEnabled(enabled);
}

void AiJobRunner::setMaxCachedDocuments(int n)
{
    m_pipeline.setMaxCachedDocuments(n);
}

int AiJobRunner::cachedEmbeddingCount()
{
    return m_pipeline.embeddingCache().count();
}

qint64 AiJobRunner::cachedEmbeddingBytes()
{
    return m_pipeline.embeddingCache().approximateBytes();
}
