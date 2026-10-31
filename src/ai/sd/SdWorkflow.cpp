#include "ai/sd/SdWorkflow.hpp"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QStringList>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

SdPortSpec port(const char* id, SdPortType type, bool required = false, bool multiple = false)
{
    SdPortSpec spec;
    spec.id = QString::fromUtf8(id);
    spec.type = type;
    spec.required = required;
    spec.multiple = multiple;
    return spec;
}

} // namespace

// ── Vocabulary ──────────────────────────────────────────────────────────────

QString nodeTypeName(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:     return QStringLiteral("checkpoint");
    case SdNodeType::DiffusionModelLoader: return QStringLiteral("diffusion_model_loader");
    case SdNodeType::TextEncoderLoader: return QStringLiteral("text_encoder_loader");
    case SdNodeType::VaeLoader:      return QStringLiteral("vae_loader");
    case SdNodeType::EngineOptions:  return QStringLiteral("engine_options");
    case SdNodeType::ControlNetLoader:return QStringLiteral("controlnet_loader");
    case SdNodeType::IpAdapterLoader:return QStringLiteral("ip_adapter_loader");
    case SdNodeType::Lora:           return QStringLiteral("lora");
    case SdNodeType::ModelSampling:  return QStringLiteral("model_sampling");
    case SdNodeType::SkipLayerGuidance: return QStringLiteral("skip_layer_guidance");
    case SdNodeType::ClipSetLastLayer: return QStringLiteral("clip_set_last_layer");
    case SdNodeType::ClipTextEncode: return QStringLiteral("clip_text_encode");
    case SdNodeType::ReferenceImages:return QStringLiteral("reference_images");
    case SdNodeType::ControlNetApply:return QStringLiteral("controlnet_apply");
    case SdNodeType::IpAdapterApply: return QStringLiteral("ip_adapter_apply");
    case SdNodeType::PhotoMaker:     return QStringLiteral("photo_maker");
    case SdNodeType::PuLid:          return QStringLiteral("pulid");
    case SdNodeType::EmptyLatentImage: return QStringLiteral("empty_latent_image");
    case SdNodeType::VaeEncode:      return QStringLiteral("vae_encode");
    case SdNodeType::VaeEncodeInpaint: return QStringLiteral("vae_encode_inpaint");
    case SdNodeType::KSampler:       return QStringLiteral("ksampler");
    case SdNodeType::HiresFix:       return QStringLiteral("hires_fix");
    case SdNodeType::InputImage:     return QStringLiteral("input_image");
    case SdNodeType::InputMask:      return QStringLiteral("input_mask");
    case SdNodeType::Canny:          return QStringLiteral("canny");
    case SdNodeType::Upscale:        return QStringLiteral("upscale");
    case SdNodeType::ADetailer:      return QStringLiteral("adetailer");
    case SdNodeType::VaeDecode:      return QStringLiteral("vae_decode");
    case SdNodeType::ImageOutput:    return QStringLiteral("image_output");
    }
    return QStringLiteral("unknown");
}

bool nodeTypeFromName(const QString& name, SdNodeType& out)
{
    const QString n = name.trimmed().toLower();
    // The version-1 spellings are kept so a document written before the graph
    // took ComfyUI's shape still parses; SdWorkflowValidator then rewires it.
    if (n == QLatin1String("checkpoint") || n == QLatin1String("model")) {
        out = SdNodeType::Checkpoint;
        return true;
    }
    if (n == QLatin1String("diffusion_model_loader")) {
        out = SdNodeType::DiffusionModelLoader;
        return true;
    }
    if (n == QLatin1String("text_encoder_loader") || n == QLatin1String("clip_loader")) {
        out = SdNodeType::TextEncoderLoader;
        return true;
    }
    if (n == QLatin1String("vae_loader"))      { out = SdNodeType::VaeLoader; return true; }
    if (n == QLatin1String("engine_options"))  { out = SdNodeType::EngineOptions; return true; }
    if (n == QLatin1String("controlnet_loader")) { out = SdNodeType::ControlNetLoader; return true; }
    if (n == QLatin1String("ip_adapter_loader")) { out = SdNodeType::IpAdapterLoader; return true; }
    if (n == QLatin1String("lora"))            { out = SdNodeType::Lora; return true; }
    if (n == QLatin1String("model_sampling"))  { out = SdNodeType::ModelSampling; return true; }
    if (n == QLatin1String("skip_layer_guidance")) {
        out = SdNodeType::SkipLayerGuidance;
        return true;
    }
    if (n == QLatin1String("clip_set_last_layer")) {
        out = SdNodeType::ClipSetLastLayer;
        return true;
    }
    if (n == QLatin1String("clip_text_encode") || n == QLatin1String("prompt")
        || n == QLatin1String("negative_prompt")) {
        out = SdNodeType::ClipTextEncode;
        return true;
    }
    if (n == QLatin1String("reference_images")) { out = SdNodeType::ReferenceImages; return true; }
    if (n == QLatin1String("controlnet_apply")) { out = SdNodeType::ControlNetApply; return true; }
    if (n == QLatin1String("ip_adapter_apply")) { out = SdNodeType::IpAdapterApply; return true; }
    if (n == QLatin1String("photo_maker")) { out = SdNodeType::PhotoMaker; return true; }
    if (n == QLatin1String("pulid")) { out = SdNodeType::PuLid; return true; }
    if (n == QLatin1String("empty_latent_image") || n == QLatin1String("image_size")) {
        out = SdNodeType::EmptyLatentImage;
        return true;
    }
    if (n == QLatin1String("vae_encode"))      { out = SdNodeType::VaeEncode; return true; }
    if (n == QLatin1String("vae_encode_inpaint")) {
        out = SdNodeType::VaeEncodeInpaint;
        return true;
    }
    if (n == QLatin1String("ksampler") || n == QLatin1String("sampling")
        || n == QLatin1String("sampling_params")) {
        out = SdNodeType::KSampler;
        return true;
    }
    if (n == QLatin1String("hires_fix"))    { out = SdNodeType::HiresFix; return true; }
    if (n == QLatin1String("input_image"))  { out = SdNodeType::InputImage; return true; }
    if (n == QLatin1String("input_mask"))   { out = SdNodeType::InputMask; return true; }
    if (n == QLatin1String("canny"))        { out = SdNodeType::Canny; return true; }
    if (n == QLatin1String("upscale"))      { out = SdNodeType::Upscale; return true; }
    if (n == QLatin1String("adetailer"))    { out = SdNodeType::ADetailer; return true; }
    if (n == QLatin1String("vae_decode") || n == QLatin1String("generate")) {
        out = SdNodeType::VaeDecode;
        return true;
    }
    if (n == QLatin1String("image_output") || n == QLatin1String("canvas_output")) {
        out = SdNodeType::ImageOutput;
        return true;
    }
    return false;
}

QString portTypeName(SdPortType type)
{
    switch (type) {
    case SdPortType::Model:        return QStringLiteral("model");
    case SdPortType::Clip:         return QStringLiteral("clip");
    case SdPortType::Vae:          return QStringLiteral("vae");
    case SdPortType::Engine:       return QStringLiteral("engine");
    case SdPortType::Conditioning: return QStringLiteral("conditioning");
    case SdPortType::Latent:       return QStringLiteral("latent");
    case SdPortType::References:   return QStringLiteral("references");
    case SdPortType::Hires:        return QStringLiteral("hires");
    case SdPortType::Image:        return QStringLiteral("image");
    case SdPortType::Mask:         return QStringLiteral("mask");
    case SdPortType::ControlNet:   return QStringLiteral("control_net");
    case SdPortType::Control:      return QStringLiteral("control");
    case SdPortType::IpAdapter:    return QStringLiteral("ip_adapter");
    case SdPortType::ImageConditioning: return QStringLiteral("image_conditioning");
    case SdPortType::PhotoMaker:   return QStringLiteral("photo_maker");
    case SdPortType::PuLid:        return QStringLiteral("pulid");
    }
    return QStringLiteral("unknown");
}

// ── Node signatures ─────────────────────────────────────────────────────────
//
// Fixed per type, which is what makes the graph checkable before anything is
// loaded. Nothing here runs inference except VAE Decode and the three
// standalone image operations: a latent is a described pipeline, and decoding
// it is what asks stable-diffusion.cpp to carry it out.

QVector<SdPortSpec> nodeInputs(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:
    case SdNodeType::DiffusionModelLoader:
        // ComfyUI has no equivalent: there, these are launch flags. Leaving it
        // unconnected keeps the backend's defaults.
        return { port("engine", SdPortType::Engine) };
    case SdNodeType::Lora:
        return { port("model", SdPortType::Model, /*required*/ true),
                 port("clip", SdPortType::Clip) };
    case SdNodeType::ModelSampling:
    case SdNodeType::SkipLayerGuidance:
        return { port("model", SdPortType::Model, /*required*/ true) };
    case SdNodeType::ClipSetLastLayer:
        return { port("clip", SdPortType::Clip, /*required*/ true) };
    case SdNodeType::ClipTextEncode:
        return { port("clip", SdPortType::Clip, /*required*/ true) };
    case SdNodeType::ReferenceImages:
        return { port("images", SdPortType::Image, true, true) };
    case SdNodeType::ControlNetApply:
        return { port("control_net", SdPortType::ControlNet, true),
                 port("image", SdPortType::Image, true) };
    case SdNodeType::IpAdapterApply:
        return { port("ip_adapter", SdPortType::IpAdapter, true),
                 port("image", SdPortType::Image, true) };
    case SdNodeType::PhotoMaker:
        return { port("images", SdPortType::Image, true, true) };
    case SdNodeType::VaeEncode:
        return { port("pixels", SdPortType::Image, true),
                 port("vae", SdPortType::Vae, true) };
    case SdNodeType::VaeEncodeInpaint:
        return { port("pixels", SdPortType::Image, true),
                 port("vae", SdPortType::Vae, true),
                 port("mask", SdPortType::Mask, true) };
    case SdNodeType::KSampler:
        return {
            port("model", SdPortType::Model, /*required*/ true),
            port("positive", SdPortType::Conditioning, /*required*/ true),
            port("negative", SdPortType::Conditioning),
            // An empty latent is txt2img; one from VAE Encode carries the
            // image to start from, and from its inpaint form a mask too.
            port("latent", SdPortType::Latent, /*required*/ true),
            port("references", SdPortType::References),
            port("control", SdPortType::Control),
            port("ip_adapter", SdPortType::ImageConditioning),
            port("photo_maker", SdPortType::PhotoMaker),
            port("pulid", SdPortType::PuLid),
            port("hires", SdPortType::Hires),
        };
    case SdNodeType::Canny:
    case SdNodeType::Upscale:
        return { port("image", SdPortType::Image, true) };
    case SdNodeType::ADetailer:
        return {
            port("model", SdPortType::Model, true),
            port("image", SdPortType::Image, true),
            port("positive", SdPortType::Conditioning, true),
            port("negative", SdPortType::Conditioning),
            port("vae", SdPortType::Vae),
        };
    case SdNodeType::VaeDecode:
        return { port("samples", SdPortType::Latent, /*required*/ true),
                 port("vae", SdPortType::Vae, /*required*/ true) };
    case SdNodeType::ImageOutput:
        return { port("image", SdPortType::Image, /*required*/ true) };
    case SdNodeType::TextEncoderLoader:
    case SdNodeType::VaeLoader:
    case SdNodeType::EngineOptions:
    case SdNodeType::ControlNetLoader:
    case SdNodeType::IpAdapterLoader:
    case SdNodeType::PuLid:
    case SdNodeType::EmptyLatentImage:
    case SdNodeType::HiresFix:
    case SdNodeType::InputImage:
    case SdNodeType::InputMask:
        break;
    }
    return {};
}

QVector<SdPortSpec> nodeOutputs(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:
        // One file carries all three, and they are wired separately so a
        // workflow can replace any of them with a loader of its own.
        return { port("model", SdPortType::Model), port("clip", SdPortType::Clip),
                 port("vae", SdPortType::Vae) };
    case SdNodeType::DiffusionModelLoader: return { port("model", SdPortType::Model) };
    case SdNodeType::TextEncoderLoader:    return { port("clip", SdPortType::Clip) };
    case SdNodeType::VaeLoader:            return { port("vae", SdPortType::Vae) };
    case SdNodeType::EngineOptions:        return { port("engine", SdPortType::Engine) };
    case SdNodeType::ControlNetLoader:return { port("control_net", SdPortType::ControlNet) };
    case SdNodeType::IpAdapterLoader:return { port("ip_adapter", SdPortType::IpAdapter) };
    case SdNodeType::Lora:
        return { port("model", SdPortType::Model), port("clip", SdPortType::Clip) };
    case SdNodeType::ModelSampling:
    case SdNodeType::SkipLayerGuidance: return { port("model", SdPortType::Model) };
    case SdNodeType::ClipSetLastLayer:  return { port("clip", SdPortType::Clip) };
    case SdNodeType::ClipTextEncode: return { port("conditioning", SdPortType::Conditioning) };
    case SdNodeType::ReferenceImages:return { port("references", SdPortType::References) };
    case SdNodeType::ControlNetApply:return { port("control", SdPortType::Control) };
    case SdNodeType::IpAdapterApply:return { port("image_conditioning", SdPortType::ImageConditioning) };
    case SdNodeType::PhotoMaker:     return { port("photo_maker", SdPortType::PhotoMaker) };
    case SdNodeType::PuLid:          return { port("pulid", SdPortType::PuLid) };
    case SdNodeType::EmptyLatentImage:
    case SdNodeType::VaeEncode:
    case SdNodeType::VaeEncodeInpaint:
    case SdNodeType::KSampler:       return { port("latent", SdPortType::Latent) };
    case SdNodeType::HiresFix:       return { port("hires", SdPortType::Hires) };
    case SdNodeType::InputImage:     return { port("image", SdPortType::Image) };
    case SdNodeType::InputMask:      return { port("mask", SdPortType::Mask) };
    case SdNodeType::Canny:
    case SdNodeType::Upscale:
    case SdNodeType::ADetailer:
    case SdNodeType::VaeDecode:      return { port("image", SdPortType::Image) };
    case SdNodeType::ImageOutput:    break;
    }
    return {};
}

bool findInput(SdNodeType type, const QString& portId, SdPortSpec& out)
{
    for (const SdPortSpec& spec : nodeInputs(type)) {
        if (spec.id == portId) {
            out = spec;
            return true;
        }
    }
    return false;
}

bool findOutput(SdNodeType type, const QString& portId, SdPortSpec& out)
{
    for (const SdPortSpec& spec : nodeOutputs(type)) {
        if (spec.id == portId) {
            out = spec;
            return true;
        }
    }
    return false;
}

// ── Errors ──────────────────────────────────────────────────────────────────

QString workflowErrorCodeName(SdWorkflowErrorCode code)
{
    switch (code) {
    case SdWorkflowErrorCode::InvalidJson:           return QStringLiteral("invalid_json");
    case SdWorkflowErrorCode::UnsupportedVersion:    return QStringLiteral("unsupported_version");
    case SdWorkflowErrorCode::EmptyWorkflow:         return QStringLiteral("empty_workflow");
    case SdWorkflowErrorCode::DuplicateNodeId:       return QStringLiteral("duplicate_node_id");
    case SdWorkflowErrorCode::MissingNodeId:         return QStringLiteral("missing_node_id");
    case SdWorkflowErrorCode::UnknownNodeType:       return QStringLiteral("unknown_node_type");
    case SdWorkflowErrorCode::UnknownNode:           return QStringLiteral("unknown_node");
    case SdWorkflowErrorCode::UnknownPort:           return QStringLiteral("unknown_port");
    case SdWorkflowErrorCode::PortTypeMismatch:      return QStringLiteral("port_type_mismatch");
    case SdWorkflowErrorCode::MissingRequiredInput:  return QStringLiteral("missing_required_input");
    case SdWorkflowErrorCode::DuplicateConnection:   return QStringLiteral("duplicate_connection");
    case SdWorkflowErrorCode::CircularDependency:    return QStringLiteral("circular_dependency");
    case SdWorkflowErrorCode::NoGenerateNode:        return QStringLiteral("no_generate_node");
    case SdWorkflowErrorCode::NoImageOutput:         return QStringLiteral("no_image_output");
    case SdWorkflowErrorCode::OutputNotConnected:    return QStringLiteral("output_not_connected");
    case SdWorkflowErrorCode::ModelUnavailable:      return QStringLiteral("model_unavailable");
    case SdWorkflowErrorCode::InvalidProperty:       return QStringLiteral("invalid_property");
    case SdWorkflowErrorCode::UnsupportedOperation:  return QStringLiteral("unsupported_operation");
    }
    return QStringLiteral("unknown");
}

QString SdWorkflowError::toString() const
{
    QString out = QStringLiteral("[%1]").arg(workflowErrorCodeName(code));
    if (!nodeId.isEmpty()) {
        out += QStringLiteral(" %1").arg(nodeId);
        if (!portId.isEmpty())
            out += QStringLiteral(".%1").arg(portId);
    }
    if (!message.isEmpty())
        out += QStringLiteral(": %1").arg(message);
    return out;
}

QString describeWorkflowErrors(const QVector<SdWorkflowError>& errors)
{
    if (errors.isEmpty())
        return {};
    // The first error is the one worth showing: validation reports the earliest
    // structural failure first, and a status line has room for one.
    QString out = errors.first().message;
    if (errors.size() > 1)
        out += QStringLiteral(" (+%1)").arg(errors.size() - 1);
    return out;
}

// ── Parsing ─────────────────────────────────────────────────────────────────

namespace {

void addError(QVector<SdWorkflowError>* errors, SdWorkflowErrorCode code,
              const QString& message, const QString& nodeId = {}, const QString& portId = {})
{
    if (!errors)
        return;
    SdWorkflowError e;
    e.code = code;
    e.message = message;
    e.nodeId = nodeId;
    e.portId = portId;
    errors->push_back(e);
}

} // namespace

bool SdWorkflowDefinition::fromJson(const QByteArray& json, SdWorkflowDefinition& out,
                                    QVector<SdWorkflowError>* errors)
{
    out = SdWorkflowDefinition {};

    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        addError(errors, SdWorkflowErrorCode::InvalidJson,
                 tr_("The workflow is not valid JSON: %1").arg(parseError.errorString()));
        return false;
    }
    if (!doc.isObject()) {
        addError(errors, SdWorkflowErrorCode::InvalidJson,
                 tr_("The workflow must be a JSON object."));
        return false;
    }

    const QJsonObject root = doc.object();
    out.version = root.value(QStringLiteral("version")).toInt(kCurrentVersion);
    if (out.version > kCurrentVersion || out.version < 1) {
        addError(errors, SdWorkflowErrorCode::UnsupportedVersion,
                 tr_("Workflow version %1 is not supported (this build understands %2).")
                     .arg(out.version).arg(kCurrentVersion));
        return false;
    }

    const QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
    if (nodes.isEmpty()) {
        addError(errors, SdWorkflowErrorCode::EmptyWorkflow,
                 tr_("The workflow has no nodes."));
        return false;
    }

    bool ok = true;
    for (const QJsonValue& value : nodes) {
        const QJsonObject o = value.toObject();
        SdWorkflowNode node;
        node.id = o.value(QStringLiteral("id")).toString().trimmed();
        if (node.id.isEmpty()) {
            addError(errors, SdWorkflowErrorCode::MissingNodeId,
                     tr_("Every node needs an id."));
            ok = false;
            continue;
        }
        const QString typeName = o.value(QStringLiteral("type")).toString();
        if (!nodeTypeFromName(typeName, node.type)) {
            addError(errors, SdWorkflowErrorCode::UnknownNodeType,
                     tr_("Unknown node type \"%1\".").arg(typeName), node.id);
            ok = false;
            continue;
        }
        node.properties = o.value(QStringLiteral("properties")).toObject();
        out.nodes.push_back(node);
    }

    for (const QJsonValue& value : root.value(QStringLiteral("connections")).toArray()) {
        const QJsonObject o = value.toObject();
        SdWorkflowConnection c;
        c.fromNode = o.value(QStringLiteral("from")).toString().trimmed();
        c.fromPort = o.value(QStringLiteral("output")).toString().trimmed();
        c.toNode = o.value(QStringLiteral("to")).toString().trimmed();
        c.toPort = o.value(QStringLiteral("input")).toString().trimmed();
        if (c.fromNode.isEmpty() || c.toNode.isEmpty()) {
            addError(errors, SdWorkflowErrorCode::InvalidJson,
                     tr_("A connection needs both a \"from\" and a \"to\" node."));
            ok = false;
            continue;
        }
        // An omitted port name means "the node's only port of that direction",
        // which keeps hand-written documents short without making the port
        // implicit in the general case.
        out.connections.push_back(c);
    }

    return ok;
}

QByteArray SdWorkflowDefinition::toJson() const
{
    QJsonArray nodeArray;
    for (const SdWorkflowNode& node : nodes) {
        QJsonObject o;
        o[QStringLiteral("id")] = node.id;
        o[QStringLiteral("type")] = nodeTypeName(node.type);
        if (!node.properties.isEmpty())
            o[QStringLiteral("properties")] = node.properties;
        nodeArray.append(o);
    }

    QJsonArray connectionArray;
    for (const SdWorkflowConnection& c : connections) {
        QJsonObject o;
        o[QStringLiteral("from")] = c.fromNode;
        o[QStringLiteral("output")] = c.fromPort;
        o[QStringLiteral("to")] = c.toNode;
        o[QStringLiteral("input")] = c.toPort;
        connectionArray.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = version;
    root[QStringLiteral("nodes")] = nodeArray;
    root[QStringLiteral("connections")] = connectionArray;
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

const SdWorkflowNode* SdWorkflowDefinition::node(const QString& id) const
{
    for (const SdWorkflowNode& n : nodes) {
        if (n.id == id)
            return &n;
    }
    return nullptr;
}

QVector<const SdWorkflowNode*> SdWorkflowDefinition::nodesOfType(SdNodeType type) const
{
    QVector<const SdWorkflowNode*> out;
    for (const SdWorkflowNode& n : nodes) {
        if (n.type == type)
            out.push_back(&n);
    }
    return out;
}

// ── The panel's fixed document ──────────────────────────────────────────────

QByteArray txt2imgWorkflowJson(const QString& modelId,
                               const QString& prompt,
                               const QString& negativePrompt,
                               int width,
                               int height,
                               int steps,
                               qint64 seed,
                               float cfgScale,
                               const QString& layerName)
{
    SdWorkflowDefinition def;

    SdWorkflowNode checkpoint;
    checkpoint.id = QStringLiteral("checkpoint");
    checkpoint.type = SdNodeType::Checkpoint;
    checkpoint.properties[QStringLiteral("modelId")] = modelId;

    SdWorkflowNode positive;
    positive.id = QStringLiteral("positive");
    positive.type = SdNodeType::ClipTextEncode;
    positive.properties[QStringLiteral("text")] = prompt;

    SdWorkflowNode negative;
    negative.id = QStringLiteral("negative");
    negative.type = SdNodeType::ClipTextEncode;
    negative.properties[QStringLiteral("text")] = negativePrompt;

    SdWorkflowNode latent;
    latent.id = QStringLiteral("latent");
    latent.type = SdNodeType::EmptyLatentImage;
    latent.properties[QStringLiteral("width")] = width;
    latent.properties[QStringLiteral("height")] = height;

    SdWorkflowNode sampler;
    sampler.id = QStringLiteral("sampler");
    sampler.type = SdNodeType::KSampler;
    sampler.properties[QStringLiteral("steps")] = steps;
    // JSON has no 64-bit integer, so a seed is written as a string — the same
    // choice the library makes in its own generation_info.
    sampler.properties[QStringLiteral("seed")] = QString::number(seed);
    sampler.properties[QStringLiteral("cfgScale")] = cfgScale;

    SdWorkflowNode decode;
    decode.id = QStringLiteral("decode");
    decode.type = SdNodeType::VaeDecode;

    SdWorkflowNode output;
    output.id = QStringLiteral("output");
    output.type = SdNodeType::ImageOutput;
    output.properties[QStringLiteral("layerName")] = layerName;

    def.nodes = { checkpoint, positive, negative, latent, sampler, decode, output };
    def.connections = {
        SdWorkflowConnection { checkpoint.id, QStringLiteral("clip"),  positive.id, QStringLiteral("clip") },
        SdWorkflowConnection { checkpoint.id, QStringLiteral("clip"),  negative.id, QStringLiteral("clip") },
        SdWorkflowConnection { checkpoint.id, QStringLiteral("model"), sampler.id,  QStringLiteral("model") },
        SdWorkflowConnection { positive.id, QStringLiteral("conditioning"), sampler.id, QStringLiteral("positive") },
        SdWorkflowConnection { negative.id, QStringLiteral("conditioning"), sampler.id, QStringLiteral("negative") },
        SdWorkflowConnection { latent.id,   QStringLiteral("latent"), sampler.id, QStringLiteral("latent") },
        SdWorkflowConnection { sampler.id,  QStringLiteral("latent"), decode.id,  QStringLiteral("samples") },
        SdWorkflowConnection { checkpoint.id, QStringLiteral("vae"),  decode.id,  QStringLiteral("vae") },
        SdWorkflowConnection { decode.id,   QStringLiteral("image"),  output.id,  QStringLiteral("image") },
    };
    return def.toJson();
}

} // namespace sdlocal
