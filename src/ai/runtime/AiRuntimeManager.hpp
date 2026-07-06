#pragma once

#include "ai/runtime/AiRuntimeSettings.hpp"
#include "ai/runtime/AiExecutionProvider.hpp"
#include "ai/runtime/AiDeviceInfo.hpp"
#include "ai/runtime/AiRuntimeTypes.hpp"
#include "ai/runtime/AiInferenceSession.hpp"
#include "ai/models/AiModelTypes.hpp"
#include "ai/models/AiModelDescriptor.hpp"

#include <QObject>
#include <QHash>
#include <QList>

#include <memory>

class OnnxRuntimeBackend;
class AiSessionBundle;

// Central authority for AI inference. Encapsulates the ONNX Runtime backend,
// owns runtime settings, resolves Execution Providers (with CPU fallback),
// creates/caches inference sessions and exposes diagnostics. Nothing in the app
// talks to ONNX Runtime except through this class and the onnx/ layer it owns.
// The UI never sees a tensor; tools never pick a backend.
class AiRuntimeManager : public QObject {
    Q_OBJECT
public:
    static AiRuntimeManager* instance();
    ~AiRuntimeManager() override;

    // Compiled with ONNX Runtime support.
    bool isRuntimeAvailable() const;
    // Runtime available AND the user enabled AI features.
    bool isAiEnabled() const;

    AiRuntimeSettings settings() const { return m_settings; }
    void setSettings(const AiRuntimeSettings& settings);

    QString selectedExecutionProvider() const { return m_settings.executionProvider; }
    QString activeExecutionProvider() const { return m_activeProvider; }

    // Availability of every known provider (for the Settings runtime section).
    QList<AiExecutionProviderInfo> availableProviders() const;
    AiDeviceInfo deviceInfo() const;
    AiRuntimeStatus status() const;
    AiRuntimeDiagnostic diagnostic() const;

    // Lazily initialises the ONNX Runtime. Returns false (with *error) if it
    // cannot be loaded or AI is disabled.
    bool ensureInitialized(QString* error = nullptr);

    // Builds (or returns a cached) inference session for an installed model.
    // Heavy on first load — callers should run it from an async job.
    std::shared_ptr<AiInferenceSession> createSession(const AiInstalledModel& model,
                                                      QString* error = nullptr);

    // As above, but forcing a specific Execution Provider (e.g. "cpu" for the
    // CUDA-OOM fallback). An empty forcedProvider behaves like the overload above.
    std::shared_ptr<AiInferenceSession> createSession(const AiInstalledModel& model,
                                                      const QString& forcedProvider,
                                                      QString* error);

    // Loads one named ONNX component from a logical model. Used by multi-session
    // pipelines such as Stable Diffusion without changing the single-session
    // contract used by SAM, matting and direct inpainting.
    std::shared_ptr<AiInferenceSession> getOrCreateSession(const AiModelDescriptor& model,
                                                           const QString& componentName,
                                                           const QString& onnxPath,
                                                           QString* error = nullptr);
    std::shared_ptr<AiSessionBundle> getOrCreateBundle(const AiModelDescriptor& model,
                                                       QString* error = nullptr);

    // Records that a CUDA out-of-memory was hit during Run(): subsequent sessions
    // prefer CPU until the settings are reloaded or the cache is cleared, so the
    // app does not keep re-hitting the same OOM. Surfaced in diagnostic().
    void noteCudaOomFallback();
    bool cudaOomFallbackActive() const { return m_cudaOomFallback; }

    void unloadSession(const QString& modelId);
    void unloadAllSessions();
    int loadedSessionCount() const { return m_sessions.size(); }

    // Frees loaded sessions and provider-dependent caches (the embedding cache
    // is wired in a later stage). Exposed as "Clear AI Cache" in Settings.
    void clearCache();

signals:
    void settingsChanged();
    void runtimeStatusChanged();
    void providerChanged();
    void sessionsCleared();

private:
    explicit AiRuntimeManager(QObject* parent = nullptr);

    // Drops least-recently-used sessions that nothing else still references until
    // the loaded count is within maxLoadedModels (a soft cap — in-use sessions are
    // never force-evicted). Called after a new session is cached.
    void enforceSessionBudget();
    std::shared_ptr<AiInferenceSession> createSessionFromFiles(const QString& cacheKey,
                                                               const QString& modelId,
                                                               const QString& directory,
                                                               const QList<AiModelFile>& files,
                                                               const QString& forcedProvider,
                                                               QString* error);

    std::unique_ptr<OnnxRuntimeBackend> m_backend;
    AiRuntimeSettings m_settings;
    QHash<QString, std::shared_ptr<AiInferenceSession>> m_sessions;
    QHash<QString, quint64> m_sessionUse;   // session cache key → last-use clock (LRU)
    quint64 m_sessionClock = 0;

    QString m_activeProvider;     // provider used by the most recent session
    QString m_lastFallback;       // "None" or a description of a fallback
    QString m_lastError;
    bool m_cudaOomFallback = false; // sticky CPU preference after a CUDA OOM
};
