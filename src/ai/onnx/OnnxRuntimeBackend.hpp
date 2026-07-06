#pragma once

#include "ai/runtime/AiRuntimeSettings.hpp"
#include "ai/onnx/OnnxSessionHandle.hpp"

#include <QString>
#include <QStringList>

#include <memory>

// The single point of contact with the ONNX Runtime C++ API. All knowledge of
// Ort::Env, SessionOptions and Execution Provider wiring lives here (and in the
// .cpp); tools, the pipeline and the UI never touch ONNX directly. A process
// loads exactly one ONNX Runtime — this class owns the shared Ort::Env.
class OnnxRuntimeBackend {
public:
    OnnxRuntimeBackend();
    ~OnnxRuntimeBackend();

    OnnxRuntimeBackend(const OnnxRuntimeBackend&) = delete;
    OnnxRuntimeBackend& operator=(const OnnxRuntimeBackend&) = delete;

    // True when the editor was compiled with ONNX Runtime support.
    static bool isCompiledIn();

    // The ONNX Runtime version the headers were built against.
    static QString headerVersion();

    // Lazily creates the shared Ort::Env. Safe to call repeatedly. Returns
    // false and fills *error if the runtime cannot be initialized.
    bool initialize(QString* error = nullptr);
    bool isInitialized() const;
    void shutdown();

    QString runtimeVersion() const;        // version reported by the loaded runtime
    QString runtimeLibraryPath() const;    // best-effort path of libonnxruntime
    QStringList availableProviders() const; // provider ids reported by the runtime

    // Creates a session for a single .onnx file using the requested provider.
    // Honours allowCpuFallback: on provider failure it retries on CPU when the
    // setting permits, recording the provider actually used in *actualProviderId.
    // Returns nullptr and fills *error on failure; never throws.
    std::shared_ptr<OnnxSessionHandle> createSession(const QString& modelPath,
                                                      const QString& requestedProviderId,
                                                      const AiRuntimeSettings& settings,
                                                      QString* actualProviderId,
                                                      QString* error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
