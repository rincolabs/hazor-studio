#include "ai/sd/SdNodeCatalog.hpp"

#include "ai/sd/SdBackend.hpp"
#include "ai/sd/SdGenerationService.hpp"
#include "ai/sd/SdGenerationTypes.hpp"

#include <QCoreApplication>
#include <QJsonArray>

namespace sdlocal {
namespace {

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

// ── Spec builders ───────────────────────────────────────────────────────────
//
// Small helpers rather than aggregate initialisation: every spec sets three or
// four of a dozen fields, and naming them at the call site is what makes the
// table below readable as a contract.

SdPropertySpec text(const char* key, const QString& label, const char* hint = "")
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::Text;
    s.defaultValue = QString();
    s.hint = QString::fromUtf8(hint);
    return s;
}

SdPropertySpec line(const char* key, const QString& label, const QString& defaultValue,
                    const char* hint = "")
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::String;
    s.defaultValue = defaultValue;
    s.hint = QString::fromUtf8(hint);
    return s;
}

SdPropertySpec boolean(const char* key, const QString& label, bool defaultValue,
                       const char* hint = "")
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::Bool;
    s.defaultValue = defaultValue;
    s.hint = QString::fromUtf8(hint);
    return s;
}

SdPropertySpec integer(const char* key, const QString& label, int defaultValue,
                       int minimum, int maximum, double step = 1.0, const char* hint = "")
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::Int;
    s.defaultValue = defaultValue;
    s.minimum = minimum;
    s.maximum = maximum;
    s.step = step;
    s.hint = QString::fromUtf8(hint);
    return s;
}

SdPropertySpec number(const char* key, const QString& label, double defaultValue,
                      double minimum, double maximum, double step, const char* hint = "")
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::Number;
    s.defaultValue = defaultValue;
    s.minimum = minimum;
    s.maximum = maximum;
    s.step = step;
    s.hint = QString::fromUtf8(hint);
    return s;
}

SdPropertySpec dimension(const char* key, const QString& label)
{
    // The bounds and the multiple-of are the validator's, not a guess: a latent
    // dimension outside them is rejected before a model is read.
    SdPropertySpec s = integer(key, label, 512, 8, 8192, 64.0);
    s.multipleOf = 8;
    s.hint = tr_("A multiple of 8; the VAE downsamples by that factor.");
    return s;
}

SdPropertySpec seed(const char* key, const QString& label)
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::Seed;
    // A string, like the library's own generation_info: a 64-bit seed does not
    // survive JSON's double.
    s.defaultValue = QStringLiteral("-1");
    s.minimum = -1.0;
    s.maximum = 0.0;   // unbounded above; the validator only parses the integer
    s.step = 1.0;
    s.hint = tr_("-1 asks for a random seed; the one that was used comes back "
                 "with the result.");
    return s;
}

SdPropertySpec enumeration(const char* key, const QString& label, const QString& defaultValue,
                           const QStringList& options, bool allowsEmpty, const char* hint = "",
                           bool strictOptions = false)
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::Enum;
    s.defaultValue = defaultValue;
    s.options = options;
    s.allowsEmpty = allowsEmpty;
    s.strictOptions = strictOptions;
    s.hint = QString::fromUtf8(hint);
    return s;
}

SdPropertySpec modelRef(const char* key, const QString& label, const QString& defaultValue,
                        const QVector<SdModelCategory>& categories, const char* hint = "")
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::ModelRef;
    s.defaultValue = defaultValue;
    s.modelCategories = categories;
    s.hint = QString::fromUtf8(hint);
    return s;
}

SdPropertySpec modelRefList(const char* key, const QString& label,
                            const QVector<SdModelCategory>& categories, const char* hint = "")
{
    SdPropertySpec s;
    s.key = QString::fromUtf8(key);
    s.label = label;
    s.kind = SdPropertyKind::ModelRefList;
    s.defaultValue = QJsonArray();
    s.modelCategories = categories;
    s.hint = QString::fromUtf8(hint);
    return s;
}

QStringList weightTypeOptions()
{
    QStringList out;
    for (const SdWeightType type : { SdWeightType::Auto, SdWeightType::F32, SdWeightType::F16,
                                     SdWeightType::Q8_0, SdWeightType::Q5_1, SdWeightType::Q5_0,
                                     SdWeightType::Q4_1, SdWeightType::Q4_0 })
        out << weightTypeName(type);
    return out;
}

QStringList devicePreferenceOptions()
{
    QStringList out;
    for (const SdDevicePreference preference : { SdDevicePreference::Auto, SdDevicePreference::Gpu,
                                                 SdDevicePreference::Cpu })
        out << devicePreferenceName(preference);
    return out;
}

} // namespace

// ── Vocabulary ──────────────────────────────────────────────────────────────

QString propertyKindName(SdPropertyKind kind)
{
    switch (kind) {
    case SdPropertyKind::String:       return QStringLiteral("string");
    case SdPropertyKind::Text:         return QStringLiteral("text");
    case SdPropertyKind::Bool:         return QStringLiteral("bool");
    case SdPropertyKind::Int:          return QStringLiteral("int");
    case SdPropertyKind::Number:       return QStringLiteral("number");
    case SdPropertyKind::Seed:         return QStringLiteral("seed");
    case SdPropertyKind::Enum:         return QStringLiteral("enum");
    case SdPropertyKind::ModelRef:     return QStringLiteral("modelRef");
    case SdPropertyKind::ModelRefList: return QStringLiteral("modelRefList");
    }
    return QStringLiteral("string");
}

QString nodeCategoryName(SdNodeCategory category)
{
    switch (category) {
    case SdNodeCategory::Models:       return QStringLiteral("models");
    case SdNodeCategory::Conditioning: return QStringLiteral("conditioning");
    case SdNodeCategory::Sampling:     return QStringLiteral("sampling");
    case SdNodeCategory::Input:        return QStringLiteral("input");
    case SdNodeCategory::Processing:   return QStringLiteral("processing");
    case SdNodeCategory::Generation:   return QStringLiteral("generation");
    case SdNodeCategory::Output:       return QStringLiteral("output");
    }
    return QStringLiteral("models");
}

QString nodeCategoryLabel(SdNodeCategory category)
{
    switch (category) {
    case SdNodeCategory::Models:       return tr_("Models");
    case SdNodeCategory::Conditioning: return tr_("Conditioning");
    case SdNodeCategory::Sampling:     return tr_("Sampling");
    case SdNodeCategory::Input:        return tr_("Input");
    case SdNodeCategory::Processing:   return tr_("Processing");
    case SdNodeCategory::Generation:   return tr_("Generation");
    case SdNodeCategory::Output:       return tr_("Output");
    }
    return tr_("Models");
}

SdNodeCategory nodeCategory(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:
    case SdNodeType::Lora:
    case SdNodeType::ControlNetLoader:
    case SdNodeType::IpAdapterLoader: return SdNodeCategory::Models;
    case SdNodeType::ControlNetApply:
    case SdNodeType::IpAdapterApply:
    case SdNodeType::PhotoMaker:
    case SdNodeType::PuLid:
    case SdNodeType::Prompt:
    case SdNodeType::NegativePrompt: return SdNodeCategory::Conditioning;
    case SdNodeType::Sampling:       return SdNodeCategory::Sampling;
    case SdNodeType::InputImage:
    case SdNodeType::InputMask:      return SdNodeCategory::Input;
    case SdNodeType::Canny:
    case SdNodeType::Upscale:
    case SdNodeType::ADetailer:      return SdNodeCategory::Processing;
    case SdNodeType::Generate:       return SdNodeCategory::Generation;
    case SdNodeType::ImageOutput:    return SdNodeCategory::Output;
    }
    return SdNodeCategory::Models;
}

QVector<SdNodeCategory> allNodeCategories()
{
    return { SdNodeCategory::Models, SdNodeCategory::Conditioning, SdNodeCategory::Sampling,
             SdNodeCategory::Input, SdNodeCategory::Processing,
             SdNodeCategory::Generation, SdNodeCategory::Output };
}

QString nodeTypeLabel(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:     return tr_("Load Checkpoint");
    case SdNodeType::Lora:           return tr_("Load LoRA");
    case SdNodeType::ControlNetLoader:return tr_("Load ControlNet");
    case SdNodeType::ControlNetApply:return tr_("Apply ControlNet");
    case SdNodeType::IpAdapterLoader:return tr_("Load IP-Adapter");
    case SdNodeType::IpAdapterApply:return tr_("Apply IP-Adapter");
    case SdNodeType::PhotoMaker:     return tr_("PhotoMaker Identity");
    case SdNodeType::PuLid:          return tr_("PuLID Identity");
    case SdNodeType::Prompt:         return tr_("Positive Prompt");
    case SdNodeType::NegativePrompt: return tr_("Negative Prompt");
    case SdNodeType::Sampling:       return tr_("Sampling Parameters");
    case SdNodeType::InputImage:     return tr_("Input Image");
    case SdNodeType::InputMask:      return tr_("Input Mask");
    case SdNodeType::Canny:          return tr_("Canny Edge");
    case SdNodeType::Upscale:        return tr_("ESRGAN Upscale");
    case SdNodeType::ADetailer:      return tr_("ADetailer");
    case SdNodeType::Generate:       return tr_("Generate");
    case SdNodeType::ImageOutput:    return tr_("Canvas Output");
    }
    return tr_("Node");
}

QVector<SdNodeType> allNodeTypes()
{
    // Palette order: the types of one category stay together, and the order
    // inside a category is the order a graph is normally built in.
    return { SdNodeType::Checkpoint, SdNodeType::Lora,
             SdNodeType::ControlNetLoader, SdNodeType::IpAdapterLoader,
             SdNodeType::Prompt, SdNodeType::NegativePrompt,
             SdNodeType::ControlNetApply, SdNodeType::IpAdapterApply,
             SdNodeType::PhotoMaker, SdNodeType::PuLid,
             SdNodeType::Sampling,
             SdNodeType::InputImage, SdNodeType::InputMask,
             SdNodeType::Canny, SdNodeType::Upscale, SdNodeType::ADetailer,
             SdNodeType::Generate,
             SdNodeType::ImageOutput };
}

// ── The table ───────────────────────────────────────────────────────────────
//
// Every key here is a key SdWorkflowExecutor reads. A property with no spec is
// a property the executor ignores, so adding one to a document has no effect —
// which is exactly why the frontend builds its widgets from this list instead
// of from a list of its own.

QVector<SdPropertySpec> nodeProperties(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:
        return {
            // "default" resolves through SdModelManager on whatever machine
            // runs the document, which is what keeps a workflow portable.
            modelRef("modelId", tr_("Model"), QStringLiteral("default"),
                     { SdModelCategory::Checkpoint, SdModelCategory::DiffusionModel },
                     "A registered checkpoint, or \"default\" for the registry's default."),
            modelRefList("textEncoderIds", tr_("Text encoders"),
                         { SdModelCategory::TextEncoder },
                         "Required only when the model is bare diffusion weights."),
            modelRef("vaeId", tr_("VAE"), QString(), { SdModelCategory::Vae },
                     "Leave empty to use the VAE the checkpoint embeds."),
            modelRefList("embeddingIds", tr_("Textual inversions"),
                         { SdModelCategory::Embedding },
                         "Embeddings are registered by name and loaded with the context."),
            enumeration("weightType", tr_("Weight type"), weightTypeName(SdWeightType::Auto),
                        weightTypeOptions(), /*allowsEmpty*/ true,
                        "f16 halves the memory an fp32 checkpoint needs."),
            enumeration("device", tr_("Device"),
                        devicePreferenceName(SdDevicePreference::Auto),
                        devicePreferenceOptions(), /*allowsEmpty*/ true),
            integer("threads", tr_("Threads"), 0, 0, 1024, 1.0,
                    "0 uses one thread per physical core."),
            boolean("allowCpuFallback", tr_("Allow CPU fallback"), true,
                    "Retry on the CPU when the GPU cannot load or run the model."),
            boolean("enableMmap", tr_("Memory map weights"), true),
            boolean("flashAttention", tr_("Flash attention"), false),
            boolean("diffusionFlashAttention", tr_("Diffusion flash attention"), false),
            boolean("vaeConvDirect", tr_("Direct VAE convolution"), false),
            boolean("batchedCfg", tr_("Batched CFG"), false),
            boolean("autoFit", tr_("Auto-fit memory"), true),
            integer("conditioningCacheSize", tr_("Conditioning cache"), 4, 0, 64),
        };
    case SdNodeType::Lora:
        return {
            modelRef("modelId", tr_("LoRA"), QString(), { SdModelCategory::Lora }),
            number("weight", tr_("Weight"), 1.0, -10.0, 10.0, 0.05),
        };
    case SdNodeType::ControlNetLoader:
        return {
            modelRef("modelId", tr_("ControlNet"), QString(),
                     { SdModelCategory::ControlNet }),
        };
    case SdNodeType::ControlNetApply:
        return {
            number("strength", tr_("Strength"), 0.9, 0.0, 10.0, 0.05),
        };
    case SdNodeType::IpAdapterLoader:
        return {
            modelRef("modelId", tr_("IP-Adapter"), QString(),
                     { SdModelCategory::IpAdapter }),
            modelRef("visionEncoderId", tr_("Vision encoder"), QString(),
                     { SdModelCategory::VisionEncoder },
                     "Required by adapters that do not embed their vision encoder."),
        };
    case SdNodeType::IpAdapterApply:
        return {
            number("strength", tr_("Strength"), 1.0, 0.0, 10.0, 0.05),
        };
    case SdNodeType::PhotoMaker:
        return {
            modelRef("modelId", tr_("PhotoMaker"), QString(),
                     { SdModelCategory::PhotoMaker }),
            modelRef("embeddingId", tr_("Identity embedding"), QString(),
                     { SdModelCategory::IdentityEmbedding }),
            number("styleStrength", tr_("Style strength"), 20.0, 0.0, 100.0, 0.5),
        };
    case SdNodeType::PuLid:
        return {
            modelRef("modelId", tr_("PuLID weights"), QString(),
                     { SdModelCategory::PuLid }),
            modelRef("embeddingId", tr_("Identity embedding"), QString(),
                     { SdModelCategory::IdentityEmbedding }),
            number("weight", tr_("Identity weight"), 0.8, 0.0, 5.0, 0.05),
        };
    case SdNodeType::Prompt:
        return { text("text", tr_("Prompt"), "What the image should contain.") };
    case SdNodeType::NegativePrompt:
        return { text("text", tr_("Negative prompt"), "What the image should avoid.") };
    case SdNodeType::Sampling:
        return {
            dimension("width", tr_("Width")),
            dimension("height", tr_("Height")),
            integer("steps", tr_("Steps"), 20, 1, 1000),
            seed("seed", tr_("Seed")),
            number("cfgScale", tr_("CFG scale"), 7.0, 0.0, 100.0, 0.5,
                   "Above 1.0 the UNet runs twice per step."),
            // Empty is the only safe default across SD 1.x, SDXL, SD3 and Flux:
            // it keeps whatever the loaded checkpoint declares.
            enumeration("sampler", tr_("Sampler"), QString(),
                        SdGenerationService::samplerNames(), /*allowsEmpty*/ true,
                        "Empty keeps the checkpoint's own sampler."),
            enumeration("scheduler", tr_("Scheduler"), QString(),
                        SdGenerationService::schedulerNames(), /*allowsEmpty*/ true,
                        "Empty keeps the checkpoint's own scheduler."),
            integer("clipSkip", tr_("CLIP skip"), -1, -1, 12, 1.0,
                    "-1 uses the model default."),
            number("denoisingStrength", tr_("Denoising strength"), 0.75, 0.0, 1.0, 0.05,
                   "img2img and inpainting only."),
            number("imageCfgScale", tr_("Image CFG"), 1.0, 0.0, 100.0, 0.1),
            number("distilledGuidance", tr_("Distilled guidance"), 0.0, 0.0, 100.0, 0.1,
                   "0 keeps the model default."),
            number("eta", tr_("Eta"), 0.0, 0.0, 1.0, 0.01),
            integer("shiftedTimestep", tr_("Shifted timestep"), 0, 0, 1000),
            number("flowShift", tr_("Flow shift"), 0.0, -100.0, 100.0, 0.1,
                   "0 keeps the model default."),
            line("customSigmas", tr_("Custom sigmas"), QString(),
                 "Comma-separated non-negative values; at least two when set."),
            line("skipLayerGuidanceLayers", tr_("SLG layers"), QString(),
                 "Comma-separated layer indices."),
            number("skipLayerGuidanceStart", tr_("SLG start"), 0.01, 0.0, 1.0, 0.01),
            number("skipLayerGuidanceEnd", tr_("SLG end"), 0.2, 0.0, 1.0, 0.01),
            number("skipLayerGuidanceScale", tr_("SLG scale"), 0.0, -100.0, 100.0, 0.1),
        };
    case SdNodeType::InputImage:
        return {
            enumeration("source", tr_("Source"), QStringLiteral("canvas"),
                        { QStringLiteral("canvas"), QStringLiteral("layer"),
                          QStringLiteral("composite"), QStringLiteral("current_layer") },
                        /*allowsEmpty*/ true,
                        "Pixels are read from the active document when the run starts.",
                        /*strictOptions*/ true),
            integer("layerIndex", tr_("Layer index"), -1, -1, 100000, 1.0,
                    "-1 uses the active layer; otherwise uses the selected flat layer index."),
        };
    case SdNodeType::InputMask:
        return {
            enumeration("source", tr_("Source"), QStringLiteral("selection"),
                        { QStringLiteral("selection") }, /*allowsEmpty*/ true,
                        "The active selection, white where the engine regenerates.",
                        /*strictOptions*/ true),
        };
    case SdNodeType::Canny:
        return {
            number("highThreshold", tr_("High threshold"), 0.8, 0.0, 1.0, 0.01),
            number("lowThreshold", tr_("Low threshold"), 0.1, 0.0, 1.0, 0.01),
            number("weak", tr_("Weak edge"), 0.5, 0.0, 1.0, 0.01),
            number("strong", tr_("Strong edge"), 1.0, 0.0, 1.0, 0.01),
            boolean("inverse", tr_("Invert"), false),
        };
    case SdNodeType::Upscale:
        return {
            modelRef("modelId", tr_("ESRGAN model"), QString(),
                     { SdModelCategory::Upscaler },
                     "stable-diffusion.cpp requires an RGB ESRGAN GGUF model."),
            integer("factor", tr_("Scale factor"), 2, 1, 8),
            integer("tileSize", tr_("Tile size"), 0, 0, 4096, 8.0,
                    "0 lets the backend choose."),
            enumeration("device", tr_("Device"),
                        devicePreferenceName(SdDevicePreference::Auto),
                        devicePreferenceOptions(), true),
        };
    case SdNodeType::ADetailer:
        return {
            modelRef("modelId", tr_("Detector"), QString(),
                     { SdModelCategory::ADetailer }),
        };
    case SdNodeType::Generate: {
        // Part of the contract and validated, but not offered: the generation
        // service hands back the first image of a batch and discards the rest,
        // so a batch above 1 would only spend minutes on pixels nothing reads.
        SdPropertySpec batch = integer("batchCount", tr_("Batch count"), 1, 1, 16, 1.0);
        batch.editable = false;
        batch.hint = tr_("This build inserts the first image of a batch only.");
        return {
            batch,
            boolean("vaeTiling", tr_("VAE tiling"), false),
            integer("vaeTileWidth", tr_("VAE tile width"), 256, 64, 4096, 8.0),
            integer("vaeTileHeight", tr_("VAE tile height"), 256, 64, 4096, 8.0),
            number("vaeTileOverlap", tr_("VAE tile overlap"), 0.5, 0.0, 1.0, 0.05),
            enumeration("cacheMode", tr_("Cache"), QStringLiteral("disabled"),
                        { QStringLiteral("disabled"), QStringLiteral("easycache"),
                          QStringLiteral("ucache"), QStringLiteral("dbcache"),
                          QStringLiteral("taylorseer"), QStringLiteral("cache-dit"),
                          QStringLiteral("spectrum") }, false, "", true),
            number("cacheThreshold", tr_("Cache threshold"), 0.2, 0.0, 10.0, 0.01),
            number("cacheStart", tr_("Cache start"), 0.0, 0.0, 1.0, 0.01),
            number("cacheEnd", tr_("Cache end"), 1.0, 0.0, 1.0, 0.01),
            boolean("hiresEnabled", tr_("High-resolution fix"), false),
            enumeration("hiresUpscaler", tr_("High-res upscaler"), QStringLiteral("Latent"),
                        { QStringLiteral("Latent"), QStringLiteral("Latent (nearest)"),
                          QStringLiteral("Latent (nearest-exact)"),
                          QStringLiteral("Latent (antialiased)"),
                          QStringLiteral("Latent (bicubic)"),
                          QStringLiteral("Latent (bicubic antialiased)"),
                          QStringLiteral("Lanczos"), QStringLiteral("Nearest"),
                          QStringLiteral("Model") }, false, "", true),
            modelRef("hiresModelId", tr_("High-res ESRGAN"), QString(),
                     { SdModelCategory::Upscaler }),
            number("hiresScale", tr_("High-res scale"), 2.0, 0.1, 8.0, 0.1),
            integer("hiresWidth", tr_("High-res width"), 0, 0, 32768, 8.0),
            integer("hiresHeight", tr_("High-res height"), 0, 0, 32768, 8.0),
            integer("hiresSteps", tr_("High-res steps"), 0, 0, 1000),
            number("hiresDenoising", tr_("High-res denoising"), 0.5, 0.0001, 1.0, 0.05),
            integer("hiresTileSize", tr_("High-res tile"), 0, 0, 4096, 8.0),
            boolean("circularX", tr_("Circular X"), false),
            boolean("circularY", tr_("Circular Y"), false),
            line("referenceArgs", tr_("Reference image arguments"), QString()),
            line("imagePreprocess", tr_("Image preprocess rules"), QString(),
                 "Semicolon-separated stable-diffusion.cpp preprocessing rules."),
        };
    }
    case SdNodeType::ImageOutput:
        return {
            line("layerName", tr_("Layer name"), QStringLiteral("AI Generated"),
                 "Name of the pixel layer the result is inserted as."),
        };
    }
    return {};
}

bool findProperty(SdNodeType type, const QString& key, SdPropertySpec& out)
{
    for (const SdPropertySpec& spec : nodeProperties(type)) {
        if (spec.key == key) {
            out = spec;
            return true;
        }
    }
    return false;
}

QJsonValue propertyDefault(SdNodeType type, const QString& key)
{
    SdPropertySpec spec;
    if (!findProperty(type, key, spec))
        return {};
    return spec.defaultValue;
}

// ── Serialisation for the frontend ──────────────────────────────────────────

namespace {

QJsonObject portToJson(const SdPortSpec& spec)
{
    QJsonObject o;
    o[QStringLiteral("id")] = spec.id;
    o[QStringLiteral("type")] = portTypeName(spec.type);
    o[QStringLiteral("required")] = spec.required;
    o[QStringLiteral("multiple")] = spec.multiple;
    return o;
}

QJsonObject propertyToJson(const SdPropertySpec& spec)
{
    QJsonObject o;
    o[QStringLiteral("key")] = spec.key;
    o[QStringLiteral("label")] = spec.label;
    o[QStringLiteral("kind")] = propertyKindName(spec.kind);
    o[QStringLiteral("default")] = spec.defaultValue;
    o[QStringLiteral("editable")] = spec.editable;
    if (!spec.hint.isEmpty())
        o[QStringLiteral("hint")] = spec.hint;

    // Only the keys a kind actually constrains, so the frontend never has to
    // guess whether a zero means "zero" or "not applicable".
    switch (spec.kind) {
    case SdPropertyKind::Int:
    case SdPropertyKind::Number:
        o[QStringLiteral("minimum")] = spec.minimum;
        o[QStringLiteral("maximum")] = spec.maximum;
        o[QStringLiteral("step")] = spec.step;
        if (spec.multipleOf > 0)
            o[QStringLiteral("multipleOf")] = spec.multipleOf;
        break;
    case SdPropertyKind::Seed:
        // Bounded below and not above: -1 means "pick one", and the validator
        // only has to parse the integer. Leaving `maximum` out says that,
        // where a zero would read as a ceiling of zero.
        o[QStringLiteral("minimum")] = spec.minimum;
        o[QStringLiteral("step")] = spec.step;
        break;
    case SdPropertyKind::Enum:
        o[QStringLiteral("options")] = QJsonArray::fromStringList(spec.options);
        o[QStringLiteral("allowsEmpty")] = spec.allowsEmpty;
        o[QStringLiteral("strictOptions")] = spec.strictOptions;
        break;
    case SdPropertyKind::ModelRef:
    case SdPropertyKind::ModelRefList: {
        QJsonArray categories;
        for (const SdModelCategory category : spec.modelCategories)
            categories.append(modelCategoryName(category));
        o[QStringLiteral("modelCategories")] = categories;
        break;
    }
    case SdPropertyKind::String:
    case SdPropertyKind::Text:
    case SdPropertyKind::Bool:
        break;
    }
    return o;
}

} // namespace

QJsonObject describeNodeCatalog()
{
    QJsonArray categories;
    for (const SdNodeCategory category : allNodeCategories()) {
        QJsonObject o;
        o[QStringLiteral("id")] = nodeCategoryName(category);
        o[QStringLiteral("label")] = nodeCategoryLabel(category);
        categories.append(o);
    }

    QJsonArray nodes;
    for (const SdNodeType type : allNodeTypes()) {
        QJsonObject o;
        o[QStringLiteral("type")] = nodeTypeName(type);
        o[QStringLiteral("label")] = nodeTypeLabel(type);
        o[QStringLiteral("category")] = nodeCategoryName(nodeCategory(type));

        QJsonArray inputs;
        for (const SdPortSpec& spec : nodeInputs(type))
            inputs.append(portToJson(spec));
        o[QStringLiteral("inputs")] = inputs;

        QJsonArray outputs;
        for (const SdPortSpec& spec : nodeOutputs(type))
            outputs.append(portToJson(spec));
        o[QStringLiteral("outputs")] = outputs;

        QJsonArray properties;
        for (const SdPropertySpec& spec : nodeProperties(type))
            properties.append(propertyToJson(spec));
        o[QStringLiteral("properties")] = properties;

        nodes.append(o);
    }

    QJsonArray portTypes;
    for (const SdPortType type : { SdPortType::Model, SdPortType::Conditioning,
                                   SdPortType::Sampling, SdPortType::Image,
                                   SdPortType::Mask, SdPortType::ControlNet,
                                   SdPortType::Control, SdPortType::IpAdapter,
                                   SdPortType::ImageConditioning, SdPortType::PhotoMaker,
                                   SdPortType::PuLid })
        portTypes.append(portTypeName(type));

    QJsonArray modelCategories;
    for (const SdModelCategory category : { SdModelCategory::Checkpoint,
                                            SdModelCategory::DiffusionModel,
                                            SdModelCategory::TextEncoder,
                                            SdModelCategory::VisionEncoder,
                                            SdModelCategory::Vae,
                                            SdModelCategory::Lora,
                                            SdModelCategory::Embedding,
                                            SdModelCategory::ControlNet,
                                            SdModelCategory::IpAdapter,
                                            SdModelCategory::PhotoMaker,
                                            SdModelCategory::PuLid,
                                            SdModelCategory::IdentityEmbedding,
                                            SdModelCategory::Upscaler,
                                            SdModelCategory::ADetailer })
        modelCategories.append(modelCategoryName(category));

    QJsonObject root;
    // The frontend refuses to build a graph for a version it does not know, so
    // the number it must write into a document travels with the contract.
    root[QStringLiteral("workflowVersion")] = SdWorkflowDefinition::kCurrentVersion;
    root[QStringLiteral("categories")] = categories;
    root[QStringLiteral("nodes")] = nodes;
    root[QStringLiteral("portTypes")] = portTypes;
    root[QStringLiteral("modelCategories")] = modelCategories;
    return root;
}

} // namespace sdlocal
