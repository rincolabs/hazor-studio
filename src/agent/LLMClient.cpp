#include "LLMClient.hpp"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrl>
#include <QRegularExpression>
#include <QDebug>

LLMClient::LLMClient(QObject* parent)
    : QObject(parent)
    , m_net(new QNetworkAccessManager(this))
{
}

LLMClient::~LLMClient()
{
    m_net->deleteLater();
}

void LLMClient::applyConfig(const AgentConfig& config)
{
    m_config = config;
    m_timeout = qMax(5, config.timeoutMs / 1000);
}

void LLMClient::setEndpoint(const QString& host, int port)
{
    m_config.provider.baseUrl = QString("%1:%2").arg(host).arg(port);
}

void LLMClient::setModel(const QString& model)
{
    m_config.provider.model = model;
}

void LLMClient::setTimeout(int seconds)
{
    m_timeout = seconds;
    m_config.timeoutMs = seconds * 1000;
}

void LLMClient::clearHistory()
{
    m_history = QJsonArray();
}

void LLMClient::addAssistantMessage(const QString& content)
{
    QJsonObject msg;
    msg["role"] = "assistant";
    msg["content"] = content;
    m_history.append(msg);

    int maxTurns = m_config.maxHistoryTurns * 2;
    while (m_history.size() > maxTurns)
        m_history.removeFirst();
}

void LLMClient::addToolResultMessage(const QString& toolResults)
{
    QJsonObject msg;
    msg["role"] = "user";
    msg["content"] = toolResults;
    m_history.append(msg);

    int maxTurns = m_config.maxHistoryTurns * 2;
    while (m_history.size() > maxTurns)
        m_history.removeFirst();
}

void LLMClient::sendPrompt(const QString& systemPrompt, const QString& userMessage,
                            std::function<void(bool, QString)> callback)
{
    QJsonArray messages;
    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userMessage;
    messages.append(userMsg);

    doSend(messages, callback);
}

void LLMClient::sendAsync(const QString& systemPrompt, const QString& userMessage)
{
    sendPrompt(systemPrompt, userMessage, nullptr);
}

void LLMClient::sendWithHistory(const QString& systemPrompt,
                                 const QString& userMessage)
{
    m_busy = true;

    QJsonArray messages;

    QJsonObject sysMsg;
    sysMsg["role"] = "system";
    sysMsg["content"] = systemPrompt;
    messages.append(sysMsg);

    for (const auto& msg : m_history)
        messages.append(msg);

    QJsonObject userMsg;
    userMsg["role"] = "user";
    userMsg["content"] = userMessage;
    messages.append(userMsg);

    int maxTurns = m_config.maxHistoryTurns * 2 + 1;
    while (messages.size() > maxTurns) {
        for (int i = 1; i < messages.size(); ++i) {
            if (messages[i].toObject().value("role").toString() != "system") {
                messages.removeAt(i);
                break;
            }
        }
    }

    doSend(messages, nullptr);
}

void LLMClient::doSend(const QJsonArray& messages,
                        std::function<void(bool, QString)> callback)
{
    m_busy = true;
    Q_UNUSED(callback)

    const ProviderConfig::Type type = m_config.provider.type;

    // Pull the leading "system" message out — Anthropic and Google carry the
    // system prompt in a dedicated field rather than the conversation array.
    QString systemPrompt;
    QJsonArray convo;
    for (const auto& v : messages) {
        QJsonObject m = v.toObject();
        if (m.value("role").toString() == "system") {
            if (systemPrompt.isEmpty())
                systemPrompt = m.value("content").toString();
            else
                systemPrompt += "\n" + m.value("content").toString();
        } else {
            convo.append(m);
        }
    }

    QUrl url(m_config.provider.apiEndpoint(AgentKind::Assistant));
    QJsonObject body;

    if (type == ProviderConfig::Anthropic) {
        // Messages API: x-api-key + anthropic-version, system separate,
        // max_tokens required, roles limited to user/assistant.
        QJsonArray amsgs;
        for (const auto& v : convo) {
            QJsonObject m = v.toObject();
            QString role = m.value("role").toString();
            if (role != "assistant") role = "user";
            QJsonObject am;
            am["role"] = role;
            am["content"] = m.value("content").toString();
            amsgs.append(am);
        }
        body["model"] = m_config.provider.model;
        body["messages"] = amsgs;
        body["max_tokens"] = m_config.modelSettings.maxTokens;
        body["temperature"] = m_config.modelSettings.temperature;
        if (!systemPrompt.isEmpty())
            body["system"] = systemPrompt;
    } else if (type == ProviderConfig::Google) {
        // generateContent: ?key=, contents[] with parts[], roles user/model,
        // optional systemInstruction.
        QString ep = url.toString();
        if (!m_config.provider.apiKey.isEmpty())
            ep += (ep.contains('?') ? "&" : "?") + QString("key=") + m_config.provider.apiKey;
        url = QUrl(ep);

        QJsonArray contents;
        for (const auto& v : convo) {
            QJsonObject m = v.toObject();
            QString role = m.value("role").toString();
            role = (role == "assistant") ? "model" : "user";
            QJsonObject part; part["text"] = m.value("content").toString();
            QJsonObject c;
            c["role"] = role;
            c["parts"] = QJsonArray{ part };
            contents.append(c);
        }
        body["contents"] = contents;
        if (!systemPrompt.isEmpty()) {
            QJsonObject sysPart; sysPart["text"] = systemPrompt;
            QJsonObject sysInstr; sysInstr["parts"] = QJsonArray{ sysPart };
            body["systemInstruction"] = sysInstr;
        }
        QJsonObject gen;
        gen["temperature"] = m_config.modelSettings.temperature;
        gen["maxOutputTokens"] = m_config.modelSettings.maxTokens;
        body["generationConfig"] = gen;
    } else {
        // Local (Ollama) + OpenAI + Custom (OpenAI-compatible)
        body["model"] = m_config.provider.model;
        body["messages"] = messages;
        body["temperature"] = m_config.modelSettings.temperature;
        body["stream"] = m_config.stream;
        if (type == ProviderConfig::OpenAI || type == ProviderConfig::Custom) {
            body["max_tokens"] = m_config.modelSettings.maxTokens;
            body["top_p"] = m_config.modelSettings.topP;
            body["frequency_penalty"] = m_config.modelSettings.frequencyPenalty;
            body["presence_penalty"] = m_config.modelSettings.presencePenalty;
        }
    }

    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(m_config.timeoutMs);

    if (type == ProviderConfig::Anthropic) {
        req.setRawHeader("x-api-key", m_config.provider.apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
    } else if (type == ProviderConfig::Google) {
        // key is in the query string; no auth header
    } else if (!m_config.provider.apiKey.isEmpty()) {
        req.setRawHeader("Authorization",
            QString("Bearer %1").arg(m_config.provider.apiKey).toUtf8());
    }

    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson());

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleReply(reply);
    });
}

// Build a GET request to the models endpoint with provider-appropriate auth.
static QNetworkRequest buildModelsRequest(const ProviderConfig& provider, int timeoutMs,
                                          AgentKind kind)
{
    QString ep = provider.modelsEndpoint(kind);
    if (provider.type == ProviderConfig::Google && !provider.apiKey.isEmpty())
        ep += (ep.contains('?') ? "&" : "?") + QString("key=") + provider.apiKey;

    QUrl url(ep);
    QNetworkRequest req(url);
    req.setTransferTimeout(timeoutMs);

    if (provider.type == ProviderConfig::Anthropic) {
        req.setRawHeader("x-api-key", provider.apiKey.toUtf8());
        req.setRawHeader("anthropic-version", "2023-06-01");
    } else if (provider.type == ProviderConfig::Google) {
        // key is in the query string
    } else if (!provider.apiKey.isEmpty()) {
        req.setRawHeader("Authorization",
            QString("Bearer %1").arg(provider.apiKey).toUtf8());
    }
    return req;
}

void LLMClient::testConnection()
{
    QNetworkRequest req = buildModelsRequest(m_config.provider, m_config.timeoutMs, m_config.kind);
    QNetworkReply* reply = m_net->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleConnectionReply(reply);
    });
}

void LLMClient::fetchModels()
{
    QNetworkRequest req = buildModelsRequest(m_config.provider, m_config.timeoutMs, m_config.kind);
    QNetworkReply* reply = m_net->get(req);

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        handleModelListReply(reply);
    });
}

QString LLMClient::extractErrorMessage(const QString& rawResponse)
{
    QString body = rawResponse.trimmed();
    if (body.isEmpty())
        return {};

    QJsonDocument doc = QJsonDocument::fromJson(body.toUtf8());
    if (doc.isObject()) {
        QJsonObject obj = doc.object();

        // OpenAI / DigitalOcean / Anthropic / Google: {"error": {"message": ...}}
        // or the shorthand {"error": "..."}.
        QJsonValue errVal = obj.value("error");
        if (errVal.isObject()) {
            QString msg = errVal.toObject().value("message").toString();
            if (!msg.isEmpty()) return msg;
        } else if (errVal.isString() && !errVal.toString().isEmpty()) {
            return errVal.toString();
        }

        // Some gateways use {"message": ...} or {"detail": ...} at the top level.
        if (!obj.value("message").toString().isEmpty())
            return obj.value("message").toString();
        if (!obj.value("detail").toString().isEmpty())
            return obj.value("detail").toString();
    }

    // Not JSON we recognize — return the raw body, capped so a huge HTML error
    // page doesn't flood the panel.
    if (body.size() > 500)
        body = body.left(500) + QStringLiteral("…");
    return body;
}

QString LLMClient::extractContent(const QString& rawResponse)
{
    QJsonDocument doc = QJsonDocument::fromJson(rawResponse.toUtf8());
    QString content;

    if (doc.isObject()) {
        QJsonObject obj = doc.object();

        switch (m_config.provider.type) {
            case ProviderConfig::OpenAI:
            case ProviderConfig::Custom: {
                auto choices = obj.value("choices").toArray();
                if (!choices.isEmpty()) {
                    auto msg = choices.first().toObject().value("message").toObject();
                    content = msg.value("content").toString();
                }
                break;
            }
            case ProviderConfig::Anthropic: {
                // { "content": [ { "type":"text", "text":"..." } ] }
                auto arr = obj.value("content").toArray();
                for (const auto& v : arr) {
                    QJsonObject part = v.toObject();
                    if (part.value("type").toString() == "text" || part.contains("text")) {
                        content += part.value("text").toString();
                    }
                }
                break;
            }
            case ProviderConfig::Google: {
                // { "candidates":[ { "content": { "parts":[ {"text":"..."} ] } } ] }
                auto cands = obj.value("candidates").toArray();
                if (!cands.isEmpty()) {
                    auto parts = cands.first().toObject()
                                     .value("content").toObject()
                                     .value("parts").toArray();
                    for (const auto& v : parts)
                        content += v.toObject().value("text").toString();
                }
                break;
            }
            case ProviderConfig::Local:
            default:
                if (obj.contains("message"))
                    content = obj["message"].toObject()["content"].toString();
                else
                    content = obj.value("response").toString();
                break;
        }
    }

    if (content.isEmpty())
        content = rawResponse.trimmed();

    return content;
}

QString LLMClient::stripToolCalls(QString& text)
{
    QString result;
    int lastEnd = 0;

    QRegularExpression re(R"(\[\s*\{[^]]*\})");
    QRegularExpressionMatchIterator it = re.globalMatch(text);

    while (it.hasNext()) {
        QRegularExpressionMatch match = it.next();
        result += text.mid(lastEnd, match.capturedStart() - lastEnd);
        lastEnd = match.capturedEnd();
    }

    result += text.mid(lastEnd);
    return result.trimmed();
}

std::vector<ToolCall> LLMClient::parseToolCalls(const QString& json)
{
    std::vector<ToolCall> result;

    QJsonDocument doc = QJsonDocument::fromJson(json.trimmed().toUtf8());
    if (doc.isNull()) return result;

    if (doc.isObject()) {
        auto obj = doc.object();

        if (obj.contains("tool")) {
            ToolCall tc;
            tc.name = obj["tool"].toString().toStdString();
            auto params = obj["params"].toObject();
            for (auto it = params.begin(); it != params.end(); ++it) {
                auto v = it.value();
                if (v.isString())
                    tc.params[it.key().toStdString()] = v.toString().toStdString();
                else
                    tc.params[it.key().toStdString()] = v.toDouble();
            }
            result.push_back(tc);
            return result;
        }

        return result;
    }

    if (doc.isArray()) {
        for (auto v : doc.array()) {
            if (v.isString()) {
                ToolCall tc;
                tc.name = v.toString().toStdString();
                result.push_back(tc);
                continue;
            }
            auto o = v.toObject();
            if (o.isEmpty()) continue;

            ToolCall tc;
            if (o.contains("tool"))
                tc.name = o["tool"].toString().toStdString();
            else if (o.contains("name"))
                tc.name = o["name"].toString().toStdString();
            else
                continue;

            QJsonObject params;
            if (o.contains("params"))
                params = o["params"].toObject();
            else if (o.contains("arguments"))
                params = o["arguments"].toObject();

            for (auto it = params.begin(); it != params.end(); ++it) {
                auto v = it.value();
                if (v.isString())
                    tc.params[it.key().toStdString()] = v.toString().toStdString();
                else
                    tc.params[it.key().toStdString()] = v.toDouble();
            }

            result.push_back(tc);
        }
    }

    return result;
}

void LLMClient::handleReply(QNetworkReply* reply)
{
    m_busy = false;

    if (reply->error() != QNetworkReply::NoError) {
        QString err = reply->errorString();

        // The transport error string ("server replied:") is often useless on its
        // own — the real reason (invalid key, unknown model, quota) is in the HTTP
        // status and response body. Surface both when present.
        int status = reply->attribute(
            QNetworkRequest::HttpStatusCodeAttribute).toInt();
        QByteArray body = reply->readAll();
        QString detail = extractErrorMessage(QString::fromUtf8(body));

        QString full = err;
        if (status > 0)
            full = QString("HTTP %1 — %2").arg(status).arg(err);
        if (!detail.isEmpty())
            full += QString("\n%1").arg(detail);

        qWarning() << "LLM request error:" << full << "body:" << body;
        emit errorOccurred(full);
        reply->deleteLater();
        return;
    }

    QByteArray data = reply->readAll();
    QString responseText = QString::fromUtf8(data);

    emit responseReceived(responseText);

    QString content = extractContent(responseText);

    content = content.trimmed();
    if (content.startsWith("```json\n")) content = content.mid(8);
    else if (content.startsWith("```\n")) content = content.mid(4);
    else if (content.startsWith("```")) content = content.mid(3);
    if (content.endsWith("```")) content = content.chopped(3);
    content = content.trimmed();

    QList<QString> jsonBlocks;
    int start = 0;
    while (true) {
        int braceStart = content.indexOf('[', start);
        if (braceStart < 0) break;

        int depth = 0;
        int end = -1;
        for (int i = braceStart; i < content.size(); ++i) {
            if (content[i] == '[') depth++;
            else if (content[i] == ']') {
                depth--;
                if (depth == 0) { end = i + 1; break; }
            }
        }
        if (end < 0) break;

        jsonBlocks.append(content.mid(braceStart, end - braceStart));
        start = end;
    }

    QString textPart = content;
    std::vector<ToolCall> tools;

    for (const auto& block : jsonBlocks) {
        textPart.replace(block, "");
        auto parsed = parseToolCalls(block);
        tools.insert(tools.end(), parsed.begin(), parsed.end());
    }

    textPart = textPart.trimmed();

    if (tools.empty() && !content.isEmpty()) {
        QString repaired = content;
        repaired.replace("][", ",");
        QJsonDocument doc2 = QJsonDocument::fromJson(repaired.toUtf8());
        if (!doc2.isArray())
            doc2 = QJsonDocument::fromJson(QString("[%1]").arg(repaired).toUtf8());
        if (doc2.isArray()) {
            for (auto v : doc2.array()) {
                if (v.isArray()) {
                    for (auto inner : v.toArray())
                        if (inner.isObject() && inner.toObject().contains("tool"))
                            v = inner;
                }
                if (v.isString()) {
                    ToolCall tc;
                    tc.name = v.toString().toStdString();
                    tools.push_back(tc);
                    continue;
                }
                auto o = v.toObject();
                if (o.isEmpty()) continue;
                ToolCall tc;
                if (o.contains("tool"))
                    tc.name = o["tool"].toString().toStdString();
                else if (o.contains("name"))
                    tc.name = o["name"].toString().toStdString();
                else
                    continue;
                QJsonObject params;
                if (o.contains("params"))
                    params = o["params"].toObject();
                else if (o.contains("arguments"))
                    params = o["arguments"].toObject();
                for (auto it = params.begin(); it != params.end(); ++it) {
                    auto val = it.value();
                    if (val.isString())
                        tc.params[it.key().toStdString()] = val.toString().toStdString();
                    else
                        tc.params[it.key().toStdString()] = val.toDouble();
                }
                tools.push_back(tc);
            }
        }
    }

    if (!textPart.isEmpty())
        emit textResponse(textPart);

    emit toolsParsed(tools);

    reply->deleteLater();
}

void LLMClient::handleConnectionReply(QNetworkReply* reply)
{
    bool ok = false;
    QString details;
    QStringList models;

    const bool genSd = m_config.kind == Generative &&
        (m_config.provider.type == ProviderConfig::Local ||
         m_config.provider.type == ProviderConfig::Custom);

    if (reply->error() == QNetworkReply::NoError) {
        ok = true;
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);

        if (genSd && doc.isArray()) {
            // A1111 /sdapi/v1/sd-models → [ { "model_name": ..., "title": ... } ]
            for (const auto& v : doc.array()) {
                QString name = v.toObject().value("model_name").toString();
                if (name.isEmpty()) name = v.toObject().value("title").toString();
                if (!name.isEmpty()) models.append(name);
            }
            m_lastModelList = models;
            details = models.isEmpty()
                ? QStringLiteral("Connected (no models listed)")
                : QString("Connected — %1 model(s) available").arg(models.size());
        } else if (doc.isObject()) {
            QJsonObject obj = doc.object();

            if (m_config.provider.type == ProviderConfig::Local) {
                auto arr = obj.value("models").toArray();
                for (const auto& v : arr) {
                    QString name = v.toObject().value("name").toString();
                    if (name.isEmpty())
                        name = v.toString();
                    if (!name.isEmpty())
                        models.append(name);
                }
            } else if (m_config.provider.type == ProviderConfig::Google) {
                // { "models": [ { "name": "models/gemini-1.5-flash" } ] }
                auto arr = obj.value("models").toArray();
                for (const auto& v : arr) {
                    QString name = v.toObject().value("name").toString();
                    if (name.startsWith("models/")) name = name.mid(7);
                    if (!name.isEmpty())
                        models.append(name);
                }
            } else {
                auto arr = obj.value("data").toArray();
                for (const auto& v : arr) {
                    QString id = v.toObject().value("id").toString();
                    if (!id.isEmpty())
                        models.append(id);
                }
            }

            m_lastModelList = models;
            if (!models.isEmpty())
                details = QString("Connected — %1 model(s) available").arg(models.size());
            else
                details = QString("Connected (no models listed)");
        } else {
            details = QString("Connected (unexpected response format)");
        }
    } else {
        details = reply->errorString();
    }

    emit connectionTestResult(ok, details, models);
    reply->deleteLater();
}

void LLMClient::handleModelListReply(QNetworkReply* reply)
{
    const bool genSd = m_config.kind == Generative &&
        (m_config.provider.type == ProviderConfig::Local ||
         m_config.provider.type == ProviderConfig::Custom);

    if (reply->error() == QNetworkReply::NoError) {
        QByteArray data = reply->readAll();
        QJsonDocument doc = QJsonDocument::fromJson(data);
        QStringList models;

        if (genSd && doc.isArray()) {
            // A1111 /sdapi/v1/sd-models → [ { "model_name": ..., "title": ... } ]
            for (const auto& v : doc.array()) {
                QString name = v.toObject().value("model_name").toString();
                if (name.isEmpty()) name = v.toObject().value("title").toString();
                if (!name.isEmpty()) models.append(name);
            }
            m_lastModelList = models;
            emit modelsFetched(models);
            reply->deleteLater();
            return;
        }

        if (doc.isObject()) {
            QJsonObject obj = doc.object();

            if (m_config.provider.type == ProviderConfig::Local) {
                auto arr = obj.value("models").toArray();
                for (const auto& v : arr) {
                    QString name = v.toObject().value("name").toString();
                    if (name.isEmpty())
                        name = v.toString();
                    if (!name.isEmpty())
                        models.append(name);
                }
            } else if (m_config.provider.type == ProviderConfig::Google) {
                auto arr = obj.value("models").toArray();
                for (const auto& v : arr) {
                    QString name = v.toObject().value("name").toString();
                    if (name.startsWith("models/")) name = name.mid(7);
                    if (!name.isEmpty())
                        models.append(name);
                }
            } else {
                auto arr = obj.value("data").toArray();
                for (const auto& v : arr) {
                    QString id = v.toObject().value("id").toString();
                    if (!id.isEmpty())
                        models.append(id);
                }
            }

            m_lastModelList = models;
            emit modelsFetched(models);
        }
    }

    reply->deleteLater();
}
