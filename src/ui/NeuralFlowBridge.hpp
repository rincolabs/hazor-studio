#pragma once

#include "ai/sd/SdGenerationTypes.hpp"

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>

class ImageController;

namespace sdlocal {
class SdCanvasBridge;
class SdWorkflowExecutor;
struct SdWorkflowError;
struct SdWorkflowRunResult;
}

// The QWebChannel surface of the Neural Flow editor: the only thing the Vue
// application can reach inside Hazor.
//
// Narrow on purpose. The frontend never sees an SdModelManager, an
// SdWorkflowExecutor, a QImage or a document — everything crossing the channel
// is a QVariant tree that maps onto JSON, and every operation here is one the
// Stable Diffusion module already implements. There is no generation logic, no
// second job queue and no second model registry behind this class; it reads the
// node catalog, reads the model registry, and hands a workflow document to the
// existing workflow controller.
//
// Lives on the GUI thread. The executor marshals everything from its inference
// thread before it arrives here, so every signal below is emitted from the
// thread QWebChannel expects.
class NeuralFlowBridge : public QObject {
    Q_OBJECT

public:
    explicit NeuralFlowBridge(QObject* parent = nullptr);
    ~NeuralFlowBridge() override;

    // (Re)binds to the active document's controller; nullptr means no document
    // is open. Same lifecycle as the right-side panels. A run already in flight
    // keeps its own latch on the document it started against.
    void setController(ImageController* controller);

    // Cancels an in-flight run, waits for the worker and frees the inference
    // context. Called before the Qt objects a result would be posted to go away.
    void shutdown();

    bool busy() const;

    // ── Invoked from JavaScript ─────────────────────────────────────────────

    // Everything the editor needs to draw itself, in one round trip: the node
    // contract, the registered models, the theme and the current run state.
    Q_INVOKABLE QVariantMap initialState();

    // The node contract: types, categories, ports and property specs, straight
    // out of sdlocal::describeNodeCatalog(). The frontend builds its nodes from
    // this and keeps no catalog of its own.
    Q_INVOKABLE QVariantMap nodeDefinitions() const;

    // The registered models, by category. Reads SdModelManager; scans a
    // registered directory only if nothing has asked it to yet.
    Q_INVOKABLE QVariantList availableModels() const;

    // Rescans every registered directory and returns the result. Explicit
    // because it is the only heavy call here: nothing watches the filesystem.
    Q_INVOKABLE QVariantList refreshModels();

    // Every ggml device this machine registered, GPUs first:
    // [{ name: "CUDA0", description, gpu, totalMiB }].
    Q_INVOKABLE QVariantList availableDevices() const;

    // The pinned device name, or "" for "let the backend resolve it". A
    // machine-level choice rather than a workflow one, so it is persisted here
    // and not carried in a document.
    Q_INVOKABLE QString pinnedDevice() const;
    Q_INVOKABLE void setPinnedDevice(const QString& name);

    // Validates and starts a run of `workflowJson`, which must be a document in
    // the schema sdlocal::SdWorkflowDefinition parses.
    //
    // Returns { started: bool, errors: [{ code, nodeId, portId, message }] }.
    // A refusal comes back in the return value because it is the answer to this
    // call; everything about a run that did start arrives as a signal. Nothing
    // is loaded and no pixels are read when `started` is false.
    Q_INVOKABLE QVariantMap executeWorkflow(const QString& workflowJson);

    Q_INVOKABLE void cancelWorkflow();

    // Puts a frontend failure into Hazor's log. Without it a JavaScript error
    // in an embedded page goes to a console nobody is looking at.
    Q_INVOKABLE void reportError(const QString& context, const QString& message);

signals:
    void workflowStarted();
    // `state` is sdlocal::generationStateName(): idle, loading, generating,
    // completed, failed or cancelled.
    void workflowStateChanged(const QString& state, const QString& detail);
    void workflowProgress(int step, int steps, double seconds);
    // The run reached the document: size, seed, device and timings, plus the
    // name of the layer that was created.
    void workflowCompleted(const QVariantMap& result);
    // Only for a run that had already started. A workflow refused before
    // anything ran reports through executeWorkflow()'s return value instead.
    void workflowFailed(const QVariantList& errors);
    void modelsChanged(const QVariantList& models);
    void documentAvailabilityChanged(bool available);
    void themeChanged(const QVariantMap& theme);

private:
    static QVariantList errorsToVariant(const QVector<sdlocal::SdWorkflowError>& errors);
    void onStateChanged(sdlocal::SdGenerationState state, const QString& detail);
    void onFinished(const sdlocal::SdWorkflowRunResult& result);

    sdlocal::SdCanvasBridge* m_canvas = nullptr;
    sdlocal::SdWorkflowExecutor* m_executor = nullptr;
    bool m_documentAvailable = false;
};
