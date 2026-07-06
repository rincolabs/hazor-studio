#include "ai/models/AiModelJson.hpp"
#include "ai/sam/SamModelConfig.hpp"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace AiModelJson {

namespace {
constexpr char kBackend[] = "onnxruntime";
constexpr char kRealEsrganBackend[] = "realesrgan-ncnn-vulkan";
} // namespace

bool hasMetadata(const QString& dir)
{
    return QFile::exists(QDir(dir).filePath(QString::fromLatin1(kMetadataFile)));
}

AiModelDescriptor read(const QString& dir, AiModelOrigin origin)
{
    AiModelDescriptor d;
    d.origin = origin;
    d.rootDir = QDir(dir).absolutePath();

    QFile f(QDir(dir).filePath(QString::fromLatin1(kMetadataFile)));
    if (!f.open(QIODevice::ReadOnly)) {
        d.status = AiModelStatus::InvalidMetadata;
        d.statusReason = QStringLiteral("model.json not found");
        return d;
    }
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (doc.isNull() || !doc.isObject()) {
        d.status = AiModelStatus::InvalidMetadata;
        d.statusReason = QStringLiteral("JSON parse error: %1").arg(perr.errorString());
        return d;
    }
    const QJsonObject root = doc.object();

    d.schemaVersion = root.value(QStringLiteral("schema_version")).toInt(0);
    d.id          = root.value(QStringLiteral("id")).toString();
    d.type        = root.value(QStringLiteral("type")).toString().trimmed().toLower();
    d.backend     = root.value(QStringLiteral("backend")).toString().trimmed();
    d.task        = root.value(QStringLiteral("task")).toString().trimmed().toLower();
    d.family      = root.value(QStringLiteral("family")).toString().trimmed().toLower();
    d.variant     = root.value(QStringLiteral("variant")).toString();
    d.displayName = root.value(QStringLiteral("display_name")).toString();
    d.description = root.value(QStringLiteral("description")).toString();
    d.quality     = root.value(QStringLiteral("quality")).toString();
    d.isDefault   = root.value(QStringLiteral("default")).toBool(false);

    const QJsonObject files = root.value(QStringLiteral("files")).toObject();
    for (auto it = files.constBegin(); it != files.constEnd(); ++it) {
        const QString key = it.key().trimmed().toLower();
        const QString filename = it.value().toString();
        if (key == QLatin1String("model"))        d.files.model = filename;
        else if (key == QLatin1String("encoder")) d.files.encoder = filename;
        else if (key == QLatin1String("decoder")) d.files.decoder = filename;
        else if (!key.isEmpty() && !filename.isEmpty()) {
            d.files.extraFiles.insert(key, filename);
        }
    }

    d.input = root.value(QStringLiteral("input")).toObject().toVariantMap();

    for (const QJsonValue& c : root.value(QStringLiteral("capabilities")).toArray())
        d.capabilities << c.toString();
    if (d.capabilities.isEmpty())
        d.capabilities = AiModelTaxonomy::defaultCapabilitiesForTask(d.task);

    const QJsonObject lic = root.value(QStringLiteral("license")).toObject();
    d.licenseName = lic.value(QStringLiteral("name")).toString();
    d.licenseFile = lic.value(QStringLiteral("file")).toString();
    d.noticeFile  = lic.value(QStringLiteral("notice")).toString();

    const QJsonObject src = root.value(QStringLiteral("source")).toObject();
    d.sourceName = src.value(QStringLiteral("name")).toString();
    d.sourceUrl  = src.value(QStringLiteral("url")).toString();

    // ── Validation ──
    if (d.schemaVersion != kSchemaVersion) {
        d.status = AiModelStatus::UnsupportedSchema;
        d.statusReason = QStringLiteral("Unsupported schema_version %1 (expected %2)")
                             .arg(d.schemaVersion).arg(kSchemaVersion);
        return d;
    }

    QStringList missing;
    if (d.id.isEmpty())          missing << QStringLiteral("id");
    if (d.type.isEmpty())        missing << QStringLiteral("type");
    if (d.backend.isEmpty())     missing << QStringLiteral("backend");
    if (d.task.isEmpty())        missing << QStringLiteral("task");
    if (d.family.isEmpty())      missing << QStringLiteral("family");
    if (d.displayName.isEmpty()) missing << QStringLiteral("display_name");
    if (d.files.isEmpty())       missing << QStringLiteral("files");
    if (d.capabilities.isEmpty())missing << QStringLiteral("capabilities");
    if (!missing.isEmpty()) {
        d.status = AiModelStatus::InvalidMetadata;
        d.statusReason = QStringLiteral("Missing required field(s): %1").arg(missing.join(QStringLiteral(", ")));
        return d;
    }

    const bool backendSupported =
        d.backend.compare(QLatin1String(kBackend), Qt::CaseInsensitive) == 0
        || d.backend.compare(QLatin1String(kRealEsrganBackend), Qt::CaseInsensitive) == 0;
    if (!backendSupported) {
        d.status = AiModelStatus::UnsupportedBackend;
        d.statusReason = QStringLiteral("Unsupported backend '%1'").arg(d.backend);
        return d;
    }

    // Required files by type/task — never assume a model is a single file.
    const bool isSam = d.type == QLatin1String("segment_anything")
                       || d.task == QLatin1String("segmentation");
    const bool isStableDiffusion = d.type == QLatin1String("stable_diffusion")
                                   || d.family == QLatin1String("stable_diffusion");
    QStringList requiredFiles;
    if (isSam) {
        if (d.files.encoder.isEmpty() || d.files.decoder.isEmpty()) {
            d.status = AiModelStatus::InvalidMetadata;
            d.statusReason = QStringLiteral("segment_anything requires files.encoder and files.decoder");
            return d;
        }
        if (!d.input.contains(QStringLiteral("input_size"))) {
            d.status = AiModelStatus::InvalidMetadata;
            d.statusReason = QStringLiteral("segment_anything requires input.input_size");
            return d;
        }
        requiredFiles << d.files.encoder << d.files.decoder;
    } else if (isStableDiffusion) {
        static const char* kRequiredComponents[] = {
            "text_encoder", "unet", "vae_encoder", "vae_decoder"
        };
        for (const char* component : kRequiredComponents) {
            const QString key = QString::fromLatin1(component);
            const QString filename = d.files.extraFiles.value(key);
            if (filename.isEmpty()) {
                d.status = AiModelStatus::InvalidMetadata;
                d.statusReason = QStringLiteral("stable_diffusion requires files.%1").arg(key);
                return d;
            }
            requiredFiles << filename;
        }
    } else {
        if (d.files.model.isEmpty()) {
            d.status = AiModelStatus::InvalidMetadata;
            d.statusReason = QStringLiteral("model requires files.model");
            return d;
        }
        requiredFiles << d.files.model;
    }

    QStringList filesToCheck = requiredFiles;
    filesToCheck << d.files.extraFiles.values();
    for (const QString& rel : filesToCheck) {
        if (rel.isEmpty())
            continue;
        if (!QFile::exists(QDir(d.rootDir).filePath(rel))) {
            d.status = AiModelStatus::MissingFiles;
            d.statusReason = QStringLiteral("Missing file on disk: %1").arg(rel);
            return d;
        }
    }

    d.status = AiModelStatus::Valid;
    return d;
}

AiModelDescriptor readLegacy(const QString& dir, AiModelOrigin origin)
{
    AiModelDescriptor d;
    d.origin = origin;
    d.rootDir = QDir(dir).absolutePath();

    // (a) Legacy "installed" model.json (no schema_version, files as an array).
    QFile f(QDir(dir).filePath(QString::fromLatin1(kMetadataFile)));
    if (f.open(QIODevice::ReadOnly)) {
        const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
        if (!root.contains(QStringLiteral("schema_version"))
            && root.value(QStringLiteral("files")).isArray()) {
            d.id          = root.value(QStringLiteral("id")).toString();
            d.family      = root.value(QStringLiteral("family")).toString().trimmed().toLower();
            d.task        = AiModelTaxonomy::roleToTask(root.value(QStringLiteral("role")).toString());
            d.type        = AiModelTaxonomy::familyToType(d.family);
            d.backend     = QString::fromLatin1(kBackend);
            d.displayName = root.value(QStringLiteral("name")).toString();
            d.capabilities = AiModelTaxonomy::defaultCapabilitiesForTask(d.task);

            QStringList required;
            for (const QJsonValue& fv : root.value(QStringLiteral("files")).toArray()) {
                const QJsonObject fo = fv.toObject();
                const QString type = fo.value(QStringLiteral("type")).toString().trimmed().toLower();
                const QString name = fo.value(QStringLiteral("filename")).toString();
                if (type == QLatin1String("encoder"))      d.files.encoder = name;
                else if (type == QLatin1String("decoder")) d.files.decoder = name;
                else if (type == QLatin1String("model")
                         || (d.files.model.isEmpty() && name.endsWith(QStringLiteral(".onnx"), Qt::CaseInsensitive))) {
                    d.files.model = name;
                } else if (!type.isEmpty() && !name.isEmpty()) {
                    d.files.extraFiles.insert(type, name);
                }
                if (!name.isEmpty()) required << name;
            }

            if (d.id.isEmpty() || required.isEmpty()) {
                d.status = AiModelStatus::InvalidMetadata;
                d.statusReason = QStringLiteral("legacy metadata missing id/files");
                return d;
            }
            d.status = AiModelStatus::Valid;
            for (const QString& rel : required) {
                if (!QFile::exists(QDir(d.rootDir).filePath(rel))) {
                    d.status = AiModelStatus::MissingFiles;
                    d.statusReason = QStringLiteral("Missing file on disk: %1").arg(rel);
                    break;
                }
            }
            return d;
        }
    }

    // (b) SAM sidecar config (config.yaml/json), no model.json.
    const QString cfgPath = SamModelConfig::findConfigInDir(dir);
    if (!cfgPath.isEmpty()) {
        const SamModelConfig cfg = SamModelConfig::parseFile(cfgPath);
        if (cfg.isValid()) {
            d.id          = QFileInfo(d.rootDir).fileName();
            d.family      = QStringLiteral("sam");
            d.type        = QStringLiteral("segment_anything");
            d.task        = QStringLiteral("segmentation");
            d.backend     = QString::fromLatin1(kBackend);
            d.displayName = cfg.displayName.isEmpty()
                ? (cfg.name.isEmpty() ? d.id : cfg.name) : cfg.displayName;
            d.files.encoder = cfg.encoderModelPath;
            d.files.decoder = cfg.decoderModelPath;
            d.input.insert(QStringLiteral("input_size"), cfg.inputSize);
            if (cfg.maxWidth > 0)  d.input.insert(QStringLiteral("max_width"), cfg.maxWidth);
            if (cfg.maxHeight > 0) d.input.insert(QStringLiteral("max_height"), cfg.maxHeight);
            d.capabilities = AiModelTaxonomy::defaultCapabilitiesForTask(d.task);

            const bool ok = QFile::exists(QDir(d.rootDir).filePath(cfg.encoderModelPath))
                            && QFile::exists(QDir(d.rootDir).filePath(cfg.decoderModelPath));
            if (!ok) {
                d.status = AiModelStatus::MissingFiles;
                d.statusReason = QStringLiteral("SAM config references a missing encoder/decoder");
            } else {
                d.status = AiModelStatus::Valid;
            }
            return d;
        }
    }

    return d; // empty id → not a model folder
}

AiModelDescriptor readAny(const QString& dir, AiModelOrigin origin)
{
    if (hasMetadata(dir)) {
        QFile f(QDir(dir).filePath(QString::fromLatin1(kMetadataFile)));
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            if (root.contains(QStringLiteral("schema_version")))
                return read(dir, origin);
        }
    }
    return readLegacy(dir, origin);
}

bool write(const QString& dir, const AiModelDescriptor& m, QString* error)
{
    QJsonObject root;
    root[QStringLiteral("schema_version")] = kSchemaVersion;
    root[QStringLiteral("id")]      = m.id;
    root[QStringLiteral("type")]    = m.type;
    root[QStringLiteral("backend")] = m.backend.isEmpty() ? QString::fromLatin1(kBackend) : m.backend;
    root[QStringLiteral("task")]    = m.task;
    root[QStringLiteral("family")]  = m.family;
    if (!m.variant.isEmpty())     root[QStringLiteral("variant")] = m.variant;
    root[QStringLiteral("display_name")] = m.displayName;
    if (!m.description.isEmpty()) root[QStringLiteral("description")] = m.description;
    if (!m.quality.isEmpty())     root[QStringLiteral("quality")] = m.quality;
    if (m.isDefault)              root[QStringLiteral("default")] = true;

    QJsonObject files;
    if (!m.files.model.isEmpty())   files[QStringLiteral("model")]   = m.files.model;
    if (!m.files.encoder.isEmpty()) files[QStringLiteral("encoder")] = m.files.encoder;
    if (!m.files.decoder.isEmpty()) files[QStringLiteral("decoder")] = m.files.decoder;
    for (auto it = m.files.extraFiles.constBegin(); it != m.files.extraFiles.constEnd(); ++it) {
        if (!it.key().isEmpty()
            && it.key() != QLatin1String("model")
            && it.key() != QLatin1String("encoder")
            && it.key() != QLatin1String("decoder")
            && !it.value().isEmpty()) {
            files[it.key()] = it.value();
        }
    }
    root[QStringLiteral("files")] = files;

    if (!m.input.isEmpty())
        root[QStringLiteral("input")] = QJsonObject::fromVariantMap(m.input);

    QJsonArray caps;
    for (const QString& c : m.capabilities)
        caps.append(c);
    root[QStringLiteral("capabilities")] = caps;

    if (!m.licenseName.isEmpty() || !m.licenseFile.isEmpty() || !m.noticeFile.isEmpty()) {
        QJsonObject lic;
        lic[QStringLiteral("name")] = m.licenseName;
        if (!m.licenseFile.isEmpty()) lic[QStringLiteral("file")]   = m.licenseFile;
        if (!m.noticeFile.isEmpty())  lic[QStringLiteral("notice")] = m.noticeFile;
        root[QStringLiteral("license")] = lic;
    }
    if (!m.sourceName.isEmpty() || !m.sourceUrl.isEmpty()) {
        QJsonObject src;
        if (!m.sourceName.isEmpty()) src[QStringLiteral("name")] = m.sourceName;
        if (!m.sourceUrl.isEmpty())  src[QStringLiteral("url")]  = m.sourceUrl;
        root[QStringLiteral("source")] = src;
    }

    QDir().mkpath(dir);
    QFile f(QDir(dir).filePath(QString::fromLatin1(kMetadataFile)));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Cannot write model.json in %1").arg(dir);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return true;
}

} // namespace AiModelJson
