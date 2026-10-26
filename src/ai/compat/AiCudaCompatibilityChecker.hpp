#pragma once

#include "ai/compat/AiCompatibilityReport.h"

#include <QList>

// Best-effort detection of the CUDA stack (driver, runtime, cuBLAS, cuDNN, GPU)
// plus the modular runtime requirement table (spec §7). The libraries come from
// AiCudaLibraryProbe (what the provider will actually be able to load); the
// driver, GPU and — on Linux — the installed sonames come from nvidia-smi and
// ldconfig. Detection never throws and degrades gracefully when those tools are
// absent. On macOS CUDA is never applicable and detect() returns an empty
// AiCudaInfo.
namespace AiCudaCompatibilityChecker {

AiCudaInfo detect();

// The full requirement table. Extend this (CUDA 13, TensorRT…) without touching
// the checkers.
QList<AiRuntimeRequirement> runtimeRequirements();

// The requirement matching the CUDA major this build's ONNX Runtime targets
// (HAZOR_ONNX_CUDA_MAJOR; default 12). Used for the "Required: CUDA 12.x" lines.
AiRuntimeRequirement activeRequirement();

} // namespace AiCudaCompatibilityChecker
