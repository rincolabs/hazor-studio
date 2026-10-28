#include "ai/sd/SdRuntime.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>

#include <exception>
#include <new>

#include <stable-diffusion.h>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

sd_type_t toSdType(SdWeightType type)
{
    switch (type) {
    case SdWeightType::F32:  return SD_TYPE_F32;
    case SdWeightType::F16:  return SD_TYPE_F16;
    case SdWeightType::Q8_0: return SD_TYPE_Q8_0;
    case SdWeightType::Q5_1: return SD_TYPE_Q5_1;
    case SdWeightType::Q5_0: return SD_TYPE_Q5_0;
    case SdWeightType::Q4_1: return SD_TYPE_Q4_1;
    case SdWeightType::Q4_0: return SD_TYPE_Q4_0;
    case SdWeightType::Auto: break;
    }
    // SD_TYPE_COUNT is what sd_ctx_params_init() leaves wtype at, and it means
    // "keep the type the checkpoint was stored in" rather than "invalid".
    return SD_TYPE_COUNT;
}

// sd_ctx_params_t holds borrowed `const char*`s, so every string handed to it
// has to outlive new_sd_ctx(). Keeping the UTF-8 copies in one struct next to
// the params makes that lifetime visible instead of depending on a pile of
// temporaries that happen to still be alive.
struct CtxStrings {
    QByteArray modelPath;
    QByteArray diffusionModelPath;
    QByteArray clipLPath;
    QByteArray clipGPath;
    QByteArray t5xxlPath;
    QByteArray vaePath;
    QByteArray backend;

    // Empty paths must stay null: the loader tests these pointers for presence,
    // and an empty-but-non-null string would be taken as "load a component
    // from this path" and fail.
    static const char* cstr(const QByteArray& value)
    {
        return value.isEmpty() ? nullptr : value.constData();
    }
};

// free_sd_ctx() dereferences its argument, and everything between new_sd_ctx()
// and the handover below can throw (the capability check is C++ underneath the
// C API, and locking a mutex can fail), so the fresh context is owned by a
// scope guard until it is safely stored. Without it, a throw on the way out
// would leak a just-loaded multi-gigabyte context.
struct CtxGuard {
    sd_ctx_t* ctx = nullptr;
    ~CtxGuard() { if (ctx) free_sd_ctx(ctx); }
    sd_ctx_t* take() { sd_ctx_t* out = ctx; ctx = nullptr; return out; }
};

} // namespace

SdRuntime::SdRuntime() = default;

SdRuntime::~SdRuntime()
{
    // Nothing may be generating here: SdGenerationJob drains its worker before
    // the service is shut down, and the service owns this object.
    freeContext();
}

SdRuntime::GenerationScope::GenerationScope(SdRuntime& runtime)
    : m_runtime(runtime)
{
    std::lock_guard<std::mutex> lock(m_runtime.m_cancelMutex);
    m_runtime.m_generatingCtx = m_runtime.m_ctx;
}

SdRuntime::GenerationScope::~GenerationScope()
{
    std::lock_guard<std::mutex> lock(m_runtime.m_cancelMutex);
    m_runtime.m_generatingCtx = nullptr;
}

void SdRuntime::requestCancel()
{
    std::lock_guard<std::mutex> lock(m_cancelMutex);
    m_cancelRequested = true;
    if (m_generatingCtx)
        sd_cancel_generation(m_generatingCtx, SD_CANCEL_ALL);
}

void SdRuntime::clearCancel()
{
    std::lock_guard<std::mutex> lock(m_cancelMutex);
    m_cancelRequested = false;
    // SD_CANCEL_RESET clears a request the library has already latched, so a
    // cancelled run cannot make the next one abort immediately.
    if (m_generatingCtx)
        sd_cancel_generation(m_generatingCtx, SD_CANCEL_RESET);
    else if (m_ctx)
        sd_cancel_generation(m_ctx, SD_CANCEL_RESET);
}

bool SdRuntime::cancelRequested() const
{
    std::lock_guard<std::mutex> lock(m_cancelMutex);
    return m_cancelRequested;
}

void SdRuntime::freeContext()
{
    sd_ctx_t* ctx = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_cancelMutex);
        if (m_generatingCtx) {
            qWarning() << "[SD] refusing to free the context while a generation is running";
            return;
        }
        ctx = m_ctx;
        m_ctx = nullptr;
    }
    if (ctx)
        free_sd_ctx(ctx);
    m_loaded = SdModelLoadOptions {};
    m_device = SdDevice {};
    m_choice = SdDeviceChoice {};
}

void SdRuntime::release()
{
    if (!m_ctx)
        return;
    qInfo().noquote() << "[SD] releasing the context |" << m_loaded.files.modelId
                      << "| device=" << m_device.name;
    freeContext();
}

QString SdRuntime::modelVersionName() const
{
    if (!m_ctx)
        return {};
    const char* name = sd_get_model_version_name(m_ctx);
    return name ? QString::fromUtf8(name) : QString();
}

bool SdRuntime::ensureContext(const SdModelLoadOptions& options,
                              const QString& deviceOverride,
                              QString* error)
{
    m_lastLoadMs = 0;
    m_lastLoadReused = false;

    QString missing;
    if (!options.files.isValid()) {
        if (error)
            *error = tr_("No model was selected for this generation.");
        return false;
    }
    // Re-checked here and not only during validation: a model file can be moved
    // or a network share can drop between the two, and the loader's own failure
    // for a missing file says far less than this does.
    if (!options.files.allPresent(&missing)) {
        if (error)
            *error = tr_("Model file not found: %1").arg(missing);
        return false;
    }

    SdDeviceChoice choice;
    if (!deviceOverride.isEmpty()) {
        choice.device.name = deviceOverride;
        for (const SdDevice& d : availableDevices()) {
            if (d.name.compare(deviceOverride, Qt::CaseInsensitive) == 0) {
                choice.device = d;
                break;
            }
        }
        choice.gpuRequested = options.device != SdDevicePreference::Cpu;
        choice.gpuAvailable = choice.device.gpu;
        choice.downgraded = choice.gpuRequested && !choice.device.gpu;
        choice.reason = tr_("Device %1 pinned for this attempt.").arg(choice.device.name);
    } else {
        choice = chooseDevice(options.device);
    }
    if (!choice.isValid()) {
        if (error)
            *error = tr_("No inference device is available.");
        return false;
    }

    // The reuse test is the whole reason load options are a separate struct:
    // same files, same device, same weight type and thread count means the
    // gigabytes already in memory are exactly what this request needs.
    if (m_ctx && m_loaded == options && m_device.name == choice.device.name) {
        m_lastLoadReused = true;
        m_choice = choice;
        qInfo().noquote() << "[SD] reusing the loaded context |" << m_loaded.files.modelId
                          << "| device=" << m_device.name;
        return true;
    }

    // Different model or device: free first, then load. Holding both at once is
    // what makes a 4 GiB model switch fail on a machine that could have run it.
    if (m_ctx)
        release();

    CtxStrings strings;
    strings.modelPath = options.files.checkpointPath.toUtf8();
    strings.diffusionModelPath = options.files.diffusionModelPath.toUtf8();
    strings.clipLPath = options.files.clipLPath.toUtf8();
    strings.clipGPath = options.files.clipGPath.toUtf8();
    strings.t5xxlPath = options.files.t5xxlPath.toUtf8();
    strings.vaePath = options.files.vaePath.toUtf8();
    strings.backend = choice.device.name.toUtf8();

    // Model loading is C++ underneath a C API: a bad allocation or a malformed
    // checkpoint can come back as an exception, and it must not cross the
    // worker-task boundary — that would swallow the result callback and leave
    // the caller stuck in Loading with its button disabled.
    try {
        sd_ctx_params_t ctxParams;
        sd_ctx_params_init(&ctxParams);
        ctxParams.model_path            = CtxStrings::cstr(strings.modelPath);
        ctxParams.diffusion_model_path  = CtxStrings::cstr(strings.diffusionModelPath);
        ctxParams.clip_l_path           = CtxStrings::cstr(strings.clipLPath);
        ctxParams.clip_g_path           = CtxStrings::cstr(strings.clipGPath);
        ctxParams.t5xxl_path            = CtxStrings::cstr(strings.t5xxlPath);
        ctxParams.vae_path              = CtxStrings::cstr(strings.vaePath);
        // Naming the device explicitly rather than leaving `backend` null: the
        // library's default would pick the best device it can find, which is
        // the one thing that makes the effective backend unknowable.
        ctxParams.backend               = CtxStrings::cstr(strings.backend);
        ctxParams.wtype                 = toSdType(options.weightType);
        if (options.threads > 0)
            ctxParams.n_threads = options.threads;

        qInfo().noquote() << "[SD] loading model |" << options.files.modelId
                          << "| files=" << options.files.paths().join(QLatin1String(", "))
                          << "| device=" << choice.device.name
                          << "(" << (choice.device.gpu ? "GPU" : "CPU") << ")"
                          << "| wtype=" << weightTypeName(options.weightType)
                          << "| threads=" << ctxParams.n_threads;
        if (choice.downgraded)
            qWarning().noquote() << "[SD]" << choice.reason;

        QElapsedTimer timer;
        timer.start();
        CtxGuard fresh;
        fresh.ctx = new_sd_ctx(&ctxParams);
        m_lastLoadMs = timer.elapsed();

        if (!fresh.ctx) {
            if (error)
                *error = tr_("Failed to load the model (see the log for details).");
            qWarning().noquote() << "[SD] new_sd_ctx failed after" << m_lastLoadMs << "ms"
                                 << "| device=" << choice.device.name;
            return false;
        }
        if (!sd_ctx_supports_image_generation(fresh.ctx)) {
            // The guard frees it: a context that cannot generate is of no use
            // to anything else in this module.
            if (error)
                *error = tr_("This checkpoint cannot generate images.");
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(m_cancelMutex);
            m_ctx = fresh.take();
        }
        m_loaded = options;
        m_device = choice.device;
        m_choice = choice;

        qInfo().noquote() << "[SD] model loaded in" << m_lastLoadMs << "ms |"
                          << "version=" << modelVersionName()
                          << "| device=" << m_device.name;
        return true;
    } catch (const std::bad_alloc&) {
        if (error)
            *error = tr_("Out of memory while loading the model.");
        qWarning() << "[SD] out of memory loading the model";
        return false;
    } catch (const std::exception& e) {
        if (error)
            *error = QString::fromUtf8(e.what());
        qWarning().noquote() << "[SD] exception while loading |" << QString::fromUtf8(e.what());
        return false;
    }
}

} // namespace sdlocal
