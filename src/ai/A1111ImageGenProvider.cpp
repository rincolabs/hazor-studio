#include "A1111ImageGenProvider.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QUrl>
#include <QDebug>
#include <QDir>
#include <QFileInfo>

namespace {

QString pngBase64(const QImage& img)
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QString::fromLatin1(ba.toBase64());
}

QImage decodePngBase64(const QString& b64)
{
    QByteArray raw = QByteArray::fromBase64(b64.toLatin1());
    QImage img;
    img.loadFromData(raw, "PNG");
    return img;
}

// Writes the sent/received images to <cwd>/tmp for inspection (debug aid).
void dumpDebug(const QString& name, const QImage& img)
{
    QDir().mkpath(QStringLiteral("tmp"));
    const QString path = QStringLiteral("tmp/genfill_%1.png").arg(name);
    const bool ok = img.save(path, "PNG");
    qInfo().noquote() << "[GenFill] dump" << QFileInfo(path).absoluteFilePath()
                      << (ok ? "ok" : "FAILED") << img.size();
}

} // namespace

A1111ImageGenProvider::A1111ImageGenProvider(const QString& baseUrl,
                                             const QString& model,
                                             QObject* parent)
    : ImageGenProvider(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl)
    , m_model(model)
{
    while (m_baseUrl.endsWith('/')) m_baseUrl.chop(1);
}

A1111ImageGenProvider::~A1111ImageGenProvider()
{
    cancel();
}

void A1111ImageGenProvider::requestInpaint(const InpaintRequest& request)
{
    if (m_reply) {
        InpaintError e{ InpaintError::Unknown, QObject::tr("A request is already in progress.") };
        emit failed(e);
        return;
    }

    m_outSize = request.outSize.isValid() ? request.outSize : request.image.size();

    QImage rgba = request.image.convertToFormat(QImage::Format_RGBA8888);
    QImage mask = request.mask.convertToFormat(QImage::Format_Grayscale8);

    dumpDebug("sent_init", rgba);
    dumpDebug("sent_mask", mask);

    QJsonObject body;
    body["init_images"] = QJsonArray{ pngBase64(rgba) };
    body["mask"] = pngBase64(mask);
    body["inpainting_fill"] = 1;           // 1 = original
    body["inpaint_full_res"] = false;
    body["mask_blur"] = 4;
    body["denoising_strength"] = request.strength;
    body["prompt"] = request.prompt;
    body["negative_prompt"] = request.negativePrompt;
    body["steps"] = request.steps;
    body["seed"] = request.seed;
    body["width"] = m_outSize.width();
    body["height"] = m_outSize.height();
    if (!m_model.isEmpty()) {
        QJsonObject override;
        override["sd_model_checkpoint"] = m_model;
        body["override_settings"] = override;
    }

    const QString endpoint = m_baseUrl + "/sdapi/v1/img2img";
    QNetworkRequest req((QUrl(endpoint)));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    qInfo().noquote() << "[GenFill] A1111 POST" << endpoint
                      << "| model=" << m_model
                      << "| size=" << m_outSize
                      << "| steps=" << request.steps
                      << "| strength=" << request.strength
                      << "| seed=" << request.seed
                      << "| prompt=" << request.prompt
                      << "| negative=" << request.negativePrompt;

    emit progress(0, QObject::tr("Sending to Stable Diffusion..."));

    m_reply = m_net->post(req, QJsonDocument(body).toJson());
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        handleReply(m_reply);
    });
}

void A1111ImageGenProvider::cancel()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
}

void A1111ImageGenProvider::handleReply(QNetworkReply* reply)
{
    if (reply != m_reply) {        // a stale/aborted reply
        reply->deleteLater();
        return;
    }
    m_reply = nullptr;

    const int httpStatus =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (reply->error() != QNetworkReply::NoError) {
        // Surface the server's body — A1111 returns JSON {detail/error} on 4xx/5xx.
        const QByteArray body = reply->readAll();
        qWarning().noquote() << "[GenFill] A1111 ERROR http=" << httpStatus
                             << "| qtError=" << reply->errorString()
                             << "| body=" << QString::fromUtf8(body.left(2000));
        InpaintError e;
        const auto err = reply->error();
        if (err == QNetworkReply::OperationCanceledError) {
            e.code = InpaintError::Canceled;
            e.message = QObject::tr("Canceled.");
        } else if (err == QNetworkReply::ConnectionRefusedError ||
                   err == QNetworkReply::HostNotFoundError ||
                   err == QNetworkReply::TimeoutError) {
            e.code = InpaintError::Network;
            e.message = QObject::tr("Could not reach the Stable Diffusion server at %1. "
                                    "Start it with --api (e.g. on :7860).").arg(m_baseUrl);
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

    QJsonDocument doc = QJsonDocument::fromJson(data);
    QJsonObject root = doc.object();
    auto images = root.value("images").toArray();
    qInfo().noquote() << "[GenFill] A1111 reply http=" << httpStatus
                      << "| bytes=" << data.size()
                      << "| images=" << images.size()
                      << "| info=" << root.value("info").toString().left(300);
    if (images.isEmpty()) {
        qWarning().noquote() << "[GenFill] A1111 no images. body head="
                             << QString::fromUtf8(data.left(1000));
        InpaintError e{ InpaintError::BadResponse,
                        QObject::tr("Stable Diffusion returned no image.") };
        emit failed(e);
        return;
    }

    QImage out = decodePngBase64(images.first().toString());
    if (out.isNull()) {
        qWarning() << "[GenFill] A1111 could not decode image (base64 len="
                   << images.first().toString().size() << ")";
        InpaintError e{ InpaintError::BadResponse,
                        QObject::tr("Could not decode the generated image.") };
        emit failed(e);
        return;
    }

    InpaintResult r;
    r.image = out.convertToFormat(QImage::Format_RGBA8888);
    dumpDebug("received", r.image);
    qInfo().noquote() << "[GenFill] A1111 decoded image" << out.size()
                      << "-> finished";
    emit progress(100, QObject::tr("Done."));
    emit finished(r);
}
