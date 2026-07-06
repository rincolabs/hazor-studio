#include "ai/tool/AiObjectSelectionController.hpp"

#include "ai/AiImageSnapshot.hpp"
#include "ai/AiMaskPipeline.hpp"
#include "ai/AiJobRunner.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/runtime/AiRuntimeSettings.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/models/AiModelTypes.hpp"
#include "ai/matting/AiRefineModelFactory.hpp"

#include "controller/ImageController.hpp"
#include "controller/Commands.hpp"
#include "core/Document.hpp"
#include "renderer/CanvasView.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"

#include <QDebug>

#include <algorithm>
#include <cmath>

namespace {

SelectMode toSelectMode(AiSelectionOperation op)
{
    switch (op) {
    case AiSelectionOperation::Add:       return SelectMode::Add;
    case AiSelectionOperation::Subtract:  return SelectMode::Subtract;
    case AiSelectionOperation::Intersect: return SelectMode::Intersect;
    case AiSelectionOperation::Replace:
    default:                              return SelectMode::Replace;
    }
}

} // namespace

AiObjectSelectionController::AiObjectSelectionController(ImageController* controller,
                                                        CanvasView* canvas,
                                                        QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_canvas(canvas)
{
    auto* runner = AiJobRunner::instance();
    connect(runner, &AiJobRunner::resultReady, this,
            &AiObjectSelectionController::handleResult);
    connect(runner, &AiJobRunner::statusChanged, this,
            &AiObjectSelectionController::handleStatus);

    // Availability tracking: refresh the options-bar warning when models are
    // installed/removed or AI settings change.
    connect(AiModelRegistry::instance(), &AiModelRegistry::installedModelsChanged,
            this, &AiObjectSelectionController::availabilityChanged);
    connect(AiRuntimeManager::instance(), &AiRuntimeManager::settingsChanged,
            this, &AiObjectSelectionController::availabilityChanged);
}

void AiObjectSelectionController::setRequestedModelId(const QString& id)
{
    m_requestedModelId = id;
}

quint64 AiObjectSelectionController::currentDocumentId() const
{
    return quint64(reinterpret_cast<quintptr>(m_controller ? m_controller->document() : nullptr));
}

// ── Availability ──────────────────────────────────────────────────────────────

QStringList AiObjectSelectionController::installedSamModels() const
{
    // Resolved by capability/task via the registry — never by file name.
    auto* reg = AiModelRegistry::instance();
    QStringList ids;
    for (const AiModelDescriptor& d : reg->modelsByCapability(QStringLiteral("object_selection")))
        ids << d.id;
    for (const AiModelDescriptor& d : reg->modelsByTask(QStringLiteral("segmentation")))
        if (!ids.contains(d.id))
            ids << d.id;
    return ids;
}

QStringList AiObjectSelectionController::installedMattingModels() const
{
    return AiRefineModelFactory::installedMattingModels();
}

QString AiObjectSelectionController::modelDisplayName(const QString& id) const
{
    const AiInstalledModel m = AiModelRegistry::instance()->installedModel(id);
    return m.name.isEmpty() ? id : m.name;
}

QString AiObjectSelectionController::resolvedModelId() const
{
    const QStringList sam = installedSamModels();
    if (sam.isEmpty())
        return QString();
    if (!m_requestedModelId.isEmpty() && sam.contains(m_requestedModelId))
        return m_requestedModelId;
    // Auto: honour the configured default segmentation model rather than grabbing
    // whichever SAM model happened to be discovered first.
    if (auto d = AiModelRegistry::instance()->defaultModelForTask(QStringLiteral("segmentation")))
        if (sam.contains(d->id))
            return d->id;
    return sam.first();
}

bool AiObjectSelectionController::isReady(QString* reason) const
{
    auto* rt = AiRuntimeManager::instance();
    if (!rt->isRuntimeAvailable()) {
        if (reason) *reason = tr("ONNX Runtime is not available in this build.");
        return false;
    }
    if (!rt->isAiEnabled()) {
        if (reason)
            *reason = tr("AI features are disabled. Enable them in Settings > AI / Machine Learning.");
        return false;
    }
    if (resolvedModelId().isEmpty()) {
        if (reason)
            *reason = tr("AI object selection requires a SAM model. Install one in "
                         "Settings > AI / Machine Learning.");
        return false;
    }
    return true;
}

bool AiObjectSelectionController::isRemoveBackgroundReady(QString* reason) const
{
    auto* rt = AiRuntimeManager::instance();
    if (!rt->isRuntimeAvailable()) {
        if (reason) *reason = tr("ONNX Runtime is not available in this build.");
        return false;
    }
    if (!rt->isAiEnabled()) {
        if (reason)
            *reason = tr("AI features are disabled. Enable them in Settings > AI / Machine Learning.");
        return false;
    }
    // Any of SAM, a matting model, or a background-removal model can drive it.
    const bool hasModel = !resolvedModelId().isEmpty()
        || !AiRefineModelFactory::resolveBackgroundRemovalModelId(QString()).isEmpty()
        || !AiRefineModelFactory::resolveMattingModelId(QString()).isEmpty();
    if (!hasModel) {
        if (reason)
            *reason = tr("Remove Background requires a SAM, RMBG or matting model. Install one in "
                         "Settings > AI / Machine Learning.");
        return false;
    }
    return true;
}

// ── Gestures ──────────────────────────────────────────────────────────────────

void AiObjectSelectionController::clickAt(const QPointF& docPos, AiSelectionOperation op,
                                          bool foreground)
{
    Document* doc = m_controller ? m_controller->document() : nullptr;
    qInfo().noquote() << "[AI][SEL] clickAt docPos=" << docPos
                      << "docSize=" << (doc ? doc->size : QSize())
                      << "fg=" << foreground << "op=" << int(op);
    AiPrompt prompt;
    prompt.kind = AiPrompt::Kind::Points;
    prompt.points.push_back(AiPromptPoint{ docPos, foreground });
    submit(AiToolMode::SelectObject, PendingAction::SelectionFromPrompt, prompt, m_sample, op);
}

void AiObjectSelectionController::boxAt(const QRectF& docBox, AiSelectionOperation op)
{
    if (docBox.width() < 2.0 || docBox.height() < 2.0)
        return;
    Document* doc = m_controller ? m_controller->document() : nullptr;
    qInfo().noquote() << "[AI][SEL] boxAt docBox=" << docBox
                      << "docSize=" << (doc ? doc->size : QSize()) << "op=" << int(op);
    AiPrompt prompt;
    prompt.kind = AiPrompt::Kind::Box;
    prompt.box = docBox.normalized();
    submit(AiToolMode::SelectObject, PendingAction::SelectionFromPrompt, prompt, m_sample, op);
}

void AiObjectSelectionController::selectSubject()
{
    AiPrompt prompt;
    prompt.kind = AiPrompt::Kind::Auto;
    // Select Subject is a *selection* gesture: it must use the same SAM model and
    // decoder as a manual box/click so its mask matches a box drawn around the same
    // object. Routing it through RemoveBackground (matting/RMBG) is what made the
    // result diverge and showed a "Removing background" label.
    submit(AiToolMode::SelectSubject, PendingAction::SelectionFromSubject, prompt,
           m_sample, AiSelectionOperation::Replace);
}

void AiObjectSelectionController::removeBackground()
{
    AiPrompt prompt;
    prompt.kind = AiPrompt::Kind::Auto;
    // Remove Background masks the active layer using its own pixels — sampling the
    // current layer keeps the subject detection aligned with what gets masked.
    submit(AiToolMode::RemoveBackground, PendingAction::MaskFromSubject, prompt,
           AiSampleSource::CurrentLayer, AiSelectionOperation::Replace);
}

void AiObjectSelectionController::refineSelection()
{
    Document* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc || !doc->selection.active() || doc->selection.isEmpty()) {
        emitError(tr("Make a selection first, then Refine Edges."));
        return;
    }
    AiPrompt prompt;
    prompt.kind = AiPrompt::Kind::None;
    // Refine the current selection in place; matting needs full RGB, so sample the
    // whole visible composition regardless of the SAM sample source.
    const QImage base = doc->selection.image().copy();
    submit(AiToolMode::RefineMask, PendingAction::SelectionFromRefine, prompt,
           AiSampleSource::AllVisible, AiSelectionOperation::Replace, base);
}

// ── Submission ────────────────────────────────────────────────────────────────

int AiObjectSelectionController::workingLongestSide(const QSize& docSize) const
{
    const AiRuntimeSettings s = AiRuntimeManager::instance()->settings();
    const int docLongest = std::max(docSize.width(), docSize.height());
    switch (s.workingResolution) {
    case AiWorkingResolution::PreserveDocument:
        return docLongest;
    case AiWorkingResolution::LimitLongestSide:
        return std::max(256, s.workingResolutionLongestSide);
    case AiWorkingResolution::Auto:
    default:
        // SAM's native input is 1024; a snapshot larger than that buys little for
        // the embedding while costing memory. Default to 1024 for a fast, stable
        // result. (Preserve-document gives crisper mask edges at higher cost.)
        return 1024;
    }
}

bool AiObjectSelectionController::buildSnapshot(AiSampleSource sample, AiImageSnapshot& out,
                                               int* targetLayer)
{
    Document* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc)
        return false;
    const QSize docSize = doc->size;
    if (docSize.width() <= 0 || docSize.height() <= 0)
        return false;

    RenderContext ctx;
    ctx.document = doc;
    ctx.outputSize = docSize;
    ctx.targetType = RenderTargetType::Export;
    ctx.highQuality = true;

    const int target = doc->activeFlatIndex;
    QImage full = (sample == AiSampleSource::CurrentLayer)
        ? DocumentCompositor::compositeOnlyFlatIndex(doc, target, ctx)
        : DocumentCompositor::composite(doc, ctx);
    if (full.isNull())
        return false;

    const int docLongest = std::max(docSize.width(), docSize.height());
    const int longest = workingLongestSide(docSize);
    QImage snap;
    if (longest >= docLongest) {
        snap = full;
    } else {
        const double scale = double(longest) / double(docLongest);
        const QSize ns(std::max(1, int(std::lround(docSize.width()  * scale))),
                       std::max(1, int(std::lround(docSize.height() * scale))));
        snap = full.scaled(ns, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    out.image = snap.convertToFormat(QImage::Format_RGBA8888);
    out.documentSize = docSize;
    out.documentId = currentDocumentId();
    out.sourceLayerIndex = (sample == AiSampleSource::CurrentLayer) ? target : -1;
    out.sourceRevision = AiImageSnapshot::hashImage(out.image);
    out.sourceDescription = (sample == AiSampleSource::CurrentLayer)
        ? QStringLiteral("current-layer") : QStringLiteral("all-visible");
    if (targetLayer) *targetLayer = target;
    qInfo().noquote() << "[AI][SEL] snapshot built docSize=" << docSize
                      << "fullComposite=" << full.size()
                      << "snapSize=" << out.image.size()
                      << "scaleDocToSnap=" << out.scaleDocToSnapX() << out.scaleDocToSnapY()
                      << "source=" << out.sourceDescription;
    return out.isValid();
}

void AiObjectSelectionController::submit(AiToolMode mode, PendingAction action,
                                         const AiPrompt& prompt, AiSampleSource sample,
                                         AiSelectionOperation op, const QImage& baseMask)
{
    QString reason;
    const bool ready = (mode == AiToolMode::RemoveBackground)
                           ? isRemoveBackgroundReady(&reason)
                           : isReady(&reason);
    if (!ready) {
        // Surface the blocking reason as an actionable alert (spec §18) — the tool
        // is already active, so this is a model/runtime condition the user must fix.
        AiCompatibilityMessage m;
        m.severity = AiCompatibilitySeverity::Warning;
        m.title = tr("AI is not ready");
        m.message = reason;
        m.actionLabel = tr("AI Settings");
        m.actionId = QStringLiteral("open_ai_settings");
        emit aiAlertRequested(m);
        return;
    }
    if (mode == AiToolMode::RefineMask
        && AiRefineModelFactory::resolveMattingModelId(m_refine.mattingModelId).isEmpty()
        && !m_refine.hasEdgeOps()) {
        emitError(tr("Install a matting model or enable an edge option to refine."));
        return;
    }

    AiImageSnapshot snapshot;
    int target = -1;
    emit statusChanged(tr("Preparing image"));
    if (!buildSnapshot(sample, snapshot, &target)) {
        emitError(tr("Failed to prepare the image for AI selection."));
        return;
    }

    AiMaskPipeline::Request req;
    req.snapshot = snapshot;
    req.prompt = prompt;
    req.segmenterModelId = resolvedModelId();
    req.mode = mode;
    req.options.operation = op;
    req.options.antiAlias = m_antiAlias;
    req.options.sample = sample;
    req.refine = m_refine;
    req.bgEngine = m_bgEngine;
    req.baseMask = baseMask;
    // RefineMask always runs the refine pass even if the global toggle is off.
    if (mode == AiToolMode::RefineMask)
        req.refine.enabled = true;

    m_pendingAction = action;
    setBusy(true);
    m_pendingJobId = AiJobRunner::instance()->submit(req, target);
    qInfo().noquote() << "[AI] Submitted job" << m_pendingJobId
                      << "mode=" << int(mode) << "model=" << req.segmenterModelId;
}

// ── Result handling ───────────────────────────────────────────────────────────

void AiObjectSelectionController::handleStatus(quint64 jobId, const QString& status)
{
    if (jobId != m_pendingJobId)
        return; // status for a superseded / other-document job
    emit statusChanged(status);
}

void AiObjectSelectionController::handleResult(const AiJobResult& result)
{
    // Drop results that are no longer ours: superseded by a newer job, or for a
    // document that is no longer the one this controller drives.
    if (result.jobId != m_pendingJobId)
        return;
    if (result.documentId != currentDocumentId()) {
        qInfo().noquote() << "[AI] Discarding result for a different document";
        m_pendingJobId = 0;
        setBusy(false);
        return;
    }

    setBusy(false);
    const PendingAction action = m_pendingAction;
    m_pendingAction = PendingAction::None;
    m_pendingJobId = 0;

    if (result.cancelled) {
        emit statusChanged(tr("Cancelled"));
        return;
    }
    if (!result.mask.isValid()) {
        // A GPU out-of-memory with CPU fallback disabled comes back as a hard
        // error: surface it as an alert offering AI Settings / Use CPU (spec §19).
        if (result.error.contains(QStringLiteral("GPU memory"), Qt::CaseInsensitive)) {
            AiCompatibilityMessage m;
            m.severity = AiCompatibilitySeverity::Error;
            m.title = tr("Not enough GPU memory");
            m.message = result.error;
            m.actionId = QStringLiteral("cuda_oom");
            emit aiAlertRequested(m);
        } else if (!result.error.isEmpty()) {
            // A real failure: surface it as an alert, not options-bar status text.
            emitError(result.error);
        } else {
            // Soft miss (the model simply found nothing): a status hint, not an error.
            emit statusChanged(tr("No object found."));
        }
        return;
    }

    qInfo().noquote() << "[AI][SEL] result mask docBounds=" << result.mask.bounds
                      << "maskSize=" << result.mask.mask.size()
                      << "action=" << int(action);

    switch (action) {
    case PendingAction::SelectionFromPrompt:
        if (applyMaskAsSelection(result.mask, result.operation, tr("AI Select")))
            emit statusChanged(tr("Ready"));
        break;
    case PendingAction::SelectionFromSubject:
        if (applyMaskAsSelection(result.mask, AiSelectionOperation::Replace,
                                 tr("AI Select Subject")))
            emit statusChanged(tr("Ready"));
        break;
    case PendingAction::SelectionFromRefine:
        if (applyMaskAsSelection(result.mask, AiSelectionOperation::Replace,
                                 tr("AI Refine Mask")))
            emit statusChanged(tr("Ready"));
        break;
    case PendingAction::MaskFromSubject: {
        // Non-destructive: turn the subject selection into a layer mask using the
        // editor's existing API (which records its own undo step).
        Document* doc = m_controller->document();
        const int target = result.targetLayerIndex;
        if (!doc || !doc->nodeAt(target)) {
            emitError(tr("The target layer is no longer available."));
            break;
        }
        if (applyMaskAsSelection(result.mask, AiSelectionOperation::Replace,
                                 tr("AI Remove Background"))) {
            m_controller->createMaskFromSelection(target);
            emit statusChanged(tr("Ready"));
        }
        break;
    }
    case PendingAction::None:
        break;
    }
}

bool AiObjectSelectionController::applyMaskAsSelection(const AiMaskResult& mask,
                                                      AiSelectionOperation op,
                                                      const QString& undoName)
{
    Document* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc || !mask.isValid())
        return false;

    const int dw = doc->size.width();
    const int dh = doc->size.height();

    QImage before = doc->selection.image().isNull()
        ? QImage() : doc->selection.image().copy();
    const bool beforeActive = doc->selection.active();

    // Start from the existing selection only when it is active and correctly
    // sized; otherwise treat the base as empty (the combine helper handles this).
    if (!beforeActive || doc->selection.image().isNull()
        || doc->selection.width() != dw || doc->selection.height() != dh) {
        doc->selection.create(dw, dh);   // zero-filled base of document size
    }

    doc->selection.combineGrayscaleMask(mask.mask, toSelectMode(op));
    doc->selection.setActive(!doc->selection.isEmpty());

    if (m_canvas) {
        m_canvas->markSelectMaskDirty();
        m_canvas->update();
    }

    QImage after = doc->selection.image().copy();
    const bool afterActive = doc->selection.active();
    m_controller->history().push(std::make_unique<SelectionCommand>(
        doc, before, after, beforeActive, afterActive, undoName));
    emit m_controller->selectionChanged();
    return true;
}

void AiObjectSelectionController::setBusy(bool busy)
{
    if (m_busy == busy)
        return;
    m_busy = busy;
    emit busyChanged(busy);
}

void AiObjectSelectionController::emitError(const QString& message, const QString& title)
{
    // Errors must surface as an alert dialog, never as options-bar status text.
    AiCompatibilityMessage m;
    m.severity = AiCompatibilitySeverity::Error;
    m.title = title.isEmpty() ? tr("AI Select failed") : title;
    m.message = message;
    emit aiAlertRequested(m);
}

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void AiObjectSelectionController::cancelPending()
{
    if (m_pendingJobId != 0) {
        AiJobRunner::instance()->cancelAll();
        m_pendingJobId = 0;
        m_pendingAction = PendingAction::None;
        setBusy(false);
    }
}

void AiObjectSelectionController::onProviderOrSettingsChanged()
{
    cancelPending();
    auto* runner = AiJobRunner::instance();
    const AiRuntimeSettings s = AiRuntimeManager::instance()->settings();
    runner->setEmbeddingCacheEnabled(s.embeddingCacheEnabled);
    runner->setMaxCachedDocuments(std::max(1, s.maxCachedDocuments));
    runner->resetPipeline();   // provider may have changed: drop loaded session
    emit availabilityChanged();
}

void AiObjectSelectionController::notifyDocumentClosing()
{
    cancelPending();
    AiJobRunner::instance()->invalidateDocument(currentDocumentId());
}
