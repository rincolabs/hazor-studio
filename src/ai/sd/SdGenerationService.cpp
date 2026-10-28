#include "ai/sd/SdGenerationService.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QDebug>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>
#include <exception>
#include <mutex>
#include <new>
#include <vector>

#include <stable-diffusion.h>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

// ── Library callbacks ───────────────────────────────────────────────────────
// stable-diffusion.cpp's log and progress hooks are process-global, so the sink
// of the one run in flight is published here under a lock. One run at a time is
// an invariant of this module (SdGenerationJob enforces it), which is what makes
// a single slot correct rather than a race.
std::mutex g_sinkMutex;
const SdProgressSink* g_sink = nullptr;

void setActiveSink(const SdProgressSink* sink)
{
    std::lock_guard<std::mutex> lock(g_sinkMutex);
    g_sink = sink;
}

// Funnels the library's own logging into Hazor's, so a failed load or a failed
// sampling leaves the same trail as the rest of the AI layer. Called from the
// inference thread.
void logCallback(sd_log_level_t level, const char* text, void*)
{
    if (!text)
        return;
    const QString line = QString::fromUtf8(text).trimmed();
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

void progressCallback(int step, int steps, float seconds, void*)
{
    qInfo().noquote() << "[SD] step" << step << "/" << steps
                      << "|" << QString::number(seconds, 'f', 2) << "s";
    std::lock_guard<std::mutex> lock(g_sinkMutex);
    if (g_sink && g_sink->onStep)
        g_sink->onStep(step, steps, seconds);
}

void installCallbacks()
{
    // Idempotent and cheap; done per run rather than once at startup so nothing
    // in this module runs before the panel asks for a generation.
    sd_set_log_callback(logCallback, nullptr);
    sd_set_progress_callback(progressCallback, nullptr);
}

void reportState(const SdProgressSink& sink, SdGenerationState state, const QString& detail = {})
{
    if (sink.onState)
        sink.onState(state, detail);
}

// ── Image marshalling ───────────────────────────────────────────────────────

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

// An sd_image_t plus the buffer it points at, so the pixels handed to the
// library outlive the call without the library ever owning Hazor's memory.
struct SdImageBuffer {
    std::vector<uint8_t> pixels;
    sd_image_t image {};

    bool empty() const { return pixels.empty(); }
};

// Packs a QImage into the tightly-packed layout sd_image_t wants.
//
// `channels` is 3 for an init image and 1 for a mask, matching what the library
// expects of each: the mask is interpolated as a single-channel tensor, and
// handing it three channels would reshape wrong. Mask polarity follows the
// library's own convention — white regenerates, black keeps the init pixels
// (pipeline/image.cpp computes `(1 - mask) * init`), which is also Hazor's
// selection polarity, so no inversion happens anywhere.
SdImageBuffer toSdImage(const QImage& source, int channels)
{
    SdImageBuffer out;
    if (source.isNull() || (channels != 1 && channels != 3))
        return out;

    const QImage img = source.format() == QImage::Format_RGBA8888
        ? source : source.convertToFormat(QImage::Format_RGBA8888);
    if (img.isNull())
        return out;

    const int w = img.width();
    const int h = img.height();
    out.pixels.resize(static_cast<size_t>(w) * h * channels);
    for (int y = 0; y < h; ++y) {
        const uchar* in = img.constScanLine(y);
        uint8_t* dst = out.pixels.data() + static_cast<size_t>(y) * w * channels;
        for (int x = 0; x < w; ++x) {
            if (channels == 1) {
                // Luminance for a mask that came in as colour; an alpha-only
                // Hazor mask has its coverage in all three channels already.
                dst[x] = static_cast<uint8_t>((in[x * 4 + 0] * 77 + in[x * 4 + 1] * 150
                                               + in[x * 4 + 2] * 29) >> 8);
            } else {
                dst[x * 3 + 0] = in[x * 4 + 0];
                dst[x * 3 + 1] = in[x * 4 + 1];
                dst[x * 3 + 2] = in[x * 4 + 2];
            }
        }
    }

    out.image.width = static_cast<uint32_t>(w);
    out.image.height = static_cast<uint32_t>(h);
    out.image.channel = static_cast<uint32_t>(channels);
    out.image.data = out.pixels.data();
    return out;
}

// free_sd_image_generation_result() owns the pixels, the generation_info and
// the item array, and every early return below has to release them.
struct ResultGuard {
    sd_image_generation_result_t* result = nullptr;
    ~ResultGuard() { if (result) free_sd_image_generation_result(result); }
};

// The seed the sampling really used, out of the library's own record of the run.
// A request that asked for a random seed (-1) only learns its seed this way, and
// re-deriving it here instead of parsing would be a second implementation of
// something the engine already knows.
qint64 seedFromGenerationInfo(const QString& json, qint64 fallback)
{
    if (json.isEmpty())
        return fallback;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8());
    if (!doc.isObject())
        return fallback;
    const QJsonValue seed = doc.object().value(QStringLiteral("seed"));
    // Written as a string by the library, because a 64-bit seed does not
    // survive JSON's double.
    if (seed.isString()) {
        bool ok = false;
        const qint64 value = seed.toString().toLongLong(&ok);
        return ok ? value : fallback;
    }
    if (seed.isDouble())
        return static_cast<qint64>(seed.toDouble());
    return fallback;
}

} // namespace

// ── Sampler / scheduler vocabulary ──────────────────────────────────────────

QStringList SdGenerationService::samplerNames()
{
    QStringList out;
    for (int i = 0; i < SAMPLE_METHOD_COUNT; ++i) {
        const char* name = sd_sample_method_name(static_cast<sample_method_t>(i));
        if (name)
            out << QString::fromUtf8(name);
    }
    return out;
}

QStringList SdGenerationService::schedulerNames()
{
    QStringList out;
    for (int i = 0; i < SCHEDULER_COUNT; ++i) {
        const char* name = sd_scheduler_name(static_cast<scheduler_t>(i));
        if (name)
            out << QString::fromUtf8(name);
    }
    return out;
}

bool SdGenerationService::isKnownSampler(const QString& name)
{
    // str_to_sample_method() returns SAMPLE_METHOD_COUNT for anything it does
    // not recognise — which is also the sentinel for "use the model default".
    // So a typo would silently become the default rather than an error, and the
    // only way to tell them apart is to treat an empty name as the default here
    // and everything else as having to round-trip.
    if (name.trimmed().isEmpty())
        return true;
    return str_to_sample_method(name.trimmed().toUtf8().constData()) != SAMPLE_METHOD_COUNT;
}

bool SdGenerationService::isKnownScheduler(const QString& name)
{
    if (name.trimmed().isEmpty())
        return true;
    return str_to_scheduler(name.trimmed().toUtf8().constData()) != SCHEDULER_COUNT;
}

// ── Service ─────────────────────────────────────────────────────────────────

SdGenerationService& SdGenerationService::instance()
{
    static SdGenerationService service;
    return service;
}

SdGenerationService::SdGenerationService() = default;
SdGenerationService::~SdGenerationService() = default;

void SdGenerationService::requestCancel()
{
    m_runtime.requestCancel();
}

void SdGenerationService::shutdown()
{
    m_runtime.requestCancel();
    m_runtime.release();
}

bool SdGenerationService::validateRequest(const SdGenerationRequest& request, QString* error) const
{
    const auto fail = [error](const QString& message) {
        if (error)
            *error = message;
        return false;
    };

    if (!request.load.files.isValid())
        return fail(tr_("No model was selected for this generation."));
    QString missing;
    if (!request.load.files.allPresent(&missing))
        return fail(tr_("Model file not found: %1").arg(missing));

    // The library's latent geometry is a multiple of the VAE scale factor; a
    // stray 500x500 would be silently rounded somewhere deep in the pipeline.
    if (request.width <= 0 || request.height <= 0)
        return fail(tr_("The output size must be positive."));
    if (request.width % 8 != 0 || request.height % 8 != 0)
        return fail(tr_("The output size must be a multiple of 8 (got %1×%2).")
                        .arg(request.width).arg(request.height));
    if (request.sampling.steps <= 0)
        return fail(tr_("The step count must be at least 1."));
    if (request.batchCount <= 0)
        return fail(tr_("The batch count must be at least 1."));
    if (!isKnownSampler(request.sampling.sampler))
        return fail(tr_("Unknown sampler \"%1\".").arg(request.sampling.sampler));
    if (!isKnownScheduler(request.sampling.scheduler))
        return fail(tr_("Unknown scheduler \"%1\".").arg(request.sampling.scheduler));

    // Each operation needs exactly the inputs it needs: offering a mask to a
    // txt2img, or running an inpaint without one, is a wiring mistake worth
    // naming rather than quietly ignoring.
    switch (request.operation) {
    case SdOperation::Txt2Img:
        break;
    case SdOperation::Img2Img:
        if (request.initImage.isNull())
            return fail(tr_("img2img needs an input image."));
        break;
    case SdOperation::Inpaint:
        if (request.initImage.isNull())
            return fail(tr_("Inpainting needs an input image."));
        if (request.maskImage.isNull())
            return fail(tr_("Inpainting needs a mask."));
        if (request.maskImage.size() != request.initImage.size()) {
            return fail(tr_("The mask (%1×%2) and the input image (%3×%4) must be the same size.")
                            .arg(request.maskImage.width()).arg(request.maskImage.height())
                            .arg(request.initImage.width()).arg(request.initImage.height()));
        }
        break;
    }

    for (const SdResolvedLora& lora : request.loras) {
        if (lora.path.isEmpty())
            return fail(tr_("A LoRA was selected but its file is unknown."));
    }
    return true;
}

SdGenerationResult SdGenerationService::run(const SdGenerationRequest& request,
                                            const SdProgressSink& sink)
{
    SdGenerationResult out;
    out.modelId = request.load.files.modelId;

    QString error;
    if (!validateRequest(request, &error)) {
        out.state = SdGenerationState::Failed;
        out.error = error;
        return out;
    }

    installCallbacks();
    // A cancel left over from a previous run must not abort this one before it
    // starts.
    m_runtime.clearCancel();

    qInfo().noquote() << "[SD] run |" << operationName(request.operation)
                      << "| model=" << request.load.files.modelId
                      << "|" << describeBackendEnvironment();

    out = attempt(request, sink, QString());

    // Runtime CPU fallback. A GPU-enabled build has to be usable on a machine
    // whose GPU cannot run this model — no driver, not enough VRAM, an
    // unsupported operation — and the honest way to do that is to retry on the
    // CPU and SAY the run was a CPU run. Only once, only when the first attempt
    // really was on a GPU, and never after a cancellation: a user who stopped
    // the generation does not want it restarted somewhere slower.
    const bool retryable = out.state == SdGenerationState::Failed
        && request.load.allowCpuFallback
        && out.gpu;
    if (retryable) {
        const SdDevice cpu = cpuDevice();
        qWarning().noquote() << "[SD] GPU attempt failed on" << out.deviceName
                             << "| retrying on" << cpu.name << "|" << out.error;
        // The failed GPU context is released first: its VRAM allocations are of
        // no use to the CPU attempt and may be what made the GPU fail.
        m_runtime.release();
        m_runtime.clearCancel();

        const QString gpuError = out.error;
        const QString gpuDevice = out.deviceName;
        out = attempt(request, sink, cpu.name);
        out.cpuFallback = true;
        out.backendNote = tr_("The %1 device could not run this model (%2); "
                              "the image was generated on the CPU.")
                              .arg(gpuDevice, gpuError);
        if (out.state == SdGenerationState::Failed) {
            // Both failed: the GPU error is the one that explains the machine,
            // so it is kept alongside the CPU one instead of being lost.
            out.error = tr_("%1 (the %2 attempt failed first: %3)")
                            .arg(out.error, gpuDevice, gpuError);
        }
    }

    return out;
}

SdGenerationResult SdGenerationService::attempt(const SdGenerationRequest& request,
                                                const SdProgressSink& sink,
                                                const QString& deviceOverride)
{
    SdGenerationResult out;
    out.modelId = request.load.files.modelId;
    out.seed = request.sampling.seed;

    reportState(sink, SdGenerationState::Loading, request.load.files.modelId);

    QString error;
    if (!m_runtime.ensureContext(request.load, deviceOverride, &error)) {
        out.state = SdGenerationState::Failed;
        out.error = error;
        // The device that was going to be used is still worth reporting: it is
        // what decides whether the caller retries on the CPU.
        const SdDeviceChoice choice = deviceOverride.isEmpty()
            ? chooseDevice(request.load.device) : SdDeviceChoice {};
        out.deviceName = deviceOverride.isEmpty() ? choice.device.name : deviceOverride;
        out.gpu = deviceOverride.isEmpty() && choice.device.gpu;
        return out;
    }

    out.deviceName = m_runtime.device().name;
    out.gpu = m_runtime.device().gpu;
    out.modelVersion = m_runtime.modelVersionName();
    out.loadMs = m_runtime.lastLoadMs();
    out.contextReused = m_runtime.lastLoadReused();
    if (m_runtime.deviceChoice().downgraded)
        out.backendNote = m_runtime.deviceChoice().reason;

    // A cancel during the model load cannot reach the library (there is no
    // context to cancel against yet), so it is honoured here instead of making
    // the user wait out a full sampling run they already stopped.
    if (m_runtime.cancelRequested()) {
        out.state = SdGenerationState::Cancelled;
        return out;
    }

    reportState(sink, SdGenerationState::Generating);
    setActiveSink(&sink);
    // Unpublished before `sink` (a caller's stack object) can go out of scope,
    // on every path out of this function.
    struct SinkGuard {
        ~SinkGuard() { setActiveSink(nullptr); }
    } sinkGuard;

    // Strings and buffers handed to the library are borrowed, so they live here
    // for the whole generate_image() call.
    const QByteArray prompt = request.prompt.toUtf8();
    const QByteArray negativePrompt = request.negativePrompt.toUtf8();
    const QByteArray sampler = request.sampling.sampler.trimmed().toUtf8();
    const QByteArray scheduler = request.sampling.scheduler.trimmed().toUtf8();

    std::vector<QByteArray> loraPaths;
    std::vector<sd_lora_t> loras;
    loraPaths.reserve(request.loras.size());
    loras.reserve(request.loras.size());
    for (const SdResolvedLora& lora : request.loras) {
        loraPaths.push_back(lora.path.toUtf8());
        sd_lora_t entry {};
        entry.path = loraPaths.back().constData();
        entry.multiplier = lora.weight;
        entry.is_high_noise = false;
        loras.push_back(entry);
    }

    const SdImageBuffer initImage = request.operation == SdOperation::Txt2Img
        ? SdImageBuffer {} : toSdImage(request.initImage, 3);
    const SdImageBuffer maskImage = request.operation == SdOperation::Inpaint
        ? toSdImage(request.maskImage, 1) : SdImageBuffer {};

    try {
        sd_img_gen_params_t params;
        sd_img_gen_params_init(&params);
        params.prompt = prompt.constData();
        // Never null: the library reads it as a string unconditionally.
        params.negative_prompt = negativePrompt.constData();
        params.width = request.width;
        params.height = request.height;
        params.batch_count = request.batchCount;
        params.seed = request.sampling.seed;
        params.clip_skip = request.sampling.clipSkip;
        params.sample_params.sample_steps = request.sampling.steps;
        params.sample_params.guidance.txt_cfg = request.sampling.cfgScale;
        // Left at the init value (SAMPLE_METHOD_COUNT / SCHEDULER_COUNT) when
        // unset, which is the library's "whatever this checkpoint declares" —
        // the only safe default across SD 1.x, SDXL, SD3 and Flux.
        if (!sampler.isEmpty())
            params.sample_params.sample_method = str_to_sample_method(sampler.constData());
        if (!scheduler.isEmpty())
            params.sample_params.scheduler = str_to_scheduler(scheduler.constData());
        if (!loras.empty()) {
            params.loras = loras.data();
            params.lora_count = static_cast<uint32_t>(loras.size());
        }
        if (!initImage.empty()) {
            params.init_image = initImage.image;
            params.strength = request.denoisingStrength;
        }
        if (!maskImage.empty())
            params.mask_image = maskImage.image;

        ResultGuard result;
        QElapsedTimer timer;
        timer.start();
        bool generated = false;
        {
            // Publishes the context so a cancel from the GUI thread can reach
            // it, and unpublishes before the result is touched.
            SdRuntime::GenerationScope scope(m_runtime);
            generated = generate_image(m_runtime.context(), &params, &result.result);
        }
        out.generateMs = timer.elapsed();

        if (m_runtime.cancelRequested()) {
            out.state = SdGenerationState::Cancelled;
            qInfo().noquote() << "[SD] cancelled after" << out.generateMs << "ms";
            return out;
        }
        if (!generated || !result.result || result.result->count <= 0 || !result.result->items) {
            out.state = SdGenerationState::Failed;
            out.error = tr_("Generation failed (see the log for details).");
            qWarning().noquote() << "[SD] generate_image failed after" << out.generateMs << "ms"
                                 << "| device=" << out.deviceName;
            return out;
        }

        const sd_image_result_t& item = result.result->items[0];
        if (item.generation_info)
            out.generationInfo = QString::fromUtf8(item.generation_info);
        out.seed = seedFromGenerationInfo(out.generationInfo, request.sampling.seed);

        // Copied out before the guard frees the library's buffers, so the image
        // handed back does not alias any sd/ggml memory.
        out.image = toQImage(item.image);
        if (out.image.isNull()) {
            out.state = SdGenerationState::Failed;
            out.error = tr_("The generated image is empty or has an unsupported pixel format.");
            qWarning().noquote() << "[SD] unusable result |" << item.image.width << "x"
                                 << item.image.height << "| channels=" << item.image.channel;
            return out;
        }
        out.size = out.image.size();
        out.state = SdGenerationState::Completed;

        qInfo().noquote() << "[SD] generated" << out.size
                          << "| device=" << out.deviceName
                          << "(" << (out.gpu ? "GPU" : "CPU") << ")"
                          << "| seed=" << out.seed
                          << "| loadMs=" << out.loadMs
                          << (out.contextReused ? "(reused)" : "")
                          << "| generateMs=" << out.generateMs;
        return out;
    } catch (const std::bad_alloc&) {
        out.state = SdGenerationState::Failed;
        out.error = tr_("Out of memory while running the model.");
        qWarning() << "[SD] out of memory";
        return out;
    } catch (const std::exception& e) {
        out.state = SdGenerationState::Failed;
        out.error = QString::fromUtf8(e.what());
        qWarning().noquote() << "[SD] exception |" << out.error;
        return out;
    }
}

} // namespace sdlocal
