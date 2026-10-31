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
    // Dependency order for the subgraph that reaches an image output. A node
    // appears after everything it reads from, so the executor evaluates each
    // exactly once even when its value is branched to several consumers.
    QStringList order;
    QStringList generateNodeIds;
    QStringList outputNodeIds;

    bool isValid() const
    {
        return !generateNodeIds.isEmpty() && !outputNodeIds.isEmpty() && !order.isEmpty();
    }
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
