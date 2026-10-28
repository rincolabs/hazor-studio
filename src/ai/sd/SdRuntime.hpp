#pragma once

#include "ai/sd/SdBackend.hpp"
#include "ai/sd/SdGenerationTypes.hpp"

#include <QString>

#include <mutex>

struct sd_ctx_t;

// Owns the one stable-diffusion.cpp inference context.
//
// One context at a time, deliberately: a loaded SD 1.5 checkpoint is gigabytes
// of weights, and a cache of several would trade the problem this class exists
// to solve (never reload the same model twice in a row) for a much worse one
// (two models resident and neither fitting). When a request needs a different
// model or an incompatible device, the current context is freed first and the
// new one loaded after.
//
// Threading: every mutating call belongs to the inference thread, which
// SdGenerationJob guarantees is a single thread with one run in flight.
// requestCancel() is the one exception — it is called from the GUI thread and
// reaches the context only while a generation has published it.
namespace sdlocal {

class SdRuntime {
public:
    SdRuntime();
    ~SdRuntime();

    SdRuntime(const SdRuntime&) = delete;
    SdRuntime& operator=(const SdRuntime&) = delete;

    // Makes a context matching `options` current, reusing the loaded one when
    // it already matches. `deviceOverride`, when non-empty, pins the ggml
    // device by name instead of resolving options.device — that is how the
    // CPU-retry path asks for the same model on a different device.
    //
    // On failure the runtime is left with no context: a half-built one is freed
    // rather than kept, so the next attempt starts from a known state.
    bool ensureContext(const SdModelLoadOptions& options,
                       const QString& deviceOverride,
                       QString* error);

    // Frees the context. Refuses while a generation has it published, because
    // the library would be left dereferencing freed memory inside its own
    // sampling loop.
    void release();

    sd_ctx_t* context() const { return m_ctx; }
    bool hasContext() const { return m_ctx != nullptr; }

    const SdDevice& device() const { return m_device; }
    const SdDeviceChoice& deviceChoice() const { return m_choice; }
    const SdModelLoadOptions& loaded() const { return m_loaded; }
    QString modelVersionName() const;

    qint64 lastLoadMs() const { return m_lastLoadMs; }
    // True when the last ensureContext() reused the context it already had, so
    // the result can report a 0 ms load as a reuse instead of a suspiciously
    // fast load.
    bool lastLoadReused() const { return m_lastLoadReused; }

    // Asks the library to abandon the generation running on the inference
    // thread. Safe to call from any thread and at any time: a no-op when no
    // generation has published a context. It cannot interrupt a model load —
    // there is no context to cancel against yet — so the flag is also latched
    // and checked by the service once the load returns.
    void requestCancel();
    void clearCancel();
    bool cancelRequested() const;

    // Publishes the context for the duration of one generate_image() call so
    // requestCancel() can reach it, and unpublishes on scope exit — including
    // during an exception unwind, so a cancel racing a failure cannot touch a
    // context that is being torn down.
    class GenerationScope {
    public:
        explicit GenerationScope(SdRuntime& runtime);
        ~GenerationScope();
        GenerationScope(const GenerationScope&) = delete;
        GenerationScope& operator=(const GenerationScope&) = delete;

    private:
        SdRuntime& m_runtime;
    };

private:
    void freeContext();

    sd_ctx_t* m_ctx = nullptr;            // inference thread only
    SdModelLoadOptions m_loaded;
    SdDevice m_device;
    SdDeviceChoice m_choice;
    qint64 m_lastLoadMs = 0;
    bool m_lastLoadReused = false;

    // Guards both the published context and the cancel flag, which is the same
    // lock the proof of concept used for exactly this race.
    mutable std::mutex m_cancelMutex;
    sd_ctx_t* m_generatingCtx = nullptr;
    bool m_cancelRequested = false;
};

} // namespace sdlocal
