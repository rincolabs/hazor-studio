#include "AiRuntimeManager.hpp"

#include "ai/onnx/OnnxRuntimeBackend.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/runtime/AiSessionBundle.hpp"

#include <QDebug>
#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace {

QString sessionCacheKey(const QString& modelId, const QString& componentName)
{
    if (componentName.isEmpty())
        return modelId;
    return modelId + QStringLiteral("::") + componentName;
}

QList<AiModelFile> singleComponentFile(const QString& componentName, const QString& onnxPath)
{
    AiModelFile file;
    file.type = componentName;
    file.filename = onnxPath;
    return { file };
}

QStringList stableDiffusionComponents()
{
    return {
        QStringLiteral("text_encoder"),
        QStringLiteral("unet"),
        QStringLiteral("vae_encoder"),
        QStringLiteral("vae_decoder")
    };
}

QString componentPath(const AiModelDescriptor& model, const QString& componentName)
{
    if (componentName == QLatin1String("model"))
        return model.files.model;
    if (componentName == QLatin1String("encoder"))
        return model.files.encoder;
    if (componentName == QLatin1String("decoder"))
        return model.files.decoder;
    return model.files.extraFiles.value(componentName);
}

} // namespace

AiRuntimeManager* AiRuntimeManager::instance()
{
    static AiRuntimeManager* s_instance = new AiRuntimeManager();
    return s_instance;
}

AiRuntimeManager::AiRuntimeManager(QObject* parent)
    : QObject(parent)
    , m_backend(std::make_unique<OnnxRuntimeBackend>())
    , m_settings(AiRuntimeSettings::load())
    , m_lastFallback(QStringLiteral("None"))
{
}

AiRuntimeManager::~AiRuntimeManager() = default;

bool AiRuntimeManager::isRuntimeAvailable() const
{
    return OnnxRuntimeBackend::isCompiledIn();
}

bool AiRuntimeManager::isAiEnabled() const
{
    return isRuntimeAvailable() && m_settings.enabled;
}

void AiRuntimeManager::setSettings(const AiRuntimeSettings& settings)
{
    const bool reload = m_settings.requiresSessionReload(settings);
    const QString prevProvider = m_settings.executionProvider;

    m_settings = settings;
    m_settings.save();

    // Keep the model registry pointed at the configured directory.
    AiModelRegistry::instance()->setModelsDirectory(m_settings.resolvedModelsDirectory());

    if (reload) {
        // Provider/option changes invalidate loaded sessions and provider-bound
        // caches. Pending AI jobs are cancelled by their owners on sessionsCleared.
        qInfo().noquote() << "[AI] Settings changed (reload required) — unloading sessions";
        unloadAllSessions();
        m_lastFallback = QStringLiteral("None");
        m_lastError.clear();
        m_cudaOomFallback = false;   // user changed config: give the GPU another chance
    }

    emit settingsChanged();
    if (prevProvider != m_settings.executionProvider)
        emit providerChanged();
    emit runtimeStatusChanged();
}

bool AiRuntimeManager::ensureInitialized(QString* error)
{
    if (!isRuntimeAvailable()) {
        m_lastError = QStringLiteral("ONNX Runtime is not available in this build.");
        if (error) *error = m_lastError;
        return false;
    }
    if (!m_settings.enabled) {
        m_lastError = QStringLiteral("AI features are disabled.");
        if (error) *error = m_lastError;
        return false;
    }
    QString initErr;
    if (!m_backend->initialize(&initErr)) {
        m_lastError = initErr;
        if (error) *error = initErr;
        emit runtimeStatusChanged();
        return false;
    }
    m_lastError.clear();
    return true;
}

QList<AiExecutionProviderInfo> AiRuntimeManager::availableProviders() const
{
    const QStringList runtimeProviders = isRuntimeAvailable() ? m_backend->availableProviders() : QStringList();

    static const char* kKnown[] = {
        AiProvider::kCpu, AiProvider::kCuda, AiProvider::kOpenVino,
        AiProvider::kDirectMl, AiProvider::kTensorRt
    };

    QList<AiExecutionProviderInfo> out;
    for (const char* idC : kKnown) {
        const QString id = QString::fromLatin1(idC);
        AiExecutionProviderInfo info;
        info.id = id;
        info.displayName = AiProvider::displayName(id);
        info.supportedByPlatform = AiProvider::supportedOnThisPlatform(id);
        info.compiledInRuntime = runtimeProviders.contains(id);
        info.dependenciesAvailable = info.compiledInRuntime; // best-effort
        info.sessionCreationWorks = (id == m_activeProvider);

        if (!isRuntimeAvailable())
            info.status = QStringLiteral("Not available in this build");
        else if (!info.supportedByPlatform)
            info.status = QStringLiteral("Not supported on this platform");
        else if (info.compiledInRuntime)
            info.status = info.sessionCreationWorks ? QStringLiteral("Active") : QStringLiteral("Available");
        else
            info.status = QStringLiteral("Not installed");

        out << info;
    }
    return out;
}

AiDeviceInfo AiRuntimeManager::deviceInfo() const
{
    AiDeviceInfo info;
    info.onnxRuntimeAvailable = isRuntimeAvailable();
    if (!isRuntimeAvailable())
        return info;

    info.onnxRuntimeVersion = m_backend->runtimeVersion();
    info.runtimeLibraryPath = m_backend->runtimeLibraryPath();
    info.availableProviders = m_backend->availableProviders();
    info.compiledProviders = info.availableProviders;

    info.hasCuda     = info.availableProviders.contains(QString::fromLatin1(AiProvider::kCuda));
    info.hasTensorRt = info.availableProviders.contains(QString::fromLatin1(AiProvider::kTensorRt));
    info.hasOpenVino = info.availableProviders.contains(QString::fromLatin1(AiProvider::kOpenVino));
    info.hasDirectMl = info.availableProviders.contains(QString::fromLatin1(AiProvider::kDirectMl));
    info.hasCoreMl   = info.availableProviders.contains(QString::fromLatin1(AiProvider::kCoreMl));
    return info;
}

AiRuntimeStatus AiRuntimeManager::status() const
{
    if (!isRuntimeAvailable())
        return AiRuntimeStatus::NotCompiled;
    if (!m_settings.enabled)
        return AiRuntimeStatus::Disabled;
    if (!m_lastError.isEmpty())
        return AiRuntimeStatus::Error;
    if (m_backend->isInitialized())
        return AiRuntimeStatus::Ready;
    return AiRuntimeStatus::NotInitialized;
}

AiRuntimeDiagnostic AiRuntimeManager::diagnostic() const
{
    AiRuntimeDiagnostic diag;
    diag.status = status();
    diag.runtimeInstalled = isRuntimeAvailable();
    if (isRuntimeAvailable()) {
        diag.runtimeVersion = m_backend->runtimeVersion();
        diag.loadedFrom = m_backend->runtimeLibraryPath();
    }
    diag.selectedProvider = AiProvider::displayName(m_settings.executionProvider);
    diag.activeProvider = m_activeProvider.isEmpty()
                              ? QStringLiteral("—")
                              : AiProvider::displayName(m_activeProvider);
    diag.lastFallback = m_lastFallback;
    diag.lastError = m_lastError;
    diag.loadedSessions = m_sessions.size();
    return diag;
}

std::shared_ptr<AiInferenceSession> AiRuntimeManager::createSession(const AiInstalledModel& model,
                                                                    QString* error)
{
    return createSession(model, QString(), error);
}

void AiRuntimeManager::noteCudaOomFallback()
{
    m_cudaOomFallback = true;
    m_activeProvider = QString::fromLatin1(AiProvider::kCpu);
    m_lastFallback = QStringLiteral("CPU fallback used (CUDA out of memory)");
    emit providerChanged();
    emit runtimeStatusChanged();
}

std::shared_ptr<AiInferenceSession> AiRuntimeManager::createSession(const AiInstalledModel& model,
                                                                    const QString& forcedProvider,
                                                                    QString* error)
{
    if (!model.isValid()) {
        if (error) *error = QStringLiteral("Invalid model descriptor.");
        return nullptr;
    }

    return createSessionFromFiles(model.id, model.id, model.directory, model.files,
                                  forcedProvider, error);
}

std::shared_ptr<AiInferenceSession> AiRuntimeManager::createSessionFromFiles(const QString& cacheKey,
                                                                             const QString& modelId,
                                                                             const QString& directory,
                                                                             const QList<AiModelFile>& files,
                                                                             const QString& forcedProvider,
                                                                             QString* error)
{
    if (cacheKey.isEmpty() || modelId.isEmpty() || directory.isEmpty() || files.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid model descriptor.");
        return nullptr;
    }

    if (auto it = m_sessions.constFind(cacheKey); it != m_sessions.constEnd()) {
        m_sessionUse[cacheKey] = ++m_sessionClock;
        return it.value();
    }

    if (!ensureInitialized(error))
        return nullptr;

    // The provider to request: an explicit force (OOM fallback) wins; otherwise a
    // sticky CUDA-OOM fallback pins CPU; otherwise the user's configured provider.
    const QString requestedProvider = !forcedProvider.isEmpty()
        ? forcedProvider
        : (m_cudaOomFallback ? QString::fromLatin1(AiProvider::kCpu) : m_settings.executionProvider);

    auto session = std::shared_ptr<AiInferenceSession>(new AiInferenceSession());
    session->m_modelId = modelId;

    QString actualProvider;
    bool loadedAnyHandle = false;
    for (const AiModelFile& file : files) {
        if (!file.filename.endsWith(QStringLiteral(".onnx"), Qt::CaseInsensitive))
            continue;
        const QString path = QFileInfo(file.filename).isAbsolute()
            ? file.filename
            : QDir(directory).filePath(file.filename);
        QString fileErr;
        QString fileProvider;
        auto handle = m_backend->createSession(path, requestedProvider,
                                               m_settings, &fileProvider, &fileErr);
        if (!handle) {
            m_lastError = QStringLiteral("Failed to load %1: %2").arg(file.filename, fileErr);
            if (error) *error = m_lastError;
            qWarning().noquote() << "[AI]" << m_lastError;
            return nullptr; // partial session discarded
        }
        session->m_handles.insert(file.type.isEmpty() ? QStringLiteral("model") : file.type, handle);
        actualProvider = fileProvider;
        loadedAnyHandle = true;
    }

    if (!loadedAnyHandle) {
        m_lastError = QStringLiteral("Model '%1' has no ONNX files to load.").arg(modelId);
        if (error) *error = m_lastError;
        return nullptr;
    }

    session->m_providerUsed = actualProvider;
    m_activeProvider = actualProvider;
    if (m_cudaOomFallback) {
        m_lastFallback = QStringLiteral("CPU fallback used (CUDA out of memory)");
    } else {
        m_lastFallback = (actualProvider != requestedProvider
                          && requestedProvider != QLatin1String(AiProvider::kAuto)
                          && actualProvider == QLatin1String(AiProvider::kCpu))
                             ? QStringLiteral("CPU fallback used")
                             : QStringLiteral("None");
    }
    m_lastError.clear();

    m_sessions.insert(cacheKey, session);
    m_sessionUse[cacheKey] = ++m_sessionClock;
    enforceSessionBudget();
    qInfo().noquote() << "[AI] Session ready:" << cacheKey
                      << "provider=" << actualProvider
                      << "sessions=" << m_sessions.size();

    emit providerChanged();
    emit runtimeStatusChanged();
    return session;
}

std::shared_ptr<AiInferenceSession> AiRuntimeManager::getOrCreateSession(const AiModelDescriptor& model,
                                                                         const QString& componentName,
                                                                         const QString& onnxPath,
                                                                         QString* error)
{
    if (!model.isValid()) {
        if (error) *error = QStringLiteral("Invalid model descriptor.");
        return nullptr;
    }

    const QString component = componentName.trimmed();
    if (component.isEmpty()) {
        if (error) *error = QStringLiteral("Session component name is empty.");
        return nullptr;
    }
    if (onnxPath.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("Stable Diffusion model is incomplete: missing %1 component.")
                                .arg(component);
        return nullptr;
    }

    return createSessionFromFiles(sessionCacheKey(model.id, component), model.id,
                                  model.rootDir, singleComponentFile(component, onnxPath),
                                  QString(), error);
}

std::shared_ptr<AiSessionBundle> AiRuntimeManager::getOrCreateBundle(const AiModelDescriptor& model,
                                                                     QString* error)
{
    if (!model.isValid()) {
        if (error) *error = QStringLiteral("Invalid model descriptor.");
        return nullptr;
    }

    auto bundle = std::make_shared<AiSessionBundle>();
    bundle->modelId = model.id;
    bundle->family = model.family;

    QStringList required;
    if (model.family == QLatin1String("stable_diffusion")
        || model.type == QLatin1String("stable_diffusion")) {
        required = stableDiffusionComponents();
    } else if (!model.files.model.isEmpty()) {
        required = { QStringLiteral("model") };
    } else {
        if (!model.files.encoder.isEmpty())
            required << QStringLiteral("encoder");
        if (!model.files.decoder.isEmpty())
            required << QStringLiteral("decoder");
    }

    if (required.isEmpty()) {
        if (error) *error = QStringLiteral("Model '%1' has no ONNX components to load.").arg(model.id);
        return nullptr;
    }

    if ((model.family == QLatin1String("stable_diffusion")
         || model.type == QLatin1String("stable_diffusion"))
        && std::max(1, m_settings.maxLoadedModels) < required.size()) {
        if (error) {
            *error = QStringLiteral("Stable Diffusion requires at least %1 loaded ONNX sessions. Increase Max loaded models in AI settings.")
                         .arg(required.size());
        }
        return nullptr;
    }

    for (const QString& component : required) {
        QString componentError;
        auto session = getOrCreateSession(model, component, componentPath(model, component),
                                          &componentError);
        if (!session) {
            if (error) *error = componentError;
            return nullptr;
        }
        bundle->addSession(component, session);
    }

    return bundle;
}

void AiRuntimeManager::enforceSessionBudget()
{
    const int budget = std::max(1, m_settings.maxLoadedModels);
    while (m_sessions.size() > budget) {
        // Pick the least-recently-used session that nothing else still holds
        // (use_count 1 = only our cache references it).
        QString victim;
        quint64 oldest = std::numeric_limits<quint64>::max();
        for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
            if (it.value().use_count() > 1)
                continue; // in use by a segmenter/matting model — keep it
            const quint64 used = m_sessionUse.value(it.key(), 0);
            if (used < oldest) { oldest = used; victim = it.key(); }
        }
        if (victim.isEmpty())
            break; // every loaded session is in use; respect the soft cap
        m_sessions.remove(victim);
        m_sessionUse.remove(victim);
        qInfo().noquote() << "[AI][CACHE] Evicted LRU session:" << victim
                          << "sessions=" << m_sessions.size();
    }
}

void AiRuntimeManager::unloadSession(const QString& modelId)
{
    QStringList keys;
    for (auto it = m_sessions.constBegin(); it != m_sessions.constEnd(); ++it) {
        if (it.key() == modelId || it.key().startsWith(modelId + QStringLiteral("::")))
            keys << it.key();
    }
    if (!keys.isEmpty()) {
        for (const QString& key : keys) {
            m_sessions.remove(key);
            m_sessionUse.remove(key);
        }
        qInfo().noquote() << "[AI][CACHE] Unloaded session:" << modelId;
        emit runtimeStatusChanged();
    }
}

void AiRuntimeManager::unloadAllSessions()
{
    if (m_sessions.isEmpty())
        return;
    qInfo().noquote() << "[AI][CACHE] Unloading" << m_sessions.size() << "session(s)";
    m_sessions.clear();
    m_sessionUse.clear();
    emit sessionsCleared();
    emit runtimeStatusChanged();
}

void AiRuntimeManager::clearCache()
{
    unloadAllSessions();
    m_activeProvider.clear();
    m_lastFallback = QStringLiteral("None");
    m_cudaOomFallback = false;   // let the GPU be tried again after a manual cache clear
    qInfo().noquote() << "[AI][CACHE] AI cache cleared";
}
