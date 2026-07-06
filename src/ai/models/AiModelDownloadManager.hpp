#pragma once

#include "ai/models/AiModelTypes.hpp"

#include <QObject>
#include <QString>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QFile;
class QCryptographicHash;

// Downloads model files declared in the manifest, validates them and installs
// them atomically. Never marks a partially-downloaded or corrupt file as
// installed, and never downloads anything without an explicit user action.
//
// Flow per model:
//   download → staging dir → validate size → validate sha256 →
//   atomically move staged folder into place → write metadata → mark installed
//
// One model is downloaded at a time (files within a model are fetched
// sequentially). On the model registry the download manager is the write side.
class AiModelDownloadManager : public QObject {
    Q_OBJECT
public:
    static AiModelDownloadManager* instance();
    ~AiModelDownloadManager() override;

    bool isBusy() const;
    bool isDownloading(const QString& modelId) const;
    QString currentModelId() const;

    // Starts downloading every file of the model into the registry's
    // downloaded/ tree. No-op (emits finished with an error) if another download
    // is already running.
    void download(const AiManifestEntry& model);
    void cancel(const QString& modelId);

signals:
    void started(QString modelId);
    // Aggregated across all files of the model. total < 0 means indeterminate.
    void progress(QString modelId, qint64 received, qint64 total);
    void statusText(QString modelId, QString text);
    void finished(QString modelId, bool success, QString error);

private:
    explicit AiModelDownloadManager(QObject* parent = nullptr);

    struct Job;

    void startNextFile();
    void onReadyRead();
    void onReplyFinished();
    void onDownloadProgress(qint64 received, qint64 total);
    bool finalizeInstall(QString* error);
    void failJob(const QString& error);
    void cleanupStaging();
    void teardownReply();

    QNetworkAccessManager* m_nam = nullptr;
    std::unique_ptr<Job> m_job;
};
