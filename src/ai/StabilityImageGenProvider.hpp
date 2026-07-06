#pragma once

#include "ImageGenProvider.hpp"

class QNetworkAccessManager;
class QNetworkReply;

// Stability AI inpaint: POST {base}/v2beta/stable-image/edit/inpaint,
// multipart image + mask + prompt. Native mask. Cloud — requires opt-in.
class StabilityImageGenProvider : public ImageGenProvider {
    Q_OBJECT

public:
    StabilityImageGenProvider(const QString& baseUrl, const QString& apiKey,
                              QObject* parent = nullptr);
    ~StabilityImageGenProvider() override;

    void requestInpaint(const InpaintRequest& request) override;
    void cancel() override;
    QString name() const override { return QStringLiteral("Stability AI"); }
    bool supportsNativeMask() const override { return true; }
    bool isCloud() const override { return true; }

private:
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QString m_baseUrl;
    QString m_apiKey;
    QSize   m_outSize;
};
