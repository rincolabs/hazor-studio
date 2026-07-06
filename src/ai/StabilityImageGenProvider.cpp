#include "StabilityImageGenProvider.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QBuffer>
#include <QUrl>

namespace {

QByteArray encodePng(const QImage& img)
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return ba;
}

QHttpPart filePart(const QString& field, const QByteArray& png)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentTypeHeader, QVariant("image/png"));
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QString("form-data; name=\"%1\"; filename=\"%1.png\"").arg(field)));
    part.setBody(png);
    return part;
}

QHttpPart textPart(const QString& field, const QString& value)
{
    QHttpPart part;
    part.setHeader(QNetworkRequest::ContentDispositionHeader,
        QVariant(QString("form-data; name=\"%1\"").arg(field)));
    part.setBody(value.toUtf8());
    return part;
}

} // namespace

StabilityImageGenProvider::StabilityImageGenProvider(const QString& baseUrl,
                                                     const QString& apiKey,
                                                     QObject* parent)
    : ImageGenProvider(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl.isEmpty() ? QStringLiteral("https://api.stability.ai") : baseUrl)
    , m_apiKey(apiKey)
{
    while (m_baseUrl.endsWith('/')) m_baseUrl.chop(1);
}

StabilityImageGenProvider::~StabilityImageGenProvider()
{
    cancel();
}

void StabilityImageGenProvider::requestInpaint(const InpaintRequest& request)
{
    if (m_reply) {
        emit failed({ InpaintError::Unknown, QObject::tr("A request is already in progress.") });
        return;
    }
    if (m_apiKey.isEmpty()) {
        emit failed({ InpaintError::Auth, QObject::tr("Stability API key is required.") });
        return;
    }

    m_outSize = request.outSize.isValid() ? request.outSize : request.image.size();

    QImage rgba = request.image.convertToFormat(QImage::Format_RGBA8888);
    // Stability mask: white = inpaint — matches our convention directly.
    QImage mask = request.mask.convertToFormat(QImage::Format_Grayscale8);

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multi->append(filePart("image", encodePng(rgba)));
    multi->append(filePart("mask", encodePng(mask)));
    multi->append(textPart("prompt", request.prompt));
    if (!request.negativePrompt.isEmpty())
        multi->append(textPart("negative_prompt", request.negativePrompt));
    if (request.seed >= 0)
        multi->append(textPart("seed", QString::number(request.seed)));
    multi->append(textPart("output_format", "png"));

    QNetworkRequest req(QUrl(m_baseUrl + "/v2beta/stable-image/edit/inpaint"));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());
    req.setRawHeader("Accept", "image/*");

    emit progress(0, QObject::tr("Sending to Stability AI..."));

    m_reply = m_net->post(req, multi);
    multi->setParent(m_reply);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        handleReply(m_reply);
    });
}

void StabilityImageGenProvider::cancel()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
}

void StabilityImageGenProvider::handleReply(QNetworkReply* reply)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return;
    }
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        InpaintError e;
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            e.code = InpaintError::Canceled;
            e.message = QObject::tr("Canceled.");
        } else if (reply->error() == QNetworkReply::AuthenticationRequiredError) {
            e.code = InpaintError::Auth;
            e.message = QObject::tr("Stability rejected the API key.");
        } else {
            e.code = InpaintError::Network;
            e.message = reply->errorString();
        }
        reply->deleteLater();
        emit failed(e);
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QImage out;
    out.loadFromData(data);     // raw PNG bytes (Accept: image/*)
    if (out.isNull()) {
        emit failed({ InpaintError::BadResponse, QObject::tr("Could not decode the Stability image.") });
        return;
    }

    InpaintResult r;
    r.image = out.convertToFormat(QImage::Format_RGBA8888);
    if (r.image.size() != m_outSize && m_outSize.isValid())
        r.image = r.image.scaled(m_outSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    emit progress(100, QObject::tr("Done."));
    emit finished(r);
}
