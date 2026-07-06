#include "ai/inpaint/StableDiffusionOnnxPipeline.hpp"

#include "ai/onnx/OnnxSessionHandle.hpp"
#include "ai/runtime/AiInferenceSession.hpp"
#include "ai/runtime/AiSessionBundle.hpp"

#include <QDir>
#include <QObject>
#include <QVariant>

#include <algorithm>
#include <cmath>

namespace {

QString firstMatchingName(const QStringList& names,
                          const QStringList& needles,
                          const QString& fallback)
{
    for (const QString& needle : needles) {
        for (const QString& name : names) {
            if (name.contains(needle, Qt::CaseInsensitive))
                return name;
        }
    }
    return names.isEmpty() ? fallback : names.first();
}

int inputIndex(const OnnxSessionHandle& handle, const QString& inputName)
{
    const QStringList names = handle.inputNames();
    for (int i = 0; i < names.size(); ++i) {
        if (names.at(i) == inputName)
            return i;
    }
    return -1;
}

OnnxTensorElementType inputType(const OnnxSessionHandle& handle,
                                const QString& inputName,
                                OnnxTensorElementType fallback = OnnxTensorElementType::Float32)
{
    const int index = inputIndex(handle, inputName);
    const std::vector<OnnxTensorElementType> types = handle.inputElementTypes();
    if (index >= 0 && static_cast<size_t>(index) < types.size())
        return types[static_cast<size_t>(index)];
    return fallback;
}

QString tokenInputName(const OnnxSessionHandle& handle)
{
    const QStringList names = handle.inputNames();
    for (const QString& name : names) {
        if (name.compare(QStringLiteral("input_ids"), Qt::CaseInsensitive) == 0)
            return name;
    }
    for (const QString& name : names) {
        if (name.contains(QStringLiteral("input_ids"), Qt::CaseInsensitive))
            return name;
    }

    const std::vector<OnnxTensorElementType> types = handle.inputElementTypes();
    for (int i = 0; i < names.size() && static_cast<size_t>(i) < types.size(); ++i) {
        if (types[static_cast<size_t>(i)] == OnnxTensorElementType::Int32
            || types[static_cast<size_t>(i)] == OnnxTensorElementType::Int64) {
            return names.at(i);
        }
    }
    return names.isEmpty() ? QStringLiteral("input_ids") : names.first();
}

bool hasInputTypeMetadata(const OnnxSessionHandle& handle, const QString& inputName)
{
    const int index = inputIndex(handle, inputName);
    const std::vector<OnnxTensorElementType> types = handle.inputElementTypes();
    return index >= 0 && static_cast<size_t>(index) < types.size();
}

std::vector<int64_t> inputShape(const OnnxSessionHandle& handle,
                                const QString& inputName,
                                std::vector<int64_t> fallback)
{
    const int index = inputIndex(handle, inputName);
    const std::vector<std::vector<int64_t>> shapes = handle.inputShapes();
    if (index >= 0 && static_cast<size_t>(index) < shapes.size())
        return shapes[static_cast<size_t>(index)];
    return fallback;
}

std::vector<int64_t> singleValueInputShape(const OnnxSessionHandle& handle,
                                           const QString& inputName)
{
    std::vector<int64_t> shape = inputShape(handle, inputName, { 1 });
    if (shape.empty())
        return { 1 };
    if (shape.size() == 1 && shape[0] <= 0)
        return { 1 };
    return shape;
}

StableDiffusionTensor truncateChannels(const StableDiffusionTensor& tensor, int channels)
{
    if (!tensor.isValid() || tensor.shape.size() != 4 || tensor.shape[1] <= channels)
        return tensor;
    StableDiffusionTensor out;
    out.shape = { tensor.shape[0], channels, tensor.shape[2], tensor.shape[3] };
    const int64_t plane = tensor.shape[2] * tensor.shape[3];
    out.data.resize(static_cast<size_t>(out.elementCount()));
    for (int64_t batch = 0; batch < tensor.shape[0]; ++batch) {
        for (int ch = 0; ch < channels; ++ch) {
            const size_t srcBase = static_cast<size_t>((batch * tensor.shape[1] + ch) * plane);
            const size_t dstBase = static_cast<size_t>((batch * channels + ch) * plane);
            std::copy_n(tensor.data.begin() + srcBase, static_cast<size_t>(plane),
                        out.data.begin() + dstBase);
        }
    }
    return out;
}

QSize alignedSize(QSize size)
{
    size.setWidth(std::max(8, (size.width() / 8) * 8));
    size.setHeight(std::max(8, (size.height() / 8) * 8));
    return size;
}

} // namespace

StableDiffusionOnnxPipeline::StableDiffusionOnnxPipeline(std::shared_ptr<AiSessionBundle> bundle,
                                                         AiModelDescriptor descriptor)
    : m_bundle(std::move(bundle))
    , m_descriptor(std::move(descriptor))
{
}

QImage StableDiffusionOnnxPipeline::textToImage(const StableDiffusionTextToImageRequest& request,
                                                QString* error)
{
    const QSize size = alignedSize(request.size);
    QImage blank(size, QImage::Format_RGBA8888);
    blank.fill(Qt::transparent);
    QImage mask(size, QImage::Format_Grayscale8);
    mask.fill(255);

    StableDiffusionInpaintRequest inpaintRequest;
    inpaintRequest.prompt = request.prompt;
    inpaintRequest.negativePrompt = request.negativePrompt;
    inpaintRequest.size = size;
    inpaintRequest.steps = request.steps;
    inpaintRequest.guidanceScale = request.guidanceScale;
    inpaintRequest.seed = request.seed;
    inpaintRequest.cancel = request.cancel;
    inpaintRequest.image = blank;
    inpaintRequest.mask = mask;
    inpaintRequest.strength = 1.0f;
    return inpaint(inpaintRequest, error);
}

QImage StableDiffusionOnnxPipeline::imageToImage(const StableDiffusionImageToImageRequest& request,
                                                 QString* error)
{
    if (request.image.isNull()) {
        if (error) *error = QObject::tr("Stable Diffusion image-to-image input is empty.");
        return {};
    }
    StableDiffusionTextToImageRequest base = request;
    return textToImage(base, error);
}

QImage StableDiffusionOnnxPipeline::inpaint(const StableDiffusionInpaintRequest& request,
                                            QString* error)
{
    if (!ensureTokenizer(error) || !ensureScheduler(error))
        return {};
    if (request.image.isNull() || request.mask.isNull()) {
        if (error) *error = QObject::tr("Stable Diffusion inpaint input is incomplete.");
        return {};
    }
    if (request.image.size() != request.mask.size()) {
        if (error) *error = QObject::tr("Stable Diffusion inpaint image and mask sizes do not match.");
        return {};
    }

    const QSize size = alignedSize(request.image.size());
    const StableDiffusionTensor imageTensor =
        StableDiffusionLatentUtils::imageToNchw(request.image, size);
    const StableDiffusionTensor maskTensor =
        StableDiffusionLatentUtils::maskToNchw(request.mask, size);
    const StableDiffusionTensor maskedImageTensor =
        StableDiffusionLatentUtils::maskedImage(imageTensor, maskTensor);
    if (!imageTensor.isValid() || !maskTensor.isValid() || !maskedImageTensor.isValid()) {
        if (error) *error = QObject::tr("Stable Diffusion inpaint preprocessing failed.");
        return {};
    }

    QString localError;
    const StableDiffusionTensor promptEmbeds = encodePrompt(request.prompt, &localError);
    if (!promptEmbeds.isValid()) {
        if (error) *error = localError;
        return {};
    }
    const StableDiffusionTensor negativeEmbeds = encodePrompt(request.negativePrompt, &localError);
    if (!negativeEmbeds.isValid()) {
        if (error) *error = localError;
        return {};
    }

    StableDiffusionTensor imageLatents = vaeEncode(imageTensor, &localError);
    if (!imageLatents.isValid()) {
        if (error) *error = localError;
        return {};
    }
    StableDiffusionTensor maskedImageLatents = vaeEncode(maskedImageTensor, &localError);
    if (!maskedImageLatents.isValid()) {
        if (error) *error = localError;
        return {};
    }

    const QSize latentSize(static_cast<int>(imageLatents.shape[3]),
                           static_cast<int>(imageLatents.shape[2]));
    const StableDiffusionTensor maskLatents =
        StableDiffusionLatentUtils::resizeMaskToLatents(maskTensor, latentSize);
    if (!maskLatents.isValid()) {
        if (error) *error = QObject::tr("Stable Diffusion mask latent preparation failed.");
        return {};
    }

    m_scheduler.setTimesteps(request.steps);
    const QVector<int> timesteps = m_scheduler.timesteps();
    if (timesteps.isEmpty()) {
        if (error) *error = QObject::tr("Stable Diffusion scheduler produced no timesteps.");
        return {};
    }

    const StableDiffusionTensor noise =
        StableDiffusionLatentUtils::randomNormal(imageLatents.shape, request.seed);
    const int stepCount = static_cast<int>(timesteps.size());
    const int initSteps = std::clamp(static_cast<int>(std::lround(stepCount * request.strength)),
                                     1, stepCount);
    const int startIndex = std::clamp(stepCount - initSteps, 0, stepCount - 1);
    StableDiffusionTensor latents = m_scheduler.addNoise(imageLatents, noise, timesteps.at(startIndex));
    if (!latents.isValid()) {
        if (error) *error = QObject::tr("Stable Diffusion latent initialization failed.");
        return {};
    }

    for (int i = startIndex; i < timesteps.size(); ++i) {
        if (request.cancel && request.cancel->load()) {
            if (error) *error = QObject::tr("Stable Diffusion inpainting cancelled.");
            return {};
        }
        const int timestep = timesteps.at(i);
        const StableDiffusionTensor modelInput = m_scheduler.scaleModelInput(latents, timestep);
        const StableDiffusionTensor noisePred = guidedNoise(modelInput, maskLatents,
                                                            maskedImageLatents, timestep,
                                                            promptEmbeds, negativeEmbeds,
                                                            request.guidanceScale, &localError);
        if (!noisePred.isValid()) {
            if (error) *error = localError;
            return {};
        }
        latents = m_scheduler.step(noisePred, timestep, latents);
        if (!latents.isValid()) {
            if (error) *error = QObject::tr("Stable Diffusion scheduler step failed.");
            return {};
        }
    }

    const StableDiffusionTensor decoded = vaeDecode(latents, &localError);
    if (!decoded.isValid()) {
        if (error) *error = localError;
        return {};
    }
    return StableDiffusionLatentUtils::nchwToImage(decoded, true);
}

StableDiffusionTensor StableDiffusionOnnxPipeline::encodePrompt(const QString& prompt,
                                                                QString* error)
{
    if (!ensureTokenizer(error))
        return {};
    if (!m_bundle) {
        if (error) *error = QObject::tr("Stable Diffusion session bundle is not available.");
        return {};
    }
    auto session = m_bundle->session(QStringLiteral("text_encoder"));
    if (!session || !session->hasHandle(QStringLiteral("text_encoder"))) {
        if (error) *error = QObject::tr("Stable Diffusion model is incomplete: missing text encoder component.");
        return {};
    }

    auto handle = session->handle(QStringLiteral("text_encoder"));
    const QString inputName = tokenInputName(*handle);
    const QVector<int64_t> ids = m_tokenizer.encode(prompt, 77);

    OnnxTensorData input;
    input.name = inputName;
    input.shape = { 1, ids.size() };
    input.elementType = inputType(*handle, inputName, OnnxTensorElementType::Int32);
    if (input.elementType == OnnxTensorElementType::Float32) {
        input.elementType = OnnxTensorElementType::Int32;
    }
    if (input.elementType == OnnxTensorElementType::Int32) {
        input.int32Data.reserve(ids.size());
        for (int64_t id : ids)
            input.int32Data.push_back(static_cast<int32_t>(id));
    } else if (input.elementType == OnnxTensorElementType::Int64) {
        input.int64Data.assign(ids.begin(), ids.end());
    } else if (!hasInputTypeMetadata(*handle, inputName)) {
        input.elementType = OnnxTensorElementType::Int32;
        input.int32Data.reserve(ids.size());
        for (int64_t id : ids)
            input.int32Data.push_back(static_cast<int32_t>(id));
    } else {
        if (error) *error = QObject::tr("Stable Diffusion text encoder input_ids must be int32 or int64.");
        return {};
    }

    std::vector<OnnxTensorData> outputs;
    QString runError;
    if (!handle->run({ input }, {}, outputs, &runError)) {
        if (error) *error = runError;
        return {};
    }
    if (outputs.empty() || outputs.front().data.empty()) {
        if (error) *error = QObject::tr("Stable Diffusion text encoder produced no embeddings.");
        return {};
    }

    StableDiffusionTensor out;
    out.shape = outputs.front().shape;
    out.data = std::move(outputs.front().data);
    return out;
}

StableDiffusionTensor StableDiffusionOnnxPipeline::vaeEncode(const StableDiffusionTensor& image,
                                                             QString* error)
{
    if (!m_bundle) {
        if (error) *error = QObject::tr("Stable Diffusion session bundle is not available.");
        return {};
    }
    auto session = m_bundle->session(QStringLiteral("vae_encoder"));
    if (!session || !session->hasHandle(QStringLiteral("vae_encoder"))) {
        if (error) *error = QObject::tr("Stable Diffusion model is incomplete: missing VAE encoder component.");
        return {};
    }
    auto handle = session->handle(QStringLiteral("vae_encoder"));
    const QString inputName = firstMatchingName(handle->inputNames(),
                                                { QStringLiteral("sample"), QStringLiteral("image") },
                                                QStringLiteral("sample"));
    std::vector<OnnxTensorData> outputs;
    QString runError;
    if (!handle->run({ tensorInput(inputName, image) }, {}, outputs, &runError)) {
        if (error) *error = runError;
        return {};
    }

    StableDiffusionTensor latents = truncateChannels(firstOutputAsTensor(outputs, error), 4);
    if (!latents.isValid())
        return {};
    return StableDiffusionLatentUtils::multiply(latents, latentScalingFactor());
}

StableDiffusionTensor StableDiffusionOnnxPipeline::vaeDecode(const StableDiffusionTensor& latents,
                                                             QString* error)
{
    if (!m_bundle) {
        if (error) *error = QObject::tr("Stable Diffusion session bundle is not available.");
        return {};
    }
    auto session = m_bundle->session(QStringLiteral("vae_decoder"));
    if (!session || !session->hasHandle(QStringLiteral("vae_decoder"))) {
        if (error) *error = QObject::tr("Stable Diffusion model is incomplete: missing VAE decoder component.");
        return {};
    }
    auto handle = session->handle(QStringLiteral("vae_decoder"));
    const QString inputName = firstMatchingName(handle->inputNames(),
                                                { QStringLiteral("latent"), QStringLiteral("sample") },
                                                QStringLiteral("latent_sample"));
    const StableDiffusionTensor scaledLatents =
        StableDiffusionLatentUtils::multiply(latents, 1.0f / latentScalingFactor());

    std::vector<OnnxTensorData> outputs;
    QString runError;
    if (!handle->run({ tensorInput(inputName, scaledLatents) }, {}, outputs, &runError)) {
        if (error) *error = runError;
        return {};
    }
    return firstOutputAsTensor(outputs, error);
}

StableDiffusionTensor StableDiffusionOnnxPipeline::runUnet(
    const StableDiffusionTensor& latents,
    const StableDiffusionTensor& maskLatents,
    const StableDiffusionTensor& maskedImageLatents,
    int timestep,
    const StableDiffusionTensor& promptEmbeds,
    QString* error)
{
    if (!m_bundle) {
        if (error) *error = QObject::tr("Stable Diffusion session bundle is not available.");
        return {};
    }
    auto session = m_bundle->session(QStringLiteral("unet"));
    if (!session || !session->hasHandle(QStringLiteral("unet"))) {
        if (error) *error = QObject::tr("Stable Diffusion model is incomplete: missing UNet component.");
        return {};
    }

    const StableDiffusionTensor sample =
        StableDiffusionLatentUtils::concatChannels(latents, maskLatents, maskedImageLatents);
    if (!sample.isValid()) {
        if (error) *error = QObject::tr("Stable Diffusion UNet input preparation failed.");
        return {};
    }

    auto handle = session->handle(QStringLiteral("unet"));
    const QStringList inputNames = handle->inputNames();
    const QString sampleName = firstMatchingName(inputNames, { QStringLiteral("sample") },
                                                QStringLiteral("sample"));
    const QString timestepName = firstMatchingName(inputNames, { QStringLiteral("timestep") },
                                                  QStringLiteral("timestep"));
    const QString hiddenName = firstMatchingName(inputNames,
                                                { QStringLiteral("encoder_hidden_states") },
                                                QStringLiteral("encoder_hidden_states"));

    OnnxTensorData timestepInput;
    timestepInput.name = timestepName;
    timestepInput.shape = singleValueInputShape(*handle, timestepName);
    timestepInput.elementType = inputType(*handle, timestepName, OnnxTensorElementType::Float32);
    if (timestepInput.elementType == OnnxTensorElementType::Float32) {
        timestepInput.data = { static_cast<float>(timestep) };
    } else if (timestepInput.elementType == OnnxTensorElementType::Int32) {
        timestepInput.int32Data = { static_cast<int32_t>(timestep) };
    } else {
        timestepInput.elementType = OnnxTensorElementType::Int64;
        timestepInput.int64Data = { timestep };
    }

    std::vector<OnnxTensorData> inputs;
    inputs.push_back(tensorInput(sampleName, sample));
    inputs.push_back(std::move(timestepInput));
    inputs.push_back(tensorInput(hiddenName, promptEmbeds));

    std::vector<OnnxTensorData> outputs;
    QString runError;
    if (!handle->run(inputs, {}, outputs, &runError)) {
        if (error) *error = runError;
        return {};
    }
    return truncateChannels(firstOutputAsTensor(outputs, error), 4);
}

StableDiffusionTensor StableDiffusionOnnxPipeline::guidedNoise(
    const StableDiffusionTensor& latents,
    const StableDiffusionTensor& maskLatents,
    const StableDiffusionTensor& maskedImageLatents,
    int timestep,
    const StableDiffusionTensor& promptEmbeds,
    const StableDiffusionTensor& negativeEmbeds,
    float guidanceScale,
    QString* error)
{
    const StableDiffusionTensor noiseUncond =
        runUnet(latents, maskLatents, maskedImageLatents, timestep, negativeEmbeds, error);
    if (!noiseUncond.isValid())
        return {};
    const StableDiffusionTensor noiseCond =
        runUnet(latents, maskLatents, maskedImageLatents, timestep, promptEmbeds, error);
    if (!noiseCond.isValid())
        return {};

    const StableDiffusionTensor delta = StableDiffusionLatentUtils::subtract(noiseCond, noiseUncond);
    return StableDiffusionLatentUtils::addScaled(noiseUncond, delta, guidanceScale);
}

StableDiffusionTensor StableDiffusionOnnxPipeline::firstOutputAsTensor(
    const std::vector<OnnxTensorData>& outputs,
    QString* error) const
{
    if (outputs.empty() || outputs.front().data.empty()) {
        if (error) *error = QObject::tr("Stable Diffusion model produced no tensor output.");
        return {};
    }
    StableDiffusionTensor out;
    out.shape = outputs.front().shape;
    out.data = outputs.front().data;
    return out;
}

OnnxTensorData StableDiffusionOnnxPipeline::tensorInput(const QString& name,
                                                        const StableDiffusionTensor& tensor) const
{
    OnnxTensorData input;
    input.name = name;
    input.shape = tensor.shape;
    input.data = tensor.data;
    return input;
}

float StableDiffusionOnnxPipeline::latentScalingFactor() const
{
    const QVariant value = m_descriptor.input.value(QStringLiteral("latentScalingFactor"),
                                                    m_descriptor.input.value(QStringLiteral("vaeScalingFactor"),
                                                                             0.18215));
    bool ok = false;
    const double parsed = value.toDouble(&ok);
    const float factor = static_cast<float>(ok ? parsed : 0.18215);
    return factor > 1.0f ? 0.18215f : std::max(1e-6f, factor);
}

bool StableDiffusionOnnxPipeline::ensureTokenizer(QString* error)
{
    if (m_tokenizerLoaded)
        return true;
    const QString vocab = filePath(QStringLiteral("tokenizer_vocab"));
    const QString merges = filePath(QStringLiteral("tokenizer_merges"));
    if (vocab.isEmpty() || merges.isEmpty()) {
        if (error) *error = QObject::tr("Stable Diffusion tokenizer files are missing.");
        return false;
    }
    m_tokenizerLoaded = m_tokenizer.load(vocab, merges, error);
    return m_tokenizerLoaded;
}

bool StableDiffusionOnnxPipeline::ensureScheduler(QString* error)
{
    if (m_schedulerLoaded)
        return true;
    const QString schedulerConfig = filePath(QStringLiteral("scheduler_config"));
    m_schedulerLoaded = m_scheduler.loadConfig(schedulerConfig, error);
    return m_schedulerLoaded;
}

QString StableDiffusionOnnxPipeline::filePath(const QString& componentName) const
{
    const QString rel = m_descriptor.files.extraFiles.value(componentName);
    if (rel.isEmpty())
        return {};
    return QDir(m_descriptor.rootDir).filePath(rel);
}
