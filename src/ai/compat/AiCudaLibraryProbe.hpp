#pragma once

#include <QString>
#include <QStringList>

// Cheap, cached probe for the CUDA libraries the ONNX Runtime CUDA and TensorRT
// Execution Providers link against (cudart, cuBLAS, cuBLASLt, cuFFT, cuRAND,
// cuDNN).
//
// The provider libraries ship with the editor, the CUDA stack does not
// (invariant 10: providers are a runtime choice, never a separate build), so on
// a machine without a matching CUDA install the platform loader cannot resolve
// them. Letting ONNX Runtime discover that is loud: appending the provider makes
// it print a raw multi-line
//
//   Error loading "...onnxruntime_providers_cuda.dll" which depends on
//   "cublasLt64_12.dll" which is missing. (Error 126)
//
// block straight to stderr, once per session, before failing — the session then
// silently falls back to the CPU provider, so what the user gets is a scary
// console dump for a situation the editor handles. Probing first lets the
// caller skip the provider and say what is missing in Hazor's own words.
namespace AiCudaLibraryProbe {

struct Result {
    bool loadable = false;          // every required library resolved
    bool cudaRuntimeFound = false;  // cudart
    bool cublasFound = false;       // cuBLAS (the cuBLASLt companion is in `missing`)
    bool cudnnFound = false;        // cuDNN
    QString cudaMajor;              // CUDA major this build targets ("12")
    QString cudnnMajor;             // cuDNN major this build targets ("9")
    QStringList missing;            // file names the loader could not resolve
};

// Probed once per process and cached: a CUDA install does not appear while the
// editor is running, and each probe asks the loader for hundreds of megabytes
// of library. Thread-safe.
const Result& probe();

} // namespace AiCudaLibraryProbe
