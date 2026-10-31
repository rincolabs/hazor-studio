#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

// The workflow document: a small graph of nodes describing one image generation.
//
// This is the format the Neural Flow frontend will eventually produce and edit;
// for now it is written by hand and by the experimental panel, and executed
// entirely in C++. It is deliberately NOT ComfyUI's internal format — the node
// types here are the operations this module actually has, not a surface to be
// compatible with — and it is deliberately not a general scripting language:
// typed configuration nodes transform values and Generate nodes run inference.
// The graph may branch and may contain several Generate nodes; only ancestors
// of requested Image Output nodes are evaluated.
//
// Nothing here knows about Qt widgets, documents, or any node-graph library.
namespace sdlocal {

enum class SdNodeType {
    // ── Configuration: produce parameters, never run inference ──────────────
    Checkpoint,
    Lora,
    ControlNetLoader,
    ControlNetApply,
    IpAdapterLoader,
    IpAdapterApply,
    PhotoMaker,
    PuLid,
    Prompt,
    NegativePrompt,
    Sampling,
    // ── Input: pull pixels from the host application ────────────────────────
    InputImage,
    InputMask,
    Canny,
    Upscale,
    ADetailer,
    // ── Processing: the one node that calls the generation service ──────────
    Generate,
    // ── Output: hands the result back to the host application ──────────────
    ImageOutput,
};

// What travels along a connection. Checked before execution, so a prompt wired
// into a sampling input is a named error rather than a surprise at run time.
enum class SdPortType {
    Model,
    Conditioning,
    Sampling,
    Image,
    Mask,
    ControlNet,
    Control,
    IpAdapter,
    ImageConditioning,
    PhotoMaker,
    PuLid,
};

QString nodeTypeName(SdNodeType type);
bool nodeTypeFromName(const QString& name, SdNodeType& out);
QString portTypeName(SdPortType type);

// One port in a node's fixed signature.
struct SdPortSpec {
    QString id;
    SdPortType type = SdPortType::Model;
    bool required = false;
    // Accepts more than one incoming connection. The editor grows variadic
    // inputs on demand; this is a graph contract, never a fixed slot count.
    bool multiple = false;
};

QVector<SdPortSpec> nodeInputs(SdNodeType type);
QVector<SdPortSpec> nodeOutputs(SdNodeType type);
// Looks up one port by id; returns false when the node type has no such port.
bool findInput(SdNodeType type, const QString& portId, SdPortSpec& out);
bool findOutput(SdNodeType type, const QString& portId, SdPortSpec& out);

// ── Errors ──────────────────────────────────────────────────────────────────

// Codes rather than only messages, so a caller (and later the frontend) can act
// on a specific failure — highlight the offending node, offer to register a
// model — instead of matching on translated text.
enum class SdWorkflowErrorCode {
    InvalidJson,
    UnsupportedVersion,
    EmptyWorkflow,
    DuplicateNodeId,
    MissingNodeId,
    UnknownNodeType,
    UnknownNode,
    UnknownPort,
    PortTypeMismatch,
    MissingRequiredInput,
    DuplicateConnection,
    CircularDependency,
    NoGenerateNode,
    NoImageOutput,
    OutputNotConnected,
    ModelUnavailable,
    InvalidProperty,
    UnsupportedOperation,
};

struct SdWorkflowError {
    SdWorkflowErrorCode code = SdWorkflowErrorCode::InvalidJson;
    QString nodeId;    // empty when the error is not about one node
    QString portId;    // empty when the error is not about one port
    QString message;   // user-presentable, already translated

    QString toString() const;
};

QString workflowErrorCodeName(SdWorkflowErrorCode code);
// Joins messages for a status line, newest first; empty for no errors.
QString describeWorkflowErrors(const QVector<SdWorkflowError>& errors);

// ── Document ────────────────────────────────────────────────────────────────

struct SdWorkflowNode {
    QString id;
    SdNodeType type = SdNodeType::Checkpoint;
    // Free-form per type. Kept as JSON rather than unpacked into a variant
    // struct per node type: the executor reads only the keys its type defines,
    // and a frontend can round-trip keys this build does not know yet.
    QJsonObject properties;
};

struct SdWorkflowConnection {
    QString fromNode;
    QString fromPort;
    QString toNode;
    QString toPort;
};

struct SdWorkflowDefinition {
    // Bumped only on an incompatible change; a document from the future is
    // refused rather than half-read.
    static constexpr int kCurrentVersion = 1;

    int version = kCurrentVersion;
    QVector<SdWorkflowNode> nodes;
    QVector<SdWorkflowConnection> connections;

    // Parses a document. Structural problems only: whether the graph makes
    // sense is SdWorkflowValidator's job.
    static bool fromJson(const QByteArray& json, SdWorkflowDefinition& out,
                         QVector<SdWorkflowError>* errors);
    QByteArray toJson() const;

    const SdWorkflowNode* node(const QString& id) const;
    QVector<const SdWorkflowNode*> nodesOfType(SdNodeType type) const;
};

// The fixed txt2img document the experimental Stable Diffusion panel runs.
//
// It exists so the panel exercises the whole architecture — parse, validate,
// resolve, execute, insert — rather than calling the generation service
// directly. A model id of "default" keeps the document portable: it resolves
// through SdModelManager on whatever machine runs it.
QByteArray txt2imgWorkflowJson(const QString& modelId,
                               const QString& prompt,
                               const QString& negativePrompt,
                               int width,
                               int height,
                               int steps,
                               qint64 seed,
                               float cfgScale,
                               const QString& layerName);

} // namespace sdlocal
