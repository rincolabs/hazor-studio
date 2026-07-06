#pragma once

#include <QString>
#include <QtGlobal>

// Stable identifiers for the ONNX Runtime Execution Providers the editor knows
// about. There is a single editor build: the choice between these is purely a
// runtime/Settings concern, never a separate binary. Not all providers are
// available on every platform or in every runtime package — availability is
// resolved at runtime and surfaced to the user, the architecture stays open.
namespace AiProvider {

inline constexpr char kAuto[]      = "auto";
inline constexpr char kCpu[]       = "cpu";
inline constexpr char kCuda[]      = "cuda";
inline constexpr char kOpenVino[]  = "openvino";
inline constexpr char kDirectMl[]  = "directml";
inline constexpr char kTensorRt[]  = "tensorrt";
inline constexpr char kCoreMl[]    = "coreml";

// Human-readable label for a provider id (e.g. "cuda" -> "CUDA / NVIDIA").
inline QString displayName(const QString& providerId)
{
    if (providerId == QLatin1String(kAuto))     return QStringLiteral("Auto");
    if (providerId == QLatin1String(kCpu))      return QStringLiteral("CPU");
    if (providerId == QLatin1String(kCuda))     return QStringLiteral("CUDA / NVIDIA");
    if (providerId == QLatin1String(kOpenVino)) return QStringLiteral("OpenVINO / Intel");
    if (providerId == QLatin1String(kDirectMl)) return QStringLiteral("DirectML / Windows");
    if (providerId == QLatin1String(kTensorRt)) return QStringLiteral("TensorRT / NVIDIA Advanced");
    if (providerId == QLatin1String(kCoreMl))   return QStringLiteral("CoreML / macOS");
    return providerId;
}

// Maps a provider id to the ONNX Runtime provider name reported by
// Ort::GetAvailableProviders() (e.g. "cuda" -> "CUDAExecutionProvider").
inline QString onnxProviderName(const QString& providerId)
{
    if (providerId == QLatin1String(kCpu))      return QStringLiteral("CPUExecutionProvider");
    if (providerId == QLatin1String(kCuda))     return QStringLiteral("CUDAExecutionProvider");
    if (providerId == QLatin1String(kOpenVino)) return QStringLiteral("OpenVINOExecutionProvider");
    if (providerId == QLatin1String(kDirectMl)) return QStringLiteral("DmlExecutionProvider");
    if (providerId == QLatin1String(kTensorRt)) return QStringLiteral("TensorrtExecutionProvider");
    if (providerId == QLatin1String(kCoreMl))   return QStringLiteral("CoreMLExecutionProvider");
    return QString();
}

// True when the provider could conceivably run on the current platform
// (e.g. DirectML only on Windows, CoreML only on macOS).
inline bool supportedOnThisPlatform(const QString& providerId)
{
    if (providerId == QLatin1String(kDirectMl)) {
#if defined(Q_OS_WIN)
        return true;
#else
        return false;
#endif
    }
    if (providerId == QLatin1String(kCoreMl)) {
#if defined(Q_OS_MACOS)
        return true;
#else
        return false;
#endif
    }
    // CPU/CUDA/OpenVINO/TensorRT/Auto are conceptually cross-platform.
    return true;
}

} // namespace AiProvider

// Per-provider availability snapshot, computed by AiRuntimeManager and shown in
// the Settings runtime section.
struct AiExecutionProviderInfo {
    QString id;                       // AiProvider::kCpu etc.
    QString displayName;

    bool supportedByPlatform = false; // OS could host this provider
    bool compiledInRuntime  = false;  // present in the loaded ONNX Runtime
    bool dependenciesAvailable = false; // external deps (CUDA/cuDNN/...) present
    bool sessionCreationWorks  = false; // a session was successfully created with it

    QString status;                   // short user-facing status line
    QString errorMessage;             // technical detail for diagnostics/logs
};
