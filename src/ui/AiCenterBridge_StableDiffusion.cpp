// Models > Stable Diffusion. Every read and write goes through
// sdlocal::SdModelManager — no second registry, no copying of model files.
//
// Compiled unconditionally (unlike SdModelManager itself, which only exists
// when HAVE_STABLE_DIFFUSION is defined — see CMakeLists.txt): the AI Center
// as a whole must still work without the Stable Diffusion module, same as
// StableDiffusionPanel's absence doesn't take the rest of the app with it.
// Without it, this tab reports an empty, unavailable registry instead of
// linking against a class that was never compiled in.
#include "AiCenterBridge.hpp"

#include <QFileDialog>

#ifdef HAVE_STABLE_DIFFUSION

#include "ai/sd/SdModelManager.hpp"

#include <QDir>
#include <QTime>

#include <algorithm>

using namespace sdlocal;

namespace {

QString sdCategoryToJs(SdModelCategory category)
{
    switch (category) {
    case SdModelCategory::Checkpoint:     return QStringLiteral("checkpoint");
    case SdModelCategory::DiffusionModel: return QStringLiteral("diffusionModel");
    case SdModelCategory::TextEncoder:    return QStringLiteral("textEncoder");
    case SdModelCategory::Vae:            return QStringLiteral("vae");
    case SdModelCategory::Lora:           return QStringLiteral("lora");
    }
    return QStringLiteral("checkpoint");
}

SdModelCategory sdCategoryFromJs(const QString& value)
{
    if (value == QStringLiteral("diffusionModel")) return SdModelCategory::DiffusionModel;
    if (value == QStringLiteral("textEncoder")) return SdModelCategory::TextEncoder;
    if (value == QStringLiteral("vae")) return SdModelCategory::Vae;
    if (value == QStringLiteral("lora")) return SdModelCategory::Lora;
    return SdModelCategory::Checkpoint;
}

QString sdEncoderRoleToJs(SdTextEncoderRole role)
{
    switch (role) {
    case SdTextEncoderRole::ClipL: return QStringLiteral("clipL");
    case SdTextEncoderRole::ClipG: return QStringLiteral("clipG");
    case SdTextEncoderRole::T5xxl: return QStringLiteral("t5xxl");
    case SdTextEncoderRole::Auto:  return QStringLiteral("auto");
    }
    return QStringLiteral("auto");
}

SdTextEncoderRole sdEncoderRoleFromJs(const QString& value)
{
    if (value == QStringLiteral("clipL")) return SdTextEncoderRole::ClipL;
    if (value == QStringLiteral("clipG")) return SdTextEncoderRole::ClipG;
    if (value == QStringLiteral("t5xxl")) return SdTextEncoderRole::T5xxl;
    return SdTextEncoderRole::Auto;
}

const char* const kAllCategories[] = {
    "checkpoint", "diffusionModel", "textEncoder", "vae", "lora"
};

} // namespace

QVariantMap AiCenterBridge::sdModelsStateVariant() const
{
    auto& manager = SdModelManager::instance();
    const QVector<SdModelEntry> entries = manager.models();
    const QVector<SdModelDirectory> directories = manager.directories();
    const QString defaultId = manager.defaultModelId();

    QVariantList entriesVariant;
    QVariantMap countsByCategory;
    for (const char* cat : kAllCategories)
        countsByCategory[QString::fromLatin1(cat)] = 0;

    for (const SdModelEntry& entry : entries) {
        QVariantMap v;
        v[QStringLiteral("id")] = entry.id;
        v[QStringLiteral("path")] = entry.path;
        v[QStringLiteral("category")] = sdCategoryToJs(entry.category);
        v[QStringLiteral("encoderRole")] = sdEncoderRoleToJs(entry.encoderRole);
        v[QStringLiteral("label")] = entry.label.isEmpty() ? entry.id : entry.label;
        v[QStringLiteral("discovered")] = entry.discovered;
        v[QStringLiteral("source")] = entry.discovered ? QStringLiteral("directory") : QStringLiteral("registeredFile");
        v[QStringLiteral("available")] = entry.fileExists();
        v[QStringLiteral("isDefault")] = (entry.id == defaultId);
        entriesVariant.append(v);

        const QString catKey = sdCategoryToJs(entry.category);
        countsByCategory[catKey] = countsByCategory.value(catKey).toInt() + 1;
    }

    QVariantList directoriesVariant;
    int unavailableDirectories = 0;
    for (const SdModelDirectory& dir : directories) {
        const bool available = QDir(dir.path).exists();
        if (!available)
            ++unavailableDirectories;
        int found = 0;
        const QString cleanDir = QDir::cleanPath(dir.path);
        for (const SdModelEntry& entry : entries) {
            if (entry.discovered && QDir::cleanPath(entry.path).startsWith(cleanDir))
                ++found;
        }
        QVariantMap v;
        v[QStringLiteral("path")] = dir.path;
        v[QStringLiteral("category")] = sdCategoryToJs(dir.category);
        v[QStringLiteral("encoderRole")] = sdEncoderRoleToJs(dir.encoderRole);
        v[QStringLiteral("recursive")] = dir.recursive;
        v[QStringLiteral("available")] = available;
        v[QStringLiteral("modelsFound")] = found;
        directoriesVariant.append(v);
    }

    QStringList missingPaths;
    for (const SdModelEntry& entry : entries) {
        if (!entry.fileExists())
            missingPaths << entry.id;
    }
    for (const SdModelDirectory& dir : directories) {
        if (!QDir(dir.path).exists())
            missingPaths << dir.path;
    }

    QVariantMap overviewSummary;
    overviewSummary[QStringLiteral("registered")] = entries.size();
    overviewSummary[QStringLiteral("categories")] = static_cast<int>(
        std::count_if(std::begin(kAllCategories), std::end(kAllCategories),
                     [&countsByCategory](const char* cat) {
                         return countsByCategory.value(QString::fromLatin1(cat)).toInt() > 0;
                     }));
    overviewSummary[QStringLiteral("directories")] = directories.size();
    overviewSummary[QStringLiteral("unavailableDirectories")] = unavailableDirectories;
    overviewSummary[QStringLiteral("missingPaths")] = missingPaths;
    overviewSummary[QStringLiteral("defaultModelId")] = defaultId;

    QVariantMap state;
    state[QStringLiteral("entries")] = entriesVariant;
    state[QStringLiteral("directories")] = directoriesVariant;
    state[QStringLiteral("defaultModelId")] = defaultId;
    state[QStringLiteral("countsByCategory")] = countsByCategory;
    state[QStringLiteral("lastScanAt")] = m_sdLastScanAt;
    state[QStringLiteral("overviewSummary")] = overviewSummary;
    return state;
}

QVariantMap AiCenterBridge::registerSdFile(const QString& path, const QString& category,
                                           const QString& encoderRole, const QString& id)
{
    SdModelEntry entry;
    entry.path = path;
    entry.category = sdCategoryFromJs(category);
    entry.encoderRole = sdEncoderRoleFromJs(encoderRole);
    entry.id = id;
    entry.label = id;

    QString error;
    if (!SdModelManager::instance().registerModel(entry, &error))
        reportError(QStringLiteral("registerSdFile"), error);

    const QVariantMap state = sdModelsStateVariant();
    emit sdModelsChanged(state);
    emitOverviewChanged();
    return state;
}

QVariantMap AiCenterBridge::registerSdDirectory(const QString& path, const QString& category,
                                                const QString& encoderRole, bool recursive)
{
    SdModelDirectory dir;
    dir.path = path;
    dir.category = sdCategoryFromJs(category);
    dir.encoderRole = sdEncoderRoleFromJs(encoderRole);
    dir.recursive = recursive;

    QString error;
    if (!SdModelManager::instance().registerDirectory(dir, &error))
        reportError(QStringLiteral("registerSdDirectory"), error);

    const QVariantMap state = sdModelsStateVariant();
    emit sdModelsChanged(state);
    emitOverviewChanged();
    return state;
}

QVariantMap AiCenterBridge::unregisterSdEntry(const QString& id)
{
    SdModelManager::instance().unregisterModel(id);
    const QVariantMap state = sdModelsStateVariant();
    emit sdModelsChanged(state);
    emitOverviewChanged();
    return state;
}

QVariantMap AiCenterBridge::unregisterSdDirectory(const QString& path)
{
    SdModelManager::instance().unregisterDirectory(path);
    const QVariantMap state = sdModelsStateVariant();
    emit sdModelsChanged(state);
    emitOverviewChanged();
    return state;
}

QVariantMap AiCenterBridge::setSdDefaultModel(const QString& id)
{
    SdModelManager::instance().setDefaultModelId(id);
    const QVariantMap state = sdModelsStateVariant();
    emit sdModelsChanged(state);
    emitOverviewChanged();
    return state;
}

QVariantMap AiCenterBridge::rescanSdDirectories()
{
    SdModelManager::instance().refresh();
    m_sdLastScanAt = QTime::currentTime().toString(QStringLiteral("HH:mm"));
    const QVariantMap state = sdModelsStateVariant();
    emit sdModelsChanged(state);
    emitOverviewChanged();
    return state;
}

#else // !HAVE_STABLE_DIFFUSION — the module isn't compiled in; report an empty, read-only registry.

namespace {

QVariantMap emptySdState(const QString& lastScanAt)
{
    QVariantMap overviewSummary;
    overviewSummary[QStringLiteral("registered")] = 0;
    overviewSummary[QStringLiteral("categories")] = 0;
    overviewSummary[QStringLiteral("directories")] = 0;
    overviewSummary[QStringLiteral("unavailableDirectories")] = 0;
    overviewSummary[QStringLiteral("missingPaths")] = QStringList();
    overviewSummary[QStringLiteral("defaultModelId")] = QString();

    QVariantMap countsByCategory;
    for (const char* cat : { "checkpoint", "diffusionModel", "textEncoder", "vae", "lora" })
        countsByCategory[QString::fromLatin1(cat)] = 0;

    QVariantMap state;
    state[QStringLiteral("entries")] = QVariantList();
    state[QStringLiteral("directories")] = QVariantList();
    state[QStringLiteral("defaultModelId")] = QString();
    state[QStringLiteral("countsByCategory")] = countsByCategory;
    state[QStringLiteral("lastScanAt")] = lastScanAt;
    state[QStringLiteral("overviewSummary")] = overviewSummary;
    return state;
}

} // namespace

QVariantMap AiCenterBridge::sdModelsStateVariant() const
{
    return emptySdState(m_sdLastScanAt);
}

QVariantMap AiCenterBridge::registerSdFile(const QString&, const QString&, const QString&, const QString&)
{
    reportError(QStringLiteral("registerSdFile"), QStringLiteral("Stable Diffusion is not available in this build."));
    return sdModelsStateVariant();
}

QVariantMap AiCenterBridge::registerSdDirectory(const QString&, const QString&, const QString&, bool)
{
    reportError(QStringLiteral("registerSdDirectory"), QStringLiteral("Stable Diffusion is not available in this build."));
    return sdModelsStateVariant();
}

QVariantMap AiCenterBridge::unregisterSdEntry(const QString&)
{
    return sdModelsStateVariant();
}

QVariantMap AiCenterBridge::unregisterSdDirectory(const QString&)
{
    return sdModelsStateVariant();
}

QVariantMap AiCenterBridge::setSdDefaultModel(const QString&)
{
    return sdModelsStateVariant();
}

QVariantMap AiCenterBridge::rescanSdDirectories()
{
    return sdModelsStateVariant();
}

#endif // HAVE_STABLE_DIFFUSION

QVariantMap AiCenterBridge::sdModelsState() const
{
    return sdModelsStateVariant();
}

QString AiCenterBridge::browseForSdFile()
{
    return QFileDialog::getOpenFileName(m_dialogParent, tr("Select Model File"), QString(),
                                        tr("Model files (*.safetensors *.ckpt *.gguf *.pt *.bin);;All files (*)"));
}

QString AiCenterBridge::browseForSdDirectory()
{
    return QFileDialog::getExistingDirectory(m_dialogParent, tr("Select Directory"));
}
