#pragma once

#include <QString>
#include <QStringList>
#include <QMap>
#include <QVariantMap>

// Where a discovered logical model came from. Drives the Settings UI badges, the
// removal policy and the conflict-resolution priority (custom > downloaded >
// bundled).
enum class AiModelOrigin {
    Bundled,     // shipped with the app/build — read-only, never removed by the app
    Downloaded,  // installed via the Settings download flow
    Custom       // added manually by the user — never deleted/overwritten by the app
};

// Result of validating a model.json against the schema and its declared files.
// Invalid models are still surfaced (in diagnostics) so a bad folder never breaks
// the app.
enum class AiModelStatus {
    Valid,
    Invalid,
    MissingFiles,
    UnsupportedSchema,
    UnsupportedBackend,
    InvalidMetadata
};

// The physical files of a logical model. A model is one logical entity that may
// own a single file ("model") or a pair (encoder + decoder, for SAM). Filenames
// are relative to the model's rootDir. The app never assumes a model is a single
// file, nor infers a file's role from its name — model.json is the source.
struct AiModelFileSet {
    QString model;
    QString encoder;
    QString decoder;
    QMap<QString, QString> extraFiles;

    bool isEmpty() const
    {
        return model.isEmpty()
               && encoder.isEmpty()
               && decoder.isEmpty()
               && extraFiles.isEmpty();
    }
    bool hasEncoderDecoder() const { return !encoder.isEmpty() && !decoder.isEmpty(); }
    QStringList allFiles() const
    {
        QStringList out;
        if (!encoder.isEmpty()) out << encoder;
        if (!decoder.isEmpty()) out << decoder;
        if (!model.isEmpty()) out << model;
        for (auto it = extraFiles.constBegin(); it != extraFiles.constEnd(); ++it)
            if (!it.value().isEmpty())
                out << it.value();
        return out;
    }
};

// A logical AI model discovered from a model.json on disk. This is the source of
// truth the app reasons about: AiModelRegistry builds these by scanning
// Models/{bundled,downloaded,custom}. The runtime file-resolution contract
// (AiInstalledModel) is derived from the valid ones for the inference layer.
struct AiModelDescriptor {
    int schemaVersion = 0;

    QString id;
    QString type;          // "segment_anything" | "birefnet" | "modnet" | "rmbg" ...
    QString backend;       // "onnxruntime"
    QString task;          // "segmentation" | "matting" | "background_removal" | "inpainting" | "upscale"
    QString family;        // "sam" | "birefnet" | "modnet" | "rmbg" ...
    QString variant;       // "vit_b" ... (optional)

    QString displayName;
    QString description;
    QString quality;       // "fast" | "balanced" | "high" | "professional" ...

    AiModelOrigin origin = AiModelOrigin::Custom;
    AiModelStatus status = AiModelStatus::InvalidMetadata;
    QString statusReason;  // human-readable explanation when not Valid

    QString rootDir;       // absolute path to the model's own folder
    AiModelFileSet files;

    QVariantMap input;     // input_size / max_width / max_height / color_space ...
    QStringList capabilities;

    QString licenseName;
    QString licenseFile;   // relative filename, if present
    QString noticeFile;    // relative filename, if present

    QString sourceName;
    QString sourceUrl;

    bool isDefault = false; // model.json "default: true"

    bool isValid() const { return status == AiModelStatus::Valid; }
};

// Central taxonomy so the app never infers a model's nature from a filename, and
// so manifest entries, legacy installs and the new schema all map onto the same
// function/task buckets.
namespace AiModelTaxonomy {

QString originToString(AiModelOrigin origin); // "Bundled" | "Downloaded" | "Custom"
QString originToKey(AiModelOrigin origin);    // "bundled" | "downloaded" | "custom"
QString statusToString(AiModelStatus status); // "Valid" | "Missing files" ...

// Manifest "role" ⇄ logical "task".
QString roleToTask(const QString& role);  // "segmenter" -> "segmentation"
QString taskToRole(const QString& task);  // "segmentation" -> "segmenter"

// Family ("sam1"/"birefnet"/...) -> schema "type" ("segment_anything"/...).
QString familyToType(const QString& family);

// Task -> the function folder name used by the on-disk tree
// ("segmentation" -> "segmenters", "background_removal" -> "background-removal").
// Discovery is recursive (by model.json), so this only shapes the layout.
QString taskToFolder(const QString& task);

// Default capabilities implied by a task, used when an entry omits them.
QStringList defaultCapabilitiesForTask(const QString& task);

// The ordered list of function buckets the Settings UI groups models under.
QStringList allTasks();

// Human label for a task bucket ("segmentation" -> "Object Selection / Segmenters").
QString taskDisplayName(const QString& task);

} // namespace AiModelTaxonomy
