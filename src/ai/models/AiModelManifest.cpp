#include "AiModelManifest.hpp"
#include "ai/models/AiModelDescriptor.hpp"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDebug>

namespace {

AiModelFile parseFile(const QJsonObject& o, QString* problem)
{
    AiModelFile f;
    f.type      = o.value(QStringLiteral("type")).toString();
    f.filename  = o.value(QStringLiteral("filename")).toString();
    f.url       = o.value(QStringLiteral("url")).toString();
    f.sha256    = o.value(QStringLiteral("sha256")).toString().trimmed().toLower();
    f.sizeBytes = static_cast<qint64>(o.value(QStringLiteral("sizeBytes")).toDouble());

    if (f.filename.isEmpty())
        *problem = QStringLiteral("file entry missing 'filename'");
    // A "..." placeholder sha is treated as "no checksum yet", not an error.
    if (f.sha256.size() != 64)
        f.sha256.clear();
    return f;
}

AiManifestEntry parseModel(const QJsonObject& o, QString* problem)
{
    AiManifestEntry m;
    m.id          = o.value(QStringLiteral("id")).toString();
    m.family      = o.value(QStringLiteral("family")).toString();
    m.role        = o.value(QStringLiteral("role")).toString();
    m.backend     = o.value(QStringLiteral("backend")).toString().trimmed();
    m.name        = o.value(QStringLiteral("name")).toString();
    m.quality     = o.value(QStringLiteral("quality")).toString();
    m.description = o.value(QStringLiteral("description")).toString();
    m.license     = o.value(QStringLiteral("license")).toString();
    m.homepage    = o.value(QStringLiteral("homepage")).toString();
    m.recommended = o.value(QStringLiteral("recommended")).toBool(false);

    const QJsonObject req = o.value(QStringLiteral("requires")).toObject();
    m.minRamMB  = req.value(QStringLiteral("minRamMB")).toInt(0);
    m.minVramMB = req.value(QStringLiteral("minVramMB")).toInt(0);

    // Optional richer metadata so an installed model.json can be generated in
    // full. Derived from role/family/task when the manifest omits them.
    m.task = o.value(QStringLiteral("task")).toString().trimmed().toLower();
    if (m.task.isEmpty())
        m.task = AiModelTaxonomy::roleToTask(m.role);
    m.type = o.value(QStringLiteral("type")).toString().trimmed().toLower();
    if (m.type.isEmpty())
        m.type = AiModelTaxonomy::familyToType(m.family);
    for (const QJsonValue& c : o.value(QStringLiteral("capabilities")).toArray())
        m.capabilities << c.toString();
    if (m.capabilities.isEmpty())
        m.capabilities = AiModelTaxonomy::defaultCapabilitiesForTask(m.task);
    m.input = o.value(QStringLiteral("input")).toObject().toVariantMap();
    const QJsonObject src = o.value(QStringLiteral("source")).toObject();
    m.sourceName = src.value(QStringLiteral("name")).toString();
    m.sourceUrl  = src.value(QStringLiteral("url")).toString();
    if (m.sourceUrl.isEmpty())
        m.sourceUrl = m.homepage;

    if (m.id.isEmpty()) {
        *problem = QStringLiteral("model entry missing 'id'");
        return m;
    }
    const QJsonArray files = o.value(QStringLiteral("files")).toArray();
    for (const QJsonValue& fv : files) {
        QString fileProblem;
        AiModelFile f = parseFile(fv.toObject(), &fileProblem);
        if (!fileProblem.isEmpty()) {
            *problem = QStringLiteral("%1: %2").arg(m.id, fileProblem);
            continue; // skip the broken file, keep the model
        }
        m.files.append(f);
    }
    if (m.files.isEmpty())
        *problem = QStringLiteral("%1: no valid files").arg(m.id);
    return m;
}

} // namespace

AiModelManifest AiModelManifest::loadBundled()
{
    QFile f(QStringLiteral(":/models/ai-models-manifest.json"));
    if (!f.open(QIODevice::ReadOnly)) {
        AiModelManifest m;
        m.m_error = QStringLiteral("Bundled model manifest not found.");
        qWarning().noquote() << "[AI]" << m.m_error;
        return m;
    }
    return fromJson(f.readAll());
}

AiModelManifest AiModelManifest::fromJson(const QByteArray& json)
{
    AiModelManifest m;
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (doc.isNull() || !doc.isObject()) {
        m.m_error = QStringLiteral("Manifest JSON parse error: %1").arg(err.errorString());
        qWarning().noquote() << "[AI]" << m.m_error;
        return m;
    }

    const QJsonObject root = doc.object();
    m.m_version = root.value(QStringLiteral("version")).toInt(0);
    if (m.m_version != kSupportedVersion) {
        m.m_error = QStringLiteral("Unsupported manifest version %1 (expected %2).")
                        .arg(m.m_version).arg(kSupportedVersion);
        qWarning().noquote() << "[AI]" << m.m_error;
        return m;
    }

    const QJsonArray models = root.value(QStringLiteral("models")).toArray();
    for (const QJsonValue& mv : models) {
        if (!mv.isObject()) {
            m.m_warnings << QStringLiteral("Ignored a non-object model entry.");
            continue;
        }
        QString problem;
        AiManifestEntry desc = parseModel(mv.toObject(), &problem);
        if (!problem.isEmpty())
            m.m_warnings << problem;
        if (desc.isValid())
            m.m_models.append(desc);
    }

    m.m_valid = true;
    if (!m.m_warnings.isEmpty())
        qWarning().noquote() << "[AI] Manifest loaded with warnings:" << m.m_warnings.join(QStringLiteral("; "));
    return m;
}

const AiManifestEntry* AiModelManifest::model(const QString& id) const
{
    for (const auto& m : m_models)
        if (m.id == id) return &m;
    return nullptr;
}

QList<AiManifestEntry> AiModelManifest::modelsOfFamily(const QString& family) const
{
    QList<AiManifestEntry> out;
    for (const auto& m : m_models)
        if (m.family == family) out << m;
    return out;
}
