#pragma once

#include "ai/sd/SdGenerationJob.hpp"
#include "ai/sd/SdGenerationTypes.hpp"
#include "ai/sd/SdWorkflow.hpp"
#include "ai/sd/SdWorkflowValidator.hpp"

#include <QObject>
#include <QString>
#include <QVector>

#include <memory>

// Runs a workflow document: parse, validate, resolve, generate, insert.
//
// The important rule it implements is that a graph like
//
//     Checkpoint ── LoRA ── LoRA ─┐
//     Prompt ──────────────────────┤
//     Sampling ────────────────────┼── Generate ── Image Output
//
// produces one generation request at Generate. Configuration nodes transform
// values without inference; additional Generate nodes are executed in
// topological order and may consume or branch images produced earlier.
//
// It knows nothing about ONNX Runtime, Real-ESRGAN, brushes or adjustments, and
// reaches the document only through SdCanvasBridge.
namespace sdlocal {

class SdCanvasBridge;
struct SdWorkflowExecutionState;

struct SdWorkflowRunResult {
    SdGenerationResult generation;
    QVector<SdWorkflowError> errors;

    // The image reached the document as a new pixel layer. False when the
    // generation failed, when it was cancelled, or when the document it was
    // started against is no longer the active one.
    bool inserted = false;
    QString layerName;

    bool ok() const { return inserted && generation.ok() && errors.isEmpty(); }
    // The first thing worth telling the user: a graph problem outranks an
    // engine problem, because it is the one they can fix.
    QString firstError() const;
};

class SdWorkflowExecutor : public QObject {
    Q_OBJECT
public:
    // `bridge` supplies and receives pixels and must outlive this object; it is
    // not owned here.
    explicit SdWorkflowExecutor(SdCanvasBridge* bridge, QObject* parent = nullptr);
    ~SdWorkflowExecutor() override;

    // Validates and starts a run. Returns false without starting anything when
    // the document is malformed, the graph is invalid, a model is unavailable,
    // or a run is already in flight — `errors` says which.
    //
    // Model loading and inference happen on a worker thread; everything up to
    // and including the canvas capture happens here, on the calling (GUI)
    // thread, because it reads the live document.
    bool run(const QByteArray& workflowJson, QVector<SdWorkflowError>* errors = nullptr);

    void cancel();
    bool busy() const { return m_execution != nullptr; }
    SdGenerationState state() const { return m_job.state(); }

    // Cancels, waits for the worker and frees the inference context.
    void shutdown();

signals:
    void stateChanged(sdlocal::SdGenerationState state, const QString& detail);
    void progress(int step, int steps, float seconds);
    void finished(const sdlocal::SdWorkflowRunResult& result);

private:
    enum class AdvanceResult { Started, Completed, Failed };

    AdvanceResult advance(QVector<SdWorkflowError>& errors);
    bool buildRequest(const SdWorkflowNode& generate,
                      SdGenerationRequest& request,
                      QVector<SdWorkflowError>& errors);
    bool buildCannyRequest(const SdWorkflowNode& node, SdGenerationRequest& request,
                           QVector<SdWorkflowError>& errors);
    bool buildUpscaleRequest(const SdWorkflowNode& node, SdGenerationRequest& request,
                             QVector<SdWorkflowError>& errors);
    bool buildADetailerRequest(const SdWorkflowNode& node, SdGenerationRequest& request,
                               QVector<SdWorkflowError>& errors);
    // Starts the job for an already-built request and records `nodeId` as the
    // operation in flight. Shared by Generate, Canny, Upscale and ADetailer:
    // everything past "here is a request" is the same for every operation node.
    AdvanceResult startOperation(const QString& nodeId, const SdGenerationRequest& request,
                                 QVector<SdWorkflowError>& errors);
    void finishExecution();
    void onJobFinished(const SdGenerationResult& result);

    SdCanvasBridge* m_bridge = nullptr;
    SdGenerationJob m_job;

    // Owns the validated graph, its evaluated values and the current position
    // while asynchronous Generate nodes yield back to the GUI thread.
    std::unique_ptr<SdWorkflowExecutionState> m_execution;
};

} // namespace sdlocal
