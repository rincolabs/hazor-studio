#include "ai/compat/AiCudaCompatibilityChecker.hpp"
#include "ai/compat/AiCudaLibraryProbe.hpp"

#include <QProcess>
#include <QRegularExpression>
#include <QStringList>

// The CUDA major this build's ONNX Runtime CUDA package targets. Single place to
// bump for a CUDA-13 build (kept in sync with the runtime requirement table).
#ifndef HAZOR_ONNX_CUDA_MAJOR
#define HAZOR_ONNX_CUDA_MAJOR 12
#endif

namespace AiCudaCompatibilityChecker {

namespace {

// Runs a command, returning trimmed stdout or an empty string when it is missing
// or fails. Short timeout so a stuck tool never blocks the (cached) report.
// (Unused on macOS, where detect() short-circuits before any probe.)
[[maybe_unused]] QString runCommand(const QString& program, const QStringList& args)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(program, args);
    if (!proc.waitForStarted(1500))
        return QString();
    if (!proc.waitForFinished(4000)) {
        proc.kill();
        proc.waitForFinished(500);
        return QString();
    }
    if (proc.exitStatus() != QProcess::NormalExit)
        return QString();
    return QString::fromUtf8(proc.readAll()).trimmed();
}

#if defined(Q_OS_LINUX)
// Highest soname major for a library base ("libcudnn" -> "9" from libcudnn.so.9).
// Reads the cached `ldconfig -p` output once per detect().
QString sonameMajor(const QString& ldconfig, const QString& libBase)
{
    QRegularExpression re(QRegularExpression::escape(libBase) + QStringLiteral("\\.so\\.(\\d+)"));
    QRegularExpressionMatchIterator it = re.globalMatch(ldconfig);
    int best = -1;
    while (it.hasNext()) {
        const int major = it.next().captured(1).toInt();
        if (major > best) best = major;
    }
    return best < 0 ? QString() : QString::number(best);
}

bool ldconfigHas(const QString& ldconfig, const QString& libBase)
{
    return ldconfig.contains(libBase + QStringLiteral(".so"));
}
#endif

} // namespace

QList<AiRuntimeRequirement> runtimeRequirements()
{
    return {
        { QStringLiteral("onnxruntime-cuda12"), QStringLiteral("cuda"),
          QStringLiteral("12"), QStringLiteral("12.0"), QStringLiteral("12.x"),
          QStringLiteral("9"),
          QStringLiteral("CUDA Toolkit 12.x"), QStringLiteral("cuDNN 9.x for CUDA 12") },
        { QStringLiteral("onnxruntime-cuda13"), QStringLiteral("cuda"),
          QStringLiteral("13"), QStringLiteral("13.0"), QStringLiteral("13.x"),
          QStringLiteral("9"),
          QStringLiteral("CUDA Toolkit 13.x"), QStringLiteral("cuDNN 9.x for CUDA 13") },
    };
}

AiRuntimeRequirement activeRequirement()
{
    const QString major = QString::number(HAZOR_ONNX_CUDA_MAJOR);
    for (const AiRuntimeRequirement& r : runtimeRequirements())
        if (r.cudaMajor == major)
            return r;
    return runtimeRequirements().first(); // default to CUDA 12 row
}

AiCudaInfo detect()
{
    AiCudaInfo info;

#if defined(Q_OS_MACOS)
    return info; // CUDA never applicable on macOS.
#elif defined(Q_OS_LINUX)
    // The loader probe is the authority on whether the provider can resolve its
    // dependencies; ldconfig only widens the picture (it also sees libraries
    // outside the probed sonames) and supplies the installed majors.
    const AiCudaLibraryProbe::Result& probe = AiCudaLibraryProbe::probe();
    info.missingLibraries = probe.missing;

    const QString ldconfig = runCommand(QStringLiteral("ldconfig"), { QStringLiteral("-p") });
    info.cudaRuntimeDetected = probe.cudaRuntimeFound || ldconfigHas(ldconfig, QStringLiteral("libcudart"));
    info.cublasDetected      = probe.cublasFound || ldconfigHas(ldconfig, QStringLiteral("libcublas"));
    info.cudnnDetected       = probe.cudnnFound || ldconfigHas(ldconfig, QStringLiteral("libcudnn"));
    info.cudaRuntimeMajor    = sonameMajor(ldconfig, QStringLiteral("libcudart"));
    info.cudnnMajor          = sonameMajor(ldconfig, QStringLiteral("libcudnn"));
    // A successful probe wins over the soname scan: ldconfig misses libraries
    // reachable only through LD_LIBRARY_PATH (pip nvidia wheels, for one), and
    // when both are present the probed major is the one the provider loads.
    if (probe.cudaRuntimeFound) info.cudaRuntimeMajor = probe.cudaMajor;
    if (probe.cudnnFound)       info.cudnnMajor = probe.cudnnMajor;
    const bool libcuda       = ldconfigHas(ldconfig, QStringLiteral("libcuda"));

    // nvidia-smi: GPU name, driver version, CUDA driver API version.
    const QString smiQuery = runCommand(QStringLiteral("nvidia-smi"),
        { QStringLiteral("--query-gpu=name,driver_version"),
          QStringLiteral("--format=csv,noheader") });
    if (!smiQuery.isEmpty()) {
        info.gpuPresent = true;
        const QString firstLine = smiQuery.split(QLatin1Char('\n')).first();
        const QStringList cols = firstLine.split(QLatin1Char(','));
        if (cols.size() >= 1) info.gpuName = cols.at(0).trimmed();
        if (cols.size() >= 2) info.driverVersion = cols.at(1).trimmed();
    }
    if (!info.driverVersion.isEmpty() || libcuda) {
        info.driverDetected = true;
        // CUDA driver API version from the nvidia-smi banner ("CUDA Version: 12.4").
        const QString smi = runCommand(QStringLiteral("nvidia-smi"), {});
        QRegularExpression re(QStringLiteral("CUDA Version:\\s*([0-9]+\\.[0-9]+)"));
        const auto m = re.match(smi);
        if (m.hasMatch()) info.cudaDriverApiVersion = m.captured(1);
    }
    return info;
#elif defined(Q_OS_WIN)
    // nvidia-smi answers for the GPU and the driver.
    const QString smiQuery = runCommand(QStringLiteral("nvidia-smi"),
        { QStringLiteral("--query-gpu=name,driver_version"),
          QStringLiteral("--format=csv,noheader") });
    if (!smiQuery.isEmpty()) {
        info.gpuPresent = true;
        info.driverDetected = true;
        const QStringList cols = smiQuery.split(QLatin1Char('\n')).first().split(QLatin1Char(','));
        if (cols.size() >= 1) info.gpuName = cols.at(0).trimmed();
        if (cols.size() >= 2) info.driverVersion = cols.at(1).trimmed();
    }

    // The libraries themselves go through the loader probe. CUDA_PATH only
    // proves a toolkit was installed at some point and says nothing about
    // cuBLAS/cuDNN being reachable from this process, which is what decides
    // whether the CUDA provider can load at all.
    const AiCudaLibraryProbe::Result& probe = AiCudaLibraryProbe::probe();
    info.cudaRuntimeDetected = probe.cudaRuntimeFound;
    info.cublasDetected      = probe.cublasFound;
    info.cudnnDetected       = probe.cudnnFound;
    info.missingLibraries    = probe.missing;
    // Windows DLLs carry their major in the file name, so a successful load of
    // cudart64_12.dll / cudnn64_9.dll is itself the version evidence.
    if (probe.cudaRuntimeFound) info.cudaRuntimeMajor = probe.cudaMajor;
    if (probe.cudnnFound)       info.cudnnMajor = probe.cudnnMajor;
    return info;
#else
    return info;
#endif
}

} // namespace AiCudaCompatibilityChecker
