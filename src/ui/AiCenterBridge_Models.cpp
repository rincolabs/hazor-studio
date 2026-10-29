// Models > Vision & Editing. Every read and write goes through
// AiModelRegistry — no second model registry, no second validity check.
#include "AiCenterBridge.hpp"

#include "ai/models/AiModelDescriptor.hpp"
#include "ai/models/AiModelManifest.hpp"
#include "ai/models/AiModelRegistry.hpp"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QUrl>

namespace {

QString modelStatusToJs(AiModelStatus status)
{
    switch (status) {
    case AiModelStatus::Valid:              return QStringLiteral("valid");
    case AiModelStatus::Invalid:            return QStringLiteral("invalid");
    case AiModelStatus::MissingFiles:       return QStringLiteral("missingFiles");
    case AiModelStatus::UnsupportedSchema:  return QStringLiteral("unsupportedSchema");
    case AiModelStatus::UnsupportedBackend: return QStringLiteral("unsupportedBackend");
    case AiModelStatus::InvalidMetadata:    return QStringLiteral("invalidMetadata");
    }
    return QStringLiteral("invalid");
}

// The one task not yet wired to an app tool (object removal / inpainting) —
// called out explicitly in the approved design as "Not yet exposed in the app UI".
bool isExposedInUi(const QString& task)
{
    return task != QStringLiteral("inpainting");
}

} // namespace

QVariantMap AiCenterBridge::toVariant(const AiModelDescriptor& model, bool isDefault) const
{
    QVariantMap v;
    v[QStringLiteral("id")] = model.id;
    v[QStringLiteral("task")] = model.task;
    v[QStringLiteral("family")] = model.family;
    v[QStringLiteral("displayName")] = model.displayName;
    v[QStringLiteral("description")] = model.description;
    v[QStringLiteral("quality")] = model.quality;
    v[QStringLiteral("origin")] = AiModelTaxonomy::originToKey(model.origin);
    v[QStringLiteral("status")] = modelStatusToJs(model.status);
    v[QStringLiteral("isValid")] = model.isValid();
    v[QStringLiteral("statusReason")] = model.statusReason;
    v[QStringLiteral("rootDir")] = model.rootDir;
    v[QStringLiteral("files")] = QVariant(model.files.allFiles());
    v[QStringLiteral("licenseName")] = model.licenseName;
    v[QStringLiteral("licenseFile")] = model.licenseFile;
    v[QStringLiteral("sourceName")] = model.sourceName;
    v[QStringLiteral("sourceUrl")] = model.sourceUrl;
    v[QStringLiteral("isDefault")] = isDefault;
    return v;
}

QVariantMap AiCenterBridge::modelsVisionStateVariant() const
{
    auto* registry = AiModelRegistry::instance();

    QVariantList taskGroups;
    for (const QString& task : AiModelTaxonomy::allTasks()) {
        const QList<AiModelDescriptor> models = registry->modelsByTask(task);
        if (models.isEmpty())
            continue;

        QString defaultId;
        if (const auto defaultModel = registry->defaultModelForTask(task))
            defaultId = defaultModel->id;

        QVariantList modelsVariant;
        for (const AiModelDescriptor& model : models)
            modelsVariant.append(toVariant(model, model.id == defaultId));

        QVariantMap group;
        group[QStringLiteral("task")] = task;
        group[QStringLiteral("taskLabel")] = AiModelTaxonomy::taskDisplayName(task);
        group[QStringLiteral("models")] = modelsVariant;
        group[QStringLiteral("defaultModelId")] = defaultId;
        group[QStringLiteral("exposedInUi")] = isExposedInUi(task);
        taskGroups.append(group);
    }

    const AiModelManifest& manifest = registry->manifest();
    QVariantList manifestEntries;
    for (const AiManifestEntry& entry : manifest.models()) {
        qint64 size = 0;
        for (const AiModelFile& file : entry.files)
            size += file.sizeBytes;
        QVariantMap v;
        v[QStringLiteral("id")] = entry.id;
        v[QStringLiteral("task")] = entry.task;
        v[QStringLiteral("displayName")] = entry.name;
        v[QStringLiteral("description")] = entry.description;
        v[QStringLiteral("sizeBytes")] = size;
        manifestEntries.append(v);
    }

    QVariantMap state;
    state[QStringLiteral("taskGroups")] = taskGroups;
    state[QStringLiteral("manifestConfigured")] = manifest.isValid() && !manifest.models().isEmpty();
    state[QStringLiteral("manifestEntries")] = manifestEntries;
    return state;
}

QVariantMap AiCenterBridge::modelsVisionState() const
{
    return modelsVisionStateVariant();
}

QVariantMap AiCenterBridge::setDefaultModel(const QString& task, const QString& modelId)
{
    AiModelRegistry::instance()->setDefaultModelForTask(task, modelId);
    const QVariantMap state = modelsVisionStateVariant();
    emit modelsVisionChanged(state);
    emitOverviewChanged();
    return state;
}

QVariantMap AiCenterBridge::removeInstalledModel(const QString& modelId)
{
    QString error;
    if (!AiModelRegistry::instance()->removeInstalled(modelId, &error))
        reportError(QStringLiteral("removeInstalledModel"), error);
    const QVariantMap state = modelsVisionStateVariant();
    emit modelsVisionChanged(state);
    emitOverviewChanged();
    return state;
}

void AiCenterBridge::showModelFiles(const QString& modelId) const
{
    const auto model = AiModelRegistry::instance()->modelById(modelId);
    if (!model || model->rootDir.isEmpty())
        return;
    QDir().mkpath(model->rootDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(model->rootDir));
}

void AiCenterBridge::openModelLicense(const QString& modelId) const
{
    const auto model = AiModelRegistry::instance()->modelById(modelId);
    if (!model)
        return;
    if (!model->licenseFile.isEmpty()) {
        const QString path = QDir(model->rootDir).filePath(model->licenseFile);
        if (QFile::exists(path)) {
            QDesktopServices::openUrl(QUrl::fromLocalFile(path));
            return;
        }
    }
    if (!model->sourceUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(model->sourceUrl));
}
