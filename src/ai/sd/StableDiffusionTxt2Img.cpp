#include "ai/sd/StableDiffusionTxt2Img.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QFileInfo>

#include <cstring>
#include <exception>
#include <mutex>
#include <new>

#include <stable-diffusion.h>

namespace sdlocal {
namespace {

// ── Hardcoded test configuration ─────────────────────────────────────────────
// EDIT HERE: absolute path to a local Stable Diffusion 1.5 checkpoint
// (.safetensors / .ckpt / .gguf) able to do 512×512 txt2img. There is no model
// picker, no scanning and no persisted setting by design — this constant is the
// single place the test model is configured, and a missing file is reported in
// the panel instead of being guessed at. No real path is committed: it would be
// valid on exactly one machine.
//
// Measured once with v1-5-pruned-emaonly.safetensors (SD 1.5, fp32, 4.27 GiB)
// on a 6-core i5-8400: 694 s for the 20 steps below. Two things dominate that
// and neither is a misconfiguration — the default txt_cfg of 7.0 runs the UNet
// twice per step (40 passes, not 20), and fp32 weights double the memory
// traffic a CPU backend is already bound by. Setting ctx_params.wtype to
// SD_TYPE_F16 is the one-line lever if that matters.
constexpr const char* kModelPath =
    "/REPLACE/WITH/ABSOLUTE/PATH/TO/v1-5-pruned-emaonly.safetensors";

constexpr const char* kPrompt =
    "A beautiful mountain landscape at sunset, cinematic lighting, highly detailed";

// Everything else the API requires is left at sd_*_params_init()'s defaults, so
// sampler, scheduler, CFG and prediction type come from whatever the loaded
// checkpoint declares rather than from guesses made here.
constexpr int kWidth      = 512;
constexpr int kHeight     = 512;
constexpr int kSteps      = 20;
constexpr int kBatchCount = 1;
constexpr int64_t kSeed   = 42;  // fixed, so two runs of the test are comparable

// CPU only for this first integration. No backend compiled in besides ggml's
// CPU one (SD_CUDA/SD_VULKAN/... stay OFF in CMakeLists.txt), but naming the
// device explicitly keeps the choice visible instead of depending on whatever
// ggml_backend_init_best() would pick if someone enables a GPU backend later.
constexpr const char* kBackend = "CPU";

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

// The library logs through a global callback; funnel it into Hazor's own
// logging so a failed load/sampling leaves the same trail as the rest of the
// AI layer. Called from the inference thread.
void logCallback(sd_log_level_t level, const char* text, void*)
{
    if (!text)
        return;
    QString line = QString::fromUtf8(text).trimmed();
    if (line.isEmpty())
        return;
    switch (level) {
    case SD_LOG_ERROR:
        qWarning().noquote() << "[SD] ERROR |" << line;
        break;
    case SD_LOG_WARN:
        qWarning().noquote() << "[SD] WARN |" << line;
        break;
    default:
        qInfo().noquote() << "[SD]" << line;
        break;
    }
}

void progressCallback(int step, int steps, float time, void*)
{
    qInfo().noquote() << "[SD] step" << step << "/" << steps
                      << "|" << QString::number(time, 'f', 2) << "s";
}

// Single-slot handle on the in-flight context, so cancelInFlight() can reach it
// from the GUI thread. sd_cancel_generation() only sets a flag inside the
// context, so publishing the pointer under a mutex is all the synchronisation
// this needs.
std::mutex g_ctxMutex;
sd_ctx_t* g_activeCtx = nullptr;

void publishCtx(sd_ctx_t* ctx)
{
    std::lock_guard<std::mutex> lock(g_ctxMutex);
    g_activeCtx = ctx;
}

// free_sd_ctx() dereferences its argument, and new_sd_ctx() is free to throw on
// the way out (its model init is C++ and nothing in the C wrapper catches), so
// ownership of both library handles is held by scope guards. Without them an
// out-of-memory on a 4 GiB checkpoint would leak the half-built context.
struct CtxGuard {
    sd_ctx_t* ctx = nullptr;
    ~CtxGuard()
    {
        // Unpublish first, and under the same mutex cancelInFlight() takes, so a
        // cancel racing with an unwind cannot reach a freed context.
        publishCtx(nullptr);
        if (ctx)
            free_sd_ctx(ctx);
    }
};

struct ResultGuard {
    sd_image_generation_result_t* result = nullptr;
    ~ResultGuard() { if (result) free_sd_image_generation_result(result); }
};

// Copies one library image out into a QImage that owns its pixels. sd_image_t
// rows are tightly packed (width * channel); QImage pads its scanlines to a
// 4-byte boundary, so this goes row by row instead of taking the whole block.
QImage toQImage(const sd_image_t& src)
{
    if (!src.data || src.width == 0 || src.height == 0)
        return {};
    if (src.channel != 3 && src.channel != 4)
        return {};

    QImage out(static_cast<int>(src.width), static_cast<int>(src.height),
               QImage::Format_RGBA8888);
    if (out.isNull())
        return {};

    const int w = out.width();
    const int h = out.height();
    const uint32_t ch = src.channel;
    for (int y = 0; y < h; ++y) {
        const uint8_t* in = src.data + static_cast<size_t>(y) * w * ch;
        uchar* dst = out.scanLine(y);
        if (ch == 4) {
            memcpy(dst, in, static_cast<size_t>(w) * 4);
            continue;
        }
        for (int x = 0; x < w; ++x) {
            dst[x * 4 + 0] = in[x * 3 + 0];
            dst[x * 4 + 1] = in[x * 3 + 1];
            dst[x * 4 + 2] = in[x * 3 + 2];
            dst[x * 4 + 3] = 255;
        }
    }
    return out;
}

} // namespace

QString modelPath() { return QString::fromUtf8(kModelPath); }
QString prompt()    { return QString::fromUtf8(kPrompt); }

void cancelInFlight()
{
    std::lock_guard<std::mutex> lock(g_ctxMutex);
    if (g_activeCtx)
        sd_cancel_generation(g_activeCtx, SD_CANCEL_ALL);
}

Txt2ImgResult runHardcodedTxt2Img(const std::function<void()>& onModelLoaded)
{
    Txt2ImgResult out;

    const QFileInfo modelInfo(modelPath());
    if (!modelInfo.isFile()) {
        out.error = tr_("Model file not found: %1").arg(modelPath());
        qWarning().noquote() << "[SD] model missing |" << modelPath();
        return out;
    }

    sd_set_log_callback(logCallback, nullptr);
    sd_set_progress_callback(progressCallback, nullptr);

    // The library signals most failures through a return value, but model
    // loading is C++ underneath a C API: a bad allocation or a malformed
    // checkpoint can come back as an exception, and it must not cross the
    // QtConcurrent task boundary — that would swallow the result callback and
    // leave the panel stuck on "Generating…" with its button disabled.
    try {
        sd_ctx_params_t ctxParams;
        sd_ctx_params_init(&ctxParams);
        ctxParams.model_path = kModelPath;
        ctxParams.backend    = kBackend;

        qInfo().noquote() << "[SD] loading model |" << modelPath()
                          << "| backend=" << kBackend
                          << "| threads=" << ctxParams.n_threads;

        QElapsedTimer timer;
        timer.start();
        CtxGuard ctx;
        ctx.ctx = new_sd_ctx(&ctxParams);
        out.loadMs = timer.elapsed();
        if (!ctx.ctx) {
            out.error = tr_("Failed to load the model (see the log for details).");
            qWarning().noquote() << "[SD] new_sd_ctx failed after" << out.loadMs << "ms";
            return out;
        }
        if (!sd_ctx_supports_image_generation(ctx.ctx)) {
            out.error = tr_("This checkpoint cannot generate images.");
            return out;
        }
        qInfo().noquote() << "[SD] model loaded in" << out.loadMs << "ms |"
                          << "version=" << sd_get_model_version_name(ctx.ctx);

        if (onModelLoaded)
            onModelLoaded();

        sd_img_gen_params_t genParams;
        sd_img_gen_params_init(&genParams);
        genParams.prompt          = kPrompt;
        genParams.negative_prompt = "";  // never null: it is read as a string
        genParams.width           = kWidth;
        genParams.height          = kHeight;
        genParams.batch_count     = kBatchCount;
        genParams.seed            = kSeed;
        genParams.sample_params.sample_steps = kSteps;

        ResultGuard result;
        publishCtx(ctx.ctx);
        timer.restart();
        const bool generated = generate_image(ctx.ctx, &genParams, &result.result);
        out.generateMs = timer.elapsed();
        publishCtx(nullptr);

        if (!generated || !result.result || result.result->count <= 0 || !result.result->items) {
            out.error = tr_("Generation failed (see the log for details).");
            qWarning().noquote() << "[SD] generate_image failed after" << out.generateMs << "ms";
            return out;
        }

        // Copied out before the guards free the library's buffers, so the image
        // handed back to the panel does not alias any sd/ggml memory.
        const sd_image_t& img = result.result->items[0].image;
        out.image = toQImage(img);
        if (out.image.isNull()) {
            out.error = tr_("The generated image is empty or has an unsupported pixel format.");
            qWarning().noquote() << "[SD] unusable result |" << img.width << "x" << img.height
                                 << "| channels=" << img.channel;
            return out;
        }

        qInfo().noquote() << "[SD] generated" << out.image.size()
                          << "in" << out.generateMs << "ms";
        return out;
    } catch (const std::bad_alloc&) {
        out.error = tr_("Out of memory while running the model.");
        qWarning() << "[SD] out of memory";
        return out;
    } catch (const std::exception& e) {
        out.error = QString::fromUtf8(e.what());
        qWarning().noquote() << "[SD] exception |" << out.error;
        return out;
    }
}

} // namespace sdlocal
