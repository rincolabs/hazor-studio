#pragma once

#include "ImageGenProvider.hpp"

class QNetworkAccessManager;
class QNetworkReply;

// Generic aggregator provider (Replicate-style): POST {base}/v1/predictions to
// create a prediction, then poll until it succeeds and download the output
// image. The model "version" comes from the preset's model field. Gives access
// to many models (FLUX, SDXL, ...) without a subclass per model. Cloud.
class AggregatorImageGenProvider : public ImageGenProvider {
    Q_OBJECT

public:
    AggregatorImageGenProvider(const QString& baseUrl, const QString& apiKey,
                               const QString& modelVersion, QObject* parent = nullptr);
    ~AggregatorImageGenProvider() override;

    void requestInpaint(const InpaintRequest& request) override;
    void cancel() override;
    QString name() const override { return QStringLiteral("Aggregator (Replicate)"); }
    bool supportsNativeMask() const override { return true; }
    bool isCloud() const override { return true; }

private:
    void onCreateReply(QNetworkReply* reply);
    void pollStatus();
    void onPollReply(QNetworkReply* reply);
    void downloadOutput(const QString& url);
    void onDownloadReply(QNetworkReply* reply);
    void fail(InpaintError::Code code, const QString& msg);

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QString m_baseUrl;
    QString m_apiKey;
    QString m_version;
    QString m_getUrl;
    QSize   m_outSize;
    int     m_polls = 0;
};
