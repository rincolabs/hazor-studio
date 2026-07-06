#include "ai/inpaint/AiInpaintPipeline.hpp"

#include "ai/inpaint/AiDirectInpaintModel.hpp"
#include "ai/inpaint/AiInpaintPreprocessor.hpp"
#include "ai/inpaint/StableDiffusionOnnxPipeline.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"

#include <QDebug>
#include <QDir>
#include <QObject>

namespace {

int imageAlphaNonZero(const QImage& image)
{
    if (image.isNull())
        return 0;
    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    int count = 0;
    for (int y = 0; y < rgba.height(); ++y) {
        const QRgb* row = reinterpret_cast<const QRgb*>(rgba.constScanLine(y));
        for (int x = 0; x < rgba.width(); ++x)
            if (qAlpha(row[x]) > 0)
                ++count;
    }
    return count;
}

void saveDebugImage(const QDir& dir, const QString& name, const QImage& image)
{
    if (image.isNull())
        return;
    image.save(dir.filePath(name));
}

QDir makeDebugDir(quint64 jobId)
{
    QDir root(QDir::current().filePath(QStringLiteral("tmp/agent/ai-remove-debug")));
    root.mkpath(QStringLiteral("."));
    const QString jobName = QStringLiteral("job-%1").arg(jobId, 4, 10, QLatin1Char('0'));
    root.mkpath(jobName);
    return QDir(root.filePath(jobName));
}

} // namespace

AiInpaintResult AiInpaintPipeline::runRemoveObject(const AiInpaintRequest& request)
{
    AiInpaintResult result;
    result.jobId = request.jobId;
    result.documentId = request.documentId;
    result.sourceRevision = request.sourceRevision;
    result.modelId = request.model.id;

    qInfo().noquote() << "[AI][INPAINT] Job start"
                      << "job=" << request.jobId
                      << "model=" << request.model.id
                      << "family=" << request.model.family
                      << "engine=" << static_cast<int>(request.options.engine)
                      << "prompt=" << request.options.prompt
                      << "source=" << request.sourceImage.size()
                      << "mask=" << request.mask.size();

    if (request.cancel && request.cancel->load()) {
        result.cancelled = true;
        return result;
    }

    QString error;
    const AiPreparedInpaintInput prepared =
        AiInpaintPreprocessor::prepare(request.sourceImage, request.mask,
                                       request.model, request.options, &error);
    if (!prepared.isValid()) {
        result.error = error;
        qWarning().noquote() << "[AI][INPAINT] Preprocess failed"
                             << "job=" << request.jobId
                             << "error=" << result.error;
        return result;
    }

    const QDir debugDir = makeDebugDir(request.jobId);
    saveDebugImage(debugDir, QStringLiteral("01-original-roi.png"), prepared.originalRoi);
    saveDebugImage(debugDir, QStringLiteral("02-mask-roi.png"), prepared.maskRoi);
    saveDebugImage(debugDir, QStringLiteral("03-model-image.png"), prepared.modelImage);
    saveDebugImage(debugDir, QStringLiteral("04-model-mask.png"), prepared.modelMask);
    qInfo().noquote() << "[AI][INPAINT] Debug dump"
                      << "job=" << request.jobId
                      << "dir=" << debugDir.absolutePath();

    const bool wantsStable = request.options.engine == AiRemoveEngine::StableDiffusion
        || request.model.family == QLatin1String("stable_diffusion");
    if (wantsStable) {
        auto bundle = AiRuntimeManager::instance()->getOrCreateBundle(request.model, &error);
        if (!bundle) {
            result.error = error.isEmpty()
                ? QObject::tr("Unable to load the Stable Diffusion model.")
                : error;
            return result;
        }

        StableDiffusionOnnxPipeline pipeline(bundle, request.model);
        StableDiffusionInpaintRequest stableRequest;
        stableRequest.prompt = request.options.prompt;
        stableRequest.negativePrompt = request.options.negativePrompt;
        stableRequest.image = prepared.modelImage;
        stableRequest.mask = prepared.modelMask;
        stableRequest.size = prepared.modelImage.size();
        stableRequest.steps = request.options.steps;
        stableRequest.strength = request.options.strength;
        stableRequest.guidanceScale = request.options.guidanceScale;
        stableRequest.seed = request.options.seed;
        stableRequest.cancel = request.cancel;
        const QImage generated = pipeline.inpaint(stableRequest, &error);
        if (request.cancel && request.cancel->load()) {
            result.cancelled = true;
            return result;
        }
        if (generated.isNull()) {
            result.error = error.isEmpty()
                ? QObject::tr("Stable Diffusion inpainting failed.")
                : error;
            return result;
        }

        saveDebugImage(debugDir, QStringLiteral("05-generated.png"), generated);
        result.patch = AiInpaintPreprocessor::blendGeneratedPatch(prepared.originalRoi,
                                                                  generated,
                                                                  prepared.maskRoi);
        saveDebugImage(debugDir, QStringLiteral("06-patch.png"), result.patch);
        result.blendMask = prepared.maskRoi;
        result.documentRoi = prepared.documentRoi;
        result.providerUsed = AiRuntimeManager::instance()->activeExecutionProvider();
        return result;
    }

    const AiInstalledModel installed =
        AiModelRegistry::instance()->installedModel(request.model.id);
    if (!installed.isValid()) {
        result.error = QObject::tr("Inpainting model is not installed.");
        return result;
    }

    auto session = AiRuntimeManager::instance()->createSession(installed, &error);
    if (!session) {
        result.error = error.isEmpty()
            ? QObject::tr("Unable to load the inpainting model.")
            : error;
        return result;
    }

    AiDirectInpaintModel model(session, request.model);
    const QImage generated = model.run(prepared.modelImage, prepared.modelMask,
                                       request.options, &error, request.cancel,
                                       debugDir.absolutePath());
    if (request.cancel && request.cancel->load()) {
        result.cancelled = true;
        return result;
    }
    if (generated.isNull()) {
        result.error = error.isEmpty()
            ? QObject::tr("Inpainting failed.")
            : error;
        qWarning().noquote() << "[AI][INPAINT] Direct model failed"
                             << "job=" << request.jobId
                             << "error=" << result.error;
        return result;
    }

    qInfo().noquote() << "[AI][INPAINT] Generated"
                      << "job=" << request.jobId
                      << "generated=" << generated.size()
                      << "alphaNonZero=" << imageAlphaNonZero(generated);
    saveDebugImage(debugDir, QStringLiteral("05-generated.png"), generated);

    result.patch = AiInpaintPreprocessor::blendGeneratedPatch(prepared.originalRoi,
                                                              generated,
                                                              prepared.maskRoi);
    saveDebugImage(debugDir, QStringLiteral("06-patch.png"), result.patch);
    result.blendMask = prepared.maskRoi;
    result.documentRoi = prepared.documentRoi;
    result.providerUsed = model.providerUsed();
    qInfo().noquote() << "[AI][INPAINT] Job result"
                      << "job=" << request.jobId
                      << "roi=" << result.documentRoi
                      << "patch=" << result.patch.size()
                      << "patchAlphaNonZero=" << imageAlphaNonZero(result.patch)
                      << "provider=" << result.providerUsed;
    return result;
}
