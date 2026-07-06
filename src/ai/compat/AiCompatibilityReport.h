#pragma once

#include <QString>
#include <QStringList>
#include <QList>

// Data-driven compatibility model for the AI features. These are plain value
// types — no ONNX/CUDA types leak here. They are produced by the checkers in
// src/ai/compat (AiPlatformDetector, AiRuntimeCompatibilityProfile,
// AiProviderCompatibilityChecker, AiCuda/CudnnCompatibilityChecker,
// AiModelCompatibilityChecker) and aggregated by AiCompatibilityManager. The
// Settings dialog and the AI Object Selection tool consult this layer instead of
// doing their own platform/provider/CUDA checks.

// How serious a single compatibility message is.
enum class AiCompatibilitySeverity {
    Ok,
    Info,
    Warning,
    Error
};

// Status of a checked subject (platform, runtime, a provider, the model set, or
// the whole AI feature).
enum class AiCompatibilityStatus {
    Compatible,
    PartiallyCompatible,
    MissingDependency,
    Incompatible,
    Unsupported,
    Disabled,
    Unknown
};

QString aiCompatibilityStatusLabel(AiCompatibilityStatus status);

// One actionable message attached to a checked subject. `actionId` is a stable
// token the UI maps to a concrete action (e.g. "open_ai_settings").
struct AiCompatibilityMessage {
    AiCompatibilitySeverity severity = AiCompatibilitySeverity::Info;
    QString title;
    QString message;
    QString details;
    QString actionLabel;
    QString actionId;
};

// Raw platform facts gathered by AiPlatformDetector (before classification).
struct AiPlatformInfo {
    QString platform;       // "linux" | "windows" | "macos"
    QString osName;         // PRETTY_NAME / product name
    QString osVersion;      // VERSION_ID / product version
    QString distroId;       // os-release ID (linux only)
    QString distroVersion;  // os-release VERSION_ID (linux only)
    QString architecture;   // "x86_64" | "arm64" | ...
};

// Platform classification (officially supported / experimental / untested).
struct AiPlatformCompatibility {
    QString osName;
    QString osVersion;
    QString architecture;

    bool officiallySupported = false;
    bool experimental = false;

    AiCompatibilityStatus status = AiCompatibilityStatus::Unknown;
    QList<AiCompatibilityMessage> messages;
};

// ONNX Runtime availability snapshot.
struct AiRuntimeCompatibility {
    bool onnxRuntimeAvailable = false;
    QString onnxRuntimeVersion;
    QString runtimePath;

    AiCompatibilityStatus status = AiCompatibilityStatus::Unknown;
    QList<AiCompatibilityMessage> messages;
};

// Per-provider compatibility, computed from platform support, runtime
// availability and (for CUDA) the CUDA/cuDNN probe + requirement table.
struct AiProviderCompatibility {
    QString id;           // cpu, cuda, openvino, directml, coreml, tensorrt
    QString displayName;  // "CPU", "CUDA / NVIDIA", ...

    bool supportedByOs = false;
    bool availableInRuntime = false;
    bool dependenciesAvailable = false;
    bool compatibleWithHardware = false;
    bool compatibleWithRuntime = false;

    AiCompatibilityStatus status = AiCompatibilityStatus::Unknown;

    QString reason;     // short "why" for the status
    QString details;    // longer free text (required/detected lines)

    QList<AiCompatibilityMessage> messages;

    // Only providers that pass every gate are offered in the selectable combobox.
    // CPU is always selectable (the universal fallback) — see the checker.
    bool isSelectable() const
    {
        return supportedByOs
            && availableInRuntime
            && dependenciesAvailable
            && compatibleWithHardware
            && compatibleWithRuntime
            && status == AiCompatibilityStatus::Compatible;
    }
};

// Installed-model summary, grouped by origin and capability.
struct AiModelCompatibility {
    int validModels = 0;
    int invalidModels = 0;
    int bundledModels = 0;
    int downloadedModels = 0;
    int customModels = 0;

    bool hasObjectSelectionModel = false;
    bool hasMattingModel = false;
    bool hasBackgroundRemovalModel = false;

    AiCompatibilityStatus status = AiCompatibilityStatus::Unknown;
    QList<AiCompatibilityMessage> messages;
};

// Full report aggregated by AiCompatibilityManager::buildReport().
struct AiCompatibilityReport {
    AiPlatformCompatibility platform;
    AiRuntimeCompatibility runtime;
    QList<AiProviderCompatibility> providers;
    AiModelCompatibility models;

    AiCompatibilityStatus globalStatus = AiCompatibilityStatus::Unknown;
    QList<AiCompatibilityMessage> messages;

    const AiProviderCompatibility* provider(const QString& id) const
    {
        for (const AiProviderCompatibility& p : providers)
            if (p.id == id)
                return &p;
        return nullptr;
    }
};

// Probed CUDA stack facts (best-effort, per platform).
struct AiCudaInfo {
    bool driverDetected = false;       // libcuda / nvidia-smi present
    bool cudaRuntimeDetected = false;  // libcudart present
    bool cublasDetected = false;       // libcublas present
    bool cudnnDetected = false;        // libcudnn present
    bool gpuPresent = false;           // an NVIDIA GPU was found

    QString driverVersion;             // e.g. "550.90.07"
    QString cudaDriverApiVersion;      // CUDA version reported by the driver
    QString cudaRuntimeMajor;          // major from libcudart soname
    QString cudnnMajor;                // major from libcudnn soname
    QString gpuName;
    QString computeCapability;
};

// One row of the modular runtime/provider requirement table (spec §7). Lets the
// app state what the *currently shipped* runtime needs and add new rules later
// (CUDA 13, TensorRT, OpenVINO…) without touching the checkers.
struct AiRuntimeRequirement {
    QString runtimeId;       // "onnxruntime-cuda12"
    QString providerId;      // "cuda"
    QString cudaMajor;       // "12"
    QString cudaMinVersion;  // "12.0"
    QString cudaMaxVersion;  // "12.x"
    QString cudnnMajor;      // "9"
    QString recommendedCudaPackage;
    QString recommendedCudnnPackage;
};
