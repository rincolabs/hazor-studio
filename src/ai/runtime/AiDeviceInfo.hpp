#pragma once

#include <QString>
#include <QStringList>

// Snapshot of the AI inference environment, used for diagnostics and to drive
// the Settings runtime section. Purely descriptive — no ONNX types leak here.
struct AiDeviceInfo {
    bool onnxRuntimeAvailable = false;   // compiled in AND able to initialize
    QString onnxRuntimeVersion;
    QString runtimeLibraryPath;          // where libonnxruntime was loaded from

    QStringList compiledProviders;       // provider ids compiled into the runtime
    QStringList availableProviders;      // provider ids usable right now

    bool hasCuda     = false;
    bool hasOpenVino = false;
    bool hasDirectMl = false;
    bool hasTensorRt = false;
    bool hasCoreMl   = false;

    QString gpuName;
    int detectedGpuMemoryMB = 0;
};
