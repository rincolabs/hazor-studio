#include "ai/sd/SdModelManager.hpp"

#include "core/AppPaths.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QSaveFile>

#include <algorithm>

namespace sdlocal {
namespace {

// The registry file. A plain JSON document under Hazor's data directory, the
// same place presets, agent configs and swatches live. No database: the whole
// thing is a list of paths and a default, and a human has to be able to fix it
// by hand when a drive letter or a mount point changes.
const char* kConfigFileName = "sd-models.json";
constexpr int kConfigVersion = 1;

// The literal a workflow uses to mean "whatever the default model is", so a
// workflow document can be shared without naming one machine's checkpoint.
const char* kDefaultModelToken = "default";

// Weights containers this revision of stable-diffusion.cpp can load. Checked
// when scanning a directory, so a scan does not turn every .txt and .json in a
// ComfyUI tree into a model entry.
const char* const kWeightSuffixes[] = { "safetensors", "ckpt", "gguf", "pt", "pth", "bin", "sft" };

// Developer escape hatches, and the reason no absolute path is committed in the
// source any more. HAZOR_SD_MODEL points at one checkpoint and makes it the
// default; HAZOR_SD_MODEL_DIR registers a checkpoints directory (a ComfyUI
// models/checkpoints, typically). Both are applied on top of the config file
// and are not persisted, so they stay a property of the shell that launched
// Hazor rather than something that silently becomes permanent.
const char* kEnvModel = "HAZOR_SD_MODEL";
const char* kEnvModelDir = "HAZOR_SD_MODEL_DIR";
const char* kEnvLoraDir = "HAZOR_SD_LORA_DIR";
const char* kEnvVaeDir = "HAZOR_SD_VAE_DIR";

QString tr_(const char* text)
{
    return QCoreApplication::translate("StableDiffusion", text);
}

bool hasWeightSuffix(const QString& fileName)
{
    const QString suffix = QFileInfo(fileName).suffix().toLower();
    for (const char* s : kWeightSuffixes) {
        if (suffix == QLatin1String(s))
            return true;
    }
    return false;
}

// Infers the encoder slot from the file name. Component releases are named
// after the encoder they contain (clip_l.safetensors, t5xxl_fp16.safetensors),
// which is the only signal available without opening the file.
SdTextEncoderRole inferEncoderRole(const QString& path)
{
    const QString name = QFileInfo(path).fileName().toLower();
    if (name.contains(QLatin1String("clip_vision")) || name.contains(QLatin1String("clip-vision")))
        return SdTextEncoderRole::ClipVision;
    if (name.contains(QLatin1String("llm_vision")) || name.contains(QLatin1String("llm-vision")))
        return SdTextEncoderRole::LlmVision;
    if (name.contains(QLatin1String("llm")) || name.contains(QLatin1String("qwen"))
        || name.contains(QLatin1String("gemma")) || name.contains(QLatin1String("llama")))
        return SdTextEncoderRole::Llm;
    if (name.contains(QLatin1String("t5")))
        return SdTextEncoderRole::T5xxl;
    if (name.contains(QLatin1String("clip_g")) || name.contains(QLatin1String("clip-g"))
        || name.contains(QLatin1String("clip_vit_g")))
        return SdTextEncoderRole::ClipG;
    if (name.contains(QLatin1String("clip_l")) || name.contains(QLatin1String("clip-l"))
        || name.contains(QLatin1String("clip_vit_l")))
        return SdTextEncoderRole::ClipL;
    return SdTextEncoderRole::Auto;
}

QString normalisedPath(const QString& path)
{
    if (path.isEmpty())
        return path;
    const QFileInfo fi(path);
    // canonicalFilePath() is empty for something that does not exist; keeping
    // the absolute form in that case is what lets a missing file be *reported*
    // as missing instead of vanishing from the error message.
    const QString canonical = fi.canonicalFilePath();
    return canonical.isEmpty() ? fi.absoluteFilePath() : canonical;
}

} // namespace

// ── Enum names (the JSON and workflow vocabulary) ───────────────────────────

QString modelCategoryName(SdModelCategory category)
{
    switch (category) {
    case SdModelCategory::Checkpoint:     return QStringLiteral("checkpoint");
    case SdModelCategory::DiffusionModel: return QStringLiteral("diffusion_model");
    case SdModelCategory::TextEncoder:    return QStringLiteral("text_encoder");
    case SdModelCategory::VisionEncoder:  return QStringLiteral("vision_encoder");
    case SdModelCategory::Vae:            return QStringLiteral("vae");
    case SdModelCategory::Lora:           return QStringLiteral("lora");
    case SdModelCategory::Embedding:      return QStringLiteral("embedding");
    case SdModelCategory::ControlNet:     return QStringLiteral("control_net");
    case SdModelCategory::IpAdapter:      return QStringLiteral("ip_adapter");
    case SdModelCategory::PhotoMaker:     return QStringLiteral("photo_maker");
    case SdModelCategory::PuLid:          return QStringLiteral("pulid");
    case SdModelCategory::IdentityEmbedding: return QStringLiteral("identity_embedding");
    case SdModelCategory::Upscaler:       return QStringLiteral("upscaler");
    case SdModelCategory::ADetailer:      return QStringLiteral("adetailer");
    }
    return QStringLiteral("checkpoint");
}

bool modelCategoryFromName(const QString& name, SdModelCategory& out)
{
    const QString n = name.trimmed().toLower();
    if (n == QLatin1String("checkpoint")) { out = SdModelCategory::Checkpoint; return true; }
    if (n == QLatin1String("diffusion_model") || n == QLatin1String("unet")) {
        out = SdModelCategory::DiffusionModel;
        return true;
    }
    if (n == QLatin1String("text_encoder") || n == QLatin1String("clip")) {
        out = SdModelCategory::TextEncoder;
        return true;
    }
    if (n == QLatin1String("vision_encoder") || n == QLatin1String("clip_vision")) {
        out = SdModelCategory::VisionEncoder;
        return true;
    }
    if (n == QLatin1String("vae")) { out = SdModelCategory::Vae; return true; }
    if (n == QLatin1String("lora")) { out = SdModelCategory::Lora; return true; }
    if (n == QLatin1String("embedding")) { out = SdModelCategory::Embedding; return true; }
    if (n == QLatin1String("control_net") || n == QLatin1String("controlnet")) {
        out = SdModelCategory::ControlNet;
        return true;
    }
    if (n == QLatin1String("ip_adapter") || n == QLatin1String("ipadapter")) {
        out = SdModelCategory::IpAdapter;
        return true;
    }
    if (n == QLatin1String("photo_maker") || n == QLatin1String("photomaker")) {
        out = SdModelCategory::PhotoMaker;
        return true;
    }
    if (n == QLatin1String("pulid")) { out = SdModelCategory::PuLid; return true; }
    if (n == QLatin1String("identity_embedding")) {
        out = SdModelCategory::IdentityEmbedding;
        return true;
    }
    if (n == QLatin1String("upscaler") || n == QLatin1String("esrgan")) {
        out = SdModelCategory::Upscaler;
        return true;
    }
    if (n == QLatin1String("adetailer") || n == QLatin1String("detector")) {
        out = SdModelCategory::ADetailer;
        return true;
    }
    return false;
}

QString textEncoderRoleName(SdTextEncoderRole role)
{
    switch (role) {
    case SdTextEncoderRole::ClipL: return QStringLiteral("clip_l");
    case SdTextEncoderRole::ClipG: return QStringLiteral("clip_g");
    case SdTextEncoderRole::T5xxl: return QStringLiteral("t5xxl");
    case SdTextEncoderRole::Llm: return QStringLiteral("llm");
    case SdTextEncoderRole::LlmVision: return QStringLiteral("llm_vision");
    case SdTextEncoderRole::ClipVision: return QStringLiteral("clip_vision");
    case SdTextEncoderRole::Auto:  break;
    }
    return QStringLiteral("auto");
}

bool textEncoderRoleFromName(const QString& name, SdTextEncoderRole& out)
{
    const QString n = name.trimmed().toLower();
    if (n.isEmpty() || n == QLatin1String("auto")) { out = SdTextEncoderRole::Auto; return true; }
    if (n == QLatin1String("clip_l") || n == QLatin1String("clip-l")) { out = SdTextEncoderRole::ClipL; return true; }
    if (n == QLatin1String("clip_g") || n == QLatin1String("clip-g")) { out = SdTextEncoderRole::ClipG; return true; }
    if (n == QLatin1String("t5xxl") || n == QLatin1String("t5")) { out = SdTextEncoderRole::T5xxl; return true; }
    if (n == QLatin1String("llm")) { out = SdTextEncoderRole::Llm; return true; }
    if (n == QLatin1String("llm_vision") || n == QLatin1String("llm-vision")) { out = SdTextEncoderRole::LlmVision; return true; }
    if (n == QLatin1String("clip_vision") || n == QLatin1String("clip-vision")) { out = SdTextEncoderRole::ClipVision; return true; }
    return false;
}

// ── SdModelEntry / SdModelFiles ─────────────────────────────────────────────

bool SdModelEntry::fileExists() const
{
    return !path.isEmpty() && QFileInfo(path).isFile();
}

QStringList SdModelFiles::paths() const
{
    QStringList out;
    for (const QString& p : { checkpointPath, diffusionModelPath, clipLPath,
                              clipGPath, t5xxlPath, llmPath, llmVisionPath,
                              clipVisionPath, vaePath, controlNetPath, ipAdapterPath,
                              photoMakerPath, pulidPath }) {
        if (!p.isEmpty())
            out << p;
    }
    for (const SdResolvedModelFile& embedding : embeddings) {
        if (!embedding.path.isEmpty())
            out << embedding.path;
    }
    return out;
}

bool SdModelFiles::allPresent(QString* missing) const
{
    for (const QString& p : paths()) {
        if (!QFileInfo(p).isFile()) {
            if (missing)
                *missing = p;
            return false;
        }
    }
    return true;
}

// ── SdModelManager ──────────────────────────────────────────────────────────

SdModelManager& SdModelManager::instance()
{
    static SdModelManager manager;
    return manager;
}

SdModelManager::SdModelManager()
{
    load();
    applyEnvironmentOverrides();
}

QString SdModelManager::configPath() const
{
    return AppPaths::dataFile(QString::fromUtf8(kConfigFileName));
}

void SdModelManager::load()
{
    const QString path = configPath();
    QFile file(path);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning().noquote() << "[SD] cannot read the model registry |" << path
                             << "|" << file.errorString();
        return;
    }

    QJsonParseError parseError {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning().noquote() << "[SD] malformed model registry |" << path
                             << "|" << parseError.errorString();
        return;
    }

    const QJsonObject root = doc.object();
    const int version = root.value(QStringLiteral("version")).toInt(kConfigVersion);
    if (version > kConfigVersion) {
        qWarning().noquote() << "[SD] model registry version" << version
                             << "is newer than this build understands; ignoring it";
        return;
    }

    for (const QJsonValue& v : root.value(QStringLiteral("directories")).toArray()) {
        const QJsonObject o = v.toObject();
        SdModelDirectory dir;
        dir.path = o.value(QStringLiteral("path")).toString();
        if (dir.path.isEmpty())
            continue;
        if (!modelCategoryFromName(o.value(QStringLiteral("category")).toString(), dir.category))
            dir.category = SdModelCategory::Checkpoint;
        if (!textEncoderRoleFromName(o.value(QStringLiteral("encoderRole")).toString(), dir.encoderRole))
            dir.encoderRole = SdTextEncoderRole::Auto;
        dir.recursive = o.value(QStringLiteral("recursive")).toBool(true);
        m_directories.push_back(dir);
    }

    for (const QJsonValue& v : root.value(QStringLiteral("models")).toArray()) {
        const QJsonObject o = v.toObject();
        SdModelEntry entry;
        entry.id = o.value(QStringLiteral("id")).toString();
        entry.path = o.value(QStringLiteral("path")).toString();
        if (entry.id.isEmpty() || entry.path.isEmpty())
            continue;
        if (!modelCategoryFromName(o.value(QStringLiteral("category")).toString(), entry.category))
            entry.category = SdModelCategory::Checkpoint;
        if (!textEncoderRoleFromName(o.value(QStringLiteral("encoderRole")).toString(), entry.encoderRole))
            entry.encoderRole = SdTextEncoderRole::Auto;
        entry.label = o.value(QStringLiteral("label")).toString();
        if (entry.label.isEmpty())
            entry.label = QFileInfo(entry.path).completeBaseName();
        m_pinned.push_back(entry);
    }

    m_defaultModelId = root.value(QStringLiteral("defaultModelId")).toString();
}

void SdModelManager::applyEnvironmentOverrides()
{
    const QProcessEnvironment env = QProcessEnvironment::systemEnvironment();

    const QString modelPath = env.value(QString::fromUtf8(kEnvModel)).trimmed();
    if (!modelPath.isEmpty()) {
        SdModelEntry entry;
        entry.id = QStringLiteral("env");
        entry.path = normalisedPath(modelPath);
        entry.category = SdModelCategory::Checkpoint;
        entry.label = QFileInfo(entry.path).completeBaseName();
        // Overrides a same-named pinned entry rather than failing: the point of
        // the variable is to win for this run.
        m_pinned.removeIf([&entry](const SdModelEntry& e) { return e.id == entry.id; });
        m_pinned.push_back(entry);
        m_defaultModelId = entry.id;
        if (!entry.fileExists()) {
            qWarning().noquote() << "[SD]" << kEnvModel << "points at a file that does not exist |"
                                 << entry.path;
        }
    }

    struct EnvDir { const char* var; SdModelCategory category; };
    for (const EnvDir& e : { EnvDir { kEnvModelDir, SdModelCategory::Checkpoint },
                             EnvDir { kEnvLoraDir, SdModelCategory::Lora },
                             EnvDir { kEnvVaeDir, SdModelCategory::Vae } }) {
        const QString dirPath = env.value(QString::fromUtf8(e.var)).trimmed();
        if (dirPath.isEmpty())
            continue;
        SdModelDirectory dir;
        dir.path = normalisedPath(dirPath);
        dir.category = e.category;
        const bool known = std::any_of(m_directories.cbegin(), m_directories.cend(),
                                       [&dir](const SdModelDirectory& d) {
                                           return d.path == dir.path && d.category == dir.category;
                                       });
        if (!known)
            m_directories.push_back(dir);
    }
}

bool SdModelManager::save(QString* error) const
{
    QJsonArray dirs;
    for (const SdModelDirectory& d : m_directories) {
        QJsonObject o;
        o[QStringLiteral("path")] = d.path;
        o[QStringLiteral("category")] = modelCategoryName(d.category);
        if (d.encoderRole != SdTextEncoderRole::Auto)
            o[QStringLiteral("encoderRole")] = textEncoderRoleName(d.encoderRole);
        if (!d.recursive)
            o[QStringLiteral("recursive")] = false;
        dirs.append(o);
    }

    QJsonArray models;
    for (const SdModelEntry& m : m_pinned) {
        QJsonObject o;
        o[QStringLiteral("id")] = m.id;
        o[QStringLiteral("path")] = m.path;
        o[QStringLiteral("category")] = modelCategoryName(m.category);
        if (m.encoderRole != SdTextEncoderRole::Auto)
            o[QStringLiteral("encoderRole")] = textEncoderRoleName(m.encoderRole);
        if (!m.label.isEmpty())
            o[QStringLiteral("label")] = m.label;
        models.append(o);
    }

    QJsonObject root;
    root[QStringLiteral("version")] = kConfigVersion;
    root[QStringLiteral("directories")] = dirs;
    root[QStringLiteral("models")] = models;
    if (!m_defaultModelId.isEmpty())
        root[QStringLiteral("defaultModelId")] = m_defaultModelId;

    // QSaveFile, so a crash or a full disk cannot leave a half-written registry
    // behind: the config is the only record of where the user's models are.
    QSaveFile file(configPath());
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error)
            *error = tr_("Cannot write the model registry: %1").arg(file.errorString());
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error)
            *error = tr_("Cannot write the model registry: %1").arg(file.errorString());
        return false;
    }
    return true;
}

void SdModelManager::ensureScanned() const
{
    if (m_scanned)
        return;
    m_scanned = true;
    m_discovered.clear();

    QVector<SdModelEntry> taken = m_pinned;
    for (const SdModelDirectory& dir : m_directories) {
        const QFileInfo dirInfo(dir.path);
        if (!dirInfo.isDir()) {
            qWarning().noquote() << "[SD] registered model directory is missing |" << dir.path;
            continue;
        }
        QDirIterator it(dir.path, QDir::Files | QDir::Readable,
                        dir.recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
        while (it.hasNext()) {
            const QString filePath = it.next();
            if (!hasWeightSuffix(filePath))
                continue;
            const QString absolute = normalisedPath(filePath);
            // A path pinned by id wins: the user's own entry keeps its id and
            // its category even when it also sits inside a scanned directory.
            const bool alreadyPinned = std::any_of(
                m_pinned.cbegin(), m_pinned.cend(),
                [&absolute](const SdModelEntry& e) { return normalisedPath(e.path) == absolute; });
            if (alreadyPinned)
                continue;

            SdModelEntry entry;
            entry.path = absolute;
            entry.category = dir.category;
            entry.encoderRole = dir.category == SdModelCategory::TextEncoder
                ? (dir.encoderRole == SdTextEncoderRole::Auto ? inferEncoderRole(absolute)
                                                              : dir.encoderRole)
                : SdTextEncoderRole::Auto;
            entry.label = QFileInfo(absolute).completeBaseName();
            entry.id = uniqueId(entry.label, taken);
            entry.discovered = true;
            taken.push_back(entry);
            m_discovered.push_back(entry);
        }
    }

    if (!m_directories.isEmpty()) {
        qInfo().noquote() << "[SD] model scan |" << m_directories.size() << "directories |"
                          << m_discovered.size() << "models found |" << m_pinned.size() << "pinned";
    }
}

void SdModelManager::refresh()
{
    m_scanned = false;
    ensureScanned();
}

QString SdModelManager::uniqueId(const QString& base, const QVector<SdModelEntry>& taken)
{
    // Ids are referenced from workflow documents, so they have to be stable and
    // readable rather than opaque: the file's base name, with a numeric suffix
    // only when two directories hold files of the same name.
    QString candidate = base.isEmpty() ? QStringLiteral("model") : base;
    const auto isTaken = [&taken](const QString& id) {
        return std::any_of(taken.cbegin(), taken.cend(),
                           [&id](const SdModelEntry& e) { return e.id == id; });
    };
    if (!isTaken(candidate))
        return candidate;
    for (int n = 2; n < 10000; ++n) {
        const QString next = QStringLiteral("%1-%2").arg(candidate).arg(n);
        if (!isTaken(next))
            return next;
    }
    return candidate;
}

bool SdModelManager::registerModel(const SdModelEntry& entry, QString* error)
{
    SdModelEntry e = entry;
    e.path = normalisedPath(e.path);
    if (e.path.isEmpty()) {
        if (error)
            *error = tr_("A model needs a file path.");
        return false;
    }
    if (!QFileInfo(e.path).isFile()) {
        if (error)
            *error = tr_("Model file not found: %1").arg(e.path);
        return false;
    }
    if (e.label.isEmpty())
        e.label = QFileInfo(e.path).completeBaseName();
    if (e.id.isEmpty())
        e.id = uniqueId(e.label, m_pinned);
    if (std::any_of(m_pinned.cbegin(), m_pinned.cend(),
                    [&e](const SdModelEntry& o) { return o.id == e.id; })) {
        if (error)
            *error = tr_("A model with the id \"%1\" is already registered.").arg(e.id);
        return false;
    }
    if (e.category == SdModelCategory::TextEncoder && e.encoderRole == SdTextEncoderRole::Auto)
        e.encoderRole = inferEncoderRole(e.path);
    e.discovered = false;
    m_pinned.push_back(e);
    m_scanned = false;   // a pinned path must drop out of the discovered set
    return save(error);
}

bool SdModelManager::unregisterModel(const QString& id)
{
    const qsizetype removed = m_pinned.removeIf(
        [&id](const SdModelEntry& e) { return e.id == id; });
    if (removed == 0)
        return false;
    if (m_defaultModelId == id)
        m_defaultModelId.clear();
    m_scanned = false;
    return save();
}

bool SdModelManager::registerDirectory(const SdModelDirectory& dir, QString* error)
{
    SdModelDirectory d = dir;
    d.path = normalisedPath(d.path);
    if (d.path.isEmpty()) {
        if (error)
            *error = tr_("A model directory needs a path.");
        return false;
    }
    if (!QFileInfo(d.path).isDir()) {
        if (error)
            *error = tr_("Model directory not found: %1").arg(d.path);
        return false;
    }
    if (std::any_of(m_directories.cbegin(), m_directories.cend(),
                    [&d](const SdModelDirectory& o) {
                        return o.path == d.path && o.category == d.category;
                    })) {
        if (error)
            *error = tr_("That directory is already registered for %1 models.")
                         .arg(modelCategoryName(d.category));
        return false;
    }
    m_directories.push_back(d);
    if (!save(error))
        return false;
    refresh();
    return true;
}

bool SdModelManager::unregisterDirectory(const QString& path)
{
    const QString target = normalisedPath(path);
    const qsizetype removed = m_directories.removeIf(
        [&target](const SdModelDirectory& d) { return d.path == target; });
    if (removed == 0)
        return false;
    m_scanned = false;
    return save();
}

QVector<SdModelDirectory> SdModelManager::directories() const
{
    return m_directories;
}

QVector<SdModelEntry> SdModelManager::models() const
{
    ensureScanned();
    QVector<SdModelEntry> out = m_pinned;
    out += m_discovered;
    return out;
}

QVector<SdModelEntry> SdModelManager::models(SdModelCategory category) const
{
    QVector<SdModelEntry> out;
    for (const SdModelEntry& e : models()) {
        if (e.category == category)
            out.push_back(e);
    }
    return out;
}

bool SdModelManager::model(const QString& id, SdModelEntry& out) const
{
    if (id.isEmpty())
        return false;
    ensureScanned();
    // Declared as an array rather than a braced list: m_discovered is mutable, so
    // in a const member function it is non-const while m_pinned is const, and an
    // initializer_list cannot deduce a single element type from the two.
    const QVector<SdModelEntry>* sets[] = { &m_pinned, &m_discovered };
    for (const QVector<SdModelEntry>* set : sets) {
        for (const SdModelEntry& e : *set) {
            if (e.id == id) {
                out = e;
                return true;
            }
        }
    }
    return false;
}

bool SdModelManager::hasModel(const QString& id) const
{
    SdModelEntry unused;
    return model(id, unused);
}

QString SdModelManager::defaultModelId() const
{
    if (!m_defaultModelId.isEmpty() && hasModel(m_defaultModelId))
        return m_defaultModelId;
    // No explicit default: a single registered checkpoint is unambiguous, so
    // use it rather than making the user pin one for the common case of one
    // model on the machine.
    const QVector<SdModelEntry> checkpoints = models(SdModelCategory::Checkpoint);
    if (checkpoints.size() == 1)
        return checkpoints.first().id;
    for (const SdModelEntry& e : checkpoints) {
        if (e.fileExists())
            return e.id;
    }
    return {};
}

void SdModelManager::setDefaultModelId(const QString& id)
{
    if (m_defaultModelId == id)
        return;
    m_defaultModelId = id;
    save();
}

bool SdModelManager::resolve(const SdModelSelection& selection, SdModelFiles& out,
                             QString* error) const
{
    out = SdModelFiles {};

    QString modelId = selection.modelId.trimmed();
    if (modelId.isEmpty() || modelId == QLatin1String(kDefaultModelToken))
        modelId = defaultModelId();
    if (modelId.isEmpty()) {
        if (error) {
            *error = tr_("No Stable Diffusion model is registered. Register a checkpoint, "
                         "or set %1 to one, and try again (registry: %2).")
                         .arg(QString::fromUtf8(kEnvModel), configPath());
        }
        return false;
    }

    SdModelEntry entry;
    if (!model(modelId, entry)) {
        if (error)
            *error = tr_("Unknown model id \"%1\".").arg(modelId);
        return false;
    }
    if (!entry.fileExists()) {
        if (error)
            *error = tr_("Model file not found: %1").arg(entry.path);
        return false;
    }

    out.modelId = entry.id;
    switch (entry.category) {
    case SdModelCategory::Checkpoint:
        out.checkpointPath = entry.path;
        break;
    case SdModelCategory::DiffusionModel:
        out.diffusionModelPath = entry.path;
        break;
    case SdModelCategory::TextEncoder:
    case SdModelCategory::VisionEncoder:
    case SdModelCategory::Vae:
    case SdModelCategory::Lora:
    case SdModelCategory::Embedding:
    case SdModelCategory::ControlNet:
    case SdModelCategory::IpAdapter:
    case SdModelCategory::PhotoMaker:
    case SdModelCategory::PuLid:
    case SdModelCategory::IdentityEmbedding:
    case SdModelCategory::Upscaler:
    case SdModelCategory::ADetailer:
        // Refusing is the point of recording categories: a VAE or a LoRA handed
        // to new_sd_ctx() as model_path fails deep inside the loader with a
        // tensor-name error that says nothing about the real mistake.
        if (error) {
            *error = tr_("\"%1\" is a %2, not a model that can be generated from.")
                         .arg(entry.id, modelCategoryName(entry.category));
        }
        return false;
    }

    for (const QString& encoderId : selection.textEncoderIds) {
        SdModelEntry encoder;
        if (!model(encoderId, encoder)) {
            if (error)
                *error = tr_("Unknown text encoder id \"%1\".").arg(encoderId);
            return false;
        }
        if (encoder.category != SdModelCategory::TextEncoder) {
            if (error)
                *error = tr_("\"%1\" is a %2, not a text encoder.")
                             .arg(encoder.id, modelCategoryName(encoder.category));
            return false;
        }
        if (!encoder.fileExists()) {
            if (error)
                *error = tr_("Text encoder file not found: %1").arg(encoder.path);
            return false;
        }
        SdTextEncoderRole role = encoder.encoderRole == SdTextEncoderRole::Auto
            ? inferEncoderRole(encoder.path) : encoder.encoderRole;
        QString* slot = nullptr;
        switch (role) {
        case SdTextEncoderRole::ClipL: slot = &out.clipLPath; break;
        case SdTextEncoderRole::ClipG: slot = &out.clipGPath; break;
        case SdTextEncoderRole::T5xxl: slot = &out.t5xxlPath; break;
        case SdTextEncoderRole::Llm: slot = &out.llmPath; break;
        case SdTextEncoderRole::LlmVision: slot = &out.llmVisionPath; break;
        case SdTextEncoderRole::ClipVision: slot = &out.clipVisionPath; break;
        case SdTextEncoderRole::Auto:  break;
        }
        if (!slot) {
            if (error) {
                *error = tr_("Cannot tell which text encoder \"%1\" is. Set its role to "
                             "clip_l, clip_g, t5xxl, llm, llm_vision or clip_vision.")
                             .arg(encoder.id);
            }
            return false;
        }
        if (!slot->isEmpty()) {
            if (error) {
                *error = tr_("Two text encoders were given for the same %1 slot.")
                             .arg(textEncoderRoleName(role));
            }
            return false;
        }
        *slot = encoder.path;
    }

    if (!selection.vaeId.isEmpty()) {
        SdModelEntry vae;
        if (!model(selection.vaeId, vae)) {
            if (error)
                *error = tr_("Unknown VAE id \"%1\".").arg(selection.vaeId);
            return false;
        }
        if (vae.category != SdModelCategory::Vae) {
            if (error)
                *error = tr_("\"%1\" is a %2, not a VAE.")
                             .arg(vae.id, modelCategoryName(vae.category));
            return false;
        }
        if (!vae.fileExists()) {
            if (error)
                *error = tr_("VAE file not found: %1").arg(vae.path);
            return false;
        }
        out.vaePath = vae.path;
    }

    for (const QString& embeddingId : selection.embeddingIds) {
        SdResolvedModelFile embedding;
        if (!resolveFile(embeddingId, SdModelCategory::Embedding, embedding, error))
            return false;
        out.embeddings.push_back(embedding);
    }

    // A bare diffusion model carries neither a text encoder nor a VAE, so a
    // context cannot be built from it alone. Saying which component is missing
    // here is the difference between a fixable message and a loader crash
    // thirty seconds into a 4 GiB read.
    if (!out.diffusionModelPath.isEmpty()) {
        QStringList missing;
        if (out.clipLPath.isEmpty() && out.t5xxlPath.isEmpty() && out.clipGPath.isEmpty())
            missing << tr_("a text encoder");
        if (out.vaePath.isEmpty())
            missing << tr_("a VAE");
        if (!missing.isEmpty()) {
            if (error) {
                *error = tr_("\"%1\" is a diffusion model and still needs %2.")
                             .arg(entry.id, missing.join(tr_(" and ")));
            }
            return false;
        }
    }

    return true;
}

bool SdModelManager::resolveLora(const QString& id, float weight, SdResolvedLora& out,
                                 QString* error) const
{
    SdModelEntry entry;
    if (!model(id, entry)) {
        if (error)
            *error = tr_("Unknown LoRA id \"%1\".").arg(id);
        return false;
    }
    if (entry.category != SdModelCategory::Lora) {
        if (error)
            *error = tr_("\"%1\" is a %2, not a LoRA.")
                         .arg(entry.id, modelCategoryName(entry.category));
        return false;
    }
    if (!entry.fileExists()) {
        if (error)
            *error = tr_("LoRA file not found: %1").arg(entry.path);
        return false;
    }
    out.id = entry.id;
    out.path = entry.path;
    out.weight = weight;
    return true;
}

bool SdModelManager::resolveFile(const QString& id, SdModelCategory category,
                                 SdResolvedModelFile& out, QString* error) const
{
    out = SdResolvedModelFile {};
    SdModelEntry entry;
    if (!model(id.trimmed(), entry)) {
        if (error)
            *error = tr_("Unknown %1 model id \"%2\".")
                         .arg(modelCategoryName(category), id);
        return false;
    }
    if (entry.category != category) {
        if (error) {
            *error = tr_("\"%1\" is a %2, not a %3 model.")
                         .arg(entry.id, modelCategoryName(entry.category),
                              modelCategoryName(category));
        }
        return false;
    }
    if (!entry.fileExists()) {
        if (error)
            *error = tr_("Model file not found: %1").arg(entry.path);
        return false;
    }
    out.id = entry.id;
    out.path = entry.path;
    return true;
}

} // namespace sdlocal
