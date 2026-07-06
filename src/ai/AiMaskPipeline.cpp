#include "ai/AiMaskPipeline.hpp"

#include "ai/sam/Sam1Segmenter.hpp"
#include "ai/sam/SamPostprocessor.hpp"
#include "ai/AiSubjectSelector.hpp"
#include "ai/AiMaskPostProcessor.hpp"
#include "ai/matting/AiMattingModel.hpp"
#include "ai/matting/AiRefineModelFactory.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/runtime/AiRuntimeError.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <QCoreApplication>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QDir>

namespace {
// Opt-in (AI_SEL_DEBUG=1): saves the exact snapshot the SAM model saw, with the
// prompt point/box and the returned mask drawn on top, so we can ground-truth
// whether the mask aligns with the object *inside the snapshot* (coords/SAM ok)
// or not (snapshot vs document misalignment).
void dumpSnapshotDebug(const AiImageSnapshot& snap, const QVector<AiPromptPoint>& snapPoints,
                       const QRectF* snapBox, const SamMaskCandidate& cand)
{
    if (qEnvironmentVariableIntValue("AI_SEL_DEBUG") <= 0)
        return;
    QImage img = snap.image.convertToFormat(QImage::Format_RGBA8888);
    if (img.isNull())
        return;
    // Green overlay where the candidate logits are positive.
    if (cand.isValid() && cand.size == img.size()) {
        for (int y = 0; y < img.height(); ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(img.scanLine(y));
            const float* m = cand.logits.data() + qsizetype(y) * img.width();
            for (int x = 0; x < img.width(); ++x)
                if (m[x] > 0.0f)
                    row[x] = qRgba(0, 255, 0, 255);
        }
    }
    QPainter p(&img);
    p.setPen(QPen(Qt::red, 2));
    for (const AiPromptPoint& pt : snapPoints) {
        p.drawLine(pt.pos.x() - 8, pt.pos.y(), pt.pos.x() + 8, pt.pos.y());
        p.drawLine(pt.pos.x(), pt.pos.y() - 8, pt.pos.x(), pt.pos.y() + 8);
    }
    if (snapBox) {
        p.setBrush(Qt::NoBrush);
        p.drawRect(*snapBox);
    }
    p.end();
    const QString path = QDir::temp().filePath(QStringLiteral("ai_sel_debug.png"));
    img.save(path);
    qInfo().noquote() << "[AI][SEL] debug snapshot saved to" << path;
}

// One consolidated, greppable line per AI action: which action ran, the model it
// chose, that model's category, and the execution provider. Logged at the point
// the model is resolved so it reflects what actually ran (not a UI guess).
void logAiAction(const char* action, const QString& modelId, const char* category,
                 const QString& provider)
{
    qInfo().noquote() << "[AI] action=" << action
                      << "model=" << (modelId.isEmpty() ? QStringLiteral("<none>") : modelId)
                      << "category=" << category
                      << "provider=" << (provider.isEmpty() ? QStringLiteral("<unknown>") : provider);
}

// Wraps a document-space grayscale alpha into an AiMaskResult (non-zero bbox).
AiMaskResult maskResultFromAlpha(const QImage& alpha, float score)
{
    AiMaskResult r;
    if (alpha.isNull())
        return r;
    QImage g = alpha.format() == QImage::Format_Grayscale8
                   ? alpha : alpha.convertToFormat(QImage::Format_Grayscale8);
    cv::Mat m(g.height(), g.width(), CV_8UC1,
              const_cast<uchar*>(g.bits()), static_cast<size_t>(g.bytesPerLine()));
    cv::Mat nz;
    cv::findNonZero(m > 16, nz);
    if (nz.empty())
        return r;
    const cv::Rect bb = cv::boundingRect(nz);
    r.mask = g;
    r.bounds = QRectF(bb.x, bb.y, bb.width, bb.height);
    r.score = score;
    r.ok = true;
    return r;
}
} // namespace

AiMaskPipeline::AiMaskPipeline() = default;
AiMaskPipeline::~AiMaskPipeline() = default;

void AiMaskPipeline::reset()
{
    m_segmenter.reset();
    m_matting.reset();
    m_bgRemoval.reset();
    m_cache.clear();
}

std::shared_ptr<Sam1Segmenter> AiMaskPipeline::ensureSegmenter(const QString& modelId,
                                                               QString* error,
                                                               const Progress& progress)
{
    if (m_segmenter && m_segmenter->modelId() == modelId)
        return m_segmenter;

    // Switching model: drop the previous segmenter and any cached embeddings,
    // which were keyed to the old model.
    if (progress) progress(QStringLiteral("Loading model"));
    m_segmenter.reset();
    auto seg = Sam1Segmenter::load(modelId, error);
    if (seg)
        m_segmenter = seg;
    return seg;
}

std::shared_ptr<const SamEmbedding> AiMaskPipeline::ensureEmbedding(
    const Request& request, Sam1Segmenter& segmenter, bool* fromCache,
    QString* error, const std::atomic<bool>* cancel, const Progress& progress)
{
    SamEmbeddingKey key;
    key.documentId = request.snapshot.documentId;
    key.sourceLayerIndex = request.snapshot.sourceLayerIndex;
    key.sourceRevision = request.snapshot.sourceRevision;
    key.modelId = segmenter.modelId();
    key.provider = segmenter.providerUsed();
    key.snapshotSize = request.snapshot.image.size();
    key.inputSize = segmenter.inputSize();

    if (m_cacheEnabled) {
        if (auto cached = m_cache.get(key)) {
            if (fromCache) *fromCache = true;
            qInfo().noquote() << "[AI][CACHE] Embedding hit" << key.modelId
                              << key.snapshotSize;
            return cached;
        }
    }

    if (cancel && cancel->load())
        return nullptr;

    if (progress) progress(QStringLiteral("Building embedding"));
    qInfo().noquote() << "[AI][CACHE] Embedding miss — encoding" << key.modelId
                      << key.snapshotSize;
    auto emb = segmenter.buildEmbedding(request.snapshot.image, error);
    if (!emb)
        return nullptr;

    std::shared_ptr<const SamEmbedding> shared = emb;
    if (m_cacheEnabled)
        m_cache.put(key, shared);
    if (fromCache) *fromCache = false;
    return shared;
}

std::shared_ptr<AiMattingModel> AiMaskPipeline::ensureMatting(const QString& modelId,
                                                             QString* error,
                                                             const Progress& progress)
{
    if (m_matting && m_matting->modelId() == modelId)
        return m_matting;
    if (progress) progress(QStringLiteral("Loading refine model"));
    m_matting.reset();
    auto m = AiRefineModelFactory::createMatting(modelId, error);
    if (m)
        m_matting = m;
    return m;
}

std::shared_ptr<AiBackgroundRemovalModel> AiMaskPipeline::ensureBgRemoval(const QString& modelId,
                                                                         QString* error,
                                                                         const Progress& progress)
{
    if (m_bgRemoval && m_bgRemoval->modelId() == modelId)
        return m_bgRemoval;
    if (progress) progress(QStringLiteral("Loading model"));
    m_bgRemoval.reset();
    auto m = AiRefineModelFactory::createBackgroundRemoval(modelId, error);
    if (m)
        m_bgRemoval = m;
    return m;
}

AiMaskResult AiMaskPipeline::refineMask(const Request& request, const QImage& baseMask,
                                        bool* refined, const std::atomic<bool>* cancel,
                                        const Progress& progress)
{
    if (refined) *refined = false;
    QImage alpha = baseMask;

    // 1) Matting model (optional): replace the hard base mask with a soft matte
    //    constrained to it.
    const QString mattingId = AiRefineModelFactory::resolveMattingModelId(request.refine.mattingModelId);
    if (!mattingId.isEmpty()) {
        QString err;
        auto model = ensureMatting(mattingId, &err, progress);
        if (model) {
            if (progress) progress(QStringLiteral("Refining mask"));
            QString runErr;
            AiAlphaResult ar = model->refine(request.snapshot, &baseMask, request.refine,
                                             cancel, &runErr);
            if (ar.isValid()) {
                alpha = ar.alpha;
                if (refined) *refined = true;
            } else {
                qWarning().noquote() << "[AI][MATTE] refine failed:" << runErr;
            }
        } else {
            qWarning().noquote() << "[AI][MATTE] could not load matting model:" << err;
        }
    }

    if (cancel && cancel->load())
        return AiMaskResult();

    // 2) Edge / cleanup adapter (always available, even with no matting model).
    if (request.refine.hasEdgeOps())
        alpha = AiMaskPostProcessor::applyRefine(alpha, request.refine);

    return maskResultFromAlpha(alpha, 0.0f);
}

AiMaskPipeline::Result AiMaskPipeline::runObjectSelectionInternal(const Request& request,
                                                          const std::atomic<bool>* cancel,
                                                          const Progress& progress)
{
    Result result;
    auto seg = ensureSegmenter(request.segmenterModelId, &result.error, progress);
    if (!seg)
        return result;
    result.providerUsed = seg->providerUsed();
    logAiAction("SelectObject", request.segmenterModelId, "segmentation", result.providerUsed);

    auto emb = ensureEmbedding(request, *seg, &result.fromCache, &result.error, cancel, progress);
    if (!emb)
        return result;
    if (cancel && cancel->load()) {
        result.error = QStringLiteral("Cancelled");
        return result;
    }

    if (progress) progress(QStringLiteral("Selecting object"));
    // Convert the document-space prompt into snapshot coordinates.
    QVector<AiPromptPoint> snapPoints;
    snapPoints.reserve(request.prompt.points.size());
    for (const AiPromptPoint& p : request.prompt.points)
        snapPoints.push_back(AiPromptPoint{ request.snapshot.documentToSnapshot(p.pos),
                                            p.foreground });

    SamMaskCandidate candidate;
    if (request.prompt.kind == AiPrompt::Kind::Box && !request.prompt.box.isEmpty()) {
        const QRectF snapBox(request.snapshot.documentToSnapshot(request.prompt.box.topLeft()),
                             request.snapshot.documentToSnapshot(request.prompt.box.bottomRight()));
        qInfo().noquote() << "[AI][SEL] decode snapBox=" << snapBox
                          << "snapshotSize=" << request.snapshot.image.size();
        candidate = seg->decode(*emb, snapPoints, &snapBox, &result.error);
    } else {
        for (const AiPromptPoint& p : snapPoints)
            qInfo().noquote() << "[AI][SEL] decode snapPoint=" << p.pos << "fg=" << p.foreground
                              << "snapshotSize=" << request.snapshot.image.size();
        candidate = seg->decode(*emb, snapPoints, nullptr, &result.error);
    }
    if (!candidate.isValid())
        return result;

    {
        const QRectF snapBox = (request.prompt.kind == AiPrompt::Kind::Box)
            ? QRectF(request.snapshot.documentToSnapshot(request.prompt.box.topLeft()),
                     request.snapshot.documentToSnapshot(request.prompt.box.bottomRight()))
            : QRectF();
        dumpSnapshotDebug(request.snapshot, snapPoints,
                          snapBox.isNull() ? nullptr : &snapBox, candidate);
    }

    if (cancel && cancel->load()) {
        result.error = QStringLiteral("Cancelled");
        return result;
    }

    result.mask = SamPostprocessor::toDocumentMask(candidate, request.snapshot.documentSize,
                                                   request.options);
    if (!result.mask.isValid()) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("No object found at this location.");
        return result;
    }

    // Optional edge refinement / matting on top of the SAM selection.
    if (request.refine.enabled) {
        bool refined = false;
        AiMaskResult r = refineMask(request, result.mask.mask, &refined, cancel, progress);
        if (r.isValid()) {
            r.score = result.mask.score;
            result.mask = r;
            result.refined = refined;
        }
    }
    return result;
}

AiMaskPipeline::Result AiMaskPipeline::runRemoveBackgroundInternal(const Request& request,
                                                           const std::atomic<bool>* cancel,
                                                           const Progress& progress)
{
    Result result;

    // Resolve the engine. Auto bases the cut-out on SAM subject selection so the
    // "Refine Edges" toggle actually controls whether the matting refine pipeline
    // runs on top of it (see runSamSubjectInternal). Only when no SAM model is
    // installed does Auto fall back to a direct foreground-matte engine.
    AiBackgroundRemovalEngine engine = request.bgEngine;
    if (engine == AiBackgroundRemovalEngine::Auto) {
        if (!request.segmenterModelId.isEmpty())
            engine = AiBackgroundRemovalEngine::SamRefine;
        else if (!AiRefineModelFactory::resolveBackgroundRemovalModelId(QString()).isEmpty())
            engine = AiBackgroundRemovalEngine::Rmbg;
        else if (!AiRefineModelFactory::resolveMattingModelId(QString()).isEmpty())
            engine = AiBackgroundRemovalEngine::BiRefNet;
        else
            engine = AiBackgroundRemovalEngine::SamRefine;
    }

    // ── Direct foreground-matte engines (no SAM) ──
    if (engine == AiBackgroundRemovalEngine::Rmbg) {
        const QString id = AiRefineModelFactory::resolveBackgroundRemovalModelId(QString());
        auto model = ensureBgRemoval(id, &result.error, progress);
        if (!model) {
            if (result.error.isEmpty())
                result.error = QStringLiteral("No background-removal model is installed.");
            return result;
        }
        result.providerUsed = model->providerUsed();
        logAiAction("RemoveBackground", id, "background_removal", result.providerUsed);
        if (progress) progress(QStringLiteral("Removing background"));
        AiBackgroundRemovalOptions opts; opts.engine = engine; opts.refine = request.refine;
        AiAlphaResult ar = model->removeBackground(request.snapshot, opts, cancel, &result.error);
        if (!ar.isValid())
            return result;
        QImage alpha = ar.alpha;
        if (request.refine.enabled && request.refine.hasEdgeOps())
            alpha = AiMaskPostProcessor::applyRefine(alpha, request.refine);
        result.mask = maskResultFromAlpha(alpha, ar.score);
        result.refined = true;
        if (!result.mask.isValid() && result.error.isEmpty())
            result.error = QStringLiteral("Could not build a foreground mask.");
        return result;
    }

    if (engine == AiBackgroundRemovalEngine::BiRefNet) {
        const QString id = AiRefineModelFactory::resolveMattingModelId(request.refine.mattingModelId);
        auto model = ensureMatting(id, &result.error, progress);
        if (!model) {
            if (result.error.isEmpty())
                result.error = QStringLiteral("No matting model is installed.");
            return result;
        }
        result.providerUsed = model->providerUsed();
        logAiAction("RemoveBackground", id, "matting", result.providerUsed);
        if (progress) progress(QStringLiteral("Removing background"));
        // Full-frame matte (no base mask): the matting model segments the subject.
        AiAlphaResult ar = model->refine(request.snapshot, nullptr, request.refine, cancel,
                                         &result.error);
        if (!ar.isValid())
            return result;
        QImage alpha = ar.alpha;
        if (request.refine.enabled && request.refine.hasEdgeOps())
            alpha = AiMaskPostProcessor::applyRefine(alpha, request.refine);
        result.mask = maskResultFromAlpha(alpha, ar.score);
        result.refined = true;
        if (!result.mask.isValid() && result.error.isEmpty())
            result.error = QStringLiteral("Could not build a foreground mask.");
        return result;
    }

    // ── SAM + (optional) refine engine ──
    // Explicit "SAM + Refine": run the shared SAM subject path (same model/decoder
    // as Select Subject). request.mode == RemoveBackground keeps the label correct.
    return runSamSubjectInternal(request, cancel, progress);
}

AiMaskPipeline::Result AiMaskPipeline::runSamSubjectInternal(const Request& request,
                                                     const std::atomic<bool>* cancel,
                                                     const Progress& progress)
{
    Result result;
    auto seg = ensureSegmenter(request.segmenterModelId, &result.error, progress);
    if (!seg)
        return result;
    result.providerUsed = seg->providerUsed();

    const bool forRemoveBg = request.mode == AiToolMode::RemoveBackground;
    logAiAction(forRemoveBg ? "RemoveBackground" : "SelectSubject",
                request.segmenterModelId, "segmentation", result.providerUsed);

    auto emb = ensureEmbedding(request, *seg, &result.fromCache, &result.error, cancel, progress);
    if (!emb)
        return result;
    if (cancel && cancel->load()) {
        result.error = QStringLiteral("Cancelled");
        return result;
    }

    if (progress)
        progress(forRemoveBg ? QStringLiteral("Removing background")
                             : QStringLiteral("Selecting subject"));
    AiSubjectSelector selector;
    SamMaskCandidate candidate = selector.selectMainSubject(
        *seg, *emb, request.snapshot.image.size(), request.options, cancel);
    if (!candidate.isValid()) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("Could not identify a main subject.");
        return result;
    }

    result.mask = SamPostprocessor::toDocumentMask(candidate, request.snapshot.documentSize,
                                                   request.options);
    if (!result.mask.isValid()) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("Could not build a foreground mask.");
        return result;
    }

    if (request.refine.enabled) {
        bool refined = false;
        const float baseScore = result.mask.score;
        AiMaskResult r = refineMask(request, result.mask.mask, &refined, cancel, progress);
        if (r.isValid()) {
            r.score = baseScore;
            result.mask = r;
            result.refined = refined;
        }
    }
    return result;
}

AiMaskPipeline::Result AiMaskPipeline::runRefineExistingMaskInternal(const Request& request,
                                                             const std::atomic<bool>* cancel,
                                                             const Progress& progress)
{
    Result result;
    if (request.baseMask.isNull()) {
        result.error = QStringLiteral("No existing mask to refine.");
        return result;
    }
    // Ensure the base is document-sized grayscale.
    QImage base = request.baseMask;
    if (base.size() != request.snapshot.documentSize)
        base = base.scaled(request.snapshot.documentSize, Qt::IgnoreAspectRatio,
                           Qt::SmoothTransformation);
    if (base.format() != QImage::Format_Grayscale8)
        base = base.convertToFormat(QImage::Format_Grayscale8);

    bool refined = false;
    result.mask = refineMask(request, base, &refined, cancel, progress);
    result.refined = refined;
    if (m_matting)
        result.providerUsed = m_matting->providerUsed();
    logAiAction("RefineMask",
                AiRefineModelFactory::resolveMattingModelId(request.refine.mattingModelId),
                "matting", result.providerUsed);
    if (!result.mask.isValid() && result.error.isEmpty())
        result.error = QStringLiteral("Mask refinement produced an empty result.");
    return result;
}

// ── Public run methods: thin CUDA-OOM→CPU retry wrappers (spec §19) ──────────

AiMaskPipeline::Result AiMaskPipeline::runWithCpuFallback(const QString& label,
                                                          const std::function<Result()>& run)
{
    Result r = run();
    if (r.ok())
        return r;

    const AiRuntimeError err = AiRuntimeError::fromMessage(r.providerUsed, r.error);
    if (!(err.isCudaProviderError() && err.isOutOfMemory()))
        return r; // not an OOM — leave the original error untouched

    auto* rt = AiRuntimeManager::instance();
    if (rt->cudaOomFallbackActive())
        return r; // this run already used CPU (sticky fallback): don't loop

    if (!rt->settings().allowCpuFallback) {
        r.error = QCoreApplication::translate("AiMaskPipeline",
            "GPU memory was not enough to run this AI operation. Enable CPU fallback in "
            "Settings > AI / Machine Learning, or reduce the AI working resolution.");
        return r;
    }

    const QString id = label.isEmpty() ? QStringLiteral("<unknown>") : label;
    qWarning().noquote() << "[AI][ONNX] CUDA Run failed with OOM for model=" << id;
    qWarning().noquote() << "[AI][ONNX] Unloading CUDA session model=" << id;
    rt->noteCudaOomFallback();      // pin subsequent sessions to CPU
    rt->unloadAllSessions();        // evict cached GPU sessions so the retry rebuilds on CPU
    m_segmenter.reset();
    m_matting.reset();
    m_bgRemoval.reset();
    qWarning().noquote() << "[AI][ONNX] Retrying job on CPU model=" << id;

    Result r2 = run();              // same request, now forced onto CPU (and not retried again)
    r2.cpuFallbackUsed = true;
    qWarning().noquote() << "[AI][ONNX] CUDA OOM fallback used model=" << id;
    return r2;
}

AiMaskPipeline::Result AiMaskPipeline::runObjectSelection(const Request& request,
                                                          const std::atomic<bool>* cancel,
                                                          const Progress& progress)
{
    return runWithCpuFallback(request.segmenterModelId,
        [&] { return runObjectSelectionInternal(request, cancel, progress); });
}

AiMaskPipeline::Result AiMaskPipeline::runRemoveBackground(const Request& request,
                                                           const std::atomic<bool>* cancel,
                                                           const Progress& progress)
{
    return runWithCpuFallback(request.segmenterModelId,
        [&] { return runRemoveBackgroundInternal(request, cancel, progress); });
}

AiMaskPipeline::Result AiMaskPipeline::runSelectSubject(const Request& request,
                                                        const std::atomic<bool>* cancel,
                                                        const Progress& progress)
{
    return runWithCpuFallback(request.segmenterModelId,
        [&] { return runSamSubjectInternal(request, cancel, progress); });
}

AiMaskPipeline::Result AiMaskPipeline::runRefineExistingMask(const Request& request,
                                                             const std::atomic<bool>* cancel,
                                                             const Progress& progress)
{
    return runWithCpuFallback(request.refine.mattingModelId,
        [&] { return runRefineExistingMaskInternal(request, cancel, progress); });
}
