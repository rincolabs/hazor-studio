#include "ai/sd/SdWorkflowExecutor.hpp"

#include "ai/sd/SdCanvasBridge.hpp"
#include "ai/sd/SdModelManager.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QHash>
#include <QJsonArray>
#include <QSet>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

const char* kDefaultLayerName = "AI Generated";

void addError(QVector<SdWorkflowError>& errors, SdWorkflowErrorCode code,
              const QString& message, const QString& nodeId = {}, const QString& portId = {})
{
    SdWorkflowError e;
    e.code = code;
    e.message = message;
    e.nodeId = nodeId;
    e.portId = portId;
    errors.push_back(e);
}

// Everything one node contributes to the request.
//
// One struct with per-type fields rather than a value hierarchy: there are nine
// node types and each produces exactly one kind of thing, so a variant and its
// visitors would be more machinery than the problem has. Which field is
// meaningful follows from the node's output port type.
struct NodeValue {
    SdModelLoadOptions load;          // Checkpoint
    QString text;                     // Prompt, NegativePrompt
    SdSamplingParams sampling;        // Sampling
    int width = 512;                  // Sampling
    int height = 512;                 // Sampling
    float denoisingStrength = 0.75f;  // Sampling
    SdResolvedLora lora;              // Lora
    QImage image;                     // InputImage, InputMask
};

// Nodes feeding `targetId`, transitively. Anything outside this set contributes
// nothing to the run and must not be evaluated: an unconnected input_image node
// would otherwise flatten the whole document for no reason.
QSet<QString> requiredNodes(const SdWorkflowDefinition& definition, const QString& targetId)
{
    QSet<QString> required { targetId };
    bool grew = true;
    while (grew) {
        grew = false;
        for (const SdWorkflowConnection& c : definition.connections) {
            if (required.contains(c.toNode) && !required.contains(c.fromNode)) {
                required.insert(c.fromNode);
                grew = true;
            }
        }
    }
    return required;
}

// The nodes wired into one input port, in document order. More than one only
// for the Generate node's LoRA input.
QStringList sourcesFor(const SdWorkflowDefinition& definition, const QString& nodeId,
                       const char* portId)
{
    QStringList out;
    for (const SdWorkflowConnection& c : definition.connections) {
        if (c.toNode == nodeId && c.toPort == QLatin1String(portId))
            out << c.fromNode;
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
    // -1 asks the library for a random seed, and the one it used comes back in
    // the result, so an unseeded run is still reproducible afterwards.
    return -1;
}

} // namespace

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

void SdWorkflowExecutor::shutdown()
{
    m_job.shutdown();
}

void SdWorkflowExecutor::cancel()
{
    m_job.cancel();
}

bool SdWorkflowExecutor::run(const QByteArray& workflowJson, QVector<SdWorkflowError>* errors)
{
    QVector<SdWorkflowError> local;
    QVector<SdWorkflowError>& out = errors ? *errors : local;

    if (m_job.busy()) {
        addError(out, SdWorkflowErrorCode::UnsupportedOperation,
                 tr_("A generation is already running."));
        return false;
    }

    SdWorkflowDefinition parsed;
    if (!SdWorkflowDefinition::fromJson(workflowJson, parsed, &out))
        return false;

    // Validation first, and in full: no model file is opened and no context is
    // created until the graph is known to be sound.
    SdWorkflowDefinition definition;
    SdWorkflowPlan plan;
    if (!SdWorkflowValidator::validate(parsed, SdModelManager::instance(),
                                       definition, plan, out)) {
        qWarning().noquote() << "[SD] workflow rejected |" << describeWorkflowErrors(out);
        return false;
    }

    // The target document is latched before any pixels are read, so the capture
    // and the later insertion are about the same document by construction.
    QString bridgeError;
    if (!m_bridge || !m_bridge->beginRun(&bridgeError)) {
        addError(out, SdWorkflowErrorCode::UnsupportedOperation,
                 bridgeError.isEmpty() ? tr_("No canvas is available.") : bridgeError,
                 plan.outputNodeId);
        return false;
    }

    SdGenerationRequest request;
    request.operation = plan.operation;
    if (!buildRequest(definition, plan, request, out)) {
        m_bridge->endRun();
        return false;
    }

    const SdWorkflowNode* output = definition.node(plan.outputNodeId);
    m_layerName = output
        ? output->properties.value(QStringLiteral("layerName")).toString()
        : QString();
    if (m_layerName.isEmpty())
        m_layerName = QString::fromUtf8(kDefaultLayerName);

    QString refusal;
    if (!m_job.start(request, &refusal)) {
        addError(out, SdWorkflowErrorCode::UnsupportedOperation, refusal, plan.generateNodeId);
        m_bridge->endRun();
        return false;
    }

    qInfo().noquote() << "[SD] workflow started |" << operationName(request.operation)
                      << "| nodes=" << definition.nodes.size()
                      << "| order=" << plan.order.join(QLatin1String(" -> "))
                      << "| model=" << request.load.files.modelId;
    return true;
}

bool SdWorkflowExecutor::buildRequest(const SdWorkflowDefinition& definition,
                                      const SdWorkflowPlan& plan,
                                      SdGenerationRequest& request,
                                      QVector<SdWorkflowError>& errors)
{
    const SdModelManager& models = SdModelManager::instance();
    const QSet<QString> required = requiredNodes(definition, plan.generateNodeId);

    // Evaluated once each, in dependency order, and cached: a prompt node wired
    // into two inputs is read once, and a configuration node is never mistaken
    // for a processing step.
    QHash<QString, NodeValue> values;

    for (const QString& nodeId : plan.order) {
        if (!required.contains(nodeId))
            continue;
        const SdWorkflowNode* node = definition.node(nodeId);
        if (!node)
            continue;
        const QJsonObject& p = node->properties;
        NodeValue value;

        switch (node->type) {
        case SdNodeType::Checkpoint: {
            SdModelSelection selection;
            selection.modelId = p.value(QStringLiteral("modelId")).toString();
            selection.vaeId = p.value(QStringLiteral("vaeId")).toString();
            for (const QJsonValue& v : p.value(QStringLiteral("textEncoderIds")).toArray())
                selection.textEncoderIds << v.toString();

            QString error;
            if (!models.resolve(selection, value.load.files, &error)) {
                // Already checked during validation; reaching here means a file
                // moved in between, which is worth reporting rather than
                // crashing on.
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return false;
            }
            // Validation rejected anything these two could not parse, so the
            // result is trusted here.
            weightTypeFromName(p.value(QStringLiteral("weightType")).toString(),
                               value.load.weightType);
            devicePreferenceFromName(p.value(QStringLiteral("device")).toString(),
                                     value.load.device);
            value.load.threads = p.value(QStringLiteral("threads")).toInt(0);
            value.load.allowCpuFallback =
                p.value(QStringLiteral("allowCpuFallback")).toBool(true);
            break;
        }
        case SdNodeType::Prompt:
        case SdNodeType::NegativePrompt:
            value.text = p.value(QStringLiteral("text")).toString();
            break;
        case SdNodeType::Sampling:
            value.width = p.value(QStringLiteral("width")).toInt(512);
            value.height = p.value(QStringLiteral("height")).toInt(512);
            value.sampling.steps = p.value(QStringLiteral("steps")).toInt(20);
            value.sampling.seed = readSeed(p);
            value.sampling.cfgScale =
                static_cast<float>(p.value(QStringLiteral("cfgScale")).toDouble(7.0));
            value.sampling.sampler = p.value(QStringLiteral("sampler")).toString();
            value.sampling.scheduler = p.value(QStringLiteral("scheduler")).toString();
            value.sampling.clipSkip = p.value(QStringLiteral("clipSkip")).toInt(-1);
            value.denoisingStrength = static_cast<float>(
                p.value(QStringLiteral("denoisingStrength")).toDouble(0.75));
            break;
        case SdNodeType::Lora: {
            const QString modelId = p.value(QStringLiteral("modelId")).toString();
            const float weight =
                static_cast<float>(p.value(QStringLiteral("weight")).toDouble(1.0));
            QString error;
            if (!models.resolveLora(modelId, weight, value.lora, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, nodeId);
                return false;
            }
            break;
        }
        case SdNodeType::InputImage: {
            const QString source =
                p.value(QStringLiteral("source")).toString().trimmed().toLower();
            QString error;
            if (!m_bridge->captureImage(source == QLatin1String("current_layer")
                                            ? SdImageSource::CurrentLayer
                                            : SdImageSource::Composite,
                                        value.image, &error)) {
                addError(errors, SdWorkflowErrorCode::UnsupportedOperation, error, nodeId);
                return false;
            }
            break;
        }
        case SdNodeType::InputMask: {
            QString error;
            if (!m_bridge->captureMask(value.image, &error)) {
                addError(errors, SdWorkflowErrorCode::UnsupportedOperation, error, nodeId);
                return false;
            }
            break;
        }
        case SdNodeType::Generate:
        case SdNodeType::ImageOutput:
            // Not configuration: the Generate node is assembled from the cache
            // below, and the output node only names a layer.
            break;
        }

        values.insert(nodeId, value);
    }

    // ── Assemble the one request ────────────────────────────────────────────
    const SdWorkflowNode* generate = definition.node(plan.generateNodeId);
    if (!generate) {
        addError(errors, SdWorkflowErrorCode::NoGenerateNode,
                 tr_("The generate node disappeared during validation."));
        return false;
    }

    const auto single = [&](const char* portId) -> const NodeValue* {
        const QStringList sources = sourcesFor(definition, plan.generateNodeId, portId);
        if (sources.isEmpty())
            return nullptr;
        const auto it = values.constFind(sources.first());
        return it == values.constEnd() ? nullptr : &it.value();
    };

    const NodeValue* model = single("model");
    const NodeValue* positive = single("positive");
    const NodeValue* sampling = single("sampling");
    // Validation guarantees these three are connected; a null here would be a
    // bug in this file rather than in the document.
    if (!model || !positive || !sampling) {
        addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                 tr_("The generate node is missing a required input."), plan.generateNodeId);
        return false;
    }

    request.load = model->load;
    request.prompt = positive->text;
    if (const NodeValue* negative = single("negative"))
        request.negativePrompt = negative->text;
    request.width = sampling->width;
    request.height = sampling->height;
    request.sampling = sampling->sampling;
    request.denoisingStrength = sampling->denoisingStrength;
    request.batchCount = generate->properties.value(QStringLiteral("batchCount")).toInt(1);
    request.label = plan.generateNodeId;

    for (const QString& loraNode : sourcesFor(definition, plan.generateNodeId, "lora")) {
        const auto it = values.constFind(loraNode);
        if (it != values.constEnd() && !it.value().lora.path.isEmpty())
            request.loras.push_back(it.value().lora);
    }

    if (const NodeValue* init = single("init_image"))
        request.initImage = init->image;
    if (const NodeValue* mask = single("mask"))
        request.maskImage = mask->image;

    // An inpaint mask has to line up with the image it masks; Hazor's selection
    // is document-sized, so a composite and its selection already match, but a
    // current-layer capture of a different size would not.
    if (request.operation == SdOperation::Inpaint
        && !request.initImage.isNull() && !request.maskImage.isNull()
        && request.maskImage.size() != request.initImage.size()) {
        request.maskImage = request.maskImage.scaled(request.initImage.size(),
                                                     Qt::IgnoreAspectRatio,
                                                     Qt::SmoothTransformation);
    }

    // img2img and inpaint generate at the input's resolution: a latent built
    // from a 1024x768 init cannot be sampled at the sampling node's 512x512.
    if (!request.initImage.isNull()) {
        request.width = request.initImage.width() - (request.initImage.width() % 8);
        request.height = request.initImage.height() - (request.initImage.height() % 8);
    }

    return true;
}

void SdWorkflowExecutor::onJobFinished(const SdGenerationResult& result)
{
    SdWorkflowRunResult out;
    out.generation = result;
    out.layerName = m_layerName;

    if (result.ok()) {
        QString error;
        // Runs on the thread that owns this object — SdGenerationJob marshals
        // its finished() there — which is what makes touching the document
        // model here legal.
        out.inserted = m_bridge && m_bridge->insertImage(result.image, m_layerName, &error);
        if (!out.inserted) {
            addError(out.errors, SdWorkflowErrorCode::UnsupportedOperation,
                     error.isEmpty() ? tr_("The layer could not be created.") : error);
        } else {
            qInfo().noquote() << "[SD] workflow finished | inserted" << result.size
                              << "as" << m_layerName
                              << "| device=" << result.deviceName
                              << "| seed=" << result.seed;
        }
    }

    if (m_bridge)
        m_bridge->endRun();
    emit finished(out);
}

} // namespace sdlocal
