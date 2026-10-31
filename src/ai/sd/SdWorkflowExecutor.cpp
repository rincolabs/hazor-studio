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

} // namespace

// A typed value produced by one graph node, mirroring what travels along the
// wire it came out of. Model and CLIP values are immutable snapshots: a LoRA
// copies its input and appends itself, so branching before or after that node
// produces independent configurations without special cases.
//
// One flat struct rather than a tagged union: every node type fills in the
// handful of fields its own output means and leaves the rest at their default,
// which is never read because the port typing already keeps a Control value
// from reaching a slot that expects an IpAdapter one.
struct NodeValue {
    // ── engine ──────────────────────────────────────────────────────────────
    SdModelLoadOptions load;
    SdCacheParams cache;

    // ── model ───────────────────────────────────────────────────────────────
    // Ids rather than paths: which text encoders and which VAE belong to the
    // context is only known once the whole chain has been walked, so the
    // registry is asked once, at the decode.
    SdModelSelection selection;
    QVector<SdResolvedLora> loras;
    float shift = 0.0f;
    int shiftedTimestep = 0;
    QVector<int> slgLayers;
    float slgStart = 0.01f;
    float slgEnd = 0.2f;
    float slgScale = 0.0f;

    // ── clip ────────────────────────────────────────────────────────────────
    QStringList textEncoderIds;
    int clipSkip = -1;

    // ── vae ─────────────────────────────────────────────────────────────────
    QString vaeId;

    // ── conditioning ────────────────────────────────────────────────────────
    QString text;
    float distilledGuidance = 0.0f;

    // ── latent ──────────────────────────────────────────────────────────────
    int width = 512;
    int height = 512;
    bool circularX = false;
    bool circularY = false;
    // A latent out of KSampler is a described-but-unrun pipeline; one out of
    // Empty Latent Image or VAE Encode only carries what to sample from.
    bool sampled = false;
    SdGenerationRequest request;

    // ── images ──────────────────────────────────────────────────────────────
    QImage image;                     // InputImage, VAE Encode, Canny, Upscale, decode
    QImage mask;                      // InputMask, VAE Encode (Inpaint)
    QVector<QImage> images;           // PhotoMaker identity images, ReferenceImages
    QString referenceArgs;

    SdHiresParams hires;

    // ── auxiliary loaders ───────────────────────────────────────────────────
    QString controlNetPath;
    float controlStrength = 0.9f;
    QString ipAdapterPath;
    QString visionEncoderPath;
    float ipAdapterStrength = 1.0f;
    QString photoMakerPath;
    QString identityEmbeddingPath;
    float photoMakerStyleStrength = 20.0f;
    QString pulidPath;
    float pulidWeight = 0.8f;
};

namespace {

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

// The model chain carries the patches, the conditioning carries what the text
// encoder was set to: the library takes both as fields of the one call.
void applyChainSettings(const NodeValue& model, const NodeValue& positive,
                        SdSamplingParams& out)
{
    out.flowShift = model.shift;
    out.shiftedTimestep = model.shiftedTimestep;
    out.skipLayerGuidanceLayers = model.slgLayers;
    out.skipLayerGuidanceStart = model.slgStart;
    out.skipLayerGuidanceEnd = model.slgEnd;
    out.skipLayerGuidanceScale = model.slgScale;
    out.clipSkip = positive.clipSkip;
    out.distilledGuidance = positive.distilledGuidance;
}

qint64 readSeed(const QJsonObject& props)
{
    const QJsonValue value = props.value(QStringLiteral("seed"));
    if (value.isString())
        return value.toString().toLongLong();
    if (value.isDouble())
        return static_cast<qint64>(value.toDouble());
    return propertyDefault(SdNodeType::KSampler, QStringLiteral("seed"))
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
        case SdNodeType::Checkpoint:
        case SdNodeType::DiffusionModelLoader: {
            value.selection.modelId = properties.value(QStringLiteral("modelId")).toString();
            for (const QJsonValue& entry
                 : properties.value(QStringLiteral("embeddingIds")).toArray()) {
                value.selection.embeddingIds << entry.toString();
            }
            // Unconnected means the backend's own defaults.
            if (const NodeValue* engine = sourceValue(
                    execution.definition, execution.values, nodeId, "engine")) {
                value.load = engine->load;
                value.cache = engine->cache;
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::TextEncoderLoader:
            for (const QJsonValue& entry
                 : properties.value(QStringLiteral("modelIds")).toArray()) {
                value.textEncoderIds << entry.toString();
            }
            execution.values.insert(nodeId, value);
            break;
        case SdNodeType::VaeLoader:
            value.vaeId = properties.value(QStringLiteral("modelId")).toString();
            execution.values.insert(nodeId, value);
            break;
        case SdNodeType::EngineOptions: {
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
            value.cache.mode = stringProperty(properties, node->type, "cacheMode");
            value.cache.reuseThreshold = floatProperty(properties, node->type, "cacheThreshold");
            value.cache.startPercent = floatProperty(properties, node->type, "cacheStart");
            value.cache.endPercent = floatProperty(properties, node->type, "cacheEnd");
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
            // One value carries both outputs, so a CLIP wired through here
            // keeps whatever was done to it upstream.
            if (const NodeValue* clip = sourceValue(
                    execution.definition, execution.values, nodeId, "clip")) {
                value.textEncoderIds = clip->textEncoderIds;
                value.clipSkip = clip->clipSkip;
            }
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
        case SdNodeType::ModelSampling:
        case SdNodeType::SkipLayerGuidance: {
            const NodeValue* input = sourceValue(
                execution.definition, execution.values, nodeId, "model");
            if (!input) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The node has no evaluated model input."), nodeId,
                         QStringLiteral("model"));
                return AdvanceResult::Failed;
            }
            value = *input;
            if (node->type == SdNodeType::ModelSampling) {
                value.shift = floatProperty(properties, node->type, "shift");
                value.shiftedTimestep =
                    intProperty(properties, node->type, "shiftedTimestep");
            } else {
                value.slgLayers =
                    parseIntList(stringProperty(properties, node->type, "layers"));
                value.slgScale = floatProperty(properties, node->type, "scale");
                value.slgStart = floatProperty(properties, node->type, "start");
                value.slgEnd = floatProperty(properties, node->type, "end");
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::ClipSetLastLayer: {
            const NodeValue* input = sourceValue(
                execution.definition, execution.values, nodeId, "clip");
            if (!input) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The node has no evaluated CLIP input."), nodeId,
                         QStringLiteral("clip"));
                return AdvanceResult::Failed;
            }
            value = *input;
            value.clipSkip = intProperty(properties, node->type, "stopAtClipLayer");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::ClipTextEncode: {
            const NodeValue* clip = sourceValue(
                execution.definition, execution.values, nodeId, "clip");
            if (!clip) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The text encode node has no evaluated CLIP input."), nodeId,
                         QStringLiteral("clip"));
                return AdvanceResult::Failed;
            }
            value.textEncoderIds = clip->textEncoderIds;
            value.clipSkip = clip->clipSkip;
            value.text = properties.value(QStringLiteral("text")).toString();
            value.distilledGuidance =
                floatProperty(properties, node->type, "distilledGuidance");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::ReferenceImages: {
            const QVector<const NodeValue*> images = sourceValues(
                execution.definition, execution.values, nodeId, "images");
            if (images.isEmpty()) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("Reference images needs at least one image."), nodeId,
                         QStringLiteral("images"));
                return AdvanceResult::Failed;
            }
            for (const NodeValue* source : images)
                value.images.push_back(source->image);
            value.referenceArgs = stringProperty(properties, node->type, "args");
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::EmptyLatentImage:
            value.width = intProperty(properties, node->type, "width");
            value.height = intProperty(properties, node->type, "height");
            value.circularX = boolProperty(properties, node->type, "circularX");
            value.circularY = boolProperty(properties, node->type, "circularY");
            execution.values.insert(nodeId, value);
            break;
        case SdNodeType::VaeEncode:
        case SdNodeType::VaeEncodeInpaint: {
            const NodeValue* pixels = sourceValue(
                execution.definition, execution.values, nodeId, "pixels");
            const NodeValue* vae = sourceValue(
                execution.definition, execution.values, nodeId, "vae");
            if (!pixels || !vae || pixels->image.isNull()) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The VAE encode node has no evaluated image input."), nodeId,
                         QStringLiteral("pixels"));
                return AdvanceResult::Failed;
            }
            value.image = pixels->image;
            value.vaeId = vae->vaeId;
            // The encoded image is the canvas: the library samples at its size.
            value.width = value.image.width() - (value.image.width() % 8);
            value.height = value.image.height() - (value.image.height() % 8);
            if (node->type == SdNodeType::VaeEncodeInpaint) {
                const NodeValue* mask = sourceValue(
                    execution.definition, execution.values, nodeId, "mask");
                if (!mask || mask->mask.isNull()) {
                    addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                             tr_("The inpaint encode node has no evaluated mask."), nodeId,
                             QStringLiteral("mask"));
                    return AdvanceResult::Failed;
                }
                value.mask = mask->mask.size() == value.image.size()
                    ? mask->mask
                    : mask->mask.scaled(value.image.size(), Qt::IgnoreAspectRatio,
                                        Qt::SmoothTransformation);
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::HiresFix: {
            value.hires.enabled = true;
            value.hires.upscaler = stringProperty(properties, node->type, "upscaler");
            value.hires.scale = floatProperty(properties, node->type, "scale");
            value.hires.targetWidth = intProperty(properties, node->type, "targetWidth");
            value.hires.targetHeight = intProperty(properties, node->type, "targetHeight");
            value.hires.steps = intProperty(properties, node->type, "steps");
            value.hires.denoisingStrength =
                floatProperty(properties, node->type, "denoisingStrength");
            value.hires.tileSize = intProperty(properties, node->type, "tileSize");
            const QString modelId =
                properties.value(QStringLiteral("modelId")).toString().trimmed();
            if (!modelId.isEmpty()) {
                SdResolvedModelFile resolved;
                QString error;
                if (!models.resolveFile(modelId, SdModelCategory::Upscaler,
                                        resolved, &error)) {
                    addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId,
                             QStringLiteral("modelId"));
                    return AdvanceResult::Failed;
                }
                value.hires.modelPath = resolved.path;
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::KSampler: {
            if (!buildSampledLatent(*node, value, errors))
                return AdvanceResult::Failed;
            execution.values.insert(nodeId, value);
            break;
        }
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
            if (!m_bridge->captureMask(value.mask, &error)) {
                addError(errors, SdWorkflowErrorCode::UnsupportedOperation, error, nodeId);
                return AdvanceResult::Failed;
            }
            execution.values.insert(nodeId, value);
            break;
        }
        case SdNodeType::VaeDecode: {
            SdGenerationRequest request;
            if (!buildDecodeRequest(*node, request, errors))
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

bool SdWorkflowExecutor::buildSampledLatent(const SdWorkflowNode& sampler, NodeValue& out,
                                            QVector<SdWorkflowError>& errors)
{
    if (!m_execution)
        return false;
    const SdWorkflowDefinition& definition = m_execution->definition;
    const QHash<QString, NodeValue>& values = m_execution->values;

    const NodeValue* model = sourceValue(definition, values, sampler.id, "model");
    const NodeValue* positive = sourceValue(definition, values, sampler.id, "positive");
    const NodeValue* latent = sourceValue(definition, values, sampler.id, "latent");
    if (!model || !positive || !latent) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("The sampler is missing an evaluated required input."), sampler.id);
        return false;
    }

    // The sampled latent keeps the canvas it was given and gains the pipeline
    // that will produce it; the decode is what finally runs this.
    out = *latent;
    out.sampled = true;
    out.selection = model->selection;
    out.selection.textEncoderIds = positive->textEncoderIds;

    const QJsonObject& properties = sampler.properties;
    SdGenerationRequest& request = out.request;
    request.load = model->load;
    request.cache = model->cache;
    request.loras = model->loras;
    request.prompt = positive->text;
    if (const NodeValue* negative = sourceValue(definition, values, sampler.id, "negative"))
        request.negativePrompt = negative->text;

    request.width = latent->width;
    request.height = latent->height;
    request.circularX = latent->circularX;
    request.circularY = latent->circularY;
    request.initImage = latent->image;
    request.maskImage = latent->mask;

    request.sampling.steps = intProperty(properties, sampler.type, "steps");
    request.sampling.seed = readSeed(properties);
    request.sampling.cfgScale = floatProperty(properties, sampler.type, "cfgScale");
    request.sampling.sampler = stringProperty(properties, sampler.type, "sampler");
    request.sampling.scheduler = stringProperty(properties, sampler.type, "scheduler");
    request.sampling.imageCfgScale = floatProperty(properties, sampler.type, "imageCfgScale");
    request.sampling.eta = floatProperty(properties, sampler.type, "eta");
    request.sampling.customSigmas =
        parseFloatList(stringProperty(properties, sampler.type, "customSigmas"));
    applyChainSettings(*model, *positive, request.sampling);
    request.denoisingStrength = floatProperty(properties, sampler.type, "denoisingStrength");
    request.imagePreprocess = stringProperty(properties, sampler.type, "imagePreprocess");
    request.label = sampler.id;

    if (const NodeValue* references = sourceValue(definition, values, sampler.id, "references")) {
        request.referenceImages = references->images;
        request.referenceImageArgs = references->referenceArgs;
    }
    if (const NodeValue* hires = sourceValue(definition, values, sampler.id, "hires"))
        request.hires = hires->hires;
    if (const NodeValue* control = sourceValue(definition, values, sampler.id, "control")) {
        request.load.files.controlNetPath = control->controlNetPath;
        request.controlImage = control->image;
        request.controlStrength = control->controlStrength;
    }
    if (const NodeValue* ip = sourceValue(definition, values, sampler.id, "ip_adapter")) {
        request.load.files.ipAdapterPath = ip->ipAdapterPath;
        if (!ip->visionEncoderPath.isEmpty())
            request.load.files.clipVisionPath = ip->visionEncoderPath;
        request.ipAdapterImage = ip->image;
        request.ipAdapterStrength = ip->ipAdapterStrength;
    }
    if (const NodeValue* pm = sourceValue(definition, values, sampler.id, "photo_maker")) {
        request.load.files.photoMakerPath = pm->photoMakerPath;
        request.photoMakerEmbeddingPath = pm->identityEmbeddingPath;
        request.photoMakerImages = pm->images;
        request.photoMakerStyleStrength = pm->photoMakerStyleStrength;
    }
    if (const NodeValue* pulid = sourceValue(definition, values, sampler.id, "pulid")) {
        request.load.files.pulidPath = pulid->pulidPath;
        request.pulidEmbeddingPath = pulid->identityEmbeddingPath;
        request.pulidWeight = pulid->pulidWeight;
    }

    // What the latent carries is what selects the operation: an empty one is
    // txt2img, an encoded image is img2img, and one with a mask is inpainting.
    if (!request.initImage.isNull() && !request.maskImage.isNull())
        request.operation = SdOperation::Inpaint;
    else if (!request.initImage.isNull())
        request.operation = SdOperation::Img2Img;
    else
        request.operation = SdOperation::Txt2Img;

    return true;
}

bool SdWorkflowExecutor::buildDecodeRequest(const SdWorkflowNode& decode,
                                            SdGenerationRequest& request,
                                            QVector<SdWorkflowError>& errors)
{
    if (!m_execution)
        return false;
    const SdWorkflowDefinition& definition = m_execution->definition;
    const QHash<QString, NodeValue>& values = m_execution->values;

    const NodeValue* latent = sourceValue(definition, values, decode.id, "samples");
    const NodeValue* vae = sourceValue(definition, values, decode.id, "vae");
    if (!latent || !vae) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("The decode node is missing an evaluated required input."), decode.id);
        return false;
    }
    if (!latent->sampled) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("This latent was never sampled: connect it through a KSampler."),
                 decode.id, QStringLiteral("samples"));
        return false;
    }

    request = latent->request;

    // The whole context is resolved once, here, because only now is every
    // component of it known: the weights, the text encoders and the VAE.
    // One context encodes and decodes, so a VAE Encode upstream has to have
    // been handed the same VAE as this node: two would need two contexts.
    if (!latent->vaeId.isEmpty() && !vae->vaeId.isEmpty() && latent->vaeId != vae->vaeId) {
        addError(errors, SdWorkflowErrorCode::UnsupportedOperation,
                 tr_("The encode and the decode were given different VAEs; one "
                     "context cannot use both."), decode.id, QStringLiteral("vae"));
        return false;
    }
    SdModelSelection selection = latent->selection;
    selection.vaeId = vae->vaeId.isEmpty() ? latent->vaeId : vae->vaeId;
    QString error;
    if (!SdModelManager::instance().resolve(selection, request.load.files, &error)) {
        addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, decode.id);
        return false;
    }
    // resolve() fills the context inputs from scratch, so the auxiliary paths
    // the sampler's branches contributed are put back.
    const SdModelFiles& sampled = latent->request.load.files;
    request.load.files.controlNetPath = sampled.controlNetPath;
    request.load.files.ipAdapterPath = sampled.ipAdapterPath;
    request.load.files.photoMakerPath = sampled.photoMakerPath;
    request.load.files.pulidPath = sampled.pulidPath;
    if (!sampled.clipVisionPath.isEmpty())
        request.load.files.clipVisionPath = sampled.clipVisionPath;

    request.vaeTiling.enabled = boolProperty(decode.properties, decode.type, "tiling");
    request.vaeTiling.tileWidth = intProperty(decode.properties, decode.type, "tileWidth");
    request.vaeTiling.tileHeight = intProperty(decode.properties, decode.type, "tileHeight");
    request.vaeTiling.overlap = floatProperty(decode.properties, decode.type, "tileOverlap");
    request.label = decode.id;
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
    if (!model || !image || !positive) {
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
    request.cache = model->cache;
    request.loras = model->loras;
    // Its own context: this node re-samples regions of an image rather than
    // continuing a latent, so it resolves the weights the way a decode does.
    SdModelSelection selection = model->selection;
    selection.textEncoderIds = positive->textEncoderIds;
    if (const NodeValue* vae = sourceValue(definition, values, node.id, "vae"))
        selection.vaeId = vae->vaeId;
    if (!SdModelManager::instance().resolve(selection, request.load.files, &error)) {
        addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
        return false;
    }
    request.auxiliaryModelPath = resolved.path;
    request.prompt = positive->text;
    if (const NodeValue* negative = sourceValue(definition, values, node.id, "negative"))
        request.negativePrompt = negative->text;
    request.width = image->image.width() - (image->image.width() % 8);
    request.height = image->image.height() - (image->image.height() % 8);
    request.sampling.steps = intProperty(node.properties, node.type, "steps");
    request.sampling.seed = readSeed(node.properties);
    request.sampling.cfgScale = floatProperty(node.properties, node.type, "cfgScale");
    request.sampling.sampler = stringProperty(node.properties, node.type, "sampler");
    request.sampling.scheduler = stringProperty(node.properties, node.type, "scheduler");
    applyChainSettings(*model, *positive, request.sampling);
    request.denoisingStrength =
        floatProperty(node.properties, node.type, "denoisingStrength");
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
