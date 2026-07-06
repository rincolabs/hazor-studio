#include "ai/tool/AiRemoveObjectController.hpp"

#include "ai/AiImageSnapshot.hpp"
#include "ai/AiJobRunner.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "controller/ImageController.hpp"
#include "core/Document.hpp"
#include "renderer/CanvasView.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"

#include <QRandomGenerator>

AiRemoveObjectController::AiRemoveObjectController(ImageController* controller,
                                                   CanvasView* canvas,
                                                   QObject* parent)
    : QObject(parent)
    , m_controller(controller)
    , m_canvas(canvas)
{
    auto* runner = AiJobRunner::instance();
    connect(runner, &AiJobRunner::inpaintResultReady,
            this, &AiRemoveObjectController::handleResult);
    connect(runner, &AiJobRunner::statusChanged,
            this, &AiRemoveObjectController::handleStatus);

    connect(AiModelRegistry::instance(), &AiModelRegistry::installedModelsChanged,
            this, &AiRemoveObjectController::availabilityChanged);
    connect(AiRuntimeManager::instance(), &AiRuntimeManager::settingsChanged,
            this, &AiRemoveObjectController::availabilityChanged);
}

quint64 AiRemoveObjectController::currentDocumentId() const
{
    return quint64(reinterpret_cast<quintptr>(m_controller ? m_controller->document() : nullptr));
}

QStringList AiRemoveObjectController::installedInpaintingModels() const
{
    QStringList ids;
    for (const AiModelDescriptor& d :
         AiModelRegistry::instance()->installedModelsForTask(QStringLiteral("remove_object"))) {
        ids << d.id;
    }
    return ids;
}

QString AiRemoveObjectController::modelDisplayName(const QString& id) const
{
    const auto model = AiModelRegistry::instance()->modelById(id);
    if (model && !model->displayName.isEmpty())
        return model->displayName;
    return id;
}

QString AiRemoveObjectController::resolvedModelId() const
{
    const QStringList installed = installedInpaintingModels();
    if (installed.isEmpty())
        return QString();
    if (!m_options.modelId.isEmpty() && installed.contains(m_options.modelId))
        return m_options.modelId;
    const auto best = AiModelRegistry::instance()->bestModelForTask(QStringLiteral("remove_object"));
    return best ? best->id : installed.first();
}

bool AiRemoveObjectController::isReady(QString* reason) const
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
            *reason = tr("AI Remove requires a local inpainting model.");
        return false;
    }
    return true;
}

bool AiRemoveObjectController::buildSnapshot(QImage& image, quint64& revision) const
{
    Document* doc = m_controller ? m_controller->document() : nullptr;
    if (!doc || doc->size.isEmpty())
        return false;

    RenderContext ctx;
    ctx.document = doc;
    ctx.outputSize = doc->size;
    ctx.targetType = RenderTargetType::Export;
    ctx.highQuality = true;

    switch (m_options.source) {
    case AiSnapshotSource::CurrentLayer:
        image = DocumentCompositor::compositeOnlyFlatIndex(doc, doc->activeFlatIndex, ctx);
        break;
    case AiSnapshotSource::CurrentAndBelow:
        image = DocumentCompositor::compositeFromFlatIndex(doc, doc->activeFlatIndex, ctx);
        break;
    case AiSnapshotSource::AllVisible:
    default:
        image = DocumentCompositor::composite(doc, ctx);
        break;
    }

    if (image.isNull())
        return false;
    image = image.convertToFormat(QImage::Format_RGBA8888);
    revision = AiImageSnapshot::hashImage(image);
    return true;
}

void AiRemoveObjectController::removeObject(const QImage& lassoMask)
{
    QString reason;
    if (!isReady(&reason)) {
        emit statusChanged(reason);
        emit error(reason);
        return;
    }
    if (lassoMask.isNull()) {
        emit statusChanged(tr("Draw a lasso around the object first."));
        return;
    }

    QImage snapshot;
    quint64 revision = 0;
    emit statusChanged(tr("Preparing image"));
    if (!buildSnapshot(snapshot, revision)) {
        emit statusChanged(tr("Failed to prepare the image for AI Remove."));
        return;
    }

    const QString modelId = resolvedModelId();
    const auto model = AiModelRegistry::instance()->modelById(modelId);
    if (!model || !model->isValid()) {
        emit statusChanged(tr("Inpainting model is not installed."));
        return;
    }

    AiInpaintRequest req;
    req.sourceImage = snapshot;
    req.mask = lassoMask.convertToFormat(QImage::Format_Grayscale8);
    req.options = m_options;
    req.options.modelId = modelId;
    if (req.options.randomSeed) {
        req.options.seed = QRandomGenerator::global()->generate64();
        if (req.options.seed == 0)
            req.options.seed = 1;
    }
    req.model = *model;
    req.sourceRevision = revision;
    req.documentId = currentDocumentId();

    cancel();
    m_sourceRevision = revision;
    setRunning(true);
    m_pendingJobId = AiJobRunner::instance()->submitInpaint(req);
}

void AiRemoveObjectController::handleStatus(quint64 jobId, const QString& status)
{
    if (jobId == m_pendingJobId)
        emit statusChanged(status);
}

void AiRemoveObjectController::handleResult(const AiInpaintResult& result)
{
    if (result.jobId != m_pendingJobId)
        return;
    if (result.documentId != currentDocumentId()
        || result.sourceRevision != m_sourceRevision) {
        setRunning(false);
        m_pendingJobId = 0;
        emit statusChanged(tr("AI Remove result is stale."));
        return;
    }

    setRunning(false);
    m_pendingJobId = 0;

    if (result.cancelled) {
        emit statusChanged(tr("AI Remove cancelled."));
        return;
    }
    if (!result.isValid()) {
        const QString message = result.error.isEmpty()
            ? tr("AI Remove failed.")
            : result.error;
        emit statusChanged(message);
        emit error(message);
        return;
    }

    AiRemoveApplyRequest apply;
    apply.generatedPatch = result.patch;
    apply.blendMask = result.blendMask;
    apply.documentRoi = result.documentRoi;
    apply.outputMode = m_options.outputMode;
    apply.sourceModelId = result.modelId;
    apply.prompt = m_options.prompt;
    if (!m_controller || !m_controller->applyAiRemoveResult(apply)) {
        emit statusChanged(tr("Unable to apply AI Remove result."));
        return;
    }
    emit statusChanged(tr("AI Remove finished."));
    emit finished();
}

void AiRemoveObjectController::setRunning(bool running)
{
    if (m_running == running)
        return;
    m_running = running;
    emit runningChanged(running);
}

void AiRemoveObjectController::cancel()
{
    if (m_pendingJobId != 0) {
        AiJobRunner::instance()->cancelAll();
        m_pendingJobId = 0;
        setRunning(false);
        emit statusChanged(tr("AI Remove cancelled."));
    }
}

void AiRemoveObjectController::notifyDocumentClosing()
{
    cancel();
}
