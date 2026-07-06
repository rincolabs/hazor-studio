#include "ai/compat/AiProviderCompatibilityChecker.hpp"
#include "ai/compat/AiCudaCompatibilityChecker.hpp"
#include "ai/compat/AiCudnnCompatibilityChecker.hpp"

#include "ai/runtime/AiExecutionProvider.hpp"

#include <QCoreApplication>

namespace AiProviderCompatibilityChecker {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("AiProviderCompatibility", s); }

// Whether a provider could ever run on this OS, applying the documented rules
// (no CUDA/TensorRT on macOS; DirectML Windows-only; CoreML macOS-only).
bool supportedByOs(const QString& id, const QString& platform)
{
    if (id == QLatin1String(AiProvider::kCuda) || id == QLatin1String(AiProvider::kTensorRt))
        return platform != QLatin1String("macos");
    return AiProvider::supportedOnThisPlatform(id);
}

AiProviderCompatibility buildCpu()
{
    AiProviderCompatibility p;
    p.id = QString::fromLatin1(AiProvider::kCpu);
    p.displayName = AiProvider::displayName(p.id);
    p.supportedByOs = true;
    p.availableInRuntime = true;
    p.dependenciesAvailable = true;
    p.compatibleWithHardware = true;
    p.compatibleWithRuntime = true;
    p.status = AiCompatibilityStatus::Compatible;
    p.reason = tr("Always available");
    p.messages.push_back({ AiCompatibilitySeverity::Ok, tr("CPU"),
                           tr("CPU inference is always available."), QString(), QString(), QString() });
    return p;
}

AiProviderCompatibility buildCuda(const QString& platform, bool runtimeAvailable,
                                  const QStringList& runtimeProviders, const AiCudaInfo& cuda)
{
    const AiRuntimeRequirement req = AiCudaCompatibilityChecker::activeRequirement();

    AiProviderCompatibility p;
    p.id = QString::fromLatin1(AiProvider::kCuda);
    p.displayName = AiProvider::displayName(p.id);
    p.supportedByOs = supportedByOs(p.id, platform);
    p.availableInRuntime = runtimeAvailable && runtimeProviders.contains(p.id);
    p.dependenciesAvailable = cuda.driverDetected && cuda.cudaRuntimeDetected && cuda.cudnnDetected;
    p.compatibleWithHardware = cuda.gpuPresent || cuda.driverDetected;
    p.compatibleWithRuntime = AiCudnnCompatibilityChecker::isCompatible(cuda, req);

    const QString detectedCuda = cuda.cudaDriverApiVersion.isEmpty()
        ? (cuda.cudaRuntimeMajor.isEmpty() ? tr("not detected")
                                           : tr("%1.x").arg(cuda.cudaRuntimeMajor))
        : cuda.cudaDriverApiVersion;
    const QString detectedCudnn = cuda.cudnnDetected
        ? (cuda.cudnnMajor.isEmpty() ? tr("present") : tr("%1.x").arg(cuda.cudnnMajor))
        : tr("not detected");
    p.details = tr("Required: %1 + %2\nDetected: CUDA %3 + cuDNN %4")
                    .arg(req.recommendedCudaPackage, req.recommendedCudnnPackage,
                         detectedCuda, detectedCudnn);

    if (!p.supportedByOs) {
        p.status = AiCompatibilityStatus::Unsupported;
        p.reason = tr("Not supported on this platform");
    } else if (!p.compatibleWithHardware) {
        p.status = AiCompatibilityStatus::Incompatible;
        p.reason = tr("No NVIDIA GPU / driver detected");
        p.messages.push_back({ AiCompatibilitySeverity::Warning, tr("NVIDIA GPU not found"),
                               tr("No NVIDIA driver or GPU was detected."), p.details, QString(), QString() });
    } else if (!p.availableInRuntime) {
        p.status = AiCompatibilityStatus::MissingDependency;
        p.reason = tr("CUDA provider not present in this runtime");
        p.messages.push_back({ AiCompatibilitySeverity::Warning, tr("CUDA provider unavailable"),
                               tr("The loaded ONNX Runtime was not built with the CUDA Execution Provider."),
                               p.details, QString(), QString() });
    } else if (!p.dependenciesAvailable || !p.compatibleWithRuntime) {
        p.status = AiCompatibilityStatus::MissingDependency;
        p.reason = cuda.cudnnDetected ? tr("Incompatible CUDA/cuDNN") : tr("Missing cuDNN");
        for (const AiCompatibilityMessage& m : AiCudnnCompatibilityChecker::messages(cuda, req))
            p.messages.push_back(m);
        if (!cuda.cudaRuntimeDetected)
            p.messages.push_back({ AiCompatibilitySeverity::Error, tr("CUDA runtime not found"),
                                   tr("The CUDA runtime libraries were not detected."),
                                   p.details, QString(), QString() });
    } else {
        p.status = AiCompatibilityStatus::Compatible;
        p.reason = tr("Compatible");
        p.messages.push_back({ AiCompatibilitySeverity::Ok, tr("CUDA ready"),
                               tr("A compatible CUDA + cuDNN stack was detected."),
                               p.details, QString(), QString() });
    }

    // CUDA acceleration depends on user-installed libraries — always say so (spec §8).
    p.messages.push_back({ AiCompatibilitySeverity::Info, tr("CUDA acceleration requirements"),
        tr("CUDA acceleration requires a compatible NVIDIA driver, CUDA Toolkit and cuDNN installed "
           "on the system."),
        tr("Current runtime requires:\n- CUDA Toolkit: %1\n- cuDNN: %2")
            .arg(req.recommendedCudaPackage, req.recommendedCudnnPackage),
        QString(), QString() });
    return p;
}

AiProviderCompatibility buildGeneric(const QString& id, const QString& platform,
                                     bool runtimeAvailable, const QStringList& runtimeProviders)
{
    AiProviderCompatibility p;
    p.id = id;
    p.displayName = AiProvider::displayName(id);
    p.supportedByOs = supportedByOs(id, platform);
    p.availableInRuntime = runtimeAvailable && runtimeProviders.contains(id);
    p.dependenciesAvailable = p.availableInRuntime; // best-effort for non-CUDA providers
    p.compatibleWithHardware = true;
    p.compatibleWithRuntime = true;

    if (!p.supportedByOs) {
        p.status = AiCompatibilityStatus::Unsupported;
        p.reason = tr("Not supported on this platform");
    } else if (!p.availableInRuntime) {
        p.status = AiCompatibilityStatus::MissingDependency;
        p.reason = tr("Not present in this runtime");
        p.messages.push_back({ AiCompatibilitySeverity::Info, p.displayName,
                               tr("This provider is not built into the loaded ONNX Runtime."),
                               QString(), QString(), QString() });
    } else {
        p.status = AiCompatibilityStatus::Compatible;
        p.reason = tr("Available");
    }
    return p;
}

} // namespace

QList<AiProviderCompatibility> check(const AiPlatformInfo& platform,
                                     bool runtimeAvailable,
                                     const QStringList& runtimeProviders,
                                     const AiCudaInfo& cuda)
{
    QList<AiProviderCompatibility> out;
    out.push_back(buildCpu());
    out.push_back(buildCuda(platform.platform, runtimeAvailable, runtimeProviders, cuda));
    out.push_back(buildGeneric(QString::fromLatin1(AiProvider::kOpenVino), platform.platform,
                               runtimeAvailable, runtimeProviders));
    out.push_back(buildGeneric(QString::fromLatin1(AiProvider::kDirectMl), platform.platform,
                               runtimeAvailable, runtimeProviders));
    out.push_back(buildGeneric(QString::fromLatin1(AiProvider::kTensorRt), platform.platform,
                               runtimeAvailable, runtimeProviders));
    out.push_back(buildGeneric(QString::fromLatin1(AiProvider::kCoreMl), platform.platform,
                               runtimeAvailable, runtimeProviders));

    // When the runtime itself is missing, nothing can run — mark CPU accordingly
    // so the combobox/diagnostics don't over-promise.
    if (!runtimeAvailable) {
        for (AiProviderCompatibility& p : out) {
            p.availableInRuntime = false;
            if (p.status == AiCompatibilityStatus::Compatible) {
                p.status = AiCompatibilityStatus::MissingDependency;
                p.reason = tr("ONNX Runtime not available");
            }
        }
    }
    return out;
}

} // namespace AiProviderCompatibilityChecker
