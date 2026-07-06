#include "ai/compat/AiCudnnCompatibilityChecker.hpp"

#include <QCoreApplication>

namespace AiCudnnCompatibilityChecker {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("AiCudnnCompatibility", s); }
} // namespace

bool isCompatible(const AiCudaInfo& cuda, const AiRuntimeRequirement& req)
{
    if (!cuda.cudnnDetected)
        return false;
    // When we can read the soname major, require it to match; otherwise accept a
    // detected cuDNN (we cannot prove a mismatch).
    if (!cuda.cudnnMajor.isEmpty() && !req.cudnnMajor.isEmpty())
        return cuda.cudnnMajor == req.cudnnMajor;
    return true;
}

QList<AiCompatibilityMessage> messages(const AiCudaInfo& cuda, const AiRuntimeRequirement& req)
{
    QList<AiCompatibilityMessage> out;
    const QString required = tr("Required: %1").arg(req.recommendedCudnnPackage);

    if (!cuda.cudnnDetected) {
        out.push_back({ AiCompatibilitySeverity::Error,
                        tr("cuDNN not found"),
                        tr("The CUDA Execution Provider needs cuDNN, which was not detected."),
                        required, QString(), QString() });
        return out;
    }
    if (!cuda.cudnnMajor.isEmpty() && !req.cudnnMajor.isEmpty()
        && cuda.cudnnMajor != req.cudnnMajor) {
        out.push_back({ AiCompatibilitySeverity::Error,
                        tr("Incompatible cuDNN version"),
                        tr("Detected cuDNN %1, but the current runtime requires cuDNN %2.")
                            .arg(cuda.cudnnMajor, req.cudnnMajor),
                        required, QString(), QString() });
        return out;
    }
    out.push_back({ AiCompatibilitySeverity::Ok,
                    tr("cuDNN detected"),
                    cuda.cudnnMajor.isEmpty()
                        ? tr("A compatible cuDNN library was found.")
                        : tr("cuDNN %1 detected.").arg(cuda.cudnnMajor),
                    required, QString(), QString() });
    return out;
}

} // namespace AiCudnnCompatibilityChecker
