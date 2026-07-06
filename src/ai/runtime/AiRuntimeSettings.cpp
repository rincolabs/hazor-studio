#include "AiRuntimeSettings.hpp"

#include "core/AppPaths.hpp"

#include <QSettings>

QString AiRuntimeSettings::resolvedModelsDirectory() const
{
    if (!modelsDirectory.trimmed().isEmpty())
        return modelsDirectory;
    return AppPaths::subDir(QStringLiteral("models"));
}

AiRuntimeSettings AiRuntimeSettings::load()
{
    AiRuntimeSettings s;
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai"));

    s.enabled                 = settings.value(QStringLiteral("enabled"), s.enabled).toBool();
    s.executionProvider       = settings.value(QStringLiteral("executionProvider"), s.executionProvider).toString();

    s.cpuThreadCount          = settings.value(QStringLiteral("cpuThreadCount"), s.cpuThreadCount).toInt();
    s.useFp16WhenAvailable    = settings.value(QStringLiteral("useFp16WhenAvailable"), s.useFp16WhenAvailable).toBool();
    s.enableGraphOptimization = settings.value(QStringLiteral("enableGraphOptimization"), s.enableGraphOptimization).toBool();
    s.enableMemoryArena       = settings.value(QStringLiteral("enableMemoryArena"), s.enableMemoryArena).toBool();
    s.enableMemoryPattern     = settings.value(QStringLiteral("enableMemoryPattern"), s.enableMemoryPattern).toBool();

    s.maxGpuMemoryMB          = settings.value(QStringLiteral("maxGpuMemoryMB"), s.maxGpuMemoryMB).toInt();
    s.maxCpuMemoryMB          = settings.value(QStringLiteral("maxCpuMemoryMB"), s.maxCpuMemoryMB).toInt();
    s.maxConcurrentAiJobs     = settings.value(QStringLiteral("maxConcurrentAiJobs"), s.maxConcurrentAiJobs).toInt();
    s.maxLoadedModels         = settings.value(QStringLiteral("maxLoadedModels"), s.maxLoadedModels).toInt();

    s.allowCpuFallback        = settings.value(QStringLiteral("allowCpuFallback"), s.allowCpuFallback).toBool();
    s.preloadModelsOnStartup  = settings.value(QStringLiteral("preloadModelsOnStartup"), s.preloadModelsOnStartup).toBool();

    s.embeddingCacheEnabled   = settings.value(QStringLiteral("embeddingCacheEnabled"), s.embeddingCacheEnabled).toBool();
    s.maxCachedDocuments      = settings.value(QStringLiteral("maxCachedDocuments"), s.maxCachedDocuments).toInt();
    s.maxCachedEmbeddingsMB   = settings.value(QStringLiteral("maxCachedEmbeddingsMB"), s.maxCachedEmbeddingsMB).toInt();

    s.workingResolution       = static_cast<AiWorkingResolution>(
        settings.value(QStringLiteral("workingResolution"),
                       static_cast<int>(s.workingResolution)).toInt());
    s.workingResolutionLongestSide = settings.value(QStringLiteral("workingResolutionLongestSide"),
                                                     s.workingResolutionLongestSide).toInt();

    s.modelsDirectory         = settings.value(QStringLiteral("modelsDirectory"), s.modelsDirectory).toString();
    s.manifestUrl             = settings.value(QStringLiteral("manifestUrl"), s.manifestUrl).toString();

    settings.endGroup();

    // Clamp to safe ranges (defensive against hand-edited config files).
    if (s.cpuThreadCount < 0) s.cpuThreadCount = 0;
    if (s.maxConcurrentAiJobs < 1) s.maxConcurrentAiJobs = 1;
    if (s.maxLoadedModels < 1) s.maxLoadedModels = 1;
    if (s.maxCachedDocuments < 0) s.maxCachedDocuments = 0;
    if (s.workingResolutionLongestSide < 256) s.workingResolutionLongestSide = 256;

    return s;
}

void AiRuntimeSettings::save() const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai"));

    settings.setValue(QStringLiteral("enabled"), enabled);
    settings.setValue(QStringLiteral("executionProvider"), executionProvider);

    settings.setValue(QStringLiteral("cpuThreadCount"), cpuThreadCount);
    settings.setValue(QStringLiteral("useFp16WhenAvailable"), useFp16WhenAvailable);
    settings.setValue(QStringLiteral("enableGraphOptimization"), enableGraphOptimization);
    settings.setValue(QStringLiteral("enableMemoryArena"), enableMemoryArena);
    settings.setValue(QStringLiteral("enableMemoryPattern"), enableMemoryPattern);

    settings.setValue(QStringLiteral("maxGpuMemoryMB"), maxGpuMemoryMB);
    settings.setValue(QStringLiteral("maxCpuMemoryMB"), maxCpuMemoryMB);
    settings.setValue(QStringLiteral("maxConcurrentAiJobs"), maxConcurrentAiJobs);
    settings.setValue(QStringLiteral("maxLoadedModels"), maxLoadedModels);

    settings.setValue(QStringLiteral("allowCpuFallback"), allowCpuFallback);
    settings.setValue(QStringLiteral("preloadModelsOnStartup"), preloadModelsOnStartup);

    settings.setValue(QStringLiteral("embeddingCacheEnabled"), embeddingCacheEnabled);
    settings.setValue(QStringLiteral("maxCachedDocuments"), maxCachedDocuments);
    settings.setValue(QStringLiteral("maxCachedEmbeddingsMB"), maxCachedEmbeddingsMB);

    settings.setValue(QStringLiteral("workingResolution"), static_cast<int>(workingResolution));
    settings.setValue(QStringLiteral("workingResolutionLongestSide"), workingResolutionLongestSide);

    settings.setValue(QStringLiteral("modelsDirectory"), modelsDirectory);
    settings.setValue(QStringLiteral("manifestUrl"), manifestUrl);

    settings.endGroup();
}

bool AiRuntimeSettings::requiresSessionReload(const AiRuntimeSettings& other) const
{
    // Anything that changes how a session is built or which device it targets
    // invalidates loaded sessions and provider-dependent caches.
    return enabled != other.enabled
        || executionProvider != other.executionProvider
        || cpuThreadCount != other.cpuThreadCount
        || useFp16WhenAvailable != other.useFp16WhenAvailable
        || enableGraphOptimization != other.enableGraphOptimization
        || enableMemoryArena != other.enableMemoryArena
        || enableMemoryPattern != other.enableMemoryPattern
        || allowCpuFallback != other.allowCpuFallback
        || maxGpuMemoryMB != other.maxGpuMemoryMB
        || workingResolution != other.workingResolution
        || workingResolutionLongestSide != other.workingResolutionLongestSide
        || resolvedModelsDirectory() != other.resolvedModelsDirectory();
}
