#pragma once

#include <QString>
#include <QStringList>

// Centralized snapshot of the application environment.
// Collected synchronously and cheaply — no process launches.
// Reusable by bug reports, support screens, or log headers.
struct AppDiagnostics {
    // Application
    QString appName;
    QString appVersion;
    QString buildDate;

    // System
    QString osName;
    QString cpuArchitecture;

    // Libraries
    QString qtVersion;
    QString opencvVersion;

    // ONNX Runtime
    bool        onnxCompiledIn        = false;
    QString     onnxVersion;
    QStringList onnxAvailableProviders; // display names, e.g. "CPU", "CUDA / NVIDIA"
    QString     onnxSelectedProvider;   // display name of the configured provider

    // ONNX Models
    int modelsFound = 0;
    int modelsTotal = 0;

    // Real-ESRGAN
    QString esrganBinaryPath;
    bool    esrganBinaryFound = false;
    bool    esrganModelsFound = false;

    // Paths (safe to display, no credentials)
    QString modelsPath;
    QString cachePath;
    QString configPath;

    // GPU (populated by AiRuntimeManager when available)
    QString gpuName;

    // Collect a fresh snapshot. Fast, no async work, no process launches.
    static AppDiagnostics collect();

    // Plain-text report suitable for clipboard / bug reports.
    QString toText() const;
};
