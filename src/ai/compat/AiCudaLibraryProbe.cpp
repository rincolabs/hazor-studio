#include "ai/compat/AiCudaLibraryProbe.hpp"

#include <QDebug>
#include <QLibrary>
#include <QList>
#include <QtGlobal>

// The CUDA major this build's ONNX Runtime CUDA package targets. Same default as
// AiCudaCompatibilityChecker, which owns the user-facing requirement table.
#ifndef HAZOR_ONNX_CUDA_MAJOR
#define HAZOR_ONNX_CUDA_MAJOR 12
#endif

namespace AiCudaLibraryProbe {

namespace {

// NVIDIA versions cuDNN, cuFFT and cuRAND independently of the CUDA toolkit, so
// their majors are spelled out here. Bump alongside the requirement table in
// AiCudaCompatibilityChecker.
constexpr int kCudaMajor   = HAZOR_ONNX_CUDA_MAJOR;
constexpr int kCudnnMajor  = 9;                           // cuDNN 9.x for CUDA 12 and 13
constexpr int kCufftMajor  = kCudaMajor >= 13 ? 12 : 11;  // cuFFT 11 ships with CUDA 12
constexpr int kCurandMajor = 10;

struct Library {
    QString kind;      // "cudart", "cublas", … — matched against the AiCudaInfo flags
    QString fileName;  // what the loader is asked for
};

// Exactly the CUDA imports of the shipped onnxruntime_providers_cuda library
// (the TensorRT provider needs a subset of the same set, plus nvinfer, which is
// not probed: TensorRT is never auto-selected). Read off the provider's import
// table, so a runtime package bump should be re-checked here.
//
// Unused on macOS — as is canLoad — where computeProbe short-circuits before
// any probing.
[[maybe_unused]] QList<Library> requiredLibraries()
{
    const auto entry = [](const char* base, int major) {
        Library lib;
        lib.kind = QLatin1String(base);
#if defined(Q_OS_WIN)
        // "cudart" + 12 -> "cudart64_12.dll" (built by concatenation: "%164"
        // would read as placeholder %16 followed by a 4).
        lib.fileName = lib.kind + QStringLiteral("64_") + QString::number(major)
                       + QStringLiteral(".dll");
#else
        lib.fileName = QStringLiteral("lib%1.so.%2").arg(lib.kind).arg(major);
#endif
        return lib;
    };

    QList<Library> libs {
        entry("cudart",   kCudaMajor),
        entry("cublas",   kCudaMajor),
        entry("cublasLt", kCudaMajor),
        entry("cufft",    kCufftMajor),
        entry("cudnn",    kCudnnMajor),
    };
#if !defined(Q_OS_WIN)
    libs << entry("curand", kCurandMajor); // only the Unix provider links cuRAND
#endif
    return libs;
}

// Asks the platform loader (LoadLibrary / dlopen) for the library, which is the
// same search ONNX Runtime's provider will perform — PATH / LD_LIBRARY_PATH
// included — so a pass here means the provider can resolve it too.
[[maybe_unused]] bool canLoad(const Library& lib)
{
    QLibrary library(lib.fileName);
    if (!library.load())
        return false;
    // Release it again: ONNX Runtime loads these itself when the provider is
    // actually used, and a CPU-only session has no reason to keep the CUDA
    // libraries mapped.
    library.unload();
    return true;
}

Result computeProbe()
{
    Result result;
    result.cudaMajor = QString::number(kCudaMajor);
    result.cudnnMajor = QString::number(kCudnnMajor);

#if defined(Q_OS_MACOS)
    qInfo().noquote() << "[AI][CUDA] CUDA is not available on macOS — inference runs on the CPU";
    return result;
#else
    result.loadable = true;
    for (const Library& lib : requiredLibraries()) {
        const bool found = canLoad(lib);
        if (!found) {
            result.loadable = false;
            result.missing << lib.fileName;
        }
        if (lib.kind == QLatin1String("cudart"))     result.cudaRuntimeFound = found;
        else if (lib.kind == QLatin1String("cublas")) result.cublasFound = found;
        else if (lib.kind == QLatin1String("cudnn"))  result.cudnnFound = found;
    }

    if (result.loadable) {
        qInfo().noquote() << "[AI][CUDA] CUDA" << result.cudaMajor << "+ cuDNN"
                          << result.cudnnMajor << "libraries found — GPU providers usable";
    } else {
        qInfo().noquote() << "[AI][CUDA] CUDA libraries not found:"
                          << result.missing.join(QStringLiteral(", "))
                          << "— GPU providers unavailable, inference stays on the CPU";
    }
    return result;
#endif
}

} // namespace

const Result& probe()
{
    // Function-local static: initialized once, thread-safe, and only paid for by
    // callers that actually consider a GPU provider.
    static const Result cached = computeProbe();
    return cached;
}

} // namespace AiCudaLibraryProbe
