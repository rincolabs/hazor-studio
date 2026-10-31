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
    case SdNodeCategory::Latent:       return QStringLiteral("latent");
    case SdNodeCategory::Sampling:     return QStringLiteral("sampling");
    case SdNodeCategory::Input:        return QStringLiteral("input");
    case SdNodeCategory::Processing:   return QStringLiteral("processing");
    case SdNodeCategory::Output:       return QStringLiteral("output");
    }
    return QStringLiteral("models");
}

QString nodeCategoryLabel(SdNodeCategory category)
{
    switch (category) {
    case SdNodeCategory::Models:       return tr_("Models");
    case SdNodeCategory::Conditioning: return tr_("Conditioning");
    case SdNodeCategory::Latent:       return tr_("Latent");
    case SdNodeCategory::Sampling:     return tr_("Sampling");
    case SdNodeCategory::Input:        return tr_("Input");
    case SdNodeCategory::Processing:   return tr_("Processing");
    case SdNodeCategory::Output:       return tr_("Output");
    }
    return tr_("Models");
}

SdNodeCategory nodeCategory(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:
    case SdNodeType::DiffusionModelLoader:
    case SdNodeType::TextEncoderLoader:
    case SdNodeType::VaeLoader:
    case SdNodeType::EngineOptions:
    case SdNodeType::ControlNetLoader:
    case SdNodeType::IpAdapterLoader:
    case SdNodeType::Lora:
    case SdNodeType::ModelSampling:
    case SdNodeType::SkipLayerGuidance:
    case SdNodeType::ClipSetLastLayer: return SdNodeCategory::Models;
    case SdNodeType::ClipTextEncode:
    case SdNodeType::ReferenceImages:
    case SdNodeType::ControlNetApply:
    case SdNodeType::IpAdapterApply:
    case SdNodeType::PhotoMaker:
    case SdNodeType::PuLid:            return SdNodeCategory::Conditioning;
    case SdNodeType::EmptyLatentImage:
    case SdNodeType::VaeEncode:
    case SdNodeType::VaeEncodeInpaint:
    case SdNodeType::VaeDecode:        return SdNodeCategory::Latent;
    case SdNodeType::KSampler:
    case SdNodeType::HiresFix:         return SdNodeCategory::Sampling;
    case SdNodeType::InputImage:
    case SdNodeType::InputMask:        return SdNodeCategory::Input;
    case SdNodeType::Canny:
    case SdNodeType::Upscale:
    case SdNodeType::ADetailer:        return SdNodeCategory::Processing;
    case SdNodeType::ImageOutput:      return SdNodeCategory::Output;
    }
    return SdNodeCategory::Models;
}

QVector<SdNodeCategory> allNodeCategories()
{
    return { SdNodeCategory::Models, SdNodeCategory::Conditioning, SdNodeCategory::Latent,
             SdNodeCategory::Sampling, SdNodeCategory::Input, SdNodeCategory::Processing,
             SdNodeCategory::Output };
}

QString nodeTypeLabel(SdNodeType type)
{
    switch (type) {
    case SdNodeType::Checkpoint:     return tr_("Load Checkpoint");
    case SdNodeType::DiffusionModelLoader: return tr_("Load Diffusion Model");
    case SdNodeType::TextEncoderLoader: return tr_("Load Text Encoders");
    case SdNodeType::VaeLoader:      return tr_("Load VAE");
    case SdNodeType::EngineOptions:  return tr_("Engine Options");
    case SdNodeType::ControlNetLoader:return tr_("Load ControlNet");
    case SdNodeType::IpAdapterLoader:return tr_("Load IP-Adapter");
    case SdNodeType::Lora:           return tr_("Load LoRA");
    case SdNodeType::ModelSampling:  return tr_("Model Sampling");
    case SdNodeType::SkipLayerGuidance: return tr_("Skip Layer Guidance");
    case SdNodeType::ClipSetLastLayer: return tr_("CLIP Set Last Layer");
    case SdNodeType::ClipTextEncode: return tr_("CLIP Text Encode");
    case SdNodeType::ReferenceImages:return tr_("Reference Images");
    case SdNodeType::ControlNetApply:return tr_("Apply ControlNet");
    case SdNodeType::IpAdapterApply: return tr_("Apply IP-Adapter");
    case SdNodeType::PhotoMaker:     return tr_("PhotoMaker Identity");
    case SdNodeType::PuLid:          return tr_("PuLID Identity");
    case SdNodeType::EmptyLatentImage: return tr_("Empty Latent Image");
    case SdNodeType::VaeEncode:      return tr_("VAE Encode");
    case SdNodeType::VaeEncodeInpaint: return tr_("VAE Encode (Inpaint)");
    case SdNodeType::KSampler:       return tr_("KSampler");
    case SdNodeType::HiresFix:       return tr_("Hi-Res Fix");
    case SdNodeType::InputImage:     return tr_("Canvas Image");
    case SdNodeType::InputMask:      return tr_("Canvas Mask");
    case SdNodeType::Canny:          return tr_("Canny Edge");
    case SdNodeType::Upscale:        return tr_("Upscale Image (ESRGAN)");
    case SdNodeType::ADetailer:      return tr_("ADetailer");
    case SdNodeType::VaeDecode:      return tr_("VAE Decode");
    case SdNodeType::ImageOutput:    return tr_("Canvas Output");
    }
    return tr_("Node");
}

QVector<SdNodeType> allNodeTypes()
{
    // Palette order: the types of one category stay together, and the order
    // inside a category is the order a graph is normally built in.
    return { SdNodeType::Checkpoint, SdNodeType::DiffusionModelLoader,
             SdNodeType::TextEncoderLoader, SdNodeType::VaeLoader,
             SdNodeType::EngineOptions,
             SdNodeType::ControlNetLoader, SdNodeType::IpAdapterLoader,
             SdNodeType::Lora, SdNodeType::ClipSetLastLayer,
             SdNodeType::ModelSampling, SdNodeType::SkipLayerGuidance,
             SdNodeType::ClipTextEncode, SdNodeType::ReferenceImages,
             SdNodeType::ControlNetApply, SdNodeType::IpAdapterApply,
             SdNodeType::PhotoMaker, SdNodeType::PuLid,
             SdNodeType::EmptyLatentImage, SdNodeType::VaeEncode,
             SdNodeType::VaeEncodeInpaint, SdNodeType::VaeDecode,
             SdNodeType::KSampler, SdNodeType::HiresFix,
             SdNodeType::InputImage, SdNodeType::InputMask,
             SdNodeType::Canny, SdNodeType::Upscale, SdNodeType::ADetailer,
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
                     { SdModelCategory::Checkpoint },
                     "A registered checkpoint, or \"default\" for the registry's default."),
            // ComfyUI has no loader for these: they are named in the prompt and
            // read by the tokenizer. Here the context loads them by name.
            modelRefList("embeddingIds", tr_("Textual inversions"),
                         { SdModelCategory::Embedding }),
        };
    case SdNodeType::DiffusionModelLoader:
        return {
            modelRef("modelId", tr_("Diffusion model"), QString(),
                     { SdModelCategory::DiffusionModel },
                     "Bare weights: wire text encoders and a VAE of your own."),
            modelRefList("embeddingIds", tr_("Textual inversions"),
                         { SdModelCategory::Embedding }),
        };
    case SdNodeType::TextEncoderLoader:
        return {
            // One node for the whole set, like ComfyUI's DualCLIPLoader: the
            // library takes one path per role and the registry infers the role.
            modelRefList("modelIds", tr_("Text encoders"),
                         { SdModelCategory::TextEncoder }),
        };
    case SdNodeType::VaeLoader:
        return {
            modelRef("modelId", tr_("VAE"), QString(), { SdModelCategory::Vae }),
        };
    case SdNodeType::EngineOptions:
        // ComfyUI has no node for any of this — it is launch flags there. The
        // node is optional, and its absence means the backend's own defaults.
        return {
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
            enumeration("cacheMode", tr_("Step cache"), QStringLiteral("disabled"),
                        { QStringLiteral("disabled"), QStringLiteral("easycache"),
                          QStringLiteral("ucache"), QStringLiteral("dbcache"),
                          QStringLiteral("taylorseer"), QStringLiteral("cache-dit"),
                          QStringLiteral("spectrum") }, false, "", true),
            number("cacheThreshold", tr_("Cache threshold"), 0.2, 0.0, 10.0, 0.01),
            number("cacheStart", tr_("Cache start"), 0.0, 0.0, 1.0, 0.01),
            number("cacheEnd", tr_("Cache end"), 1.0, 0.0, 1.0, 0.01),
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
    case SdNodeType::Lora:
        return {
            modelRef("modelId", tr_("LoRA"), QString(), { SdModelCategory::Lora }),
            number("weight", tr_("Weight"), 1.0, -10.0, 10.0, 0.05),
        };
    case SdNodeType::ClipSetLastLayer:
        return {
            integer("stopAtClipLayer", tr_("Stop at CLIP layer"), -1, -12, -1, 1.0,
                    "-1 keeps every layer."),
        };
    case SdNodeType::ModelSampling:
        return {
            number("shift", tr_("Shift"), 0.0, -100.0, 100.0, 0.1,
                   "0 keeps the model default."),
            integer("shiftedTimestep", tr_("Shifted timestep"), 0, 0, 1000, 1.0,
                    "0 disables the timestep shift."),
        };
    case SdNodeType::SkipLayerGuidance:
        return {
            line("layers", tr_("Layers"), QString(),
                 "Comma-separated layer indices."),
            number("scale", tr_("Scale"), 0.0, -100.0, 100.0, 0.1),
            number("start", tr_("Start"), 0.01, 0.0, 1.0, 0.01),
            number("end", tr_("End"), 0.2, 0.0, 1.0, 0.01),
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
    case SdNodeType::ClipTextEncode:
        return {
            text("text", tr_("Text"), "What the image should contain, or avoid."),
            // Flux's distilled guidance rides on the positive conditioning,
            // which is why ComfyUI puts it inside CLIPTextEncodeFlux too.
            number("distilledGuidance", tr_("Distilled guidance"), 0.0, 0.0, 100.0, 0.1,
                   "Flux only; 0 keeps the model default."),
        };
    case SdNodeType::ReferenceImages:
        return {
            // The library reads these only together with the images they
            // describe, so they travel with them.
            line("args", tr_("Arguments"), QString(),
                 "Comma-separated key=value pairs, e.g. ref_index_mode=increase."),
        };
    case SdNodeType::EmptyLatentImage: {
        // The service hands back the first image of a batch and discards the
        // rest, so a batch above 1 would only spend minutes on unread pixels.
        SdPropertySpec batch = integer("batchCount", tr_("Batch size"), 1, 1, 16, 1.0);
        batch.editable = false;
        batch.hint = tr_("This build inserts the first image of a batch only.");
        return {
            dimension("width", tr_("Width")),
            dimension("height", tr_("Height")),
            batch,
            boolean("circularX", tr_("Seamless X"),  false,
                    "Wrap the convolutions horizontally, for a tileable image."),
            boolean("circularY", tr_("Seamless Y"), false,
                    "Wrap the convolutions vertically, for a tileable image."),
        };
    }
    case SdNodeType::VaeEncode:
    case SdNodeType::VaeEncodeInpaint:
        // Like ComfyUI's: the pixels and the VAE are inputs, and there is
        // nothing to configure. The denoising strength belongs to the sampler.
        return {};
    case SdNodeType::KSampler:
        return {
            seed("seed", tr_("Seed")),
            integer("steps", tr_("Steps"), 20, 1, 1000),
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
            number("denoisingStrength", tr_("Denoise"), 1.0, 0.0, 1.0, 0.05,
                   "Below 1.0 keeps the structure of an encoded image."),
            number("imageCfgScale", tr_("Image CFG"), 1.0, 0.0, 100.0, 0.1),
            // Eta and the sigma schedule belong to the algorithm this node
            // already selects; ComfyUI reaches them through SamplerCustom.
            number("eta", tr_("Eta"), 0.0, 0.0, 1.0, 0.01),
            line("customSigmas", tr_("Custom sigmas"), QString(),
                 "Comma-separated non-negative values; at least two when set."),
            // One rule set addressing every image of the call by target name,
            // so no single input node can own it.
            line("imagePreprocess", tr_("Image preprocess rules"), QString(),
                 "Semicolon-separated target=...,key=value rules."),
        };
    case SdNodeType::HiresFix:
        // In ComfyUI this is an upscale followed by a second KSampler. The
        // library runs it inside the same call and exposes only these knobs.
        return {
            enumeration("upscaler", tr_("Upscaler"), QStringLiteral("Latent"),
                        { QStringLiteral("Latent"), QStringLiteral("Latent (nearest)"),
                          QStringLiteral("Latent (nearest-exact)"),
                          QStringLiteral("Latent (antialiased)"),
                          QStringLiteral("Latent (bicubic)"),
                          QStringLiteral("Latent (bicubic antialiased)"),
                          QStringLiteral("Lanczos"), QStringLiteral("Nearest"),
                          QStringLiteral("Model") }, false, "", true),
            modelRef("modelId", tr_("ESRGAN model"), QString(),
                     { SdModelCategory::Upscaler }, "Used by the \"Model\" upscaler."),
            number("scale", tr_("Scale"), 2.0, 0.1, 8.0, 0.1),
            integer("targetWidth", tr_("Target width"), 0, 0, 32768, 8.0,
                    "0 derives the size from the scale."),
            integer("targetHeight", tr_("Target height"), 0, 0, 32768, 8.0,
                    "0 derives the size from the scale."),
            integer("steps", tr_("Steps"), 0, 0, 1000, 1.0,
                    "0 reuses the first pass's step count."),
            number("denoisingStrength", tr_("Denoise"), 0.5, 0.0001, 1.0, 0.05),
            integer("tileSize", tr_("Upscale tile"), 0, 0, 4096, 8.0,
                    "0 lets the backend choose."),
        };
    case SdNodeType::VaeDecode:
        // Tiling is what ComfyUI splits into VAEDecodeTiled: same decode, less
        // memory. It is a property here because the decode is one node.
        return {
            boolean("tiling", tr_("Tiled decode"), false),
            integer("tileWidth", tr_("Tile width"), 256, 64, 4096, 8.0),
            integer("tileHeight", tr_("Tile height"), 256, 64, 4096, 8.0),
            number("tileOverlap", tr_("Tile overlap"), 0.5, 0.0, 1.0, 0.05),
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
        // A detail pass of its own: it re-samples the regions the detector
        // finds, so it carries the sampler settings for that pass.
        return {
            modelRef("modelId", tr_("Detector"), QString(),
                     { SdModelCategory::ADetailer }),
            seed("seed", tr_("Seed")),
            integer("steps", tr_("Steps"), 20, 1, 1000),
            number("cfgScale", tr_("CFG scale"), 7.0, 0.0, 100.0, 0.5),
            enumeration("sampler", tr_("Sampler"), QString(),
                        SdGenerationService::samplerNames(), true,
                        "Empty keeps the checkpoint's own sampler."),
            enumeration("scheduler", tr_("Scheduler"), QString(),
                        SdGenerationService::schedulerNames(), true,
                        "Empty keeps the checkpoint's own scheduler."),
            number("denoisingStrength", tr_("Denoise"), 0.4, 0.0, 1.0, 0.05),
        };
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
    for (const SdPortType type : { SdPortType::Model, SdPortType::Clip,
                                   SdPortType::Vae, SdPortType::Engine,
                                   SdPortType::Conditioning, SdPortType::Latent,
                                   SdPortType::References, SdPortType::Hires,
                                   SdPortType::Image,
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
