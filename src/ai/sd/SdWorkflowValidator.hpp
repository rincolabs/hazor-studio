#pragma once

#include "ai/sd/SdGenerationTypes.hpp"
#include "ai/sd/SdWorkflow.hpp"

#include <QString>
#include <QStringList>
#include <QVector>

// Checks a workflow before anything is loaded.
//
// Validation runs to completion before the first byte of a model is read: a
// graph with a typo in a sampler name, a missing checkpoint or a cycle must
// fail in milliseconds, not after a four-gigabyte load. Everything it needs —
// the node signatures and the model registry — is available without touching
// the inference engine.
namespace sdlocal {

class SdModelManager;

// A validated graph, ready to execute. Produced only on success.
struct SdWorkflowPlan {
    // Dependency order. A node appears after everything it reads from, so the
    // executor evaluates each exactly once.
    QStringList order;

    QString generateNodeId;
    QString outputNodeId;

    // Derived from how the Generate node is wired, not from a property: no
    // init image is txt2img, an init image is img2img, an init image and a
    // mask is inpainting.
    SdOperation operation = SdOperation::Txt2Img;

    bool isValid() const { return !generateNodeId.isEmpty() && !outputNodeId.isEmpty(); }
};

class SdWorkflowValidator {
public:
    // Checks `definition` and fills `plan`. `normalised` is the same graph with
    // every connection's port names made explicit, so the executor never has to
    // repeat the "which port did they mean" inference.
    //
    // `models` is consulted for availability only; nothing is loaded and no
    // directory is scanned that a query would not already have scanned.
    static bool validate(const SdWorkflowDefinition& definition,
                         const SdModelManager& models,
                         SdWorkflowDefinition& normalised,
                         SdWorkflowPlan& plan,
                         QVector<SdWorkflowError>& errors);
};

} // namespace sdlocal
