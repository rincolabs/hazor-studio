#pragma once

#include "ai/sd/SdBackend.hpp"
#include "ai/sd/SdModelManager.hpp"

#include <QImage>
#include <QSize>
#include <QString>
#include <QVector>

#include <functional>

// The request/result vocabulary of the local Stable Diffusion module.
//
// Nothing here knows about Qt widgets, documents or workflows: a request is a
// plain value a panel, a workflow executor or a future Neural Flow frontend can
// all build, and a result is a plain value any of them can consume. The library
// header is included by the implementation only, so none of this leaks
// stable-diffusion.h into the rest of Hazor.
namespace sdlocal {

enum class SdOperation {
    Txt2Img,
    Img2Img,
    Inpaint,
    Canny,
    Upscale,
    ADetailer,
};

QString operationName(SdOperation operation);
bool operationFromName(const QString& name, SdOperation& out);

// The state machine the job controller reports. Loading and Generating are kept
// apart because the first costs minutes on a cold 4 GiB checkpoint and the
// second reports per-step progress — collapsing them would make the panel look
// hung for the whole load.
enum class SdGenerationState {
    Idle,
    Loading,
    Generating,
    Completed,
    Failed,
    Cancelled,
};

QString generationStateName(SdGenerationState state);

// Weight-type override for the load. Auto keeps whatever the checkpoint
// declares, which is what sd_ctx_params_init() does (wtype = SD_TYPE_COUNT).
// F16 is the lever that matters in practice: an fp32 SD 1.5 checkpoint is
// 4.27 GiB and will not fit a 4 GiB GPU, while the same weights at F16 will.
enum class SdWeightType { Auto, F32, F16, Q8_0, Q5_1, Q5_0, Q4_1, Q4_0 };

QString weightTypeName(SdWeightType type);
bool weightTypeFromName(const QString& name, SdWeightType& out);

// Everything that decides WHICH context has to exist.
//
// Split from the per-generation parameters on purpose: two requests with equal
// load options reuse the same sd_ctx_t, so changing a prompt, a seed or a step
// count never costs a multi-gigabyte reload, while changing a model or a device
// correctly forces one.
struct SdModelLoadOptions {
    SdModelFiles files;
    SdDevicePreference device = SdDevicePreference::Auto;
    SdWeightType weightType = SdWeightType::Auto;
    int threads = 0;                 // 0 -> sd_get_num_physical_cores()

    // Retry on the CPU device when the GPU cannot load or run the model. This
    // is what makes one GPU-enabled build usable on machines with no GPU, an
    // unusable driver, or too little VRAM for the checkpoint — the run is
    // always reported as the CPU run it became, never as GPU acceleration.
    bool allowCpuFallback = true;
    bool enableMmap = true;
    bool flashAttention = false;
    bool diffusionFlashAttention = false;
    bool vaeConvDirect = false;
    bool batchedCfg = false;
    bool autoFit = true;
    int conditioningCacheSize = 4;

    bool operator==(const SdModelLoadOptions& o) const
    {
        return files == o.files && device == o.device && weightType == o.weightType
            && threads == o.threads
            && enableMmap == o.enableMmap
            && flashAttention == o.flashAttention
            && diffusionFlashAttention == o.diffusionFlashAttention
            && vaeConvDirect == o.vaeConvDirect
            && batchedCfg == o.batchedCfg
            && autoFit == o.autoFit
            && conditioningCacheSize == o.conditioningCacheSize;
    }
    bool operator!=(const SdModelLoadOptions& o) const { return !(*this == o); }
};

// Sampling parameters shared by every operation.
struct SdSamplingParams {
    int steps = 20;
    // < 0 asks the library for a random seed; the one it used comes back in the
    // result, so a run is always reproducible after the fact.
    qint64 seed = -1;
    float cfgScale = 7.0f;
    // Empty means "whatever the loaded checkpoint declares", which is how the
    // proof of concept ran and the only safe default across SD 1.x, SDXL, SD3
    // and Flux. Names are the library's own (sd_sample_method_name /
    // sd_scheduler_name), validated before a load is paid for.
    QString sampler;
    QString scheduler;
    int clipSkip = -1;               // -1 -> model default
    float imageCfgScale = 1.0f;
    float distilledGuidance = 0.0f; // 0 keeps the model/library default
    float eta = 0.0f;
    int shiftedTimestep = 0;
    float flowShift = 0.0f;
    QVector<float> customSigmas;
    QVector<int> skipLayerGuidanceLayers;
    float skipLayerGuidanceStart = 0.01f;
    float skipLayerGuidanceEnd = 0.2f;
    float skipLayerGuidanceScale = 0.0f;
};

struct SdVaeTilingParams {
    bool enabled = false;
    int tileWidth = 256;
    int tileHeight = 256;
    float overlap = 0.5f;
};

struct SdCacheParams {
    QString mode = QStringLiteral("disabled");
    float reuseThreshold = 0.2f;
    float startPercent = 0.0f;
    float endPercent = 1.0f;
};

struct SdHiresParams {
    bool enabled = false;
    QString upscaler = QStringLiteral("latent");
    QString modelPath;
    float scale = 2.0f;
    int targetWidth = 0;
    int targetHeight = 0;
    int steps = 0;
    float denoisingStrength = 0.5f;
    int tileSize = 0;
};

struct SdGenerationRequest {
    SdOperation operation = SdOperation::Txt2Img;
    SdModelLoadOptions load;

    QString prompt;
    QString negativePrompt;
    int width = 512;
    int height = 512;
    SdSamplingParams sampling;
    int batchCount = 1;

    // LoRAs are per-generation in this revision (sd_img_gen_params_t::loras),
    // not context parameters, so adding one does not reload the model.
    QVector<SdResolvedLora> loras;

    // Img2Img and Inpaint only. Format is normalised by the service; a null
    // image with an operation that needs one is a request error, not a crash.
    QImage initImage;
    QImage maskImage;                // Inpaint only
    float denoisingStrength = 0.75f; // sd_img_gen_params_t::strength

    QVector<QImage> referenceImages;
    QString referenceImageArgs;
    QImage controlImage;
    float controlStrength = 0.9f;
    QImage ipAdapterImage;
    float ipAdapterStrength = 1.0f;
    QVector<QImage> photoMakerImages;
    QString photoMakerEmbeddingPath;
    float photoMakerStyleStrength = 20.0f;
    QString pulidEmbeddingPath;
    float pulidWeight = 0.8f;

    SdVaeTilingParams vaeTiling;
    SdCacheParams cache;
    SdHiresParams hires;
    bool circularX = false;
    bool circularY = false;
    QString imagePreprocess;

    // Standalone processing operations.
    QString auxiliaryModelPath;      // ESRGAN or ADetailer detector
    int upscaleFactor = 2;
    int tileSize = 0;
    float cannyHigh = 0.8f;
    float cannyLow = 0.1f;
    float cannyWeak = 0.5f;
    float cannyStrong = 1.0f;
    bool cannyInverse = false;

    // Identity of the run, for logs and for the result. Not used by the engine.
    QString label;
};

// What actually happened. Everything the caller needs to describe the run
// without asking the engine a second question.
struct SdGenerationResult {
    SdGenerationState state = SdGenerationState::Idle;
    QString error;                   // user-presentable; empty on success

    // Format_RGBA8888 and owning its own buffer, so it stays valid after every
    // stable-diffusion.cpp allocation behind it has been freed. Ownership of
    // the pixels is Hazor's from the moment this is filled.
    QImage image;
    QSize size;

    qint64 seed = -1;                // the seed the sampling really used
    QString modelId;
    QString modelVersion;            // sd_get_model_version_name()

    // The EFFECTIVE device, not the compiled backend and not what was asked
    // for. `cpuFallback` says a GPU was wanted and the run ended up on the CPU.
    QString deviceName;
    bool gpu = false;
    bool cpuFallback = false;
    QString backendNote;             // why, when the device is not what was asked for

    qint64 loadMs = 0;               // 0 when a cached context was reused
    bool contextReused = false;
    qint64 generateMs = 0;

    // The library's own sdcpp.generation/v1 JSON for the run: prompt, sampler,
    // scheduler, guidance, LoRAs. Kept verbatim rather than re-derived, so it
    // cannot drift from what the engine actually did.
    QString generationInfo;

    bool ok() const { return state == SdGenerationState::Completed && !image.isNull(); }
};

// Progress plumbing for a run. Both callbacks fire on the inference thread and
// must not touch Qt objects directly — SdGenerationJob marshals them.
struct SdProgressSink {
    std::function<void(SdGenerationState state, const QString& detail)> onState;
    std::function<void(int step, int steps, float seconds)> onStep;
};

} // namespace sdlocal
