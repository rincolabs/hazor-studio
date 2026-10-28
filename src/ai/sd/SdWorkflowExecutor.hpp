#pragma once

#include "ai/sd/SdGenerationJob.hpp"
#include "ai/sd/SdGenerationTypes.hpp"
#include "ai/sd/SdWorkflow.hpp"
#include "ai/sd/SdWorkflowValidator.hpp"

#include <QObject>
#include <QString>
#include <QVector>

// Runs a workflow document: parse, validate, resolve, generate, insert.
//
// The important rule it implements is that a graph like
//
//     Checkpoint ─┐
//     Prompt ─────┤
//     Sampling ───┼── Generate ── Image Output
//     LoRA ───────┘
//
// produces ONE generation request, not one inference per node. The
// configuration nodes are parameters; only the Generate node runs anything.
//
// It knows nothing about ONNX Runtime, Real-ESRGAN, brushes or adjustments, and
// reaches the document only through SdCanvasBridge.
namespace sdlocal {

class SdCanvasBridge;

struct SdWorkflowRunResult {
    SdGenerationResult generation;
    QVector<SdWorkflowError> errors;

    // The image reached the document as a new pixel layer. False when the
    // generation failed, when it was cancelled, or when the document it was
    // started against is no longer the active one.
    bool inserted = false;
    QString layerName;

    bool ok() const { return inserted && generation.ok(); }
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

    // Validates and starts a run. Returns false without starting anything when
    // the document is malformed, the graph is invalid, a model is unavailable,
    // or a run is already in flight — `errors` says which.
    //
    // Model loading and inference happen on a worker thread; everything up to
    // and including the canvas capture happens here, on the calling (GUI)
    // thread, because it reads the live document.
    bool run(const QByteArray& workflowJson, QVector<SdWorkflowError>* errors = nullptr);

    void cancel();
    bool busy() const { return m_job.busy(); }
    SdGenerationState state() const { return m_job.state(); }

    // Cancels, waits for the worker and frees the inference context.
    void shutdown();

signals:
    void stateChanged(sdlocal::SdGenerationState state, const QString& detail);
    void progress(int step, int steps, float seconds);
    void finished(const sdlocal::SdWorkflowRunResult& result);

private:
    bool buildRequest(const SdWorkflowDefinition& definition,
                      const SdWorkflowPlan& plan,
                      SdGenerationRequest& request,
                      QVector<SdWorkflowError>& errors);
    void onJobFinished(const SdGenerationResult& result);

    SdCanvasBridge* m_bridge = nullptr;
    SdGenerationJob m_job;

    // The run in flight: kept so the output node's properties are read from the
    // document that was validated, not from whatever is current when it ends.
    QString m_layerName;
};

} // namespace sdlocal
