#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

// Model registry for the local Stable Diffusion module.
//
// Deliberately NOT src/ai/models/AiModelRegistry: that one owns the ONNX models
// Hazor ships or downloads, with a manifest, a download manager and an
// installed-models directory under the user's data dir. This one owns no files
// at all. It records where the user's existing Stable Diffusion weights already
// live — including a ComfyUI tree shared with other applications — and resolves
// them into the set of paths new_sd_ctx() needs. Nothing is copied, nothing is
// downloaded, and no directory is scanned during Hazor's startup.
namespace sdlocal {

// Which slot of sd_ctx_params_t an entry fills.
//
// A .safetensors file is not self-describing enough to tell a full checkpoint
// from a bare UNet, a text encoder or a VAE — they all share the extension and
// the container. So the category is recorded per entry (or inherited from the
// registered directory it was found in) instead of being inferred from the file
// name: guessing it wrong means handing the library a UNet as model_path and
// getting a load failure with no useful explanation.
enum class SdModelCategory {
    Checkpoint,      // complete model        -> sd_ctx_params_t::model_path
    DiffusionModel,  // UNet/DiT weights only -> diffusion_model_path (+ encoders + VAE)
    TextEncoder,     // CLIP-L / CLIP-G / T5-XXL -> clip_l_path / clip_g_path / t5xxl_path
    Vae,             // -> vae_path
    Lora,            // per-generation sd_lora_t; never a context parameter
};

// Which text-encoder slot a TextEncoder entry fills. Auto infers it from the
// file name, which is how every published component release names them
// (clip_l*, clip_g*, t5xxl*); an explicit role wins when the name is unusual.
enum class SdTextEncoderRole { Auto, ClipL, ClipG, T5xxl };

QString modelCategoryName(SdModelCategory category);
bool modelCategoryFromName(const QString& name, SdModelCategory& out);
QString textEncoderRoleName(SdTextEncoderRole role);
bool textEncoderRoleFromName(const QString& name, SdTextEncoderRole& out);

// One registered weights file. `path` is used where it is: the registry never
// copies a model into Hazor's data directory.
struct SdModelEntry {
    QString id;        // unique within the registry; referenced by workflows
    QString path;      // absolute path to the file
    SdModelCategory category = SdModelCategory::Checkpoint;
    SdTextEncoderRole encoderRole = SdTextEncoderRole::Auto;  // TextEncoder only
    QString label;     // display name; defaults to the file's base name

    // Found by scanning a registered directory rather than pinned by the user.
    // Discovered entries are not persisted — the directory is, so renaming a
    // file on disk cannot leave a stale id behind in the config.
    bool discovered = false;

    bool isValid() const { return !id.isEmpty() && !path.isEmpty(); }
    bool fileExists() const;
};

// A directory to look in. Everything under it is taken to be of `category`,
// which is what makes a ComfyUI layout work without any per-file bookkeeping:
// its models/checkpoints, models/vae and models/loras are already sorted by
// kind, so each maps to one registered directory.
struct SdModelDirectory {
    QString path;
    SdModelCategory category = SdModelCategory::Checkpoint;
    SdTextEncoderRole encoderRole = SdTextEncoderRole::Auto;
    bool recursive = true;

    bool isValid() const { return !path.isEmpty(); }
};

// What a caller asks the registry to resolve.
struct SdModelSelection {
    // A Checkpoint or DiffusionModel entry id. Empty or "default" resolves to
    // SdModelManager::defaultModelId().
    QString modelId;
    // Required when `modelId` names a DiffusionModel: that file carries no text
    // encoder, so the context cannot be built without them.
    QStringList textEncoderIds;
    // Optional for a Checkpoint (which usually embeds its VAE), required for a
    // DiffusionModel.
    QString vaeId;

    bool operator==(const SdModelSelection& o) const
    {
        return modelId == o.modelId && textEncoderIds == o.textEncoderIds && vaeId == o.vaeId;
    }
};

// The resolved context inputs: absolute paths that existed when they were
// resolved. Only the sd_ctx_params_t fields this module supports are here;
// everything else stays at sd_ctx_params_init()'s defaults.
//
// Carried inside the generation request so the inference thread never touches
// the registry — resolution happens up front, on the thread that validated the
// workflow, and the worker only ever sees paths.
struct SdModelFiles {
    QString modelId;   // the entry that was asked for, for logs and results
    QString checkpointPath;
    QString diffusionModelPath;
    QString clipLPath;
    QString clipGPath;
    QString t5xxlPath;
    QString vaePath;

    bool isValid() const { return !checkpointPath.isEmpty() || !diffusionModelPath.isEmpty(); }
    QStringList paths() const;   // every non-empty path, for logging
    // True when every path still exists on disk. Re-checked before a load: a
    // file can be moved between validation and execution.
    bool allPresent(QString* missing = nullptr) const;

    bool operator==(const SdModelFiles& o) const
    {
        return checkpointPath == o.checkpointPath
            && diffusionModelPath == o.diffusionModelPath
            && clipLPath == o.clipLPath
            && clipGPath == o.clipGPath
            && t5xxlPath == o.t5xxlPath
            && vaePath == o.vaePath;
    }
    bool operator!=(const SdModelFiles& o) const { return !(*this == o); }
};

// A LoRA with its weight, resolved to a path. LoRAs are per-generation in this
// revision of the library (sd_img_gen_params_t::loras), not context parameters,
// so they travel with the request rather than with the load options.
struct SdResolvedLora {
    QString id;
    QString path;
    float weight = 1.0f;
};

class SdModelManager {
public:
    // One registry per process. The config file is read on first use, not at
    // application startup: nothing here runs unless the Stable Diffusion panel
    // or a workflow actually asks for a model.
    static SdModelManager& instance();

    // ── Registration ────────────────────────────────────────────────────────
    // Fails when the id is already taken by a pinned entry, when the path is
    // empty, or when the file does not exist.
    bool registerModel(const SdModelEntry& entry, QString* error = nullptr);
    bool unregisterModel(const QString& id);
    // Registers the directory and scans it immediately, so the caller can tell
    // straight away whether anything was found.
    bool registerDirectory(const SdModelDirectory& dir, QString* error = nullptr);
    bool unregisterDirectory(const QString& path);

    // ── Queries ─────────────────────────────────────────────────────────────
    QVector<SdModelDirectory> directories() const;
    // Every known entry, pinned and discovered. Scans the registered
    // directories on first access.
    QVector<SdModelEntry> models() const;
    QVector<SdModelEntry> models(SdModelCategory category) const;
    bool model(const QString& id, SdModelEntry& out) const;
    bool hasModel(const QString& id) const;

    // The entry a selection of "default" (or an empty model id) resolves to.
    // Falls back to the only registered checkpoint when nothing was pinned, so
    // a single registered model needs no further configuration.
    QString defaultModelId() const;
    void setDefaultModelId(const QString& id);

    // ── Resolution ──────────────────────────────────────────────────────────
    // Turns a selection into the file set a context needs, or explains exactly
    // which file or component is missing. Never loads anything.
    bool resolve(const SdModelSelection& selection, SdModelFiles& out,
                 QString* error = nullptr) const;
    bool resolveLora(const QString& id, float weight, SdResolvedLora& out,
                     QString* error = nullptr) const;

    // Rescans every registered directory. Explicit on purpose — this is the
    // only heavy operation here and it is never triggered by startup.
    void refresh();

    QString configPath() const;
    bool save(QString* error = nullptr) const;

private:
    // Reads the config file and applies the environment overrides. Runs on the
    // first instance() call, which is on demand from the panel or a workflow —
    // never from Hazor's startup path.
    SdModelManager();

    void load();
    void applyEnvironmentOverrides();
    // Scans the registered directories once, on the first query that needs
    // them. Mutable state because every query is const: the alternative is
    // scanning in the constructor, which is the one thing this class must not
    // do (a ComfyUI tree can hold tens of gigabytes across hundreds of files).
    void ensureScanned() const;
    static QString uniqueId(const QString& base, const QVector<SdModelEntry>& taken);

    QVector<SdModelEntry> m_pinned;              // persisted
    QVector<SdModelDirectory> m_directories;     // persisted
    QString m_defaultModelId;                    // persisted
    mutable QVector<SdModelEntry> m_discovered;  // from directory scans
    mutable bool m_scanned = false;
};

} // namespace sdlocal
