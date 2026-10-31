#pragma once

#include "ai/sd/SdModelManager.hpp"
#include "ai/sd/SdWorkflow.hpp"

#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QStringList>
#include <QVector>

// The editable surface of a workflow node: one spec per property the executor
// reads.
//
// It exists because three consumers need the same facts and must not each keep
// a copy of them: SdWorkflowValidator checks a document against these specs,
// SdWorkflowExecutor takes its fallbacks from them, and the Neural Flow
// frontend builds its node widgets out of them (src/ui/NeuralFlowBridge
// serialises this table to JSON). A range written twice is a range that will
// eventually disagree with itself — a widget offering 1024 steps for a property
// the validator caps at 1000.
//
// What is deliberately NOT here are the node signatures. Ports live in
// SdWorkflow (nodeInputs / nodeOutputs) because the shape of the graph is
// checkable without knowing anything about properties.
namespace sdlocal {

enum class SdPropertyKind {
    String,        // one line
    Text,          // several lines; a prompt
    Bool,
    Int,
    Number,        // double
    Seed,          // 64-bit, carried as a string because JSON has no such integer
    Enum,          // one of `options`
    ModelRef,      // one registered model id, of `modelCategories`
    ModelRefList,  // several registered model ids, of `modelCategories`
};

QString propertyKindName(SdPropertyKind kind);

struct SdPropertySpec {
    QString key;
    QString label;
    SdPropertyKind kind = SdPropertyKind::String;

    // What the executor uses when the document omits the key. The executor
    // reads it from here rather than repeating a literal, so "what is the
    // default width" has exactly one answer.
    QJsonValue defaultValue;

    // Int, Number and Seed. Inclusive, and exactly what the validator enforces.
    double minimum = 0.0;
    double maximum = 0.0;
    double step = 1.0;        // increment hint; the frontend's business only
    // A latent dimension has to be a positive multiple of 8, because the VAE
    // downsamples by that factor. 0 means unconstrained.
    int multipleOf = 0;

    QStringList options;      // Enum
    // Enum only: an empty value is legal and means "whatever the checkpoint
    // declares". The sampler and the scheduler both work that way.
    bool allowsEmpty = false;
    // Enum only: `options` is the authoritative list, so the validator can
    // check membership against it. False where a parser in the module decides
    // instead and accepts spellings this list does not carry — weightType
    // takes "fp16" for "f16", and the sampler names go through the library's
    // own str_to_sample_method().
    bool strictOptions = false;

    QVector<SdModelCategory> modelCategories;  // ModelRef / ModelRefList

    // Offered to the user as a widget. False for a property that belongs to the
    // contract and is validated, but that this build cannot honour usefully —
    // `hint` says why.
    bool editable = true;

    QString hint;             // one line, user-presentable
};

// One grouping in the node palette. A backend fact, because it follows the node
// types rather than any particular layout.
enum class SdNodeCategory { Models, Conditioning, Latent, Sampling, Input, Processing, Output };

QString nodeCategoryName(SdNodeCategory category);
QString nodeCategoryLabel(SdNodeCategory category);
SdNodeCategory nodeCategory(SdNodeType type);
QVector<SdNodeCategory> allNodeCategories();

// Display name for a node type: "Load Checkpoint" for `checkpoint`.
QString nodeTypeLabel(SdNodeType type);

// Every property of `type`, editable or not, in display order.
QVector<SdPropertySpec> nodeProperties(SdNodeType type);
bool findProperty(SdNodeType type, const QString& key, SdPropertySpec& out);
// The default the executor applies for an absent key; a null value for a key
// the type does not define.
QJsonValue propertyDefault(SdNodeType type, const QString& key);

// Every node type this build can execute, in palette order.
QVector<SdNodeType> allNodeTypes();

// The whole node contract as JSON: types with their category, ports and
// property specs, plus the vocabularies a picker needs. This is the only
// description of the node surface the frontend gets — it keeps no copy of its
// own, so a node type added here appears there without a second edit.
QJsonObject describeNodeCatalog();

} // namespace sdlocal
