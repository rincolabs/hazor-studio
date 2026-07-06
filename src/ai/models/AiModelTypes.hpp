#pragma once

#include <QString>
#include <QStringList>
#include <QList>
#include <QVariantMap>
#include <QDateTime>

// One physical file belonging to a model. A model may ship several files
// (e.g. SAM has a separate encoder and decoder), so the app never assumes a
// model is a single file.
struct AiModelFile {
    QString type;        // "encoder" | "decoder" | "model" ...
    QString filename;
    QString url;
    QString sha256;      // lowercase hex, may be empty/placeholder in dev manifests
    qint64  sizeBytes = 0;

    bool hasChecksum() const { return sha256.size() == 64; }
};

// A model entry as declared in the manifest (available to install / download).
// This is the "what can be fetched" record; once installed, a model becomes a
// logical AiModelDescriptor discovered from its model.json on disk.
struct AiManifestEntry {
    QString id;
    QString family;      // "sam1" | "birefnet" | "modnet" | "rmbg" ...
    QString role;        // "segmenter" | "matting" | "background-removal" ...
    QString backend;     // "onnxruntime" | "realesrgan-ncnn-vulkan" ...
    QString name;
    QString quality;     // "compatible" | "recommended" | "high" ...
    QString description;
    QString license;
    QString homepage;

    // Optional richer metadata so the model.json written on install is complete.
    // Derived from role/family/task when omitted by the manifest.
    QString task;            // explicit task; derived from role when empty
    QString type;            // explicit type; derived from family when empty
    QStringList capabilities;// explicit capabilities; derived from task when empty
    QVariantMap input;       // input_size / max_width / max_height / color_space
    QString sourceName;
    QString sourceUrl;

    QList<AiModelFile> files;

    bool recommended = false;
    int minRamMB  = 0;
    int minVramMB = 0;

    bool isValid() const { return !id.isEmpty() && !files.isEmpty(); }
    const AiModelFile* fileOfType(const QString& fileType) const
    {
        for (const auto& f : files)
            if (f.type == fileType) return &f;
        return nullptr;
    }
};

// A model that has been validated into the models directory and is ready for the
// inference layer. Resolved view consumed by AiRuntimeManager::createSession and
// the segmenter/matting loaders: a folder (directory) plus its physical files.
struct AiInstalledModel {
    QString id;
    QString family;
    QString role;
    QString name;
    QString directory;            // absolute path to the model's folder
    QList<AiModelFile> files;     // with resolved local filenames
    QDateTime installedAt;
    int manifestVersion = 0;

    bool isValid() const { return !id.isEmpty() && !files.isEmpty(); }
};
