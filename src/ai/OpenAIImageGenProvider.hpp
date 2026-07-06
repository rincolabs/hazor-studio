#pragma once

#include "ImageGenProvider.hpp"

class QNetworkAccessManager;
class QNetworkReply;

// OpenAI Images edits (gpt-image-1): POST {base}/images/edits, multipart with
// image + mask + prompt. Native mask. Cloud — requires opt-in.
class OpenAIImageGenProvider : public ImageGenProvider {
    Q_OBJECT

public:
    OpenAIImageGenProvider(const QString& baseUrl, const QString& apiKey,
                           const QString& model, QObject* parent = nullptr);
    ~OpenAIImageGenProvider() override;

    void requestInpaint(const InpaintRequest& request) override;
    void cancel() override;
    QString name() const override { return QStringLiteral("OpenAI Images"); }
    bool supportsNativeMask() const override { return true; }
    bool isCloud() const override { return true; }

private:
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QString m_baseUrl;
    QString m_apiKey;
    QString m_model;
    QSize   m_outSize;
};
