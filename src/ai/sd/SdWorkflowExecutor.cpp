#include "ai/sd/SdWorkflowExecutor.hpp"

#include "ai/sd/SdCanvasBridge.hpp"
#include "ai/sd/SdModelManager.hpp"
#include "ai/sd/SdNodeCatalog.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QJsonArray>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

void addError(QVector<SdWorkflowError>& errors, SdWorkflowErrorCode code,
              const QString& message, const QString& nodeId = {}, const QString& portId = {})
{
    SdWorkflowError error;
    error.code = code;
    error.message = message;
    error.nodeId = nodeId;
    error.portId = portId;
    errors.push_back(error);
}

// A typed value produced by one graph node. Model values are immutable
// snapshots: a LoRA copies its input and appends itself, so branching before or
// after that node produces independent configurations without special cases.
//
// One flat struct rather than a tagged union, matching how Checkpoint/Lora
// already share `load`/`loras`: every node type fills in the handful of
// fields its own output means and leaves the rest at their default, which is
// never read because the port typing already keeps a Control value from
// reaching a slot that expects an IpAdapter one.
struct NodeValue {
    SdModelLoadOptions load;          // Checkpoint, transformed by Lora
    QVector<SdResolvedLora> loras;    // ordered model configuration
    QString text;                     // Prompt, NegativePrompt
    SdSamplingParams sampling;        // Sampling
    int width = 512;                  // Sampling
    int height = 512;                 // Sampling
    float denoisingStrength = 0.75f;  // Sampling
    QImage image;                     // InputImage, InputMask, Generate, Canny, Upscale, ADetailer
    QVector<QImage> images;           // PhotoMaker identity images

    // ControlNetLoader, carried forward by ControlNetApply
    QString controlNetPath;
    float controlStrength = 0.9f;     // ControlNetApply

    // IpAdapterLoader, carried forward by IpAdapterApply
    QString ipAdapterPath;
    QString visionEncoderPath;
    float ipAdapterStrength = 1.0f;   // IpAdapterApply

    // PhotoMaker / PuLid: both resolve an IdentityEmbedding model, so one
    // field serves either.
    QString photoMakerPath;
    QString identityEmbeddingPath;
    float photoMakerStyleStrength = 20.0f;
    QString pulidPath;
    float pulidWeight = 0.8f;
};

QStringList sourcesFor(const SdWorkflowDefinition& definition, const QString& nodeId,
                       const char* portId)
{
    QStringList out;
    for (const SdWorkflowConnection& connection : definition.connections) {
        if (connection.toNode == nodeId && connection.toPort == QLatin1String(portId))
            out << connection.fromNode;
    }
    return out;
}

const NodeValue* sourceValue(const SdWorkflowDefinition& definition,
                             const QHash<QString, NodeValue>& values,
                             const QString& nodeId, const char* portId)
{
    const QStringList sources = sourcesFor(definition, nodeId, portId);
    if (sources.isEmpty())
        return nullptr;
    const auto it = values.constFind(sources.first());
    return it == values.constEnd() ? nullptr : &it.value();
}

// Every connected source of a variadic port (PhotoMaker.images, Generate.references).
QVector<const NodeValue*> sourceValues(const SdWorkflowDefinition& definition,
                                       const QHash<QString, NodeValue>& values,
                                       const QString& nodeId, const char* portId)
{
    QVector<const NodeValue*> out;
    for (const QString& source : sourcesFor(definition, nodeId, portId)) {
        const auto it = values.constFind(source);
        if (it != values.constEnd())
            out.push_back(&it.value());
    }
    return out;
}

// Every fallback below comes from SdNodeCatalog rather than from a literal:
// the frontend shows the same default in the widget it builds.
int intProperty(const QJsonObject& props, SdNodeType type, const char* key)
{
    const QString name = QString::fromUtf8(key);
    return props.value(name).toInt(propertyDefault(type, name).toInt());
}

float floatProperty(const QJsonObject& props, SdNodeType type, const char* key)
{
    const QString name = QString::fromUtf8(key);
    return static_cast<float>(
        props.value(name).toDouble(propertyDefault(type, name).toDouble()));
}

QString stringProperty(const QJsonObject& props, SdNodeType type, const char* key)
{
    const QString name = QString::fromUtf8(key);
    return props.value(name).toString(propertyDefault(type, name).toString());
}

bool boolProperty(const QJsonObject& props, SdNodeType type, const char* key)
{
    const QString name = QString::fromUtf8(key);
    return props.value(name).toBool(propertyDefault(type, name).toBool());
}

// "customSigmas" and "skipLayerGuidanceLayers" are comma-separated lines
// rather than array-valued properties, matching every other free-form list
// in this catalog (referenceImageArgs, imagePreprocess): one text widget
// instead of a repeating sub-form for what is, in practice, a short list.
QVector<float> parseFloatList(const QString& text)
{
    QVector<float> out;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const float value = part.trimmed().toFloat(&ok);
        if (ok)
            out.push_back(value);
    }
    return out;
}

QVector<int> parseIntList(const QString& text)
{
    QVector<int> out;
    for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        bool ok = false;
        const int value = part.trimmed().toInt(&ok);
        if (ok)
            out.push_back(value);
    }
    return out;
}

qint64 readSeed(const QJsonObject& props)
{
    const QJsonValue value = props.value(QStringLiteral("seed"));
    if (value.isString())
        return value.toString().toLongLong();
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    return propertyDefault(SdNodeType::Sampling, QStringLiteral("seed"))
        .toString().toLongLong();
}

} // namespace

// Kept out of the public header: it is an implementation detail whose lifetime
// spans several asynchronous stable-diffusion.cpp calls.
struct SdWorkflowExecutionState {
    SdWorkflowDefinition definition;
    SdWorkflowPlan plan;
    QHash<QString, NodeValue> values;
    int nextNode = 0;
    QString activeGenerateNodeId;
    SdWorkflowRunResult result;
    int insertedOutputs = 0;
};

QString SdWorkflowRunResult::firstError() const
{
    if (!errors.isEmpty())
        return errors.first().message;
    return generation.error;
}

SdWorkflowExecutor::SdWorkflowExecutor(SdCanvasBridge* bridge, QObject* parent)
    : QObject(parent)
    , m_bridge(bridge)
{
    connect(&m_job, &SdGenerationJob::stateChanged,
            this, &SdWorkflowExecutor::stateChanged);
    connect(&m_job, &SdGenerationJob::progress,
            this, &SdWorkflowExecutor::progress);
    connect(&m_job, &SdGenerationJob::finished,
            this, &SdWorkflowExecutor::onJobFinished);
}

SdWorkflowExecutor::~SdWorkflowExecutor()
{
    shutdown();
}

void SdWorkflowExecutor::shutdown()
{
    m_job.shutdown();
    if (m_execution && m_bridge)
        m_bridge->endRun();
    m_execution.reset();
}

void SdWorkflowExecutor::cancel()
{
    m_job.cancel();
}

bool SdWorkflowExecutor::run(const QByteArray& workflowJson, QVector<SdWorkflowError>* errors)
{
    QVector<SdWorkflowError> local;
    QVector<SdWorkflowError>& out = errors ? *errors : local;

    if (m_execution || m_job.busy()) {
        addError(out, SdWorkflowErrorCode::UnsupportedOperation,
                 tr_("A workflow is already running."));
        return false;
    }

    SdWorkflowDefinition parsed;
    if (!SdWorkflowDefinition::fromJson(workflowJson, parsed, &out))
        return false;

    SdWorkflowDefinition definition;
    SdWorkflowPlan plan;
    if (!SdWorkflowValidator::validate(parsed, SdModelManager::instance(),
                                       definition, plan, out)) {
        qWarning().noquote() << "[SD] workflow rejected |" << describeWorkflowErrors(out);
        return false;
    }

    QString bridgeError;
    if (!m_bridge || !m_bridge->beginRun(&bridgeError)) {
        addError(out, SdWorkflowErrorCode::UnsupportedOperation,
                 bridgeError.isEmpty() ? tr_("No canvas is available.") : bridgeError,
                 plan.outputNodeIds.value(0));
        return false;
    }

    m_execution = std::make_unique<SdWorkflowExecutionState>();
    m_execution->definition = std::move(definition);
    m_execution->plan = std::move(plan);

    const AdvanceResult advanced = advance(out);
    if (advanced == AdvanceResult::Failed) {
        m_bridge->endRun();
        m_execution.reset();
        return false;
    }

    // A valid generation workflow always yields at its first Generate node.
    if (advanced == AdvanceResult::Completed) {
        addError(out, SdWorkflowErrorCode::NoGenerateNode,
                 tr_("The workflow completed without running a generate node."));
        m_bridge->endRun();
        m_execution.reset();
        return false;
    }

    qInfo().noquote() << "[SD] workflow started | nodes="
                      << m_execution->definition.nodes.size()
                      << "| operations=" << m_execution->plan.generateNodeIds.size()
                      << "| outputs=" << m_execution->plan.outputNodeIds.size()
                      << "| order=" << m_execution->plan.order.join(QLatin1String(" -> "));
    return true;
}

SdWorkflowExecutor::AdvanceResult SdWorkflowExecutor::advance(
    QVector<SdWorkflowError>& errors)
{
    if (!m_execution)
        return AdvanceResult::Failed;

    SdWorkflowExecutionState& execution = *m_execution;
    const SdModelManager& models = SdModelManager::instance();

    while (execution.nextNode < execution.plan.order.size()) {
        const QString nodeId = execution.plan.order.at(execution.nextNode);
        const SdWorkflowNode* node = execution.definition.node(nodeId);
        if (!node) {
            addError(errors, SdWorkflowErrorCode::UnknownNode,
                     tr_("A planned node no longer exists."), nodeId);
            return AdvanceResult::Failed;
        }

        const QJsonObject& properties = node->properties;
        NodeValue value;

        switch (node->type) {
        case SdNodeType::Checkpoint: {
            SdModelSelection selection;
            selection.modelId = properties.value(QStringLiteral("modelId")).toString();
            selection.vaeId = properties.value(QStringLiteral("vaeId")).toString();
            for (const QJsonValue& entry
                 : properties.value(QStringLiteral("textEncoderIds")).toArray()) {
                selection.textEncoderIds << entry.toString();
            }
            for (const QJsonValue& entry
                 : properties.value(QStringLiteral("embeddingIds")).toArray()) {
                selection.embeddingIds << entry.toString();
            }

            QString error;
            if (!models.resolve(selection, value.load.files, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return AdvanceResult::Failed;
            }
            weightTypeFromName(properties.value(QStringLiteral("weightType")).toString(),
                               value.load.weightType);
            devicePreferenceFromName(properties.value(QStringLiteral("device")).toString(),
                                     value.load.device);
            value.load.threads = intProperty(properties, node->type, "threads");
            value.load.allowCpuFallback =
                boolProperty(properties, node->type, "allowCpuFallback");
            value.load.enableMmap = boolProperty(properties, node->type, "enableMmap");
            value.load.flashAttention = boolProperty(properties, node->type, "flashAttention");
            value.load.diffusionFlashAttention =
                boolProperty(properties, node->type, "diffusionFlashAttention");
            value.load.vaeConvDirect = boolProperty(properties, node->type, "vaeConvDirect");
            value.load.batchedCfg = boolProperty(properties, node->type, "batchedCfg");
            value.load.autoFit = boolProperty(properties, node->type, "autoFit");
            value.load.conditioningCacheSize =
                intProperty(properties, node->type, "conditioningCacheSize");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::ControlNetLoader: {
            SdResolvedModelFile resolved;
            QString error;
            if (!models.resolveFile(properties.value(QStringLiteral("modelId")).toString(),
                                    SdModelCategory::ControlNet, resolved, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return AdvanceResult::Failed;
            }
            value.controlNetPath = resolved.path;
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::ControlNetApply: {
            const NodeValue* net = sourceValue(
                execution.definition, execution.values, nodeId, "control_net");
            const NodeValue* image = sourceValue(
                execution.definition, execution.values, nodeId, "image");
            if (!net || !image) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The ControlNet apply node has no evaluated input."), nodeId);
                return AdvanceResult::Failed;
            }
            value.controlNetPath = net->controlNetPath;
            value.image = image->image;
            value.controlStrength = floatProperty(properties, node->type, "strength");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::IpAdapterLoader: {
            SdResolvedModelFile resolved;
            QString error;
            if (!models.resolveFile(properties.value(QStringLiteral("modelId")).toString(),
                                    SdModelCategory::IpAdapter, resolved, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return AdvanceResult::Failed;
            }
            value.ipAdapterPath = resolved.path;
            const QString visionEncoderId =
                properties.value(QStringLiteral("visionEncoderId")).toString().trimmed();
            if (!visionEncoderId.isEmpty()) {
                SdResolvedModelFile vision;
                if (!models.resolveFile(visionEncoderId, SdModelCategory::VisionEncoder,
                                        vision, &error)) {
                    addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                    return AdvanceResult::Failed;
                }
                value.visionEncoderPath = vision.path;
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::IpAdapterApply: {
            const NodeValue* adapter = sourceValue(
                execution.definition, execution.values, nodeId, "ip_adapter");
            const NodeValue* image = sourceValue(
                execution.definition, execution.values, nodeId, "image");
            if (!adapter || !image) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The IP-Adapter apply node has no evaluated input."), nodeId);
                return AdvanceResult::Failed;
            }
            value.ipAdapterPath = adapter->ipAdapterPath;
            value.visionEncoderPath = adapter->visionEncoderPath;
            value.image = image->image;
            value.ipAdapterStrength = floatProperty(properties, node->type, "strength");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::PhotoMaker: {
            const QVector<const NodeValue*> images = sourceValues(
                execution.definition, execution.values, nodeId, "images");
            if (images.isEmpty()) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("PhotoMaker needs at least one identity image."), nodeId,
                         QStringLiteral("images"));
                return AdvanceResult::Failed;
            }
            QString error;
            SdResolvedModelFile resolved;
            if (!models.resolveFile(properties.value(QStringLiteral("modelId")).toString(),
                                    SdModelCategory::PhotoMaker, resolved, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return AdvanceResult::Failed;
            }
            value.photoMakerPath = resolved.path;
            const QString embeddingId =
                properties.value(QStringLiteral("embeddingId")).toString().trimmed();
            if (!embeddingId.isEmpty()) {
                SdResolvedModelFile embedding;
                if (!models.resolveFile(embeddingId, SdModelCategory::IdentityEmbedding,
                                        embedding, &error)) {
                    addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                    return AdvanceResult::Failed;
                }
                value.identityEmbeddingPath = embedding.path;
            }
            for (const NodeValue* source : images)
                value.images.push_back(source->image);
            value.photoMakerStyleStrength = floatProperty(properties, node->type, "styleStrength");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::PuLid: {
            QString error;
            SdResolvedModelFile resolved;
            if (!models.resolveFile(properties.value(QStringLiteral("modelId")).toString(),
                                    SdModelCategory::PuLid, resolved, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return AdvanceResult::Failed;
            }
            value.pulidPath = resolved.path;
            const QString embeddingId =
                properties.value(QStringLiteral("embeddingId")).toString().trimmed();
            if (!embeddingId.isEmpty()) {
                SdResolvedModelFile embedding;
                if (!models.resolveFile(embeddingId, SdModelCategory::IdentityEmbedding,
                                        embedding, &error)) {
                    addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                    return AdvanceResult::Failed;
                }
                value.identityEmbeddingPath = embedding.path;
            }
            value.pulidWeight = floatProperty(properties, node->type, "weight");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::Lora: {
            const NodeValue* input = sourceValue(
                execution.definition, execution.values, nodeId, "model");
            if (!input) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The LoRA node has no evaluated model input."), nodeId,
                         QStringLiteral("model"));
                return AdvanceResult::Failed;
            }
            value = *input;
            SdResolvedLora lora;
            QString error;
            if (!models.resolveLora(
                    properties.value(QStringLiteral("modelId")).toString(),
                    floatProperty(properties, node->type, "weight"), lora, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return AdvanceResult::Failed;
            }
            value.loras.push_back(lora);
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::Prompt:
        case SdNodeType::NegativePrompt:
            value.text = properties.value(QStringLiteral("text")).toString();
            execution.values.insert(nodeId, value);
            break;
        case SdNodeType::Sampling:
            value.width = intProperty(properties, node->type, "width");
            value.height = intProperty(properties, node->type, "height");
            value.sampling.steps = intProperty(properties, node->type, "steps");
            value.sampling.seed = readSeed(properties);
            value.sampling.cfgScale = floatProperty(properties, node->type, "cfgScale");
            value.sampling.sampler = stringProperty(properties, node->type, "sampler");
            value.sampling.scheduler = stringProperty(properties, node->type, "scheduler");
            value.sampling.clipSkip = intProperty(properties, node->type, "clipSkip");
            value.sampling.imageCfgScale = floatProperty(properties, node->type, "imageCfgScale");
            value.sampling.distilledGuidance =
                floatProperty(properties, node->type, "distilledGuidance");
            value.sampling.eta = floatProperty(properties, node->type, "eta");
            value.sampling.shiftedTimestep =
                intProperty(properties, node->type, "shiftedTimestep");
            value.sampling.flowShift = floatProperty(properties, node->type, "flowShift");
            value.sampling.customSigmas =
                parseFloatList(stringProperty(properties, node->type, "customSigmas"));
            value.sampling.skipLayerGuidanceLayers = parseIntList(
                stringProperty(properties, node->type, "skipLayerGuidanceLayers"));
            value.sampling.skipLayerGuidanceStart =
                floatProperty(properties, node->type, "skipLayerGuidanceStart");
            value.sampling.skipLayerGuidanceEnd =
                floatProperty(properties, node->type, "skipLayerGuidanceEnd");
            value.sampling.skipLayerGuidanceScale =
                floatProperty(properties, node->type, "skipLayerGuidanceScale");
            value.denoisingStrength =
                floatProperty(properties, node->type, "denoisingStrength");
            execution.values.insert(nodeId, value);
            break;
        case SdNodeType::InputImage: {
            const QString source =
                stringProperty(properties, node->type, "source").trimmed().toLower();
            // "layer" and "current_layer" both read one specific layer; "canvas"
            // and "composite" (and anything else) read the flattened document.
            // -1 means the document's active layer.
            const bool oneLayer = source == QLatin1String("layer")
                || source == QLatin1String("current_layer");
            const int layerIndex = oneLayer
                ? intProperty(properties, node->type, "layerIndex") : -1;
            QString error;
            if (!m_bridge->captureImage(
                    oneLayer ? SdImageSource::Layer : SdImageSource::Composite, layerIndex,
                    value.image, &error)) {
                addError(errors, SdWorkflowErrorCode::UnsupportedOperation, error, nodeId);
                return AdvanceResult::Failed;
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::InputMask: {
            QString error;
            if (!m_bridge->captureMask(value.image, &error)) {
                addError(errors, SdWorkflowErrorCode::UnsupportedOperation, error, nodeId);
                return AdvanceResult::Failed;
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::Generate: {
            SdGenerationRequest request;
            if (!buildRequest(*node, request, errors))
                return AdvanceResult::Failed;
            return startOperation(nodeId, request, errors);
        }
        case SdNodeType::Canny: {
            SdGenerationRequest request;
            if (!buildCannyRequest(*node, request, errors))
                return AdvanceResult::Failed;
            return startOperation(nodeId, request, errors);
        }
        case SdNodeType::Upscale: {
            SdGenerationRequest request;
            if (!buildUpscaleRequest(*node, request, errors))
                return AdvanceResult::Failed;
            return startOperation(nodeId, request, errors);
        }
        case SdNodeType::ADetailer: {
            SdGenerationRequest request;
            if (!buildADetailerRequest(*node, request, errors))
                return AdvanceResult::Failed;
            return startOperation(nodeId, request, errors);
        }
        case SdNodeType::ImageOutput: {
            const NodeValue* input = sourceValue(
                execution.definition, execution.values, nodeId, "image");
            if (!input || input->image.isNull()) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The image output has no evaluated image."), nodeId,
                         QStringLiteral("image"));
                return AdvanceResult::Failed;
            }
            QString layerName = stringProperty(properties, node->type, "layerName");
            if (layerName.isEmpty()) {
                layerName = propertyDefault(SdNodeType::ImageOutput,
                                            QStringLiteral("layerName")).toString();
            }
            QString error;
            if (!m_bridge->insertImage(input->image, layerName, &error)) {
                addError(errors, SdWorkflowErrorCode::UnsupportedOperation,
                         error.isEmpty() ? tr_("The layer could not be created.") : error,
                         nodeId);
                return AdvanceResult::Failed;
            }
            execution.result.inserted = true;
            execution.result.layerName = layerName;
            ++execution.insertedOutputs;
            qInfo().noquote() << "[SD] graph output inserted |" << nodeId
                              << "| layer=" << layerName
                              << "| size=" << input->image.size();
            break;
        }
        }

        ++execution.nextNode;
    }

    return AdvanceResult::Completed;
}

bool SdWorkflowExecutor::buildRequest(const SdWorkflowNode& generate,
                                      SdGenerationRequest& request,
                                      QVector<SdWorkflowError>& errors)
{
    if (!m_execution)
        return false;
    const SdWorkflowDefinition& definition = m_execution->definition;
    const QHash<QString, NodeValue>& values = m_execution->values;

    const NodeValue* model = sourceValue(definition, values, generate.id, "model");
    const NodeValue* positive = sourceValue(definition, values, generate.id, "positive");
    const NodeValue* sampling = sourceValue(definition, values, generate.id, "sampling");
    if (!model || !positive || !sampling) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("The generate node is missing an evaluated required input."), generate.id);
        return false;
    }

    request.load = model->load;
    request.loras = model->loras;
    request.prompt = positive->text;
    if (const NodeValue* negative = sourceValue(
            definition, values, generate.id, "negative")) {
        request.negativePrompt = negative->text;
    }
    request.width = sampling->width;
    request.height = sampling->height;
    request.sampling = sampling->sampling;
    request.denoisingStrength = sampling->denoisingStrength;
    request.batchCount = intProperty(generate.properties, generate.type, "batchCount");
    request.label = generate.id;

    if (const NodeValue* init = sourceValue(
            definition, values, generate.id, "init_image")) {
        request.initImage = init->image;
    }
    if (const NodeValue* mask = sourceValue(definition, values, generate.id, "mask"))
        request.maskImage = mask->image;

    for (const NodeValue* reference : sourceValues(definition, values, generate.id, "references"))
        request.referenceImages.push_back(reference->image);

    if (const NodeValue* control = sourceValue(definition, values, generate.id, "control")) {
        request.load.files.controlNetPath = control->controlNetPath;
        request.controlImage = control->image;
        request.controlStrength = control->controlStrength;
    }
    if (const NodeValue* ip = sourceValue(definition, values, generate.id, "ip_adapter")) {
        request.load.files.ipAdapterPath = ip->ipAdapterPath;
        if (!ip->visionEncoderPath.isEmpty())
            request.load.files.clipVisionPath = ip->visionEncoderPath;
        request.ipAdapterImage = ip->image;
        request.ipAdapterStrength = ip->ipAdapterStrength;
    }
    if (const NodeValue* pm = sourceValue(definition, values, generate.id, "photo_maker")) {
        request.load.files.photoMakerPath = pm->photoMakerPath;
        request.photoMakerEmbeddingPath = pm->identityEmbeddingPath;
        request.photoMakerImages = pm->images;
        request.photoMakerStyleStrength = pm->photoMakerStyleStrength;
    }
    if (const NodeValue* pulid = sourceValue(definition, values, generate.id, "pulid")) {
        request.load.files.pulidPath = pulid->pulidPath;
        request.pulidEmbeddingPath = pulid->identityEmbeddingPath;
        request.pulidWeight = pulid->pulidWeight;
    }

    if (!request.initImage.isNull() && !request.maskImage.isNull())
        request.operation = SdOperation::Inpaint;
    else if (!request.initImage.isNull())
        request.operation = SdOperation::Img2Img;
    else
        request.operation = SdOperation::Txt2Img;

    if (request.operation == SdOperation::Inpaint
        && request.maskImage.size() != request.initImage.size()) {
        request.maskImage = request.maskImage.scaled(request.initImage.size(),
                                                     Qt::IgnoreAspectRatio,
                                                     Qt::SmoothTransformation);
    }

    if (!request.initImage.isNull()) {
        request.width = request.initImage.width() - (request.initImage.width() % 8);
        request.height = request.initImage.height() - (request.initImage.height() % 8);
    }

    request.referenceImageArgs = stringProperty(generate.properties, generate.type, "referenceArgs");
    request.imagePreprocess = stringProperty(generate.properties, generate.type, "imagePreprocess");
    request.circularX = boolProperty(generate.properties, generate.type, "circularX");
    request.circularY = boolProperty(generate.properties, generate.type, "circularY");

    request.vaeTiling.enabled = boolProperty(generate.properties, generate.type, "vaeTiling");
    request.vaeTiling.tileWidth = intProperty(generate.properties, generate.type, "vaeTileWidth");
    request.vaeTiling.tileHeight = intProperty(generate.properties, generate.type, "vaeTileHeight");
    request.vaeTiling.overlap = floatProperty(generate.properties, generate.type, "vaeTileOverlap");

    request.cache.mode = stringProperty(generate.properties, generate.type, "cacheMode");
    request.cache.reuseThreshold = floatProperty(generate.properties, generate.type, "cacheThreshold");
    request.cache.startPercent = floatProperty(generate.properties, generate.type, "cacheStart");
    request.cache.endPercent = floatProperty(generate.properties, generate.type, "cacheEnd");

    request.hires.enabled = boolProperty(generate.properties, generate.type, "hiresEnabled");
    request.hires.upscaler = stringProperty(generate.properties, generate.type, "hiresUpscaler");
    request.hires.scale = floatProperty(generate.properties, generate.type, "hiresScale");
    request.hires.targetWidth = intProperty(generate.properties, generate.type, "hiresWidth");
    request.hires.targetHeight = intProperty(generate.properties, generate.type, "hiresHeight");
    request.hires.steps = intProperty(generate.properties, generate.type, "hiresSteps");
    request.hires.denoisingStrength =
        floatProperty(generate.properties, generate.type, "hiresDenoising");
    request.hires.tileSize = intProperty(generate.properties, generate.type, "hiresTileSize");
    const QString hiresModelId =
        generate.properties.value(QStringLiteral("hiresModelId")).toString().trimmed();
    if (!hiresModelId.isEmpty()) {
        SdResolvedModelFile resolved;
        QString error;
        if (!SdModelManager::instance().resolveFile(hiresModelId, SdModelCategory::Upscaler,
                                                    resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, generate.id,
                     QStringLiteral("hiresModelId"));
            return false;
        }
        request.hires.modelPath = resolved.path;
    }

    return true;
}

bool SdWorkflowExecutor::buildCannyRequest(const SdWorkflowNode& node,
                                           SdGenerationRequest& request,
                                           QVector<SdWorkflowError>& errors)
{
    if (!m_execution)
        return false;
    const NodeValue* image = sourceValue(
        m_execution->definition, m_execution->values, node.id, "image");
    if (!image) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("Canny has no evaluated image input."), node.id, QStringLiteral("image"));
        return false;
    }
    request.operation = SdOperation::Canny;
    request.initImage = image->image;
    request.cannyHigh = floatProperty(node.properties, node.type, "highThreshold");
    request.cannyLow = floatProperty(node.properties, node.type, "lowThreshold");
    request.cannyWeak = floatProperty(node.properties, node.type, "weak");
    request.cannyStrong = floatProperty(node.properties, node.type, "strong");
    request.cannyInverse = boolProperty(node.properties, node.type, "inverse");
    request.label = node.id;
    return true;
}

bool SdWorkflowExecutor::buildUpscaleRequest(const SdWorkflowNode& node,
                                             SdGenerationRequest& request,
                                             QVector<SdWorkflowError>& errors)
{
    if (!m_execution)
        return false;
    const NodeValue* image = sourceValue(
        m_execution->definition, m_execution->values, node.id, "image");
    if (!image) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("Upscale has no evaluated image input."), node.id, QStringLiteral("image"));
        return false;
    }
    SdResolvedModelFile resolved;
    QString error;
    if (!SdModelManager::instance().resolveFile(
            node.properties.value(QStringLiteral("modelId")).toString(),
            SdModelCategory::Upscaler, resolved, &error)) {
        addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
        return false;
    }
    request.operation = SdOperation::Upscale;
    request.initImage = image->image;
    request.auxiliaryModelPath = resolved.path;
    request.upscaleFactor = intProperty(node.properties, node.type, "factor");
    request.tileSize = intProperty(node.properties, node.type, "tileSize");
    devicePreferenceFromName(stringProperty(node.properties, node.type, "device"),
                             request.load.device);
    request.label = node.id;
    return true;
}

bool SdWorkflowExecutor::buildADetailerRequest(const SdWorkflowNode& node,
                                               SdGenerationRequest& request,
                                               QVector<SdWorkflowError>& errors)
{
    if (!m_execution)
        return false;
    const SdWorkflowDefinition& definition = m_execution->definition;
    const QHash<QString, NodeValue>& values = m_execution->values;

    const NodeValue* model = sourceValue(definition, values, node.id, "model");
    const NodeValue* image = sourceValue(definition, values, node.id, "image");
    const NodeValue* positive = sourceValue(definition, values, node.id, "positive");
    const NodeValue* sampling = sourceValue(definition, values, node.id, "sampling");
    if (!model || !image || !positive || !sampling) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("ADetailer is missing an evaluated required input."), node.id);
        return false;
    }

    SdResolvedModelFile resolved;
    QString error;
    if (!SdModelManager::instance().resolveFile(
            node.properties.value(QStringLiteral("modelId")).toString(),
            SdModelCategory::ADetailer, resolved, &error)) {
        addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
        return false;
    }

    request.operation = SdOperation::ADetailer;
    request.load = model->load;
    request.loras = model->loras;
    request.auxiliaryModelPath = resolved.path;
    request.prompt = positive->text;
    if (const NodeValue* negative = sourceValue(definition, values, node.id, "negative"))
        request.negativePrompt = negative->text;
    request.width = sampling->width;
    request.height = sampling->height;
    request.sampling = sampling->sampling;
    request.denoisingStrength = sampling->denoisingStrength;
    request.initImage = image->image;
    request.label = node.id;
    return true;
}

SdWorkflowExecutor::AdvanceResult SdWorkflowExecutor::startOperation(
    const QString& nodeId, const SdGenerationRequest& request, QVector<SdWorkflowError>& errors)
{
    QString refusal;
    if (!m_job.start(request, &refusal)) {
        addError(errors, SdWorkflowErrorCode::UnsupportedOperation, refusal, nodeId);
        return AdvanceResult::Failed;
    }
    m_execution->activeGenerateNodeId = nodeId;
    ++m_execution->nextNode;
    qInfo().noquote() << "[SD] graph operation started |" << nodeId
                      << "|" << operationName(request.operation)
                      << "| model=" << request.load.files.modelId
                      << "| loras=" << request.loras.size();
    return AdvanceResult::Started;
}

void SdWorkflowExecutor::onJobFinished(const SdGenerationResult& result)
{
    if (!m_execution)
        return;

    SdWorkflowExecutionState& execution = *m_execution;
    const QString generateNodeId = execution.activeGenerateNodeId;
    execution.result.generation = result;

    if (!result.ok()) {
        if (result.state != SdGenerationState::Cancelled) {
            addError(execution.result.errors, SdWorkflowErrorCode::UnsupportedOperation,
                     result.error.isEmpty() ? tr_("Generation failed.") : result.error,
                     generateNodeId);
        }
        finishExecution();
        return;
    }

    NodeValue value;
    value.image = result.image;
    execution.values.insert(generateNodeId, value);
    execution.activeGenerateNodeId.clear();

    QVector<SdWorkflowError> errors;
    const AdvanceResult advanced = advance(errors);
    if (advanced == AdvanceResult::Started)
        return;
    if (advanced == AdvanceResult::Failed)
        execution.result.errors += errors;
    finishExecution();
}

void SdWorkflowExecutor::finishExecution()
{
    if (!m_execution)
        return;

    SdWorkflowRunResult result = std::move(m_execution->result);
    const int insertedOutputs = m_execution->insertedOutputs;
    m_execution.reset();
    if (m_bridge)
        m_bridge->endRun();

    if (result.ok()) {
        qInfo().noquote() << "[SD] workflow finished | outputs=" << insertedOutputs
                          << "| device=" << result.generation.deviceName
                          << "| seed=" << result.generation.seed;
    }
    emit finished(result);
}

} // namespace sdlocal
