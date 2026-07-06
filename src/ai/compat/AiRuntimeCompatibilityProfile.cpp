#include "ai/compat/AiRuntimeCompatibilityProfile.hpp"

#include "ai/runtime/AiRuntimeManager.hpp"

#include <QCoreApplication>

namespace AiRuntimeCompatibilityProfile {

namespace {
QString tr(const char* s) { return QCoreApplication::translate("AiRuntimeCompatibility", s); }
} // namespace

AiRuntimeCompatibility check()
{
    AiRuntimeCompatibility result;
    auto* rt = AiRuntimeManager::instance();

    if (!rt->isRuntimeAvailable()) {
        result.status = AiCompatibilityStatus::MissingDependency;
        result.messages.push_back({
            AiCompatibilitySeverity::Error,
            tr("ONNX Runtime is not available"),
            tr("AI features require ONNX Runtime, which is not present in this build."),
            tr("Install or configure ONNX Runtime, then restart the editor."),
            tr("AI Settings"),
            QStringLiteral("open_ai_settings") });
        return result;
    }

    const AiDeviceInfo dev = rt->deviceInfo();
    result.onnxRuntimeAvailable = true;
    result.onnxRuntimeVersion = dev.onnxRuntimeVersion;
    result.runtimePath = dev.runtimeLibraryPath;
    result.status = AiCompatibilityStatus::Compatible;
    result.messages.push_back({
        AiCompatibilitySeverity::Ok,
        tr("ONNX Runtime loaded"),
        result.onnxRuntimeVersion.isEmpty()
            ? tr("ONNX Runtime is available.")
            : tr("ONNX Runtime %1 is loaded.").arg(result.onnxRuntimeVersion),
        result.runtimePath, QString(), QString() });
    return result;
}

} // namespace AiRuntimeCompatibilityProfile
