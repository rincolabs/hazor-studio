#pragma once

#include "ImageGenProvider.hpp"

class QNetworkAccessManager;
class QNetworkReply;

// Stable Diffusion local backend speaking the AUTOMATIC1111 web API
// (SD.Next / Forge are compatible). The server runs separately, like Ollama —
// default http://localhost:7860, started with --api. Nothing leaves the
// machine, so this provider does not require the cloud opt-in.
class A1111ImageGenProvider : public ImageGenProvider {
    Q_OBJECT

public:
    A1111ImageGenProvider(const QString& baseUrl, const QString& model,
                          QObject* parent = nullptr);
    ~A1111ImageGenProvider() override;

    void requestInpaint(const InpaintRequest& request) override;
    void cancel() override;
    QString name() const override { return QStringLiteral("Stable Diffusion (A1111)"); }
    bool supportsNativeMask() const override { return true; }
    bool isCloud() const override { return false; }

private:
    void handleReply(QNetworkReply* reply);

    QNetworkAccessManager* m_net = nullptr;
    QNetworkReply* m_reply = nullptr;
    QString m_baseUrl;
    QString m_model;
    QSize   m_outSize;
};
