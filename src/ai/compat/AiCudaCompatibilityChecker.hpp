#pragma once

#include "ai/compat/AiCompatibilityReport.h"

#include <QList>

// Best-effort detection of the CUDA stack (driver, runtime, cuBLAS, cuDNN, GPU)
// plus the modular runtime requirement table (spec §7). Detection never throws
// and degrades gracefully when nvidia-smi / ldconfig are absent. On macOS CUDA is
// never applicable and detect() returns an empty AiCudaInfo.
namespace AiCudaCompatibilityChecker {

AiCudaInfo detect();

// The full requirement table. Extend this (CUDA 13, TensorRT…) without touching
// the checkers.
QList<AiRuntimeRequirement> runtimeRequirements();

// The requirement matching the CUDA major this build's ONNX Runtime targets
// (HAZOR_ONNX_CUDA_MAJOR; default 12). Used for the "Required: CUDA 12.x" lines.
AiRuntimeRequirement activeRequirement();

} // namespace AiCudaCompatibilityChecker
