#include "OpenAIImageGenProvider.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
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

// OpenAI edits: transparent pixels of the mask mark where to repaint.
// Our grayscale mask uses white = repaint, so white -> alpha 0.
QImage toOpenAIMask(const QImage& image, const QImage& mask)
{
    QImage g = mask.convertToFormat(QImage::Format_Grayscale8);
    QImage out(image.size(), QImage::Format_RGBA8888);
    for (int y = 0; y < out.height(); ++y) {
        const uchar* m = (y < g.height()) ? g.constScanLine(y) : nullptr;
        QRgb* o = reinterpret_cast<QRgb*>(out.scanLine(y));
        for (int x = 0; x < out.width(); ++x) {
            int v = (m && x < g.width()) ? m[x] : 0;
            int alpha = 255 - v;     // white(repaint) -> 0 (transparent/edit)
            o[x] = qRgba(0, 0, 0, alpha);
        }
    }
    return out;
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

OpenAIImageGenProvider::OpenAIImageGenProvider(const QString& baseUrl,
                                               const QString& apiKey,
                                               const QString& model,
                                               QObject* parent)
    : ImageGenProvider(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl)
    , m_apiKey(apiKey)
    , m_model(model.isEmpty() ? QStringLiteral("gpt-image-1") : model)
{
    while (m_baseUrl.endsWith('/')) m_baseUrl.chop(1);
}

OpenAIImageGenProvider::~OpenAIImageGenProvider()
{
    cancel();
}

void OpenAIImageGenProvider::requestInpaint(const InpaintRequest& request)
{
    if (m_reply) {
        emit failed({ InpaintError::Unknown, QObject::tr("A request is already in progress.") });
        return;
    }
    if (m_apiKey.isEmpty()) {
        emit failed({ InpaintError::Auth, QObject::tr("OpenAI API key is required.") });
        return;
    }

    m_outSize = request.outSize.isValid() ? request.outSize : request.image.size();

    QImage rgba = request.image.convertToFormat(QImage::Format_RGBA8888);
    QImage mask = toOpenAIMask(rgba, request.mask);

    auto* multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    multi->append(filePart("image", encodePng(rgba)));
    multi->append(filePart("mask", encodePng(mask)));
    multi->append(textPart("prompt", request.prompt));
    multi->append(textPart("model", m_model));
    multi->append(textPart("n", "1"));

    QNetworkRequest req(QUrl(m_baseUrl + "/images/edits"));
    req.setRawHeader("Authorization", QString("Bearer %1").arg(m_apiKey).toUtf8());

    emit progress(0, QObject::tr("Sending to OpenAI..."));

    m_reply = m_net->post(req, multi);
    multi->setParent(m_reply);    // freed with the reply
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        handleReply(m_reply);
    });
}

void OpenAIImageGenProvider::cancel()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
}

void OpenAIImageGenProvider::handleReply(QNetworkReply* reply)
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
            e.message = QObject::tr("OpenAI rejected the API key.");
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

    auto arr = QJsonDocument::fromJson(data).object().value("data").toArray();
    if (arr.isEmpty()) {
        emit failed({ InpaintError::BadResponse, QObject::tr("OpenAI returned no image.") });
        return;
    }
    QString b64 = arr.first().toObject().value("b64_json").toString();
    QImage out;
    out.loadFromData(QByteArray::fromBase64(b64.toLatin1()), "PNG");
    if (out.isNull()) {
        emit failed({ InpaintError::BadResponse, QObject::tr("Could not decode the OpenAI image.") });
        return;
    }

    InpaintResult r;
    r.image = out.convertToFormat(QImage::Format_RGBA8888);
    if (r.image.size() != m_outSize && m_outSize.isValid())
        r.image = r.image.scaled(m_outSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    emit progress(100, QObject::tr("Done."));
    emit finished(r);
}
