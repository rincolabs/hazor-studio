#pragma once

#include <QImage>
#include <QString>

#include <functional>

// Experimental, development-only bridge to stable-diffusion.cpp. Compiled into
// Hazor only when ENABLE_STABLE_DIFFUSION=ON (see docs/BUILD.md), which is a
// local Linux developer build — no packaged target ever sees this translation
// unit, and nothing else in Hazor links the dependency.
//
// Deliberately NOT a provider/engine abstraction: the model path, the prompt
// and every sampling parameter are compile-time constants in the .cpp, because
// the only question this code answers is whether a local CPU inference can land
// its pixels in a Hazor pixel layer. src/ai/ImageGenProvider.hpp is where a
// real local backend would go once that is settled.
namespace sdlocal {

// The hardcoded checkpoint this test runs against, for status/error messages.
QString modelPath();
// The hardcoded prompt, for logging.
QString prompt();

struct Txt2ImgResult {
    // Format_RGBA8888 and owning its own buffer, so it stays valid after the
    // library result it was copied out of has been freed.
    QImage image;
    QString error;          // empty on success; already user-presentable
    qint64 loadMs = 0;      // model load wall time
    qint64 generateMs = 0;  // sampling + VAE decode wall time

    bool ok() const { return error.isEmpty() && !image.isNull(); }
};

// Loads the model, generates one 512×512 txt2img image and frees every
// stable-diffusion.cpp resource before returning. Blocking and CPU-bound for
// minutes: must never be called on the GUI thread.
//
// `onModelLoaded`, when set, is invoked on the calling thread once the model is
// in memory and sampling is about to start, so the caller can tell the two
// states apart in its status indicator. It must not touch Qt objects directly.
//
// Not reentrant — one call at a time (the panel enforces that; cancelInFlight()
// uses the same single-slot context handle).
Txt2ImgResult runHardcodedTxt2Img(const std::function<void()>& onModelLoaded = {});

// Asks the library to abandon the generation running in another thread as soon
// as it can, so a closing panel / quitting app does not have to wait out the
// full step count. No-op when nothing is in flight, and it cannot interrupt the
// model load itself (there is no context to cancel against yet).
void cancelInFlight();

} // namespace sdlocal
