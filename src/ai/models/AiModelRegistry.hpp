#pragma once

#include "ai/models/AiModelTypes.hpp"
#include "ai/models/AiModelDescriptor.hpp"
#include "ai/models/AiModelManifest.hpp"

#include <QObject>
#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

// Local registry of AI models. Discovers logical models by scanning the
// Models/{bundled,downloaded,custom} tree for model.json files (the source of
// truth), classifies them by origin, resolves id conflicts by priority
// (custom > downloaded > bundled) and exposes them by task/capability so tools
// never reason about file names or hardcoded paths.
//
// The inference layer keeps consuming the resolved AiInstalledModel contract
// (directory + files), derived here from the valid descriptors.
// AiModelDownloadManager is the write side and calls refresh() on completion.
class AiModelRegistry : public QObject {
    Q_OBJECT
public:
    static AiModelRegistry* instance();

    QString modelsDirectory() const { return m_modelsDir; }
    void setModelsDirectory(const QString& dir);

    // ── Origin roots / install layout ──
    QString bundledRoot() const;     // 3rdparty/onnx/models (distributed with app)
    QString downloadedRoot() const;  // <modelsDir>/downloaded/onnx
    QString customRoot() const;      // <modelsDir>/custom/onnx
    QString downloadedStagingDir(const QString& id) const; // <modelsDir>/downloaded/.tmp/<id>
    // Final install folder for a downloaded model: downloaded/onnx/<task>/<family>/<id>.
    QString downloadedModelDir(const QString& task, const QString& family, const QString& id) const;

    // ── Manifest (available to download) ──
    const AiModelManifest& manifest() const { return m_manifest; }
    QList<AiManifestEntry> availableFromManifest() const { return m_manifest.models(); }
    QStringList manifestWarnings() const { return m_manifest.warnings(); }
    void refreshManifest();

    // ── Discovery / logical models ──
    void refresh();
    QList<AiModelDescriptor> allModels() const { return m_models; }
    QList<AiModelDescriptor> validModels() const;
    QList<AiModelDescriptor> invalidModels() const;
    QList<AiModelDescriptor> modelsByTask(const QString& task) const;
    QList<AiModelDescriptor> installedInpaintingModels() const;
    QList<AiModelDescriptor> installedModelsForTask(const QString& task) const;
    std::optional<AiModelDescriptor> bestModelForTask(const QString& task) const;
    QList<AiModelDescriptor> modelsByCapability(const QString& capability) const;
    std::optional<AiModelDescriptor> modelById(const QString& id) const;
    std::optional<AiModelDescriptor> defaultModelForTask(const QString& task) const;
    bool validateModel(const AiModelDescriptor& model) const;

    // User-chosen default per task (persisted under QSettings ai/defaultModel/<task>).
    void setDefaultModelForTask(const QString& task, const QString& modelId);
    QString userDefaultModelId(const QString& task) const;

    // ── Inference compat (runtime contract) ──
    QList<AiInstalledModel> installedModels() const;
    bool isInstalled(const QString& modelId) const { return m_installed.contains(modelId); }
    AiInstalledModel installedModel(const QString& modelId) const;
    void refreshInstalledModels() { refresh(); }

    bool validateModelFiles(const QString& modelId, QString* reason = nullptr) const;

    // Removes a model's folder. Origin-aware: only Downloaded models can be
    // removed; Bundled and Custom are refused (the app never deletes them).
    bool removeInstalled(const QString& modelId, QString* error = nullptr);

    // Folder of a discovered model (its rootDir), or empty if unknown.
    QString modelDirectory(const QString& modelId) const;

signals:
    void installedModelsChanged();
    void manifestChanged();

private:
    explicit AiModelRegistry(QObject* parent = nullptr);

    void discoverInto(const QString& root, AiModelOrigin origin, QList<AiModelDescriptor>& out) const;
    void discoverLegacyFlat(QList<AiModelDescriptor>& out) const;
    void rebuildIndex();
    AiInstalledModel toInstalled(const AiModelDescriptor& d) const;

    QString m_modelsDir;
    AiModelManifest m_manifest;
    QList<AiModelDescriptor> m_models;            // everything discovered (valid + invalid)
    QHash<QString, AiModelDescriptor> m_byId;     // winning valid descriptor per id
    QHash<QString, AiInstalledModel> m_installed; // derived inference contract
};
