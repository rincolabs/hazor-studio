// System > Runtime / Performance / Diagnostics. Every read and write goes
// through AiRuntimeManager::instance() — never AiRuntimeSettings::load()/save()
// or QSettings directly — so the existing session-reload-on-change and
// fallback-tracking logic in AiRuntimeManager::setSettings() keeps firing for
// changes made from here exactly as it does for every other caller.
#include "AiCenterBridge.hpp"

#include "ai/AiJobRunner.hpp"
#include "ai/models/AiModelDescriptor.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ui/AppDiagnostics.hpp"

namespace {

QString workingResolutionToJs(AiWorkingResolution value)
{
    switch (value) {
    case AiWorkingResolution::PreserveDocument:  return QStringLiteral("preserveDocument");
    case AiWorkingResolution::LimitLongestSide:  return QStringLiteral("limitLongestSide");
    case AiWorkingResolution::Auto:              return QStringLiteral("auto");
    }
    return QStringLiteral("auto");
}

AiWorkingResolution workingResolutionFromJs(const QString& value)
{
    if (value == QStringLiteral("preserveDocument")) return AiWorkingResolution::PreserveDocument;
    if (value == QStringLiteral("limitLongestSide")) return AiWorkingResolution::LimitLongestSide;
    return AiWorkingResolution::Auto;
}

QString runtimeStatusToJs(AiRuntimeStatus status)
{
    switch (status) {
    case AiRuntimeStatus::NotCompiled:    return QStringLiteral("notCompiled");
    case AiRuntimeStatus::Disabled:       return QStringLiteral("disabled");
    case AiRuntimeStatus::NotInitialized: return QStringLiteral("notInitialized");
    case AiRuntimeStatus::Ready:          return QStringLiteral("ready");
    case AiRuntimeStatus::Error:          return QStringLiteral("error");
    }
    return QStringLiteral("notCompiled");
}

} // namespace

QVariantMap AiCenterBridge::toVariant(const AiRuntimeSettings& s) const
{
    QVariantMap v;
    v[QStringLiteral("enabled")] = s.enabled;
    v[QStringLiteral("executionProvider")] = s.executionProvider;
    v[QStringLiteral("cpuThreadCount")] = s.cpuThreadCount;
    v[QStringLiteral("useFp16WhenAvailable")] = s.useFp16WhenAvailable;
    v[QStringLiteral("enableGraphOptimization")] = s.enableGraphOptimization;
    v[QStringLiteral("enableMemoryArena")] = s.enableMemoryArena;
    v[QStringLiteral("enableMemoryPattern")] = s.enableMemoryPattern;
    v[QStringLiteral("maxGpuMemoryMB")] = s.maxGpuMemoryMB;
    v[QStringLiteral("maxCpuMemoryMB")] = s.maxCpuMemoryMB;
    v[QStringLiteral("maxConcurrentAiJobs")] = s.maxConcurrentAiJobs;
    v[QStringLiteral("maxLoadedModels")] = s.maxLoadedModels;
    v[QStringLiteral("allowCpuFallback")] = s.allowCpuFallback;
    v[QStringLiteral("preloadModelsOnStartup")] = s.preloadModelsOnStartup;
    v[QStringLiteral("embeddingCacheEnabled")] = s.embeddingCacheEnabled;
    v[QStringLiteral("maxCachedDocuments")] = s.maxCachedDocuments;
    v[QStringLiteral("maxCachedEmbeddingsMB")] = s.maxCachedEmbeddingsMB;
    v[QStringLiteral("workingResolution")] = workingResolutionToJs(s.workingResolution);
    v[QStringLiteral("workingResolutionLongestSide")] = s.workingResolutionLongestSide;
    return v;
}

QVariantMap AiCenterBridge::runtimeStateVariant() const
{
    auto* manager = AiRuntimeManager::instance();
    const AiRuntimeDiagnostic diag = manager->diagnostic();

    QVariantMap diagVariant;
    diagVariant[QStringLiteral("status")] = runtimeStatusToJs(diag.status);
    diagVariant[QStringLiteral("runtimeInstalled")] = diag.runtimeInstalled;
    diagVariant[QStringLiteral("runtimeVersion")] = diag.runtimeVersion;
    diagVariant[QStringLiteral("loadedFrom")] = diag.loadedFrom;
    diagVariant[QStringLiteral("selectedProvider")] = diag.selectedProvider;
    diagVariant[QStringLiteral("activeProvider")] = diag.activeProvider;
    diagVariant[QStringLiteral("lastFallback")] = diag.lastFallback;
    diagVariant[QStringLiteral("lastError")] = diag.lastError;
    diagVariant[QStringLiteral("loadedSessions")] = diag.loadedSessions;

    QVariantList providers;
    for (const AiExecutionProviderInfo& info : manager->availableProviders()) {
        QVariantMap p;
        p[QStringLiteral("id")] = info.id;
        p[QStringLiteral("displayName")] = info.displayName;
        p[QStringLiteral("supportedByPlatform")] = info.supportedByPlatform;
        p[QStringLiteral("compiledInRuntime")] = info.compiledInRuntime;
        p[QStringLiteral("dependenciesAvailable")] = info.dependenciesAvailable;
        p[QStringLiteral("sessionCreationWorks")] = info.sessionCreationWorks;
        p[QStringLiteral("status")] = info.status;
        p[QStringLiteral("errorMessage")] = info.errorMessage;
        providers.append(p);
    }

    int bundled = 0, downloaded = 0, custom = 0, invalid = 0;
    for (const AiModelDescriptor& model : AiModelRegistry::instance()->allModels()) {
        if (model.status != AiModelStatus::Valid) {
            ++invalid;
            continue;
        }
        switch (model.origin) {
        case AiModelOrigin::Bundled:    ++bundled; break;
        case AiModelOrigin::Downloaded: ++downloaded; break;
        case AiModelOrigin::Custom:     ++custom; break;
        }
    }

    const AppDiagnostics app = AppDiagnostics::collect();

    QVariantMap state;
    state[QStringLiteral("settings")] = toVariant(manager->settings());
    state[QStringLiteral("diagnostic")] = diagVariant;
    state[QStringLiteral("availableProviders")] = providers;
    state[QStringLiteral("cachedEmbeddingCount")] = AiJobRunner::instance()->cachedEmbeddingCount();
    state[QStringLiteral("cachedEmbeddingBytes")] = AiJobRunner::instance()->cachedEmbeddingBytes();
    state[QStringLiteral("onnxRuntimeVersion")] = diag.runtimeVersion;
    state[QStringLiteral("osName")] = app.osName;
    state[QStringLiteral("cpuArchitecture")] = app.cpuArchitecture;
    state[QStringLiteral("bundledModelCount")] = bundled;
    state[QStringLiteral("downloadedModelCount")] = downloaded;
    state[QStringLiteral("customModelCount")] = custom;
    state[QStringLiteral("invalidModelCount")] = invalid;
    return state;
}

QVariantMap AiCenterBridge::runtimeState() const
{
    return runtimeStateVariant();
}

QVariantMap AiCenterBridge::applyRuntimeSettings(const QVariantMap& patch)
{
    auto* manager = AiRuntimeManager::instance();
    AiRuntimeSettings s = manager->settings();

    if (patch.contains(QStringLiteral("enabled")))
        s.enabled = patch.value(QStringLiteral("enabled")).toBool();
    if (patch.contains(QStringLiteral("executionProvider")))
        s.executionProvider = patch.value(QStringLiteral("executionProvider")).toString();
    if (patch.contains(QStringLiteral("cpuThreadCount")))
        s.cpuThreadCount = patch.value(QStringLiteral("cpuThreadCount")).toInt();
    if (patch.contains(QStringLiteral("useFp16WhenAvailable")))
        s.useFp16WhenAvailable = patch.value(QStringLiteral("useFp16WhenAvailable")).toBool();
    if (patch.contains(QStringLiteral("enableGraphOptimization")))
        s.enableGraphOptimization = patch.value(QStringLiteral("enableGraphOptimization")).toBool();
    if (patch.contains(QStringLiteral("enableMemoryArena")))
        s.enableMemoryArena = patch.value(QStringLiteral("enableMemoryArena")).toBool();
    if (patch.contains(QStringLiteral("enableMemoryPattern")))
        s.enableMemoryPattern = patch.value(QStringLiteral("enableMemoryPattern")).toBool();
    if (patch.contains(QStringLiteral("maxGpuMemoryMB")))
        s.maxGpuMemoryMB = patch.value(QStringLiteral("maxGpuMemoryMB")).toInt();
    if (patch.contains(QStringLiteral("maxCpuMemoryMB")))
        s.maxCpuMemoryMB = patch.value(QStringLiteral("maxCpuMemoryMB")).toInt();
    if (patch.contains(QStringLiteral("maxConcurrentAiJobs")))
        s.maxConcurrentAiJobs = patch.value(QStringLiteral("maxConcurrentAiJobs")).toInt();
    if (patch.contains(QStringLiteral("maxLoadedModels")))
        s.maxLoadedModels = patch.value(QStringLiteral("maxLoadedModels")).toInt();
    if (patch.contains(QStringLiteral("allowCpuFallback")))
        s.allowCpuFallback = patch.value(QStringLiteral("allowCpuFallback")).toBool();
    if (patch.contains(QStringLiteral("preloadModelsOnStartup")))
        s.preloadModelsOnStartup = patch.value(QStringLiteral("preloadModelsOnStartup")).toBool();
    if (patch.contains(QStringLiteral("embeddingCacheEnabled")))
        s.embeddingCacheEnabled = patch.value(QStringLiteral("embeddingCacheEnabled")).toBool();
    if (patch.contains(QStringLiteral("maxCachedDocuments")))
        s.maxCachedDocuments = patch.value(QStringLiteral("maxCachedDocuments")).toInt();
    if (patch.contains(QStringLiteral("maxCachedEmbeddingsMB")))
        s.maxCachedEmbeddingsMB = patch.value(QStringLiteral("maxCachedEmbeddingsMB")).toInt();
    if (patch.contains(QStringLiteral("workingResolution")))
        s.workingResolution = workingResolutionFromJs(patch.value(QStringLiteral("workingResolution")).toString());
    if (patch.contains(QStringLiteral("workingResolutionLongestSide")))
        s.workingResolutionLongestSide = patch.value(QStringLiteral("workingResolutionLongestSide")).toInt();

    manager->setSettings(s); // persists, and reloads sessions when requiresSessionReload() says so
    return runtimeStateVariant();
}

QVariantMap AiCenterBridge::clearAiCache()
{
    AiRuntimeManager::instance()->clearCache();
    return runtimeStateVariant();
}
