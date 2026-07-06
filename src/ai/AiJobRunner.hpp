#pragma once

#include "ai/AiMaskPipeline.hpp"
#include "ai/AiJobTypes.hpp"
#include "ai/inpaint/AiInpaintPipeline.hpp"
#include "ai/inpaint/AiInpaintTypes.hpp"

#include <QObject>
#include <QThreadPool>
#include <QMutex>

#include <atomic>
#include <memory>

// Serialised, cancellable async executor for AI inference. Owns the (compute-
// only) AiMaskPipeline and runs every heavy request on a single background
// thread so the UI never blocks. Submitting a new job cancels the in-flight one
// (the runtime default is one concurrent AI job); results are marshalled back to
// the UI thread via queued signals, tagged with the job's document/layer/
// revision identity so the controller can drop stale results.
class AiJobRunner : public QObject {
    Q_OBJECT
public:
    // One shared runner for the whole app: a single worker thread serialises all
    // ONNX session creation/inference (so AiRuntimeManager's session cache is
    // never touched concurrently) and a single embedding cache is shared across
    // documents (keyed by document id). Per-canvas controllers filter results by
    // their own document/job id.
    static AiJobRunner* instance();

    explicit AiJobRunner(QObject* parent = nullptr);
    ~AiJobRunner() override;

    // Starts a new job, cancelling any previous one. Returns the new job id.
    quint64 submit(const AiMaskPipeline::Request& request, int targetLayerIndex);
    quint64 submitInpaint(const AiInpaintRequest& request);

    // Cancels the in-flight job (its result will arrive flagged cancelled).
    void cancelAll();

    // Provider changed: drop the loaded segmenter + caches (runs on the worker).
    void resetPipeline();
    void invalidateDocument(quint64 documentId);

    void setEmbeddingCacheEnabled(bool enabled);
    void setMaxCachedDocuments(int n);

    quint64 currentJobId() const { return m_currentJobId.load(); }

    // Cache diagnostics for the Settings dialog (thread-safe).
    int    cachedEmbeddingCount();
    qint64 cachedEmbeddingBytes();

signals:
    void statusChanged(quint64 jobId, QString status);
    void resultReady(AiJobResult result);
    void inpaintResultReady(AiInpaintResult result);

private:
    AiMaskPipeline m_pipeline;     // touched only by the single pool thread
    AiInpaintPipeline m_inpaintPipeline; // touched only by the single pool thread
    QThreadPool m_pool;            // maxThreadCount == 1 (serialised)

    QMutex m_mutex;
    std::shared_ptr<std::atomic<bool>> m_currentCancel;
    std::atomic<quint64> m_nextId{1};
    std::atomic<quint64> m_currentJobId{0};
};
