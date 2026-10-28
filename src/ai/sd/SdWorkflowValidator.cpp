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

    switch (node.type) {
    case SdNodeType::Checkpoint: {
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
        // Already known to be an array of strings, or absent.
        for (const QJsonValue& entry : p.value(QStringLiteral("textEncoderIds")).toArray())
            selection.textEncoderIds << entry.toString();

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
    case SdNodeType::Sampling: {
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
        return ok;
    }
    // Nothing a spec could not already answer: their enums are strict and the
    // generic pass checked them.
    case SdNodeType::Prompt:
    case SdNodeType::NegativePrompt:
    case SdNodeType::InputImage:
    case SdNodeType::InputMask:
    case SdNodeType::Generate:
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
