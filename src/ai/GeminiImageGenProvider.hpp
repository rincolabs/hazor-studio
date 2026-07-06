#pragma once

#include "ImageGenProvider.hpp"

class QNetworkAccessManager;
class QNetworkReply;

// Google Gemini image (e.g. gemini-2.5-flash-image): edits by prompt over an
// inline image, no explicit mask. supportsNativeMask() == false — the caller
// sends only the cropped region and stitches the result through the selection
// mask locally. Cloud — requires opt-in.
class GeminiImageGenProvider : public ImageGenProvider {
    Q_OBJECT

public:
    GeminiImageGenProvider(const QString& baseUrl, const QString& apiKey,
                           const QString& model, QObject* parent = nullptr);
    ~GeminiImageGenProvider() override;

    void requestInpaint(const InpaintRequest& request) override;
    void cancel() override;
    QString name() const override { return QStringLiteral("Google Gemini Image"); }
    bool supportsNativeMask() const override { return false; }
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
