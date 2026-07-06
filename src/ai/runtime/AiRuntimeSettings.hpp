#pragma once

#include <QString>

// Working-resolution policy for inference input. Limiting the longest side
// trades precision for speed/memory; the UI explains this to the user.
enum class AiWorkingResolution {
    Auto = 0,
    PreserveDocument,
    LimitLongestSide
};

// All user-configurable AI runtime preferences. Persisted to QSettings under the
// "ai/" group. Defaults prioritise stability over raw performance.
struct AiRuntimeSettings {
    bool enabled = false;

    // Execution provider id (see AiProvider::). "auto" picks the best available.
    QString executionProvider = QStringLiteral("auto");

    // ── Performance ──
    int cpuThreadCount = 0;          // 0 = automatic
    bool useFp16WhenAvailable   = true;
    bool enableGraphOptimization = true;
    bool enableMemoryArena   = true;
    bool enableMemoryPattern = true;

    int maxGpuMemoryMB = 0;          // 0 = automatic (app preference, soft limit)
    int maxCpuMemoryMB = 0;          // 0 = automatic
    int maxConcurrentAiJobs = 1;     // serialise heavy jobs by default
    int maxLoadedModels = 2;         // LRU cap on concurrently-loaded ONNX sessions

    bool allowCpuFallback = true;
    bool preloadModelsOnStartup = false;

    // ── Embedding cache (used from Etapa 2) ──
    bool embeddingCacheEnabled = true;
    int maxCachedDocuments = 2;
    int maxCachedEmbeddingsMB = 0;   // 0 = automatic

    // ── Working resolution ──
    AiWorkingResolution workingResolution = AiWorkingResolution::Auto;
    int workingResolutionLongestSide = 1024;

    // ── Storage / source ──
    QString modelsDirectory;         // empty = default (AppPaths models subdir)
    QString manifestUrl;             // optional remote manifest override

    // Resolves modelsDirectory, falling back to the default app data location.
    QString resolvedModelsDirectory() const;

    // Persistence (QSettings, "ai/" group).
    static AiRuntimeSettings load();
    void save() const;

    // Returns true when a settings change requires loaded ONNX sessions and
    // provider-dependent caches to be invalidated.
    bool requiresSessionReload(const AiRuntimeSettings& other) const;
};
