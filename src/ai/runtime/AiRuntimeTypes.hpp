#pragma once

#include <QString>

// Overall state of the AI runtime, surfaced in the Settings diagnostics area.
enum class AiRuntimeStatus {
    NotCompiled,     // built without ONNX Runtime support
    Disabled,        // compiled in, but AI features turned off by the user
    NotInitialized,  // enabled, runtime not yet loaded
    Ready,           // runtime loaded and usable
    Error            // failed to initialize / last operation errored
};

inline QString aiRuntimeStatusLabel(AiRuntimeStatus status)
{
    switch (status) {
    case AiRuntimeStatus::NotCompiled:    return QStringLiteral("Not available in this build");
    case AiRuntimeStatus::Disabled:       return QStringLiteral("Disabled");
    case AiRuntimeStatus::NotInitialized: return QStringLiteral("Not initialized");
    case AiRuntimeStatus::Ready:          return QStringLiteral("Ready");
    case AiRuntimeStatus::Error:          return QStringLiteral("Error");
    }
    return QString();
}

// Human-readable diagnostic block (Settings > AI > Runtime diagnostics).
struct AiRuntimeDiagnostic {
    AiRuntimeStatus status = AiRuntimeStatus::NotCompiled;

    bool runtimeInstalled = false;
    QString runtimeVersion;
    QString loadedFrom;

    QString selectedProvider;   // what the user picked (or "auto")
    QString activeProvider;     // what was actually used last
    QString lastFallback;       // "None" or e.g. "CPU fallback used"
    QString lastError;

    int loadedSessions = 0;
};
