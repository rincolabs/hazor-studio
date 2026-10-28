#include "ai/sd/SdWorkflowValidator.hpp"

#include "ai/sd/SdGenerationService.hpp"
#include "ai/sd/SdModelManager.hpp"

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

bool checkInt(const QJsonObject& props, const QString& key, int minimum, int maximum,
              const QString& nodeId, QVector<SdWorkflowError>& errors)
{
    if (!props.contains(key))
        return true;
    const QJsonValue value = props.value(key);
    if (!value.isDouble()) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be a number.").arg(key), nodeId);
        return false;
    }
    const int number = value.toInt();
    if (number < minimum || number > maximum) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be between %2 and %3 (got %4).")
                     .arg(key).arg(minimum).arg(maximum).arg(number), nodeId);
        return false;
    }
    return true;
}

bool checkDouble(const QJsonObject& props, const QString& key, double minimum, double maximum,
                 const QString& nodeId, QVector<SdWorkflowError>& errors)
{
    if (!props.contains(key))
        return true;
    const QJsonValue value = props.value(key);
    if (!value.isDouble()) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be a number.").arg(key), nodeId);
        return false;
    }
    const double number = value.toDouble();
    if (number < minimum || number > maximum) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be between %2 and %3.").arg(key).arg(minimum).arg(maximum),
                 nodeId);
        return false;
    }
    return true;
}

// A latent dimension has to be a positive multiple of 8: the VAE downsamples by
// that factor, and anything else is silently rounded somewhere inside the
// pipeline rather than rejected.
bool checkDimension(const QJsonObject& props, const QString& key, const QString& nodeId,
                    QVector<SdWorkflowError>& errors)
{
    if (!checkInt(props, key, 8, 8192, nodeId, errors))
        return false;
    if (!props.contains(key))
        return true;
    const int value = props.value(key).toInt();
    if (value % 8 != 0) {
        addError(errors, SdWorkflowErrorCode::InvalidProperty,
                 tr_("\"%1\" must be a multiple of 8 (got %2).").arg(key).arg(value), nodeId);
        return false;
    }
    return true;
}

// Seeds are strings: a 64-bit seed does not survive JSON's double, and the
// library writes its own the same way. A number is accepted too, for documents
// written by hand.
bool checkSeed(const QJsonObject& props, const QString& nodeId,
               QVector<SdWorkflowError>& errors)
{
    const QString key = QStringLiteral("seed");
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
             tr_("\"seed\" must be an integer, optionally written as a string."), nodeId);
    return false;
}

bool validateProperties(const SdWorkflowNode& node, const SdModelManager& models,
                        QVector<SdWorkflowError>& errors)
{
    const QJsonObject& p = node.properties;
    bool ok = true;

    switch (node.type) {
    case SdNodeType::Checkpoint: {
        ok &= checkString(p, QStringLiteral("modelId"), node.id, errors);
        ok &= checkString(p, QStringLiteral("vaeId"), node.id, errors);
        ok &= checkString(p, QStringLiteral("weightType"), node.id, errors);
        ok &= checkString(p, QStringLiteral("device"), node.id, errors);
        ok &= checkInt(p, QStringLiteral("threads"), 0, 1024, node.id, errors);
        ok &= checkBool(p, QStringLiteral("allowCpuFallback"), node.id, errors);
        if (!ok)
            return false;

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

        SdModelSelection selection;
        selection.modelId = p.value(QStringLiteral("modelId")).toString();
        selection.vaeId = p.value(QStringLiteral("vaeId")).toString();
        const QJsonValue encoders = p.value(QStringLiteral("textEncoderIds"));
        if (!encoders.isUndefined() && !encoders.isNull()) {
            if (!encoders.isArray()) {
                addError(errors, SdWorkflowErrorCode::InvalidProperty,
                         tr_("\"textEncoderIds\" must be an array of model ids."), node.id);
                return false;
            }
            for (const QJsonValue& v : encoders.toArray()) {
                if (!v.isString()) {
                    addError(errors, SdWorkflowErrorCode::InvalidProperty,
                             tr_("\"textEncoderIds\" must contain only model ids."), node.id);
                    return false;
                }
                selection.textEncoderIds << v.toString();
            }
        }

        // The whole point of validating before executing: a missing checkpoint
        // or a missing component is reported now, with the registry's own
        // explanation, instead of as a loader failure minutes later.
        SdModelFiles files;
        QString error;
        if (!models.resolve(selection, files, &error)) {
            addError(errors, SdWorkflowErrorCode::ModelUnavailable, error, node.id);
            ok = false;
        }
        return ok;
    }
    case SdNodeType::Lora: {
        ok &= checkString(p, QStringLiteral("modelId"), node.id, errors);
        ok &= checkDouble(p, QStringLiteral("weight"), -10.0, 10.0, node.id, errors);
        if (!ok)
            return false;
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
    case SdNodeType::Prompt:
    case SdNodeType::NegativePrompt:
        return checkString(p, QStringLiteral("text"), node.id, errors);
    case SdNodeType::Sampling: {
        ok &= checkDimension(p, QStringLiteral("width"), node.id, errors);
        ok &= checkDimension(p, QStringLiteral("height"), node.id, errors);
        ok &= checkInt(p, QStringLiteral("steps"), 1, 1000, node.id, errors);
        ok &= checkInt(p, QStringLiteral("clipSkip"), -1, 12, node.id, errors);
        ok &= checkDouble(p, QStringLiteral("cfgScale"), 0.0, 100.0, node.id, errors);
        ok &= checkDouble(p, QStringLiteral("denoisingStrength"), 0.0, 1.0, node.id, errors);
        ok &= checkSeed(p, node.id, errors);
        ok &= checkString(p, QStringLiteral("sampler"), node.id, errors);
        ok &= checkString(p, QStringLiteral("scheduler"), node.id, errors);
        if (!ok)
            return false;
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
        return ok;
    }
    case SdNodeType::InputImage: {
        if (!checkString(p, QStringLiteral("source"), node.id, errors))
            return false;
        const QString source = p.value(QStringLiteral("source")).toString().trimmed().toLower();
        if (!source.isEmpty() && source != QLatin1String("composite")
            && source != QLatin1String("current_layer")) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("Unknown image source \"%1\" (use composite or current_layer).")
                         .arg(source), node.id);
            return false;
        }
        return true;
    }
    case SdNodeType::InputMask: {
        if (!checkString(p, QStringLiteral("source"), node.id, errors))
            return false;
        const QString source = p.value(QStringLiteral("source")).toString().trimmed().toLower();
        if (!source.isEmpty() && source != QLatin1String("selection")) {
            addError(errors, SdWorkflowErrorCode::InvalidProperty,
                     tr_("Unknown mask source \"%1\" (only selection is supported).")
                         .arg(source), node.id);
            return false;
        }
        return true;
    }
    case SdNodeType::Generate:
        return checkInt(p, QStringLiteral("batchCount"), 1, 16, node.id, errors);
    case SdNodeType::ImageOutput:
        return checkString(p, QStringLiteral("layerName"), node.id, errors);
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

} // namespace

bool SdWorkflowValidator::validate(const SdWorkflowDefinition& definition,
                                   const SdModelManager& models,
                                   SdWorkflowDefinition& normalised,
                                   SdWorkflowPlan& plan,
                                   QVector<SdWorkflowError>& errors)
{
    normalised = definition;
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

    // ── Properties, including model availability ────────────────────────────
    for (const SdWorkflowNode& node : definition.nodes)
        validateProperties(node, models, errors);

    // ── Connections: endpoints, ports and types ─────────────────────────────
    // Counted per destination port so a required input can be found missing and
    // a single-connection port can be found over-subscribed.
    QHash<QString, int> incoming;             // "node\0port" -> count
    QHash<QString, QStringList> dependencies; // node -> nodes it reads from
    const auto portKey = [](const QString& nodeId, const QString& portId) {
        return nodeId + QLatin1Char('\0') + portId;
    };

    for (SdWorkflowConnection& c : normalised.connections) {
        const SdWorkflowNode* from = definition.node(c.fromNode);
        const SdWorkflowNode* to = definition.node(c.toNode);
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

    // ── Required inputs ─────────────────────────────────────────────────────
    for (const SdWorkflowNode& node : definition.nodes) {
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

    // ── Exactly one Generate, reaching exactly one output ───────────────────
    const QVector<const SdWorkflowNode*> generates = definition.nodesOfType(SdNodeType::Generate);
    if (generates.isEmpty()) {
        addError(errors, SdWorkflowErrorCode::NoGenerateNode,
                 tr_("The workflow has no generate node."));
    } else if (generates.size() > 1) {
        // The executor builds one generation request per run by design; chained
        // generations are a later step, not something to half-support now.
        addError(errors, SdWorkflowErrorCode::MultipleGenerateNodes,
                 tr_("This build runs one generate node per workflow; this one has %1.")
                     .arg(generates.size()));
    } else {
        plan.generateNodeId = generates.first()->id;
    }

    const QVector<const SdWorkflowNode*> outputs = definition.nodesOfType(SdNodeType::ImageOutput);
    if (outputs.isEmpty()) {
        addError(errors, SdWorkflowErrorCode::NoImageOutput,
                 tr_("The workflow has no image output node."));
    } else if (outputs.size() > 1) {
        addError(errors, SdWorkflowErrorCode::NoImageOutput,
                 tr_("This build runs one image output per workflow; this one has %1.")
                     .arg(outputs.size()));
    } else {
        plan.outputNodeId = outputs.first()->id;
    }

    if (!plan.generateNodeId.isEmpty() && !plan.outputNodeId.isEmpty()) {
        const bool connected = std::any_of(
            normalised.connections.cbegin(), normalised.connections.cend(),
            [&plan](const SdWorkflowConnection& c) {
                return c.fromNode == plan.generateNodeId && c.toNode == plan.outputNodeId
                    && c.toPort == QLatin1String("image");
            });
        if (!connected) {
            addError(errors, SdWorkflowErrorCode::OutputNotConnected,
                     tr_("The generate node is not connected to the image output."),
                     plan.generateNodeId, QStringLiteral("image"));
        }

        // The operation follows the wiring, so an inpaint without a mask simply
        // is an img2img and never reaches the engine as a half-configured one.
        const auto wired = [&normalised, &plan](const char* portId) {
            return std::any_of(normalised.connections.cbegin(), normalised.connections.cend(),
                               [&plan, portId](const SdWorkflowConnection& c) {
                                   return c.toNode == plan.generateNodeId
                                       && c.toPort == QLatin1String(portId);
                               });
        };
        const bool hasInit = wired("init_image");
        const bool hasMask = wired("mask");
        if (hasInit && hasMask)
            plan.operation = SdOperation::Inpaint;
        else if (hasInit)
            plan.operation = SdOperation::Img2Img;
        else
            plan.operation = SdOperation::Txt2Img;

        if (hasMask && !hasInit) {
            addError(errors, SdWorkflowErrorCode::MissingRequiredInput,
                     tr_("A mask needs an input image: connect one to \"init_image\" "
                         "or remove the mask."),
                     plan.generateNodeId, QStringLiteral("init_image"));
        }
    }

    // ── Dependency order, and the cycle check that falls out of it ──────────
    // Kahn's algorithm: whatever is left when no node has all its dependencies
    // satisfied is exactly the set of nodes in (or downstream of) a cycle.
    {
        QStringList remaining;
        for (const SdWorkflowNode& node : definition.nodes)
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
