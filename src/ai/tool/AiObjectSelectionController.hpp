#pragma once

#include "ai/AiSelectionTypes.hpp"
#include "ai/AiJobTypes.hpp"
#include "ai/matting/AiRefineTypes.hpp"
#include "ai/compat/AiCompatibilityReport.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QPointF>
#include <QRectF>

#include <cstdint>

class ImageController;
class CanvasView;
struct AiImageSnapshot;

// UI-side brain of the AI Object Selection tool. It is the only place that knows
// both the editor (ImageController/Document/SelectionMask) and the AI service
// (the shared AiJobRunner). The canvas forwards user gestures here in document
// coordinates; this class captures a thread-safe snapshot, dispatches an async
// job, and — when a still-valid result returns — lands it in the document using
// the editor's existing selection / layer-mask APIs (no parallel systems, no
// changes to existing mask/selection math).
//
// One instance per canvas/document; results from the shared runner are filtered
// by document + job id so a job that finishes for another tab is discarded.
class AiObjectSelectionController : public QObject {
    Q_OBJECT
public:
    AiObjectSelectionController(ImageController* controller, CanvasView* canvas,
                                QObject* parent = nullptr);

    // ── Options (driven by the options bar) ──
    void setMode(AiToolMode mode) { m_mode = mode; }
    AiToolMode mode() const { return m_mode; }
    void setOperation(AiSelectionOperation op) { m_operation = op; }
    AiSelectionOperation operation() const { return m_operation; }
    void setSampleSource(AiSampleSource s) { m_sample = s; }
    void setRequestedModelId(const QString& id);   // empty => Auto
    QString requestedModelId() const { return m_requestedModelId; }
    void setAntiAlias(bool on) { m_antiAlias = on; }

    // ── Refine / matting (Etapa 3) ──
    void setRefineOptions(const AiRefineOptions& opts) { m_refine = opts; }
    AiRefineOptions refineOptions() const { return m_refine; }
    void setRefineEnabled(bool on) { m_refine.enabled = on; }
    bool refineEnabled() const { return m_refine.enabled; }
    void setBackgroundRemovalEngine(AiBackgroundRemovalEngine e) { m_bgEngine = e; }
    AiBackgroundRemovalEngine backgroundRemovalEngine() const { return m_bgEngine; }

    // ── Availability ──
    // True when AI is enabled, the runtime is present and a SAM model is
    // installed. *reason is filled with a user-facing message otherwise.
    bool isReady(QString* reason = nullptr) const;
    // Remove Background works with SAM *or* a matting/background-removal model.
    bool isRemoveBackgroundReady(QString* reason = nullptr) const;
    QStringList installedSamModels() const;       // ids
    QStringList installedMattingModels() const;    // ids (refine model combo)
    QString modelDisplayName(const QString& id) const;
    QString resolvedModelId() const;              // model that will actually run

    // ── Canvas gestures (document-pixel coordinates) ──
    void clickAt(const QPointF& docPos, AiSelectionOperation op, bool foreground);
    void boxAt(const QRectF& docBox, AiSelectionOperation op);
    void removeBackground();   // main-subject mask → layer mask
    void selectSubject();      // main-subject mask → selection
    void refineSelection();    // refine the active selection with a matting model

    // ── Lifecycle ──
    void cancelPending();              // tool switch / Esc / document change
    void onProviderOrSettingsChanged();// reload sessions / invalidate caches
    void notifyDocumentClosing();      // drop this document's cached embeddings

signals:
    void statusChanged(const QString& status); // Ready / Building embedding / Failed…
    void busyChanged(bool busy);
    void availabilityChanged();                // installed models / AI-enabled changed
    // Hard, actionable AI failure that must surface as an alert dialog (handled in
    // MainWindow via AiAlertService). `message.actionId` selects the dialog kind
    // ("open_ai_settings", "cuda_oom"). Soft progress stays on statusChanged.
    void aiAlertRequested(const AiCompatibilityMessage& message);

private:
    // What to do with a returned mask. The job's compute mode (SelectObject vs
    // RemoveBackground/subject) is separate from this post-action because a
    // subject mask can become either a selection (Select Subject) or a layer mask
    // (Remove Background).
    enum class PendingAction { None, SelectionFromPrompt, SelectionFromSubject, MaskFromSubject,
                               SelectionFromRefine };

    void submit(AiToolMode mode, PendingAction action, const AiPrompt& prompt,
                AiSampleSource sample, AiSelectionOperation op,
                const QImage& baseMask = QImage());
    bool buildSnapshot(AiSampleSource sample, AiImageSnapshot& out, int* targetLayer);
    int  workingLongestSide(const QSize& docSize) const;
    void handleResult(const AiJobResult& result);
    void handleStatus(quint64 jobId, const QString& status);
    // Surface a recoverable AI failure as an alert dialog (never as options-bar
    // status text). `title` defaults to a generic AI error heading.
    void emitError(const QString& message, const QString& title = QString());
    bool applyMaskAsSelection(const AiMaskResult& mask, AiSelectionOperation op,
                              const QString& undoName);
    quint64 currentDocumentId() const;
    void setBusy(bool busy);

    ImageController* m_controller = nullptr;
    CanvasView* m_canvas = nullptr;

    AiToolMode m_mode = AiToolMode::SelectObject;
    AiSelectionOperation m_operation = AiSelectionOperation::Replace;
    AiSampleSource m_sample = AiSampleSource::AllVisible;
    QString m_requestedModelId;        // empty = Auto
    bool m_antiAlias = true;
    AiRefineOptions m_refine;          // edge/cleanup + matting model selection
    AiBackgroundRemovalEngine m_bgEngine = AiBackgroundRemovalEngine::Auto;

    quint64 m_pendingJobId = 0;        // last job we submitted (for stale filtering)
    PendingAction m_pendingAction = PendingAction::None;
    bool m_busy = false;
};
