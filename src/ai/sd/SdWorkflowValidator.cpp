#include "ai/sd/SdWorkflowValidator.hpp"

#include "ai/sd/SdGenerationService.hpp"
#include "ai/sd/SdModelManager.hpp"
#include "ai/sd/SdNodeCatalog.hpp"

#include <QCoreApplication>
#include <QHash>
#include <QJsonArray>
#include <QJsonValue>
#include <QSet>

#include <algorithm>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

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

// ── Property checking ───────────────────────────────────────────────────────
//
// Kinds, ranges and defaults come from SdNodeCatalog rather than from literals
// here: the executor takes its fallbacks from the same table and the Neural
// Flow frontend builds its widgets out of it, so a bound restated in this file
// is a bound that will eventually disagree with the spinner offering it.
//
// What stays here is everything a spec cannot express — the library's own
// sampler and scheduler parsers, and whether a model id resolves to files that
// exist on this machine.
//
// The executor reads these same keys without re-checking them, so everything it
// relies on has to be rejected here: that invariant is what keeps the executor
// free of error paths for values it cannot do anything about.

bool checkString(const QJsonObject& props, const QString& key, const QString& nodeId,
                 QVector<SdWorkflowError>& errors)
{
    if (!props.contains(key))
        return true;
    if (props.value(key).isString())
        return true;
    addError(errors, SdWorkflowErrorCode::InvalidProperty,
             tr_("\"%1\" must be a string.").arg(key), nodeId);
    return false;
}

bool checkBool(const QJsonObject& props, const QString& key, const QString& nodeId,
               QVector<SdWorkflowError>& errors)
{
    if (!props.contains(key))
        return true;
    if (props.value(key).isBool())
        return true;
    addError(errors, SdWorkflowErrorCode::InvalidProperty,
             tr_("\"%1\" must be true or false.").arg(key), nodeId);
    return false;
}

// Int and Number share this: the only difference is whether the bound is
// reported as an integer and whether the multiple-of constraint applies.
bool checkNumber(const QJsonObject& props, const SdPropertySpec& spec, const QString& nodeId,
                 QVector<SdWorkflowError>& errors)
{
    if (!props.contains(spec.key))
        return true;
    const QJsonValue value = props.value(spec.key);
    if (!value.isDouble()) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be a number.").arg(spec.key), nodeId);
        return false;
    }

    if (spec.kind == SdPropertyKind::Int) {
        const int number = value.toInt();
        if (number < spec.minimum || number > spec.maximum) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("\"%1\" must be between %2 and %3 (got %4).")
                         .arg(spec.key)
                         .arg(static_cast<int>(spec.minimum))
                         .arg(static_cast<int>(spec.maximum))
                         .arg(number),
                     nodeId);
            return false;
        }
        // A latent dimension has to be a positive multiple of 8: the VAE
        // downsamples by that factor, and anything else is silently rounded
        // somewhere inside the pipeline rather than rejected.
        if (spec.multipleOf > 0 && number % spec.multipleOf != 0) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("\"%1\" must be a multiple of %2 (got %3).")
                         .arg(spec.key).arg(spec.multipleOf).arg(number),
                     nodeId);
            return false;
        }
        return true;
    }

    const double number = value.toDouble();
    if (number < spec.minimum || number > spec.maximum) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be between %2 and %3.")
                     .arg(spec.key).arg(spec.minimum).arg(spec.maximum),
                 nodeId);
        return false;
    }
    return true;
}

// Seeds are strings: a 64-bit seed does not survive JSON's double, and the
// library writes its own the same way. A number is accepted too, for documents
// written by hand.
bool checkSeed(const QJsonObject& props, const QString& key, const QString& nodeId,
               QVector<SdWorkflowError>& errors)
{
    if (!props.contains(key))
        return true;
    const QJsonValue value = props.value(key);
    if (value.isDouble())
        return true;
    if (value.isString()) {
        bool ok = false;
        value.toString().toLongLong(&ok);
        if (ok)
            return true;
    }
    addError(errors, SdWorkflowErrorCode::InvalidProperty,
             tr_("\"%1\" must be an integer, optionally written as a string.").arg(key),
             nodeId);
    return false;
}

bool checkEnum(const QJsonObject& props, const SdPropertySpec& spec, const QString& nodeId,
               QVector<SdWorkflowError>& errors)
{
    if (!checkString(props, spec.key, nodeId, errors))
        return false;
    // Membership is only checkable here when the spec says its option list is
    // the whole truth. Where a parser in the module decides instead, the
    // per-type block below asks it and reports its own message.
    if (!spec.strictOptions || !props.contains(spec.key))
        return true;
    const QString value = props.value(spec.key).toString().trimmed().toLower();
    if (value.isEmpty() && spec.allowsEmpty)
        return true;
    if (spec.options.contains(value, Qt::CaseInsensitive))
        return true;
    addError(errors, SdWorkflowErrorCode::InvalidProperty,
             tr_("Unknown %1 \"%2\" (use %3).")
                 .arg(spec.label.toLower(), props.value(spec.key).toString(),
                      spec.options.join(QStringLiteral(" or "))),
             nodeId);
    return false;
}

bool checkModelRefList(const QJsonObject& props, const SdPropertySpec& spec,
                       const QString& nodeId, QVector<SdWorkflowError>& errors)
{
    const QJsonValue value = props.value(spec.key);
    if (value.isUndefined() || value.isNull())
        return true;
    if (!value.isArray()) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be an array of model ids.").arg(spec.key), nodeId);
        return false;
    }
    for (const QJsonValue& entry : value.toArray()) {
        if (!entry.isString()) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("\"%1\" must contain only model ids.").arg(spec.key), nodeId);
            return false;
        }
    }
    return true;
}

bool checkProperty(const QJsonObject& props, const SdPropertySpec& spec, const QString& nodeId,
                   QVector<SdWorkflowError>& errors)
{
    switch (spec.kind) {
    case SdPropertyKind::String:
    case SdPropertyKind::Text:
    // A model id is a string here; whether it names anything is the registry's
    // answer to give, below.
    case SdPropertyKind::ModelRef:
        return checkString(props, spec.key, nodeId, errors);
    case SdPropertyKind::Bool:
        return checkBool(props, spec.key, nodeId, errors);
    case SdPropertyKind::Int:
    case SdPropertyKind::Number:
        return checkNumber(props, spec, nodeId, errors);
    case SdPropertyKind::Seed:
        return checkSeed(props, spec.key, nodeId, errors);
    case SdPropertyKind::Enum:
        return checkEnum(props, spec, nodeId, errors);
    case SdPropertyKind::ModelRefList:
        return checkModelRefList(props, spec, nodeId, errors);
    }
    return true;
}

bool validateProperties(const SdWorkflowNode& node, const SdModelManager& models,
                        QVector<SdWorkflowError>& errors)
{
    const QJsonObject& p = node.properties;

    // Kinds and ranges first, for every property the type declares. Nothing
    // below may run against a value whose type is already wrong.
    bool ok = true;
    for (const SdPropertySpec& spec : nodeProperties(node.type))
        ok &= checkProperty(p, spec, node.id, errors);
    if (!ok)
        return false;

    const auto resolveEach = [&](const char* key, SdModelCategory category) {
        for (const QJsonValue& entry : p.value(QString::fromUtf8(key)).toArray()) {
            SdResolvedModelFile resolved;
            QString error;
            if (!models.resolveFile(entry.toString(), category, resolved, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
                ok = false;
            }
        }
    };

    switch (node.type) {
    case SdNodeType::Checkpoint: {
        // A checkpoint is self-contained, so the registry can answer now. The
        // text encoders and the VAE are wired further down and are checked
        // where the whole context is assembled.
        SdModelSelection selection;
        selection.modelId = p.value(QStringLiteral("modelId")).toString();
        for (const QJsonValue& entry : p.value(QStringLiteral("embeddingIds")).toArray())
            selection.embeddingIds << entry.toString();
        SdModelFiles files;
        QString error;
        if (!models.resolve(selection, files, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        return ok;
    }
    case SdNodeType::DiffusionModelLoader: {
        SdResolvedModelFile resolved;
        QString error;
        if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                SdModelCategory::DiffusionModel, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        resolveEach("embeddingIds", SdModelCategory::Embedding);
        return ok;
    }
    case SdNodeType::TextEncoderLoader: {
        if (p.value(QStringLiteral("modelIds")).toArray().isEmpty()) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("A text encoder node needs at least one model id."), node.id);
            return false;
        }
        resolveEach("modelIds", SdModelCategory::TextEncoder);
        return ok;
    }
    case SdNodeType::VaeLoader: {
        SdResolvedModelFile resolved;
        QString error;
        if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                SdModelCategory::Vae, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            return false;
        }
        return true;
    }
    case SdNodeType::EngineOptions: {
        SdWeightType weightType = SdWeightType::Auto;
        const QString weightName = p.value(QStringLiteral("weightType")).toString();
        if (!weightTypeFromName(weightName, weightType)) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("Unknown weight type \"%1\".").arg(weightName), node.id);
            ok = false;
        }
        SdDevicePreference device = SdDevicePreference::Auto;
        const QString deviceName = p.value(QStringLiteral("device")).toString();
        if (!devicePreferenceFromName(deviceName, device)) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("Unknown device preference \"%1\" (use auto, gpu or cpu).")
                         .arg(deviceName), node.id);
            ok = false;
        }
        return ok;
    }
    case SdNodeType::Lora: {
        const QString modelId = p.value(QStringLiteral("modelId")).toString().trimmed();
        if (modelId.isEmpty()) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("A LoRA node needs a \"modelId\"."), node.id);
            return false;
        }
        SdResolvedLora resolved;
        QString error;
        if (!models.resolveLora(modelId, 1.0f, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            return false;
        }
        return true;
    }
    case SdNodeType::KSampler:
    case SdNodeType::ADetailer: {
        // The library's own vocabulary, asked of the library: str_to_*() is the
        // only authority on what it will accept.
        const QString sampler = p.value(QStringLiteral("sampler")).toString();
        if (!SdGenerationService::isKnownSampler(sampler)) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("Unknown sampler \"%1\".").arg(sampler), node.id);
            ok = false;
        }
        const QString scheduler = p.value(QStringLiteral("scheduler")).toString();
        if (!SdGenerationService::isKnownScheduler(scheduler)) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("Unknown scheduler \"%1\".").arg(scheduler), node.id);
            ok = false;
        }
        if (node.type == SdNodeType::ADetailer) {
            SdResolvedModelFile resolved;
            QString error;
            if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                    SdModelCategory::ADetailer, resolved, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
                ok = false;
            }
        }
        return ok;
    }
    case SdNodeType::HiresFix: {
        const QString modelId = p.value(QStringLiteral("modelId")).toString().trimmed();
        if (!modelId.isEmpty()) {
            SdResolvedModelFile resolved;
            QString error;
            if (!models.resolveFile(modelId, SdModelCategory::Upscaler, resolved, &error)) {
                addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id,
                         QStringLiteral("modelId"));
                ok = false;
            }
        }
        return ok;
    }
    case SdNodeType::ControlNetLoader: {
        SdResolvedModelFile resolved;
        QString error;
        if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                SdModelCategory::ControlNet, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            return false;
        }
        return true;
    }
    case SdNodeType::IpAdapterLoader: {
        SdResolvedModelFile resolved;
        QString error;
        if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                SdModelCategory::IpAdapter, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        const QString visionEncoderId = p.value(QStringLiteral("visionEncoderId")).toString().trimmed();
        if (!visionEncoderId.isEmpty()
            && !models.resolveFile(visionEncoderId, SdModelCategory::VisionEncoder, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        return ok;
    }
    case SdNodeType::PhotoMaker: {
        SdResolvedModelFile resolved;
        QString error;
        if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                SdModelCategory::PhotoMaker, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        const QString embeddingId = p.value(QStringLiteral("embeddingId")).toString().trimmed();
        if (!embeddingId.isEmpty()
            && !models.resolveFile(embeddingId, SdModelCategory::IdentityEmbedding, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        return ok;
    }
    case SdNodeType::PuLid: {
        SdResolvedModelFile resolved;
        QString error;
        if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                SdModelCategory::PuLid, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        const QString embeddingId = p.value(QStringLiteral("embeddingId")).toString().trimmed();
        if (!embeddingId.isEmpty()
            && !models.resolveFile(embeddingId, SdModelCategory::IdentityEmbedding, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        return ok;
    }
    case SdNodeType::Upscale: {
        SdResolvedModelFile resolved;
        QString error;
        if (!models.resolveFile(p.value(QStringLiteral("modelId")).toString(),
                                SdModelCategory::Upscaler, resolved, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        return ok;
    }
    // Nothing a spec could not already answer: their enums are strict and the
    // generic pass checked them, and their model references (when any) are
    // plain strings resolved by the executor from a connected loader's value
    // rather than from a property on the node itself.
    case SdNodeType::ClipTextEncode:
    case SdNodeType::ClipSetLastLayer:
    case SdNodeType::ModelSampling:
    case SdNodeType::SkipLayerGuidance:
    case SdNodeType::ReferenceImages:
    case SdNodeType::EmptyLatentImage:
    case SdNodeType::VaeEncode:
    case SdNodeType::VaeEncodeInpaint:
    case SdNodeType::VaeDecode:
    case SdNodeType::InputImage:
    case SdNodeType::InputMask:
    case SdNodeType::ControlNetApply:
    case SdNodeType::IpAdapterApply:
    case SdNodeType::Canny:
    case SdNodeType::ImageOutput:
        break;
    }
    return ok;
}

// Fills in a connection's port names when the document left them out. Only
// unambiguous for a node with a single port of that direction — which every
// configuration node has, and the Generate node deliberately does not.
bool resolvePortNames(SdWorkflowConnection& c, const SdWorkflowNode& from,
                      const SdWorkflowNode& to, QVector<SdWorkflowError>& errors)
{
    if (c.fromPort.isEmpty()) {
        const QVector<SdPortSpec> outputs = nodeOutputs(from.type);
        if (outputs.size() != 1) {
            addError(errors, SdWorkflowErrorCode::UnknownPort,
                     tr_("Name the output port: a %1 node has %2 of them.")
                         .arg(nodeTypeName(from.type)).arg(outputs.size()),
                     from.id);
            return false;
        }
        c.fromPort = outputs.first().id;
    }
    if (c.toPort.isEmpty()) {
        const QVector<SdPortSpec> inputs = nodeInputs(to.type);
        if (inputs.size() != 1) {
            addError(errors, SdWorkflowErrorCode::UnknownPort,
                     tr_("Name the input port: a %1 node has %2 of them.")
                         .arg(nodeTypeName(to.type)).arg(inputs.size()),
                     to.id);
            return false;
        }
        c.toPort = inputs.first().id;
    }
    return true;
}

// Version-1 workflows created by the first Neural Flow UI wired several LoRA
// values directly into Generate.lora. Convert that representation into the
// model-transform chain the dynamic graph uses now. The order of the old
// connections is retained, so the generated request receives the same LoRAs
// in the same order. New documents never contain the legacy port.
void normaliseLegacyLoraChains(SdWorkflowDefinition& definition)
{
    for (const SdWorkflowNode& generate : definition.nodes) {
        if (generate.type != SdNodeType::VaeDecode)
            continue;

        QVector<SdWorkflowConnection> loras;
        QVector<SdWorkflowConnection> models;
        for (const SdWorkflowConnection& connection : definition.connections) {
            if (connection.toNode != generate.id)
                continue;
            if (connection.toPort == QLatin1String("lora"))
                loras.push_back(connection);
            else if (connection.toPort == QLatin1String("model"))
                models.push_back(connection);
        }
        if (loras.isEmpty() || models.size() != 1)
            continue;

        QVector<SdWorkflowConnection> migrated;
        migrated.reserve(definition.connections.size() + loras.size());
        for (const SdWorkflowConnection& connection : definition.connections) {
            const bool oldModel = connection.toNode == generate.id
                && connection.toPort == QLatin1String("model");
            const bool oldLora = connection.toNode == generate.id
                && connection.toPort == QLatin1String("lora");
            if (!oldModel && !oldLora)
                migrated.push_back(connection);
        }

        QString fromNode = models.first().fromNode;
        QString fromPort = models.first().fromPort;
        for (const SdWorkflowConnection& legacy : loras) {
            migrated.push_back(SdWorkflowConnection {
                fromNode, fromPort, legacy.fromNode, QStringLiteral("model")
            });
            fromNode = legacy.fromNode;
            fromPort = QStringLiteral("model");
        }
        migrated.push_back(SdWorkflowConnection {
            fromNode, fromPort, generate.id, QStringLiteral("model")
        });
        definition.connections = migrated;
    }
}

// ── Version 1 → 2 ───────────────────────────────────────────────────────────
//
// Version 1 had one Generate node fed by a Sampling node, and kept the image
// size, the components of the checkpoint, the engine options and the second
// pass as properties. Version 2 is ComfyUI-shaped: the sampling node became
// the KSampler, Generate became the VAE Decode that runs it, and each of those
// property groups became the node that owns it. Rewriting is what lets a
// document written before the change still run.

QString freeNodeId(const SdWorkflowDefinition& definition, const QString& base)
{
    QString id = base;
    for (int suffix = 2; definition.node(id); ++suffix)
        id = base + QString::number(suffix);
    return id;
}

QString incomingSource(const SdWorkflowDefinition& definition, const QString& nodeId,
                       const char* portId)
{
    for (const SdWorkflowConnection& c : definition.connections) {
        if (c.toNode == nodeId && c.toPort == QLatin1String(portId))
            return c.fromNode;
    }
    return {};
}

void connect_(SdWorkflowDefinition& definition, const QString& from, const char* fromPort,
              const QString& to, const char* toPort)
{
    definition.connections.push_back(SdWorkflowConnection {
        from, QString::fromUtf8(fromPort), to, QString::fromUtf8(toPort) });
}

void dropConnectionsTo(SdWorkflowDefinition& definition, const QString& nodeId,
                       const char* portId)
{
    definition.connections.erase(
        std::remove_if(definition.connections.begin(), definition.connections.end(),
                       [&nodeId, portId](const SdWorkflowConnection& c) {
                           return c.toNode == nodeId && c.toPort == QLatin1String(portId);
                       }),
        definition.connections.end());
}

void renameProperty(SdWorkflowNode& node, const char* from, const char* to)
{
    const QString fromKey = QString::fromUtf8(from);
    const QJsonValue value = node.properties.value(fromKey);
    if (!value.isUndefined() && !value.isNull())
        node.properties[QString::fromUtf8(to)] = value;
    node.properties.remove(fromKey);
}

void migrateVersion1(SdWorkflowDefinition& definition)
{
    const auto findNode = [&definition](const QString& id) -> SdWorkflowNode* {
        for (SdWorkflowNode& node : definition.nodes) {
            if (node.id == id)
                return &node;
        }
        return nullptr;
    };
    const auto addNode = [&definition](const SdWorkflowNode& node) {
        definition.nodes.push_back(node);
    };

    QStringList decodeIds;   // what used to be the Generate nodes
    for (const SdWorkflowNode& node : definition.nodes) {
        if (node.type == SdNodeType::VaeDecode)
            decodeIds << node.id;
    }

    for (const QString& decodeId : decodeIds) {
        const QString samplerId = incomingSource(definition, decodeId, "sampling");
        if (samplerId.isEmpty())
            continue;
        const QJsonObject decodeProps = definition.node(decodeId)->properties;
        const QJsonObject samplerProps = definition.node(samplerId)->properties;
        const QString checkpointId = incomingSource(definition, decodeId, "model");

        // Everything that fed Generate now feeds the sampler, except the image
        // the decode produces.
        for (const char* portId : { "model", "positive", "negative", "control",
                                    "ip_adapter", "photo_maker", "pulid" }) {
            for (SdWorkflowConnection& c : definition.connections) {
                if (c.toNode == decodeId && c.toPort == QLatin1String(portId))
                    c.toNode = samplerId;
            }
        }
        // The sampler used to be wired into Generate as a value; it is the
        // node that samples now, so that connection has no successor.
        dropConnectionsTo(definition, decodeId, "sampling");
        if (SdWorkflowNode* sampler = findNode(samplerId)) {
            const QJsonValue rules = decodeProps.value(QStringLiteral("imagePreprocess"));
            if (!rules.isUndefined() && !rules.isNull())
                sampler->properties[QStringLiteral("imagePreprocess")] = rules;
        }

        // The latent: an encode of the init image, or an empty one sized by
        // what the sampling node used to carry.
        const QString initImageId = incomingSource(definition, decodeId, "init_image");
        const QString maskId = incomingSource(definition, decodeId, "mask");
        QString latentId;
        if (!initImageId.isEmpty()) {
            SdWorkflowNode encode;
            encode.id = freeNodeId(definition, samplerId + QStringLiteral("_encode"));
            encode.type = maskId.isEmpty() ? SdNodeType::VaeEncode
                                           : SdNodeType::VaeEncodeInpaint;
            addNode(encode);
            connect_(definition, initImageId, "image", encode.id, "pixels");
            if (!maskId.isEmpty())
                connect_(definition, maskId, "mask", encode.id, "mask");
            latentId = encode.id;
        } else {
            SdWorkflowNode latent;
            latent.id = freeNodeId(definition, samplerId + QStringLiteral("_latent"));
            latent.type = SdNodeType::EmptyLatentImage;
            for (const char* key : { "width", "height" }) {
                const QJsonValue value = samplerProps.value(QString::fromUtf8(key));
                if (!value.isUndefined())
                    latent.properties[QString::fromUtf8(key)] = value;
            }
            for (const char* key : { "circularX", "circularY" }) {
                const QJsonValue value = decodeProps.value(QString::fromUtf8(key));
                if (!value.isUndefined())
                    latent.properties[QString::fromUtf8(key)] = value;
            }
            addNode(latent);
            latentId = latent.id;
        }
        dropConnectionsTo(definition, decodeId, "init_image");
        dropConnectionsTo(definition, decodeId, "mask");
        connect_(definition, latentId, "latent", samplerId, "latent");
        connect_(definition, samplerId, "latent", decodeId, "samples");

        // Reference images used to be wired one by one; they now go through
        // the node that carries their arguments.
        QStringList referenceSources;
        for (const SdWorkflowConnection& c : definition.connections) {
            if (c.toNode == decodeId && c.toPort == QLatin1String("references"))
                referenceSources << c.fromNode;
        }
        if (!referenceSources.isEmpty()) {
            dropConnectionsTo(definition, decodeId, "references");
            SdWorkflowNode references;
            references.id = freeNodeId(definition, samplerId + QStringLiteral("_references"));
            references.type = SdNodeType::ReferenceImages;
            const QJsonValue args = decodeProps.value(QStringLiteral("referenceArgs"));
            if (!args.isUndefined())
                references.properties[QStringLiteral("args")] = args;
            addNode(references);
            for (const QString& source : referenceSources)
                connect_(definition, source, "image", references.id, "images");
            connect_(definition, references.id, "references", samplerId, "references");
        }

        if (decodeProps.value(QStringLiteral("hiresEnabled")).toBool()) {
            SdWorkflowNode hires;
            hires.id = freeNodeId(definition, samplerId + QStringLiteral("_hires"));
            hires.type = SdNodeType::HiresFix;
            const auto copy = [&decodeProps, &hires](const char* from, const char* to) {
                const QJsonValue value = decodeProps.value(QString::fromUtf8(from));
                if (!value.isUndefined() && !value.isNull())
                    hires.properties[QString::fromUtf8(to)] = value;
            };
            copy("hiresUpscaler", "upscaler");
            copy("hiresModelId", "modelId");
            copy("hiresScale", "scale");
            copy("hiresWidth", "targetWidth");
            copy("hiresHeight", "targetHeight");
            copy("hiresSteps", "steps");
            copy("hiresDenoising", "denoisingStrength");
            copy("hiresTileSize", "tileSize");
            addNode(hires);
            connect_(definition, hires.id, "hires", samplerId, "hires");
        }

        // The CLIP and the VAE were implicit in version 1; they are wires now.
        // Its properties are copied because inserting nodes below can move it.
        const SdWorkflowNode* checkpointNode = definition.node(checkpointId);
        const bool isCheckpoint =
            checkpointNode && checkpointNode->type == SdNodeType::Checkpoint;
        const QJsonObject checkpointProps =
            checkpointNode ? checkpointNode->properties : QJsonObject();
        QString clipSourceId = checkpointId;
        const char* clipSourcePort = "clip";
        QString vaeSourceId = checkpointId;
        const char* vaeSourcePort = "vae";
        if (isCheckpoint) {
            const QJsonArray encoderIds =
                checkpointProps.value(QStringLiteral("textEncoderIds")).toArray();
            if (!encoderIds.isEmpty()) {
                SdWorkflowNode encoders;
                encoders.id = freeNodeId(definition, checkpointId + QStringLiteral("_encoders"));
                encoders.type = SdNodeType::TextEncoderLoader;
                encoders.properties[QStringLiteral("modelIds")] = encoderIds;
                addNode(encoders);
                clipSourceId = encoders.id;
                clipSourcePort = "clip";
            }
            const QString vaeId =
                checkpointProps.value(QStringLiteral("vaeId")).toString().trimmed();
            if (!vaeId.isEmpty()) {
                SdWorkflowNode vae;
                vae.id = freeNodeId(definition, checkpointId + QStringLiteral("_vae"));
                vae.type = SdNodeType::VaeLoader;
                vae.properties[QStringLiteral("modelId")] = vaeId;
                addNode(vae);
                vaeSourceId = vae.id;
                vaeSourcePort = "vae";
            }
        }
        if (!vaeSourceId.isEmpty()) {
            connect_(definition, vaeSourceId, vaeSourcePort, decodeId, "vae");
            if (!initImageId.isEmpty())
                connect_(definition, vaeSourceId, vaeSourcePort, latentId, "vae");
        }

        // CLIP skip was a sampling property and is a node of its own now.
        const int clipSkip = samplerProps.value(QStringLiteral("clipSkip")).toInt(-1);
        if (!clipSourceId.isEmpty() && clipSkip != -1) {
            SdWorkflowNode setLayer;
            setLayer.id = freeNodeId(definition, samplerId + QStringLiteral("_clip_skip"));
            setLayer.type = SdNodeType::ClipSetLastLayer;
            setLayer.properties[QStringLiteral("stopAtClipLayer")] = clipSkip;
            addNode(setLayer);
            connect_(definition, clipSourceId, clipSourcePort, setLayer.id, "clip");
            clipSourceId = setLayer.id;
            clipSourcePort = "clip";
        }
        if (!clipSourceId.isEmpty()) {
            for (const char* portId : { "positive", "negative" }) {
                const QString textId = incomingSource(definition, samplerId, portId);
                SdWorkflowNode* encode = findNode(textId);
                if (!encode || encode->type != SdNodeType::ClipTextEncode)
                    continue;
                connect_(definition, clipSourceId, clipSourcePort, textId, "clip");
                if (qstrcmp(portId, "positive") == 0) {
                    const QJsonValue guidance =
                        samplerProps.value(QStringLiteral("distilledGuidance"));
                    if (!guidance.isUndefined())
                        encode->properties[QStringLiteral("distilledGuidance")] = guidance;
                }
            }
        }

        // The model patches that used to be sampling properties.
        const double shift = samplerProps.value(QStringLiteral("flowShift")).toDouble();
        const int shiftedTimestep =
            samplerProps.value(QStringLiteral("shiftedTimestep")).toInt();
        const QString slgLayers =
            samplerProps.value(QStringLiteral("skipLayerGuidanceLayers")).toString();
        QString modelSourceId = incomingSource(definition, samplerId, "model");
        if (!modelSourceId.isEmpty() && (shift != 0.0 || shiftedTimestep != 0)) {
            SdWorkflowNode patch;
            patch.id = freeNodeId(definition, samplerId + QStringLiteral("_model_sampling"));
            patch.type = SdNodeType::ModelSampling;
            patch.properties[QStringLiteral("shift")] = shift;
            patch.properties[QStringLiteral("shiftedTimestep")] = shiftedTimestep;
            addNode(patch);
            connect_(definition, modelSourceId, "model", patch.id, "model");
            dropConnectionsTo(definition, samplerId, "model");
            connect_(definition, patch.id, "model", samplerId, "model");
            modelSourceId = patch.id;
        }
        if (!modelSourceId.isEmpty() && !slgLayers.trimmed().isEmpty()) {
            SdWorkflowNode slg;
            slg.id = freeNodeId(definition, samplerId + QStringLiteral("_slg"));
            slg.type = SdNodeType::SkipLayerGuidance;
            slg.properties[QStringLiteral("layers")] = slgLayers;
            for (const auto& pair : { std::pair<const char*, const char*> {
                                          "skipLayerGuidanceScale", "scale" },
                                      { "skipLayerGuidanceStart", "start" },
                                      { "skipLayerGuidanceEnd", "end" } }) {
                const QJsonValue value = samplerProps.value(QString::fromUtf8(pair.first));
                if (!value.isUndefined())
                    slg.properties[QString::fromUtf8(pair.second)] = value;
            }
            addNode(slg);
            connect_(definition, modelSourceId, "model", slg.id, "model");
            dropConnectionsTo(definition, samplerId, "model");
            connect_(definition, slg.id, "model", samplerId, "model");
        }

        // The engine options and the step cache left the checkpoint.
        if (checkpointNode) {
            static const char* kEngineKeys[] = {
                "weightType", "device", "threads", "allowCpuFallback", "enableMmap",
                "flashAttention", "diffusionFlashAttention", "vaeConvDirect",
                "batchedCfg", "autoFit", "conditioningCacheSize" };
            SdWorkflowNode engine;
            engine.type = SdNodeType::EngineOptions;
            for (const char* key : kEngineKeys) {
                const QJsonValue value = checkpointProps.value(QString::fromUtf8(key));
                if (!value.isUndefined() && !value.isNull())
                    engine.properties[QString::fromUtf8(key)] = value;
            }
            for (const char* key : { "cacheMode", "cacheThreshold", "cacheStart", "cacheEnd" }) {
                const QJsonValue value = decodeProps.value(QString::fromUtf8(key));
                if (!value.isUndefined() && !value.isNull())
                    engine.properties[QString::fromUtf8(key)] = value;
            }
            if (!engine.properties.isEmpty()
                && incomingSource(definition, checkpointId, "engine").isEmpty()) {
                engine.id = freeNodeId(definition, checkpointId + QStringLiteral("_engine"));
                addNode(engine);
                connect_(definition, engine.id, "engine", checkpointId, "engine");
            }
        }
    }

    // Finally the properties that stayed on a node but changed name.
    for (SdWorkflowNode& node : definition.nodes) {
        if (node.type != SdNodeType::VaeDecode)
            continue;
        renameProperty(node, "vaeTiling", "tiling");
        renameProperty(node, "vaeTileWidth", "tileWidth");
        renameProperty(node, "vaeTileHeight", "tileHeight");
        renameProperty(node, "vaeTileOverlap", "tileOverlap");
    }
    definition.version = SdWorkflowDefinition::kCurrentVersion;
}

} // namespace

bool SdWorkflowValidator::validate(const SdWorkflowDefinition& definition,
                                   const SdModelManager& models,
                                   SdWorkflowDefinition& normalised,
                                   SdWorkflowPlan& plan,
                                   QVector<SdWorkflowError>& errors)
{
    normalised = definition;
    normaliseLegacyLoraChains(normalised);
    if (normalised.version < SdWorkflowDefinition::kCurrentVersion)
        migrateVersion1(normalised);
    plan = SdWorkflowPlan {};
    const int errorsBefore = static_cast<int>(errors.size());

    // ── Unique ids ──────────────────────────────────────────────────────────
    QSet<QString> seen;
    for (const SdWorkflowNode& node : definition.nodes) {
        if (seen.contains(node.id)) {
            addError(errors, SdWorkflowErrorCode::DuplicateNodeId,
                     tr_("Two nodes share the id \"%1\".").arg(node.id), node.id);
        }
        seen.insert(node.id);
    }

    // ── Connections: endpoints, ports and types ─────────────────────────────
    // Counted per destination port so a required input can be found missing and
    // a single-connection port can be found over-subscribed.
    QHash<QString, int> incoming;             // "node\0port" -> count
    QHash<QString, QStringList> dependencies; // node -> nodes it reads from
    const auto portKey = [](const QString& nodeId, const QString& portId) {
        return nodeId + QLatin1Char('\0') + portId;
    };

    for (SdWorkflowConnection& c : normalised.connections) {
        const SdWorkflowNode* from = normalised.node(c.fromNode);
        const SdWorkflowNode* to = normalised.node(c.toNode);
        if (!from) {
            addError(errors, SdWorkflowErrorCode::UnknownNode,
                     tr_("Connection from unknown node \"%1\".").arg(c.fromNode), c.fromNode);
            continue;
        }
        if (!to) {
            addError(errors, SdWorkflowErrorCode::UnknownNode,
                     tr_("Connection to unknown node \"%1\".").arg(c.toNode), c.toNode);
            continue;
        }
        if (!resolvePortNames(c, *from, *to, errors))
            continue;

        SdPortSpec output;
        if (!findOutput(from->type, c.fromPort, output)) {
            addError(errors, SdWorkflowErrorCode::UnknownPort,
                     tr_("A %1 node has no output called \"%2\".")
                         .arg(nodeTypeName(from->type), c.fromPort),
                     from->id, c.fromPort);
            continue;
        }
        SdPortSpec input;
        if (!findInput(to->type, c.toPort, input)) {
            addError(errors, SdWorkflowErrorCode::UnknownPort,
                     tr_("A %1 node has no input called \"%2\".")
                         .arg(nodeTypeName(to->type), c.toPort),
                     to->id, c.toPort);
            continue;
        }
        if (output.type != input.type) {
            addError(errors, SdWorkflowErrorCode::PortTypeMismatch,
                     tr_("Cannot connect a %1 output to a %2 input.")
                         .arg(portTypeName(output.type), portTypeName(input.type)),
                     to->id, c.toPort);
            continue;
        }

        const QString key = portKey(to->id, c.toPort);
        const int count = ++incoming[key];
        if (count > 1 && !input.multiple) {
            addError(errors, SdWorkflowErrorCode::DuplicateConnection,
                     tr_("The input \"%1\" accepts one connection.").arg(c.toPort),
                     to->id, c.toPort);
            continue;
        }
        dependencies[to->id] << from->id;
    }

    // Only ancestors of output nodes participate in this run. This lets a user
    // keep an unfinished alternative branch on the canvas without having it
    // load a model, capture pixels or invalidate an otherwise runnable result.
    QSet<QString> active;
    for (const SdWorkflowNode& node : normalised.nodes) {
        if (node.type == SdNodeType::ImageOutput) {
            active.insert(node.id);
            plan.outputNodeIds << node.id;
        }
    }
    bool activeGrew = true;
    while (activeGrew) {
        activeGrew = false;
        for (const SdWorkflowConnection& connection : normalised.connections) {
            if (active.contains(connection.toNode) && !active.contains(connection.fromNode)) {
                active.insert(connection.fromNode);
                activeGrew = true;
            }
        }
    }

    // ── Properties and required inputs in the executable subgraph ──────────
    for (const SdWorkflowNode& node : normalised.nodes) {
        if (!active.contains(node.id))
            continue;
        validateProperties(node, models, errors);
        for (const SdPortSpec& spec : nodeInputs(node.type)) {
            if (!spec.required)
                continue;
            if (incoming.value(portKey(node.id, spec.id)) == 0) {
                addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                         tr_("The input \"%1\" of \"%2\" is not connected.")
                             .arg(spec.id, node.id),
                         node.id, spec.id);
            }
        }
    }

    // Every node that calls the generation service: the decode that runs a
    // sampled latent, and the three standalone image operations. There may be
    // any number — an image can branch to several outputs or feed a later one.
    const auto isOperation = [](SdNodeType type) {
        return type == SdNodeType::VaeDecode || type == SdNodeType::Canny
            || type == SdNodeType::Upscale || type == SdNodeType::ADetailer;
    };
    for (const SdWorkflowNode& node : normalised.nodes) {
        if (active.contains(node.id) && isOperation(node.type))
            plan.generateNodeIds << node.id;
    }
    if (plan.generateNodeIds.isEmpty()) {
        addError(errors, SdWorkflowErrorCode::NoGenerateNode,
                 tr_("Nothing in this graph produces an image: a decode has to reach an output."));
    }

    if (plan.outputNodeIds.isEmpty()) {
        addError(errors, SdWorkflowErrorCode::NoImageOutput,
                 tr_("The workflow has no image output node."));
    }

    // An Image Output is a requested result. It may be downstream of several
    // processing nodes, but it must ultimately depend on a Generate operation;
    // forwarding a captured canvas image is not a generation workflow.
    for (const QString& outputId : plan.outputNodeIds) {
        QSet<QString> ancestors { outputId };
        bool grew = true;
        while (grew) {
            grew = false;
            for (const SdWorkflowConnection& connection : normalised.connections) {
                if (ancestors.contains(connection.toNode)
                    && !ancestors.contains(connection.fromNode)) {
                    ancestors.insert(connection.fromNode);
                    grew = true;
                }
            }
        }
        const bool hasGenerate = std::any_of(
            plan.generateNodeIds.cbegin(), plan.generateNodeIds.cend(),
            [&ancestors](const QString& id) { return ancestors.contains(id); });
        if (!hasGenerate) {
            addError(errors, SdWorkflowErrorCode::OutputNotConnected,
                     tr_("The image output does not depend on anything that produces an image."),
                     outputId, QStringLiteral("image"));
        }
    }

    // ── Dependency order, and the cycle check that falls out of it ──────────
    // Kahn's algorithm: whatever is left when no node has all its dependencies
    // satisfied is exactly the set of nodes in (or downstream of) a cycle.
    {
        QStringList remaining;
        for (const SdWorkflowNode& node : normalised.nodes)
            remaining << node.id;

        QSet<QString> resolved;
        while (!remaining.isEmpty()) {
            bool progressed = false;
            for (int i = 0; i < remaining.size();) {
                const QString id = remaining.at(i);
                const QStringList& deps = dependencies[id];
                const bool ready = std::all_of(deps.cbegin(), deps.cend(),
                                               [&resolved](const QString& dep) {
                                                   return resolved.contains(dep);
                                               });
                if (ready) {
                    if (active.contains(id))
                        plan.order << id;
                    resolved.insert(id);
                    remaining.removeAt(i);
                    progressed = true;
                } else {
                    ++i;
                }
            }
            if (!progressed) {
                addError(errors, SdWorkflowErrorCode::CircularDependency,
                         tr_("The workflow has a circular dependency involving: %1")
                             .arg(remaining.join(QStringLiteral(", "))),
                         remaining.first());
                break;
            }
        }
    }

    return static_cast<int>(errors.size()) == errorsBefore && plan.isValid();
}

} // namespace sdlocal
