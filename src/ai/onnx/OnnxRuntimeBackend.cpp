#include "OnnxRuntimeBackend.hpp"
#include "ai/runtime/AiExecutionProvider.hpp"

#include <QDebug>
#include <QDateTime>
#include <QFileInfo>

#ifdef HAVE_ONNXRUNTIME
#include "OnnxSessionHandle_p.hpp"   // Ort types + OnnxSessionFactory
#include <limits>
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
#include <dlfcn.h>
#endif
#endif

struct OnnxRuntimeBackend::Impl {
#ifdef HAVE_ONNXRUNTIME
    std::unique_ptr<Ort::Env> env;
#endif
    bool initialized = false;
    QString libraryPath;
};

OnnxRuntimeBackend::OnnxRuntimeBackend()
    : d(std::make_unique<Impl>())
{
}

OnnxRuntimeBackend::~OnnxRuntimeBackend() = default;

bool OnnxRuntimeBackend::isCompiledIn()
{
#ifdef HAVE_ONNXRUNTIME
    return true;
#else
    return false;
#endif
}

QString OnnxRuntimeBackend::headerVersion()
{
#ifdef HAVE_ONNXRUNTIME
    return QStringLiteral("API %1").arg(ORT_API_VERSION);
#else
    return QString();
#endif
}

bool OnnxRuntimeBackend::isInitialized() const
{
    return d->initialized;
}

#ifdef HAVE_ONNXRUNTIME

bool OnnxRuntimeBackend::initialize(QString* error)
{
    if (d->initialized)
        return true;
    try {
        d->env = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_WARNING, "HazorStudioAI");

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
        // Best-effort: resolve where libonnxruntime was actually loaded from.
        Dl_info info;
        if (dladdr(reinterpret_cast<const void*>(&OrtGetApiBase), &info) && info.dli_fname)
            d->libraryPath = QString::fromUtf8(info.dli_fname);
#endif
        d->initialized = true;

        qInfo().noquote() << "[AI][ONNX] Header/runtime expected version:" << headerVersion();
        qInfo().noquote() << "[AI][ONNX] Loaded runtime version:" << runtimeVersion();
        qInfo().noquote() << "[AI][ONNX] Loaded from:" << (d->libraryPath.isEmpty() ? QStringLiteral("<unknown>") : d->libraryPath);
        qInfo().noquote() << "[AI][ONNX] Available providers:" << availableProviders().join(QStringLiteral(", "));
        return true;
    } catch (const Ort::Exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        qWarning() << "[AI][ONNX] Failed to initialize runtime:" << e.what();
        return false;
    } catch (const std::exception& e) {
        if (error) *error = QString::fromUtf8(e.what());
        qWarning() << "[AI][ONNX] Failed to initialize runtime:" << e.what();
        return false;
    }
}

void OnnxRuntimeBackend::shutdown()
{
    d->env.reset();
    d->initialized = false;
}

QString OnnxRuntimeBackend::runtimeVersion() const
{
    return QString::fromStdString(Ort::GetVersionString());
}

QString OnnxRuntimeBackend::runtimeLibraryPath() const
{
    return d->libraryPath;
}

QStringList OnnxRuntimeBackend::availableProviders() const
{
    QStringList ids;
    try {
        for (const std::string& ortName : Ort::GetAvailableProviders()) {
            const QString name = QString::fromStdString(ortName);
            if (name == QLatin1String("CPUExecutionProvider"))       ids << QString::fromLatin1(AiProvider::kCpu);
            else if (name == QLatin1String("CUDAExecutionProvider")) ids << QString::fromLatin1(AiProvider::kCuda);
            else if (name == QLatin1String("TensorrtExecutionProvider")) ids << QString::fromLatin1(AiProvider::kTensorRt);
            else if (name == QLatin1String("OpenVINOExecutionProvider")) ids << QString::fromLatin1(AiProvider::kOpenVino);
            else if (name == QLatin1String("DmlExecutionProvider"))  ids << QString::fromLatin1(AiProvider::kDirectMl);
            else if (name == QLatin1String("CoreMLExecutionProvider")) ids << QString::fromLatin1(AiProvider::kCoreMl);
        }
    } catch (const std::exception& e) {
        qWarning() << "[AI][ONNX] availableProviders failed:" << e.what();
    }
    if (!ids.contains(QString::fromLatin1(AiProvider::kCpu)))
        ids.prepend(QString::fromLatin1(AiProvider::kCpu)); // CPU is the universal fallback
    return ids;
}

namespace {

// Picks the best provider for "auto", honouring the documented priority. Avoids
// TensorRT as an automatic default (it needs build/cache configuration).
QString autoSelectProvider(const QStringList& available)
{
    if (available.contains(QString::fromLatin1(AiProvider::kCuda)))     return QString::fromLatin1(AiProvider::kCuda);
    if (available.contains(QString::fromLatin1(AiProvider::kOpenVino))) return QString::fromLatin1(AiProvider::kOpenVino);
    if (available.contains(QString::fromLatin1(AiProvider::kDirectMl))) return QString::fromLatin1(AiProvider::kDirectMl);
    return QString::fromLatin1(AiProvider::kCpu);
}

void configureSessionOptions(Ort::SessionOptions& options, const AiRuntimeSettings& settings)
{
    options.SetGraphOptimizationLevel(settings.enableGraphOptimization
                                          ? GraphOptimizationLevel::ORT_ENABLE_ALL
                                          : GraphOptimizationLevel::ORT_DISABLE_ALL);
    if (settings.cpuThreadCount > 0)
        options.SetIntraOpNumThreads(settings.cpuThreadCount);

    if (settings.enableMemoryPattern) options.EnableMemPattern();
    else                              options.DisableMemPattern();

    if (settings.enableMemoryArena) options.EnableCpuMemArena();
    else                            options.DisableCpuMemArena();
}

// Appends the Execution Provider for providerId. Returns false (with *reason)
// when the provider cannot be used, so the caller can try the next candidate.
bool appendProvider(Ort::SessionOptions& options,
                    const QString& providerId,
                    const QStringList& available,
                    const AiRuntimeSettings& settings,
                    QString* reason)
{
    if (providerId == QLatin1String(AiProvider::kCpu))
        return true; // default CPU EP, nothing to append

    if (!available.contains(providerId)) {
        if (reason) *reason = QStringLiteral("provider not present in this runtime");
        return false;
    }
    if (!AiProvider::supportedOnThisPlatform(providerId)) {
        if (reason) *reason = QStringLiteral("not supported on this platform");
        return false;
    }

    try {
        if (providerId == QLatin1String(AiProvider::kCuda)) {
            OrtCUDAProviderOptions cuda{};
            cuda.device_id = 0;
            cuda.gpu_mem_limit = (settings.maxGpuMemoryMB > 0)
                                     ? static_cast<size_t>(settings.maxGpuMemoryMB) * 1024ull * 1024ull
                                     : std::numeric_limits<size_t>::max();
            cuda.arena_extend_strategy = 0;
            cuda.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
            cuda.do_copy_in_default_stream = 1;
            options.AppendExecutionProvider_CUDA(cuda);
            return true;
        }
        if (providerId == QLatin1String(AiProvider::kTensorRt)) {
            OrtTensorRTProviderOptions trt{};
            trt.device_id = 0;
            options.AppendExecutionProvider_TensorRT(trt);
            // Pair TensorRT with CUDA so unsupported ops fall back on-device.
            if (available.contains(QString::fromLatin1(AiProvider::kCuda))) {
                OrtCUDAProviderOptions cuda{};
                cuda.device_id = 0;
                cuda.gpu_mem_limit = std::numeric_limits<size_t>::max();
                cuda.cudnn_conv_algo_search = OrtCudnnConvAlgoSearchExhaustive;
                cuda.do_copy_in_default_stream = 1;
                options.AppendExecutionProvider_CUDA(cuda);
            }
            return true;
        }
        if (providerId == QLatin1String(AiProvider::kOpenVino)) {
            OrtOpenVINOProviderOptions ov{};
            options.AppendExecutionProvider_OpenVINO(ov);
            return true;
        }
        // DirectML/CoreML are handled by platform support checks above; not wired
        // on this platform.
        if (reason) *reason = QStringLiteral("provider not wired on this platform");
        return false;
    } catch (const Ort::Exception& e) {
        if (reason) *reason = QString::fromUtf8(e.what());
        return false;
    } catch (const std::exception& e) {
        if (reason) *reason = QString::fromUtf8(e.what());
        return false;
    }
}

} // namespace

std::shared_ptr<OnnxSessionHandle> OnnxRuntimeBackend::createSession(const QString& modelPath,
                                                                    const QString& requestedProviderId,
                                                                    const AiRuntimeSettings& settings,
                                                                    QString* actualProviderId,
                                                                    QString* error)
{
    if (!initialize(error))
        return nullptr;

    if (!QFileInfo::exists(modelPath)) {
        if (error) *error = QStringLiteral("Model file not found: %1").arg(modelPath);
        return nullptr;
    }

    const QStringList available = availableProviders();
    QString primary = requestedProviderId;
    if (primary.isEmpty() || primary == QLatin1String(AiProvider::kAuto))
        primary = autoSelectProvider(available);

    // Try the chosen provider, then CPU when fallback is permitted.
    QStringList order;
    order << primary;
    if (settings.allowCpuFallback && primary != QLatin1String(AiProvider::kCpu))
        order << QString::fromLatin1(AiProvider::kCpu);

    QStringList collectedErrors;
    for (const QString& providerId : order) {
        Ort::SessionOptions options;
        configureSessionOptions(options, settings);

        QString reason;
        if (!appendProvider(options, providerId, available, settings, &reason)) {
            collectedErrors << QStringLiteral("%1: %2").arg(AiProvider::displayName(providerId), reason);
            qWarning().noquote() << "[AI][ONNX] Provider" << providerId << "unavailable:" << reason;
            continue;
        }

        QString createErr;
        const qint64 t0 = QDateTime::currentMSecsSinceEpoch();
        auto handle = OnnxSessionFactory::create(*d->env, options, modelPath, providerId, &createErr);
        const qint64 elapsed = QDateTime::currentMSecsSinceEpoch() - t0;
        if (handle) {
            if (actualProviderId) *actualProviderId = providerId;
            if (providerId != primary)
                qWarning().noquote() << "[AI][ONNX] CPU fallback used (requested" << primary << ")";
            qInfo().noquote() << "[AI][ONNX] Loaded" << QFileInfo(modelPath).fileName()
                              << "provider=" << providerId << "in" << elapsed << "ms";
            return handle;
        }
        collectedErrors << QStringLiteral("%1: %2").arg(AiProvider::displayName(providerId), createErr);
        qWarning().noquote() << "[AI][ONNX] Session creation failed on" << providerId << ":" << createErr;
    }

    if (error) *error = collectedErrors.join(QStringLiteral("; "));
    return nullptr;
}

#else // !HAVE_ONNXRUNTIME

bool OnnxRuntimeBackend::initialize(QString* error)
{
    if (error) *error = QStringLiteral("ONNX Runtime is not available in this build.");
    return false;
}

void OnnxRuntimeBackend::shutdown() {}
QString OnnxRuntimeBackend::runtimeVersion() const { return QString(); }
QString OnnxRuntimeBackend::runtimeLibraryPath() const { return QString(); }
QStringList OnnxRuntimeBackend::availableProviders() const { return {}; }

std::shared_ptr<OnnxSessionHandle> OnnxRuntimeBackend::createSession(const QString&,
                                                                    const QString&,
                                                                    const AiRuntimeSettings&,
                                                                    QString*,
                                                                    QString* error)
{
    if (error) *error = QStringLiteral("ONNX Runtime is not available in this build.");
    return nullptr;
}

#endif // HAVE_ONNXRUNTIME
