#pragma once

#include "ai/sd/SdGenerationTypes.hpp"
#include "ai/sd/SdRuntime.hpp"

#include <QString>
#include <QStringList>

// Runs generation requests through stable-diffusion.cpp.
//
// This is the only place in Hazor that calls generate_image(). It takes a
// structured request that knows nothing about the user interface, owns the one
// SdRuntime, and hands back pixels Hazor owns. It implements no diffusion of
// its own: sampler, scheduler, guidance and the operations themselves are the
// library's.
//
// A single instance per process, because a single inference context is. Every
// call to run() must come from the inference thread SdGenerationJob owns;
// requestCancel() and shutdown() are the thread-safe entry points.
namespace sdlocal {

class SdGenerationService {
public:
    static SdGenerationService& instance();

    // Blocking and compute-bound for minutes. Never call on the GUI thread.
    //
    // Loads or reuses a context, generates one image and returns it as a QImage
    // that owns its pixels — every library allocation behind it is freed before
    // this returns, so nothing in the result aliases sd/ggml memory.
    SdGenerationResult run(const SdGenerationRequest& request,
                           const SdProgressSink& sink = {});

    // Asks the running generation to stop. Safe from any thread; a no-op when
    // nothing is in flight.
    void requestCancel();

    // Frees the inference context. Call with nothing in flight — the job
    // controller drains its worker first — so the application can exit without
    // a multi-gigabyte context outliving the Qt objects that drove it.
    void shutdown();

    // Checks a request without loading anything, so a caller can refuse early
    // instead of after a multi-minute load. Returns false and fills `error`.
    bool validateRequest(const SdGenerationRequest& request, QString* error) const;

    // The sampler/scheduler vocabulary this revision of the library accepts,
    // for validation and for a future frontend's pickers.
    static QStringList samplerNames();
    static QStringList schedulerNames();
    static bool isKnownSampler(const QString& name);
    static bool isKnownScheduler(const QString& name);

    const SdRuntime& runtime() const { return m_runtime; }

private:
    SdGenerationService();
    ~SdGenerationService();

    SdGenerationService(const SdGenerationService&) = delete;
    SdGenerationService& operator=(const SdGenerationService&) = delete;

    // One attempt on one device. `deviceOverride` pins the ggml device, which
    // is how the CPU retry asks for the same model somewhere else.
    SdGenerationResult attempt(const SdGenerationRequest& request,
                               const SdProgressSink& sink,
                               const QString& deviceOverride);

    SdRuntime m_runtime;
};

} // namespace sdlocal
