#include "AiModelRegistry.hpp"
#include "ai/models/AiModelJson.hpp"
#include "ai/runtime/AiRuntimeSettings.hpp"
#include "core/AppPaths.hpp"

#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSettings>
#include <QDebug>

namespace {

// Origin priority for id-conflict resolution: custom > downloaded > bundled.
int originPriority(AiModelOrigin origin)
{
    switch (origin) {
    case AiModelOrigin::Custom:     return 3;
    case AiModelOrigin::Downloaded: return 2;
    case AiModelOrigin::Bundled:    return 1;
    }
    return 0;
}

// Lower is better. Used to pick a default among models of the same task.
int qualityRank(const QString& quality)
{
    const QString q = quality.trimmed().toLower();
    if (q == QLatin1String("professional")) return 0;
    if (q == QLatin1String("high"))         return 1;
    if (q == QLatin1String("balanced") || q == QLatin1String("recommended")) return 2;
    if (q == QLatin1String("fast") || q == QLatin1String("compatible")) return 3;
    return 2;
}

bool shouldSkipPath(const QString& path)
{
    // Never descend into staging/temp areas.
    return path.contains(QStringLiteral("/.tmp/")) || path.contains(QStringLiteral("/.staging/"));
}

} // namespace

AiModelRegistry* AiModelRegistry::instance()
{
    static AiModelRegistry* s_instance = new AiModelRegistry();
    return s_instance;
}

AiModelRegistry::AiModelRegistry(QObject* parent)
    : QObject(parent)
    , m_modelsDir(AiRuntimeSettings::load().resolvedModelsDirectory())
{
    refreshManifest();
    refresh();
}

void AiModelRegistry::setModelsDirectory(const QString& dir)
{
    const QString resolved = dir.trimmed().isEmpty()
                                 ? AiRuntimeSettings::load().resolvedModelsDirectory()
                                 : dir;
    if (resolved == m_modelsDir)
        return;
    m_modelsDir = resolved;
    QDir().mkpath(m_modelsDir);
    refresh();
}

QString AiModelRegistry::bundledRoot() const
{
    // Bundled/default models live in the read-only 3rdparty tree
    // (3rdparty/onnx/models), NOT under the user-modifiable models directory.
    // The user's downloaded/custom models never mix with the trusted bundled set.
    return AppPaths::bundledModelsDir();
}
QString AiModelRegistry::downloadedRoot() const
{
    return QDir(m_modelsDir).filePath(QStringLiteral("downloaded/onnx"));
}
QString AiModelRegistry::customRoot() const
{
    return QDir(m_modelsDir).filePath(QStringLiteral("custom/onnx"));
}
QString AiModelRegistry::downloadedStagingDir(const QString& id) const
{
    return QDir(m_modelsDir).filePath(QStringLiteral("downloaded/.tmp/") + id);
}
QString AiModelRegistry::downloadedModelDir(const QString& task, const QString& family, const QString& id) const
{
    const QString folder = AiModelTaxonomy::taskToFolder(task);
    const QString fam = family.trimmed().isEmpty() ? id : family.trimmed();
    return QDir(downloadedRoot()).filePath(QStringLiteral("%1/%2/%3").arg(folder, fam, id));
}

void AiModelRegistry::refreshManifest()
{
    m_manifest = AiModelManifest::loadBundled();
    emit manifestChanged();
}

// ── Discovery ───────────────────────────────────────────────────────────────

void AiModelRegistry::discoverInto(const QString& root, AiModelOrigin origin,
                                   QList<AiModelDescriptor>& out) const
{
    // Read-only scan: a non-existent root (e.g. bundled resources with nothing
    // shipped) simply yields no models. The directory is never created here.
    if (!QDir(root).exists())
        return;
    QDirIterator it(root, { QString::fromLatin1(AiModelJson::kMetadataFile) },
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString metaPath = it.next();
        if (shouldSkipPath(metaPath))
            continue;
        const QString dir = QFileInfo(metaPath).absolutePath();
        AiModelDescriptor d = AiModelJson::readAny(dir, origin);
        if (!d.id.isEmpty() || d.status != AiModelStatus::Valid)
            out.append(d);
    }
}

void AiModelRegistry::discoverLegacyFlat(QList<AiModelDescriptor>& out) const
{
    // Folders directly under <modelsDir> that aren't the new origin roots: these
    // are pre-existing installs from the flat layout (or user-dropped SAM config
    // folders). Treated as Downloaded for continuity with the old manager.
    QDir root(m_modelsDir);
    const QFileInfoList dirs = root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QFileInfo& fi : dirs) {
        const QString name = fi.fileName();
        if (name == QLatin1String("bundled") || name == QLatin1String("downloaded")
            || name == QLatin1String("custom") || name.startsWith(QLatin1Char('.')))
            continue;
        AiModelDescriptor d = AiModelJson::readLegacy(fi.absoluteFilePath(), AiModelOrigin::Downloaded);
        if (!d.id.isEmpty())
            out.append(d);
    }
}

void AiModelRegistry::rebuildIndex()
{
    m_byId.clear();
    m_installed.clear();

    for (const AiModelDescriptor& d : m_models) {
        if (!d.isValid())
            continue;
        auto it = m_byId.find(d.id);
        if (it == m_byId.end()) {
            m_byId.insert(d.id, d);
            continue;
        }
        // Conflict: keep the higher-priority origin, never silently.
        qInfo().noquote() << "[AI][MODELS] Duplicate model id detected:" << d.id;
        if (originPriority(d.origin) > originPriority(it.value().origin)) {
            qInfo().noquote() << "[AI][MODELS] Using" << AiModelTaxonomy::originToString(d.origin)
                              << "model over" << AiModelTaxonomy::originToString(it.value().origin);
            it.value() = d;
        } else {
            qInfo().noquote() << "[AI][MODELS] Keeping" << AiModelTaxonomy::originToString(it.value().origin)
                              << "model over" << AiModelTaxonomy::originToString(d.origin);
        }
    }

    for (auto it = m_byId.cbegin(); it != m_byId.cend(); ++it)
        m_installed.insert(it.key(), toInstalled(it.value()));
}

void AiModelRegistry::refresh()
{
    m_models.clear();

    // Ensure the user-writable trees exist so they show up for the user and for
    // downloads. The bundled root is read-only app resources (spec §14) and is
    // never created here — it is scanned only if it ships with the build.
    for (const QString& root : { downloadedRoot(), customRoot() }) {
        QDir().mkpath(root);
        for (const QString& task : AiModelTaxonomy::allTasks())
            QDir().mkpath(QDir(root).filePath(AiModelTaxonomy::taskToFolder(task)));
    }

    discoverInto(bundledRoot(), AiModelOrigin::Bundled, m_models);
    discoverInto(downloadedRoot(), AiModelOrigin::Downloaded, m_models);
    discoverInto(customRoot(), AiModelOrigin::Custom, m_models);
    discoverLegacyFlat(m_models);

    rebuildIndex();

    int valid = 0, invalid = 0;
    for (const AiModelDescriptor& d : m_models)
        (d.isValid() ? valid : invalid)++;
    qInfo().noquote() << "[AI][MODELS] Discovered" << valid << "valid," << invalid
                      << "invalid model(s) in" << m_modelsDir;

    emit installedModelsChanged();
}

AiInstalledModel AiModelRegistry::toInstalled(const AiModelDescriptor& d) const
{
    AiInstalledModel m;
    m.id = d.id;
    m.family = d.family;
    m.role = AiModelTaxonomy::taskToRole(d.task);
    m.name = d.displayName;
    m.directory = d.rootDir;

    auto addFile = [&](const QString& type, const QString& filename) {
        if (filename.isEmpty())
            return;
        AiModelFile f;
        f.type = type;
        f.filename = filename;
        f.sizeBytes = QFileInfo(QDir(d.rootDir).filePath(filename)).size();
        m.files.append(f);
    };
    addFile(QStringLiteral("encoder"), d.files.encoder);
    addFile(QStringLiteral("decoder"), d.files.decoder);
    addFile(QStringLiteral("model"),   d.files.model);
    for (auto it = d.files.extraFiles.constBegin(); it != d.files.extraFiles.constEnd(); ++it)
        addFile(it.key(), it.value());
    return m;
}

// ── Queries ───────────────────────────────────────────────────────────────--

QList<AiModelDescriptor> AiModelRegistry::validModels() const
{
    return m_byId.values();
}

QList<AiModelDescriptor> AiModelRegistry::invalidModels() const
{
    QList<AiModelDescriptor> out;
    for (const AiModelDescriptor& d : m_models)
        if (!d.isValid())
            out.append(d);
    return out;
}

QList<AiModelDescriptor> AiModelRegistry::modelsByTask(const QString& task) const
{
    const QString t = task.trimmed().toLower();
    QList<AiModelDescriptor> out;
    for (const AiModelDescriptor& d : m_byId)
        if (d.task == t)
            out.append(d);
    return out;
}

QList<AiModelDescriptor> AiModelRegistry::installedInpaintingModels() const
{
    return installedModelsForTask(QStringLiteral("remove_object"));
}

QList<AiModelDescriptor> AiModelRegistry::installedModelsForTask(const QString& task) const
{
    const QString t = task.trimmed().toLower();
    QList<AiModelDescriptor> out;
    for (const AiModelDescriptor& d : m_byId) {
        if (d.task == t || d.type == t || d.capabilities.contains(t, Qt::CaseInsensitive)) {
            out.append(d);
            continue;
        }
        if (t == QLatin1String("remove_object")
            && (d.task == QLatin1String("inpainting")
                || d.capabilities.contains(QStringLiteral("inpainting"), Qt::CaseInsensitive)
                || d.capabilities.contains(QStringLiteral("inpaint"), Qt::CaseInsensitive))) {
            out.append(d);
        }
    }
    return out;
}

std::optional<AiModelDescriptor> AiModelRegistry::bestModelForTask(const QString& task) const
{
    const QList<AiModelDescriptor> candidates = installedModelsForTask(task);
    if (candidates.isEmpty())
        return std::nullopt;

    const QString userId = userDefaultModelId(task);
    if (!userId.isEmpty()) {
        for (const AiModelDescriptor& d : candidates)
            if (d.id == userId)
                return d;
    }
    for (const AiModelDescriptor& d : candidates)
        if (d.isDefault)
            return d;
    return candidates.first();
}

QList<AiModelDescriptor> AiModelRegistry::modelsByCapability(const QString& capability) const
{
    QList<AiModelDescriptor> out;
    for (const AiModelDescriptor& d : m_byId)
        if (d.capabilities.contains(capability, Qt::CaseInsensitive))
            out.append(d);
    return out;
}

std::optional<AiModelDescriptor> AiModelRegistry::modelById(const QString& id) const
{
    if (auto it = m_byId.constFind(id); it != m_byId.constEnd())
        return it.value();
    // Fall back to an invalid match so callers/diagnostics can still inspect it.
    for (const AiModelDescriptor& d : m_models)
        if (d.id == id)
            return d;
    return std::nullopt;
}

std::optional<AiModelDescriptor> AiModelRegistry::defaultModelForTask(const QString& task) const
{
    const QString t = task.trimmed().toLower();
    const QList<AiModelDescriptor> candidates = modelsByTask(t);
    if (candidates.isEmpty())
        return std::nullopt;

    // 1) Honour an explicit user choice when it is still valid for this task.
    const QString userId = userDefaultModelId(t);
    if (!userId.isEmpty()) {
        for (const AiModelDescriptor& d : candidates)
            if (d.id == userId)
                return d;
    }

    // 2) model.json default → quality → origin priority.
    const AiModelDescriptor* best = nullptr;
    for (const AiModelDescriptor& d : candidates) {
        if (!best) { best = &d; continue; }
        const bool dDefault = d.isDefault, bDefault = best->isDefault;
        if (dDefault != bDefault) { if (dDefault) best = &d; continue; }
        const int dq = qualityRank(d.quality), bq = qualityRank(best->quality);
        if (dq != bq) { if (dq < bq) best = &d; continue; }
        if (originPriority(d.origin) > originPriority(best->origin)) best = &d;
    }
    return best ? std::optional<AiModelDescriptor>(*best) : std::nullopt;
}

bool AiModelRegistry::validateModel(const AiModelDescriptor& model) const
{
    if (model.status != AiModelStatus::Valid)
        return false;
    for (const QString& rel : model.files.allFiles())
        if (!rel.isEmpty() && !QFile::exists(QDir(model.rootDir).filePath(rel)))
            return false;
    return true;
}

void AiModelRegistry::setDefaultModelForTask(const QString& task, const QString& modelId)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai/defaultModel"));
    if (modelId.isEmpty())
        settings.remove(task.trimmed().toLower());
    else
        settings.setValue(task.trimmed().toLower(), modelId);
    settings.endGroup();
    emit installedModelsChanged();
}

QString AiModelRegistry::userDefaultModelId(const QString& task) const
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("ai/defaultModel"));
    const QString id = settings.value(task.trimmed().toLower()).toString();
    settings.endGroup();
    return id;
}

// ── Inference compat ─────────────────────────────────────────────────────────

QList<AiInstalledModel> AiModelRegistry::installedModels() const
{
    return m_installed.values();
}

AiInstalledModel AiModelRegistry::installedModel(const QString& modelId) const
{
    return m_installed.value(modelId);
}

QString AiModelRegistry::modelDirectory(const QString& modelId) const
{
    if (auto it = m_byId.constFind(modelId); it != m_byId.constEnd())
        return it.value().rootDir;
    if (auto d = modelById(modelId))
        return d->rootDir;
    return QString();
}

bool AiModelRegistry::validateModelFiles(const QString& modelId, QString* reason) const
{
    const AiInstalledModel meta = m_installed.value(modelId);
    if (!meta.isValid()) {
        if (reason) *reason = QStringLiteral("model not installed");
        return false;
    }
    for (const AiModelFile& f : meta.files) {
        const QString path = QDir(meta.directory).filePath(f.filename);
        QFileInfo fi(path);
        if (!fi.exists()) {
            if (reason) *reason = QStringLiteral("missing file: %1").arg(f.filename);
            return false;
        }
        if (f.sizeBytes > 0 && fi.size() != f.sizeBytes) {
            if (reason) *reason = QStringLiteral("size mismatch: %1").arg(f.filename);
            return false;
        }
        if (f.hasChecksum()) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly)) {
                if (reason) *reason = QStringLiteral("cannot read: %1").arg(f.filename);
                return false;
            }
            QCryptographicHash hash(QCryptographicHash::Sha256);
            if (!hash.addData(&file)) {
                if (reason) *reason = QStringLiteral("hash failed: %1").arg(f.filename);
                return false;
            }
            if (QString::fromLatin1(hash.result().toHex()) != f.sha256) {
                if (reason) *reason = QStringLiteral("sha256 mismatch: %1").arg(f.filename);
                return false;
            }
        }
    }
    return true;
}

bool AiModelRegistry::removeInstalled(const QString& modelId, QString* error)
{
    const auto d = modelById(modelId);
    if (!d) {
        m_installed.remove(modelId);
        emit installedModelsChanged();
        return true;
    }
    if (d->origin != AiModelOrigin::Downloaded) {
        if (error)
            *error = (d->origin == AiModelOrigin::Bundled)
                         ? QStringLiteral("Bundled models cannot be removed.")
                         : QStringLiteral("Custom models are not deleted by the app.");
        return false;
    }
    QDir dir(d->rootDir);
    if (dir.exists() && !dir.removeRecursively()) {
        if (error) *error = QStringLiteral("Could not remove model folder: %1").arg(d->rootDir);
        return false;
    }
    qInfo().noquote() << "[AI][MODELS] Removed downloaded model:" << modelId;
    refresh();
    return true;
}
