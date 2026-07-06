#include "AiModelDownloadManager.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/models/AiModelJson.hpp"
#include "ai/models/AiModelDescriptor.hpp"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QDebug>

struct AiModelDownloadManager::Job {
    AiManifestEntry model;
    QString stagingDir;             // <modelsDir>/downloaded/.tmp/<id>
    QString finalDir;               // <modelsDir>/downloaded/onnx/<folder>/<family>/<id>
    int fileIndex = 0;
    qint64 baseBytes = 0;           // bytes from already-completed files
    qint64 totalBytes = -1;         // sum of known sizeBytes, or -1 if any unknown
    QList<AiModelFile> installedFiles;
    bool cancelled = false;

    QNetworkReply* reply = nullptr;
    std::unique_ptr<QFile> outFile;
    std::unique_ptr<QCryptographicHash> hash;
    qint64 currentReceived = 0;
};

AiModelDownloadManager* AiModelDownloadManager::instance()
{
    static AiModelDownloadManager* s_instance = new AiModelDownloadManager();
    return s_instance;
}

AiModelDownloadManager::AiModelDownloadManager(QObject* parent)
    : QObject(parent)
    , m_nam(new QNetworkAccessManager(this))
{
}

AiModelDownloadManager::~AiModelDownloadManager() = default;

bool AiModelDownloadManager::isBusy() const
{
    return m_job != nullptr;
}

bool AiModelDownloadManager::isDownloading(const QString& modelId) const
{
    return m_job && m_job->model.id == modelId;
}

QString AiModelDownloadManager::currentModelId() const
{
    return m_job ? m_job->model.id : QString();
}

void AiModelDownloadManager::download(const AiManifestEntry& model)
{
    if (!model.isValid()) {
        emit finished(model.id, false, tr("Invalid model."));
        return;
    }
    if (m_job) {
        emit finished(model.id, false, tr("Another download is already in progress."));
        return;
    }

    auto* registry = AiModelRegistry::instance();
    auto job = std::make_unique<Job>();
    job->model = model;
    // Downloads always install into downloaded/onnx/<folder>/<family>/<id> — never
    // into bundled/ or custom/.
    job->finalDir = registry->downloadedModelDir(model.task, model.family, model.id);
    job->stagingDir = registry->downloadedStagingDir(model.id);

    // Compute aggregate total when every file advertises a size.
    qint64 total = 0;
    bool allKnown = true;
    for (const AiModelFile& f : model.files) {
        if (f.sizeBytes > 0) total += f.sizeBytes;
        else allKnown = false;
    }
    job->totalBytes = allKnown ? total : -1;

    // Fresh staging area.
    QDir staging(job->stagingDir);
    if (staging.exists())
        staging.removeRecursively();
    if (!QDir().mkpath(job->stagingDir)) {
        emit finished(model.id, false, tr("Cannot create download folder. Check permissions."));
        return;
    }

    m_job = std::move(job);
    qInfo().noquote() << "[AI][DOWNLOAD] Started:" << model.id;
    emit started(model.id);
    emit statusText(model.id, tr("Starting download…"));
    startNextFile();
}

void AiModelDownloadManager::startNextFile()
{
    if (!m_job)
        return;
    if (m_job->cancelled) {
        failJob(tr("Cancelled."));
        return;
    }
    if (m_job->fileIndex >= m_job->model.files.size()) {
        QString err;
        if (finalizeInstall(&err)) {
            const QString id = m_job->model.id;
            qInfo().noquote() << "[AI][DOWNLOAD] Completed:" << id;
            m_job.reset();
            AiModelRegistry::instance()->refreshInstalledModels();
            emit finished(id, true, QString());
        } else {
            failJob(err);
        }
        return;
    }

    const AiModelFile& file = m_job->model.files.at(m_job->fileIndex);
    const QUrl url(file.url);
    if (!url.isValid() || url.scheme().isEmpty()) {
        failJob(tr("Model file '%1' has no valid download URL.").arg(file.filename));
        return;
    }

    const QString partPath = QDir(m_job->stagingDir).filePath(file.filename + QStringLiteral(".part"));
    m_job->outFile = std::make_unique<QFile>(partPath);
    if (!m_job->outFile->open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        failJob(tr("Cannot write to download folder."));
        return;
    }
    m_job->hash = std::make_unique<QCryptographicHash>(QCryptographicHash::Sha256);
    m_job->currentReceived = 0;

    emit statusText(m_job->model.id,
                    tr("Downloading %1 (%2/%3)…")
                        .arg(file.filename)
                        .arg(m_job->fileIndex + 1)
                        .arg(m_job->model.files.size()));

    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("HazorStudio"));
    m_job->reply = m_nam->get(req);
    connect(m_job->reply, &QNetworkReply::readyRead, this, &AiModelDownloadManager::onReadyRead);
    connect(m_job->reply, &QNetworkReply::finished, this, &AiModelDownloadManager::onReplyFinished);
    connect(m_job->reply, &QNetworkReply::downloadProgress, this, &AiModelDownloadManager::onDownloadProgress);
}

void AiModelDownloadManager::onReadyRead()
{
    if (!m_job || !m_job->reply || !m_job->outFile)
        return;
    const QByteArray chunk = m_job->reply->readAll();
    if (chunk.isEmpty())
        return;
    if (m_job->outFile->write(chunk) != chunk.size()) {
        m_job->reply->abort();
        failJob(tr("Write error while downloading (disk full?)."));
        return;
    }
    m_job->hash->addData(chunk);
}

void AiModelDownloadManager::onDownloadProgress(qint64 received, qint64 total)
{
    if (!m_job)
        return;
    m_job->currentReceived = received;

    qint64 aggregate = m_job->baseBytes + received;
    qint64 aggregateTotal = m_job->totalBytes;
    if (aggregateTotal < 0 && total > 0) {
        // Fall back to per-file totals when manifest sizes are unknown.
        aggregateTotal = m_job->baseBytes + total;
    }
    emit progress(m_job->model.id, aggregate, aggregateTotal);
}

void AiModelDownloadManager::onReplyFinished()
{
    if (!m_job || !m_job->reply)
        return;

    QNetworkReply* reply = m_job->reply;
    const AiModelFile file = m_job->model.files.at(m_job->fileIndex);

    if (m_job->cancelled) {
        failJob(tr("Cancelled."));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        failJob(tr("Network error downloading %1: %2").arg(file.filename, reply->errorString()));
        return;
    }

    // Flush and close the part file.
    m_job->outFile->flush();
    m_job->outFile->close();
    const QString partPath = m_job->outFile->fileName();
    const qint64 actualSize = QFileInfo(partPath).size();

    // Validate size.
    if (file.sizeBytes > 0 && actualSize != file.sizeBytes) {
        failJob(tr("%1 is incomplete or corrupted (size mismatch).").arg(file.filename));
        return;
    }
    if (actualSize <= 0) {
        failJob(tr("%1 downloaded empty.").arg(file.filename));
        return;
    }

    // Validate sha256 when the manifest provides one.
    if (file.hasChecksum()) {
        const QString digest = QString::fromLatin1(m_job->hash->result().toHex());
        if (digest != file.sha256) {
            qWarning().noquote() << "[AI][DOWNLOAD] sha256 mismatch for" << file.filename
                                 << "expected" << file.sha256 << "got" << digest;
            failJob(tr("%1 failed checksum verification.").arg(file.filename));
            return;
        }
    }

    // Promote .part → final staged name (still inside the staging dir).
    const QString finalPart = QDir(m_job->stagingDir).filePath(file.filename);
    QFile::remove(finalPart);
    if (!QFile::rename(partPath, finalPart)) {
        failJob(tr("Cannot finalize %1.").arg(file.filename));
        return;
    }

    AiModelFile installed = file;
    installed.sizeBytes = actualSize;
    m_job->installedFiles.append(installed);
    m_job->baseBytes += actualSize;

    qInfo().noquote() << "[AI][DOWNLOAD] File ready:" << file.filename << actualSize << "bytes";

    teardownReply();
    m_job->fileIndex++;
    startNextFile();
}

bool AiModelDownloadManager::finalizeInstall(QString* error)
{
    const AiManifestEntry& entry = m_job->model;

    // Build the schema-v1 model.json descriptor that becomes the source of truth.
    AiModelDescriptor d;
    d.id          = entry.id;
    d.task        = entry.task.isEmpty() ? AiModelTaxonomy::roleToTask(entry.role) : entry.task;
    d.family      = entry.family;
    d.type        = entry.type.isEmpty() ? AiModelTaxonomy::familyToType(entry.family) : entry.type;
    d.backend     = entry.backend.isEmpty()
        ? QStringLiteral("onnxruntime") : entry.backend;
    d.displayName = entry.name;
    d.description = entry.description;
    d.quality     = entry.quality;
    d.capabilities = entry.capabilities.isEmpty()
        ? AiModelTaxonomy::defaultCapabilitiesForTask(d.task) : entry.capabilities;
    d.input       = entry.input;
    d.licenseName = entry.license;
    d.sourceName  = entry.sourceName;
    d.sourceUrl   = entry.sourceUrl;
    for (const AiModelFile& f : m_job->installedFiles) {
        if (f.type == QLatin1String("encoder"))      d.files.encoder = f.filename;
        else if (f.type == QLatin1String("decoder")) d.files.decoder = f.filename;
        else if (f.type == QLatin1String("model") || f.type.isEmpty()) {
            d.files.model = f.filename;
        } else if (!f.filename.isEmpty()) {
            d.files.extraFiles.insert(f.type, f.filename);
        }
    }
    // Pick up LICENSE/NOTICE if they came down with the model.
    if (QFile::exists(QDir(m_job->stagingDir).filePath(QStringLiteral("LICENSE"))))
        d.licenseFile = QStringLiteral("LICENSE");
    if (QFile::exists(QDir(m_job->stagingDir).filePath(QStringLiteral("NOTICE.md"))))
        d.noticeFile = QStringLiteral("NOTICE.md");

    // Write model.json into the staging dir before promoting it, so the move is
    // atomic and never leaves a half-installed folder without metadata.
    QString metaErr;
    if (!AiModelJson::write(m_job->stagingDir, d, &metaErr)) {
        if (error) *error = metaErr;
        return false;
    }

    // Atomically swap the staged folder into the nested destination.
    QDir finalDir(m_job->finalDir);
    if (finalDir.exists())
        finalDir.removeRecursively();
    QDir().mkpath(QFileInfo(m_job->finalDir).absolutePath()); // ensure parents

    if (!QDir().rename(m_job->stagingDir, m_job->finalDir)) {
        if (error) *error = tr("Cannot install model files into place.");
        return false;
    }
    return true;
}

void AiModelDownloadManager::cancel(const QString& modelId)
{
    if (!m_job || m_job->model.id != modelId)
        return;
    m_job->cancelled = true;
    qInfo().noquote() << "[AI][DOWNLOAD] Cancel requested:" << modelId;
    if (m_job->reply)
        m_job->reply->abort();
    else
        failJob(tr("Cancelled."));
}

void AiModelDownloadManager::failJob(const QString& error)
{
    if (!m_job)
        return;
    const QString id = m_job->model.id;
    const bool cancelled = m_job->cancelled;

    teardownReply();
    cleanupStaging();
    m_job.reset();

    qWarning().noquote() << "[AI][DOWNLOAD]" << (cancelled ? "Cancelled:" : "Failed:") << id << error;
    emit finished(id, false, error);
}

void AiModelDownloadManager::teardownReply()
{
    if (!m_job)
        return;
    if (m_job->reply) {
        m_job->reply->disconnect(this);
        m_job->reply->deleteLater();
        m_job->reply = nullptr;
    }
    if (m_job->outFile) {
        if (m_job->outFile->isOpen())
            m_job->outFile->close();
        m_job->outFile.reset();
    }
    m_job->hash.reset();
}

void AiModelDownloadManager::cleanupStaging()
{
    if (!m_job)
        return;
    QDir staging(m_job->stagingDir);
    if (staging.exists())
        staging.removeRecursively();
}
