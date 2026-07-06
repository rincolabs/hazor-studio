#include "AggregatorImageGenProvider.hpp"

#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QBuffer>
#include <QTimer>
#include <QUrl>

namespace {

QString dataUri(const QImage& img)
{
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(ba.toBase64());
}

constexpr int kMaxPolls = 120;   // ~120 * 1.5s = 3 min ceiling

} // namespace

AggregatorImageGenProvider::AggregatorImageGenProvider(const QString& baseUrl,
                                                       const QString& apiKey,
                                                       const QString& modelVersion,
                                                       QObject* parent)
    : ImageGenProvider(parent)
    , m_net(new QNetworkAccessManager(this))
    , m_baseUrl(baseUrl.isEmpty() ? QStringLiteral("https://api.replicate.com") : baseUrl)
    , m_apiKey(apiKey)
    , m_version(modelVersion)
{
    while (m_baseUrl.endsWith('/')) m_baseUrl.chop(1);
}

AggregatorImageGenProvider::~AggregatorImageGenProvider()
{
    cancel();
}

void AggregatorImageGenProvider::fail(InpaintError::Code code, const QString& msg)
{
    emit failed({ code, msg });
}

void AggregatorImageGenProvider::requestInpaint(const InpaintRequest& request)
{
    if (m_reply) {
        emit failed({ InpaintError::Unknown, QObject::tr("A request is already in progress.") });
        return;
    }
    if (m_apiKey.isEmpty()) {
        emit failed({ InpaintError::Auth, QObject::tr("Aggregator API token is required.") });
        return;
    }
    if (m_version.isEmpty()) {
        emit failed({ InpaintError::Unsupported,
                      QObject::tr("Set the model version (Model field) for the aggregator.") });
        return;
    }

    m_outSize = request.outSize.isValid() ? request.outSize : request.image.size();
    m_polls = 0;
    m_getUrl.clear();

    QImage rgba = request.image.convertToFormat(QImage::Format_RGBA8888);
    QImage mask = request.mask.convertToFormat(QImage::Format_Grayscale8);

    QJsonObject input;
    input["prompt"] = request.prompt;
    if (!request.negativePrompt.isEmpty())
        input["negative_prompt"] = request.negativePrompt;
    input["image"] = dataUri(rgba);
    input["mask"] = dataUri(mask);
    if (request.seed >= 0)
        input["seed"] = request.seed;

    QJsonObject body;
    body["version"] = m_version;
    body["input"] = input;

    QNetworkRequest req(QUrl(m_baseUrl + "/v1/predictions"));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setRawHeader("Authorization", QString("Token %1").arg(m_apiKey).toUtf8());

    emit progress(0, QObject::tr("Creating prediction..."));

    m_reply = m_net->post(req, QJsonDocument(body).toJson());
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        onCreateReply(m_reply);
    });
}

void AggregatorImageGenProvider::cancel()
{
    if (m_reply) {
        QNetworkReply* r = m_reply;
        m_reply = nullptr;
        r->abort();
        r->deleteLater();
    }
}

void AggregatorImageGenProvider::onCreateReply(QNetworkReply* reply)
{
    if (reply != m_reply) { reply->deleteLater(); return; }
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        InpaintError::Code c = reply->error() == QNetworkReply::OperationCanceledError
            ? InpaintError::Canceled : InpaintError::Network;
        QString m = reply->errorString();
        reply->deleteLater();
        fail(c, m);
        return;
    }

    QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();

    m_getUrl = obj.value("urls").toObject().value("get").toString();
    if (m_getUrl.isEmpty()) {
        fail(InpaintError::BadResponse, QObject::tr("Aggregator did not return a poll URL."));
        return;
    }
    QTimer::singleShot(1500, this, [this]() { pollStatus(); });
}

void AggregatorImageGenProvider::pollStatus()
{
    if (++m_polls > kMaxPolls) {
        fail(InpaintError::Network, QObject::tr("Aggregator timed out."));
        return;
    }
    QNetworkRequest req((QUrl(m_getUrl)));
    req.setRawHeader("Authorization", QString("Token %1").arg(m_apiKey).toUtf8());
    emit progress(qMin(95, m_polls * 5), QObject::tr("Generating..."));

    m_reply = m_net->get(req);
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        onPollReply(m_reply);
    });
}

void AggregatorImageGenProvider::onPollReply(QNetworkReply* reply)
{
    if (reply != m_reply) { reply->deleteLater(); return; }
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        InpaintError::Code c = reply->error() == QNetworkReply::OperationCanceledError
            ? InpaintError::Canceled : InpaintError::Network;
        QString m = reply->errorString();
        reply->deleteLater();
        fail(c, m);
        return;
    }

    QJsonObject obj = QJsonDocument::fromJson(reply->readAll()).object();
    reply->deleteLater();

    QString status = obj.value("status").toString();
    if (status == "succeeded") {
        QJsonValue out = obj.value("output");
        QString url = out.isArray() ? (out.toArray().isEmpty() ? QString()
                                       : out.toArray().first().toString())
                                    : out.toString();
        if (url.isEmpty()) {
            fail(InpaintError::BadResponse, QObject::tr("Aggregator produced no output."));
            return;
        }
        downloadOutput(url);
    } else if (status == "failed" || status == "canceled") {
        fail(InpaintError::BadResponse,
             obj.value("error").toString(QObject::tr("Aggregator prediction failed.")));
    } else {
        QTimer::singleShot(1500, this, [this]() { pollStatus(); });
    }
}

void AggregatorImageGenProvider::downloadOutput(const QString& url)
{
    m_reply = m_net->get(QNetworkRequest(QUrl(url)));
    connect(m_reply, &QNetworkReply::finished, this, [this]() {
        onDownloadReply(m_reply);
    });
}

void AggregatorImageGenProvider::onDownloadReply(QNetworkReply* reply)
{
    if (reply != m_reply) { reply->deleteLater(); return; }
    m_reply = nullptr;

    if (reply->error() != QNetworkReply::NoError) {
        QString m = reply->errorString();
        reply->deleteLater();
        fail(InpaintError::Network, m);
        return;
    }

    QByteArray data = reply->readAll();
    reply->deleteLater();

    QImage out;
    out.loadFromData(data);
    if (out.isNull()) {
        fail(InpaintError::BadResponse, QObject::tr("Could not decode the aggregator image."));
        return;
    }

    InpaintResult r;
    r.image = out.convertToFormat(QImage::Format_RGBA8888);
    if (r.image.size() != m_outSize && m_outSize.isValid())
        r.image = r.image.scaled(m_outSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    emit progress(100, QObject::tr("Done."));
    emit finished(r);
}
