#include "GeminiImageGenProvider.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QUrl>
#include <QDebug>

namespace {

QString pngBase64(const QImage& img)
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(ba.toBase64());
}

} // namespace

GeminiImageGenProvider::GeminiImageGenProvider(const QString& baseUrl,
                                               const QString& apiKey,
                                               const QString& model,
                                               QObject* parent)
    : ImageGenProvider(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl.isEmpty()
                ? QStringLiteral("https://generativelanguage.googleapis.com") : baseUrl)
    , m_apiKey(apiKey)
    , m_model(model.isEmpty() ? QStringLiteral("gemini-2.5-flash-image") : model)
{
    while (m_baseUrl.endsWith('/')) m_baseUrl.chop(1);
}

GeminiImageGenProvider::~GeminiImageGenProvider()
{
    cancel();
}

void GeminiImageGenProvider::requestInpaint(const InpaintRequest& request)
{
    if (m_reply) {
        emit failed({ InpaintError::Unknown, QObject::tr("A request is already in progress.") });
        return;
    }
    if (m_apiKey.isEmpty()) {
        emit failed({ InpaintError::Auth, QObject::tr("Google API key is required.") });
        return;
    }

    m_outSize = request.outSize.isValid() ? request.outSize : request.image.size();
    QImage rgba = request.image.convertToFormat(QImage::Format_RGBA8888);

    QJsonObject inlineData;
    inlineData["mime_type"] = "image/png";
    inlineData["data"] = pngBase64(rgba);
    QJsonObject imgPart;  imgPart["inline_data"] = inlineData;
    QJsonObject textPart; textPart["text"] = request.prompt;

    QJsonObject content;
    content["role"] = "user";
    content["parts"] = QJsonArray{ textPart, imgPart };

    QJsonObject body;
    body["contents"] = QJsonArray{ content };

    QString ep = m_baseUrl + "/v1beta/models/" + m_model + ":generateContent?key=" + m_apiKey;
    QNetworkRequest req((QUrl(ep)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qInfo().noquote() << "[GenFill] Gemini POST model=" << m_model
                      << "| size=" << m_outSize
                      << "| prompt=" << request.prompt;

    emit progress(0, QObject::tr("Sending to Gemini..."));

    m_reply = m_net->post(req, QJsonDocument(body).toJson());
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        handleReply(m_reply);
    });
}

void GeminiImageGenProvider::cancel()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
}

void GeminiImageGenProvider::handleReply(QNetworkReply* reply)
{
    if (reply != m_reply) {
        reply->deleteLater();
        return;
    }
    m_reply = nullptr;

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        // Google returns { "error": { "code", "status", "message" } } in the body.
        const QByteArray body = reply->readAll();
        QString apiMsg = QJsonDocument::fromJson(body).object()
                             .value("error").toObject().value("message").toString();
        qWarning().noquote() << "[GenFill] Gemini ERROR http=" << httpStatus
                             << "| model=" << m_model
                             << "| qtError=" << reply->errorString()
                             << "| apiMessage=" << apiMsg
                             << "| body=" << QString::fromUtf8(body.left(2000));
        InpaintError e;
        if (reply->error() == QNetworkReply::OperationCanceledError) {
            e.code = InpaintError::Canceled;
            e.message = QObject::tr("Canceled.");
        } else if (reply->error() == QNetworkReply::AuthenticationRequiredError ||
                   httpStatus == 401 || httpStatus == 403) {
            e.code = InpaintError::Auth;
            e.message = apiMsg.isEmpty() ? reply->errorString() : apiMsg;
        } else {
            e.code = InpaintError::Network;
            e.message = apiMsg.isEmpty() ? reply->errorString() : apiMsg;
        }
        reply->deleteLater();
        emit failed(e);
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    // candidates[0].content.parts[] — find the part carrying inline image data.
    auto cands = QJsonDocument::fromJson(data).object().value("candidates").toArray();
    QString b64;
    if (!cands.isEmpty()) {
        auto parts = cands.first().toObject().value("content").toObject()
                          .value("parts").toArray();
        for (const auto& p : parts) {
            QJsonObject po = p.toObject();
            QJsonObject inl = po.contains("inlineData")
                ? po.value("inlineData").toObject()
                : po.value("inline_data").toObject();
            if (inl.contains("data")) { b64 = inl.value("data").toString(); break; }
        }
    }
    if (b64.isEmpty()) {
        emit failed({ InpaintError::BadResponse, QObject::tr("Gemini returned no image.") });
        return;
    }

    QImage out;
    out.loadFromData(QByteArray::fromBase64(b64.toLatin1()));
    if (out.isNull()) {
        emit failed({ InpaintError::BadResponse, QObject::tr("Could not decode the Gemini image.") });
        return;
    }

    InpaintResult r;
    r.image = out.convertToFormat(QImage::Format_RGBA8888);
    if (r.image.size() != m_outSize && m_outSize.isValid())
        r.image = r.image.scaled(m_outSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    emit progress(100, QObject::tr("Done."));
    emit finished(r);
}
