#pragma once

#include "ai/compat/AiCompatibilityReport.h"

#include <QList>

// Evaluates cuDNN against the active runtime requirement (spec §7/§8). Pure
// logic over the already-probed AiCudaInfo — it does no I/O of its own.
namespace AiCudnnCompatibilityChecker {

// cuDNN present AND its major matches what the runtime requires.
bool isCompatible(const AiCudaInfo& cuda, const AiRuntimeRequirement& req);

// Human-facing messages describing the cuDNN situation (missing / wrong major /
// ok), including the "Required: cuDNN 9.x for CUDA 12" line.
QList<AiCompatibilityMessage> messages(const AiCudaInfo& cuda, const AiRuntimeRequirement& req);

} // namespace AiCudnnCompatibilityChecker
