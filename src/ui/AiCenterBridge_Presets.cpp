// Assistants / Image Generation presets. Every read and write goes through
// AgentPresetManager, filtered by AgentKind — the same structure
// AgentConfigWidget already used, just reached over the bridge instead of a
// Qt Widgets form. No second preset format, no second persistence.
#include "AiCenterBridge.hpp"

#include "agent/AgentConfig.hpp"
#include "agent/AgentPresetManager.hpp"
#include "agent/LLMClient.hpp"

#include <QElapsedTimer>
#include <QPointer>

QVariantMap AiCenterBridge::toVariant(const AgentConfig& c) const
{
    QVariantMap provider;
    provider[QStringLiteral("type")] = ProviderConfig::typeToString(c.provider.type);
    provider[QStringLiteral("baseUrl")] = c.provider.baseUrl;
    provider[QStringLiteral("apiKey")] = c.provider.apiKey;
    provider[QStringLiteral("model")] = c.provider.model;

    QVariantMap modelSettings;
    modelSettings[QStringLiteral("temperature")] = c.modelSettings.temperature;
    modelSettings[QStringLiteral("maxTokens")] = c.modelSettings.maxTokens;
    modelSettings[QStringLiteral("topP")] = c.modelSettings.topP;
    modelSettings[QStringLiteral("frequencyPenalty")] = c.modelSettings.frequencyPenalty;
    modelSettings[QStringLiteral("presencePenalty")] = c.modelSettings.presencePenalty;

    QVariantMap behavior;
    behavior[QStringLiteral("systemPrompt")] = c.behavior.systemPrompt;
    behavior[QStringLiteral("enabledTools")] = QVariant(c.behavior.enabledTools);
    behavior[QStringLiteral("toolMode")] = AgentBehavior::toolModeToString(c.behavior.toolMode);

    QVariantMap generative;
    generative[QStringLiteral("steps")] = c.generative.steps;
    generative[QStringLiteral("strength")] = c.generative.strength;
    generative[QStringLiteral("seed")] = static_cast<qint64>(c.generative.seed);
    generative[QStringLiteral("negativePrompt")] = c.generative.negativePrompt;
    generative[QStringLiteral("promptAssistEnabled")] = c.generative.promptAssistEnabled;
    generative[QStringLiteral("promptAssistPreset")] = c.generative.promptAssistPreset;

    QVariantMap v;
    v[QStringLiteral("name")] = c.name;
    v[QStringLiteral("description")] = c.description;
    v[QStringLiteral("kind")] = agentKindToString(c.kind);
    v[QStringLiteral("provider")] = provider;
    v[QStringLiteral("modelSettings")] = modelSettings;
    v[QStringLiteral("behavior")] = behavior;
    v[QStringLiteral("generative")] = generative;
    v[QStringLiteral("timeoutMs")] = c.timeoutMs;
    v[QStringLiteral("retries")] = c.retries;
    v[QStringLiteral("stream")] = c.stream;
    v[QStringLiteral("logLevel")] = c.logLevel;
    v[QStringLiteral("cache")] = c.cache;
    v[QStringLiteral("maxHistoryTurns")] = c.maxHistoryTurns;
    v[QStringLiteral("resultFeedback")] = c.resultFeedback;
    return v;
}

AgentConfig AiCenterBridge::fromVariant(const QVariantMap& map, const AgentConfig& base) const
{
    AgentConfig c = base;
    c.name = map.value(QStringLiteral("name"), c.name).toString();
    c.description = map.value(QStringLiteral("description"), c.description).toString();
    c.kind = agentKindFromString(map.value(QStringLiteral("kind")).toString());

    const QVariantMap provider = map.value(QStringLiteral("provider")).toMap();
    c.provider.type = ProviderConfig::typeFromString(provider.value(QStringLiteral("type")).toString());
    c.provider.baseUrl = provider.value(QStringLiteral("baseUrl")).toString();
    c.provider.apiKey = provider.value(QStringLiteral("apiKey")).toString();
    c.provider.model = provider.value(QStringLiteral("model")).toString();

    const QVariantMap modelSettings = map.value(QStringLiteral("modelSettings")).toMap();
    c.modelSettings.temperature = modelSettings.value(QStringLiteral("temperature"), c.modelSettings.temperature).toDouble();
    c.modelSettings.maxTokens = modelSettings.value(QStringLiteral("maxTokens"), c.modelSettings.maxTokens).toInt();
    c.modelSettings.topP = modelSettings.value(QStringLiteral("topP"), c.modelSettings.topP).toDouble();
    c.modelSettings.frequencyPenalty = modelSettings.value(QStringLiteral("frequencyPenalty"), c.modelSettings.frequencyPenalty).toDouble();
    c.modelSettings.presencePenalty = modelSettings.value(QStringLiteral("presencePenalty"), c.modelSettings.presencePenalty).toDouble();

    const QVariantMap behavior = map.value(QStringLiteral("behavior")).toMap();
    c.behavior.systemPrompt = behavior.value(QStringLiteral("systemPrompt")).toString();
    c.behavior.enabledTools.clear();
    for (const QVariant& tool : behavior.value(QStringLiteral("enabledTools")).toList())
        c.behavior.enabledTools << tool.toString();
    c.behavior.toolMode = AgentBehavior::toolModeFromString(behavior.value(QStringLiteral("toolMode")).toString());

    const QVariantMap generative = map.value(QStringLiteral("generative")).toMap();
    c.generative.steps = generative.value(QStringLiteral("steps"), c.generative.steps).toInt();
    c.generative.strength = generative.value(QStringLiteral("strength"), c.generative.strength).toDouble();
    c.generative.seed = generative.value(QStringLiteral("seed"), c.generative.seed).toInt();
    c.generative.negativePrompt = generative.value(QStringLiteral("negativePrompt")).toString();
    c.generative.promptAssistEnabled = generative.value(QStringLiteral("promptAssistEnabled")).toBool();
    c.generative.promptAssistPreset = generative.value(QStringLiteral("promptAssistPreset")).toString();

    c.timeoutMs = map.value(QStringLiteral("timeoutMs"), c.timeoutMs).toInt();
    c.retries = map.value(QStringLiteral("retries"), c.retries).toInt();
    c.stream = map.value(QStringLiteral("stream"), c.stream).toBool();
    c.logLevel = map.value(QStringLiteral("logLevel"), c.logLevel).toString();
    c.cache = map.value(QStringLiteral("cache"), c.cache).toBool();
    c.maxHistoryTurns = map.value(QStringLiteral("maxHistoryTurns"), c.maxHistoryTurns).toInt();
    c.resultFeedback = map.value(QStringLiteral("resultFeedback"), c.resultFeedback).toBool();
    return c;
}

QVariantMap AiCenterBridge::getPreset(const QString& name) const
{
    return toVariant(m_presetManager->preset(name));
}

QVariantMap AiCenterBridge::createPreset(const QString& kind, const QString& name,
                                         const QString& description, const QString& providerType)
{
    AgentConfig cfg;
    cfg.kind = agentKindFromString(kind);
    cfg.name = name;
    cfg.description = description;
    cfg.provider.type = ProviderConfig::typeFromString(providerType);
    cfg.provider.baseUrl = ProviderConfig::defaultBaseUrl(cfg.provider.type);

    m_presetManager->save(cfg); // emits presetsChanged, which emitPresetsChanged()/emitOverviewChanged() are connected to
    return toVariant(cfg);
}

QString AiCenterBridge::duplicatePreset(const QString& name)
{
    return m_presetManager->duplicate(name);
}

QVariantMap AiCenterBridge::deletePreset(const QString& name)
{
    m_presetManager->remove(name);
    return presetListsVariant();
}

QVariantMap AiCenterBridge::savePreset(const QString& name, const QVariantMap& preset)
{
    const AgentConfig base = m_presetManager->exists(name) ? m_presetManager->preset(name) : AgentConfig();
    const AgentConfig cfg = fromVariant(preset, base);
    m_presetManager->save(cfg);
    return toVariant(cfg);
}

QVariantMap AiCenterBridge::setActivePreset(const QString& name, const QString& kind)
{
    if (agentKindFromString(kind) == Generative)
        m_presetManager->setActiveGenerative(name);
    else
        m_presetManager->setActivePreset(name);
    return presetListsVariant();
}

void AiCenterBridge::testConnection(const QString& name)
{
    if (!m_client || !m_presetManager->exists(name))
        return;
    m_pendingTestName = name;
    m_client->applyConfig(m_presetManager->preset(name));
    m_client->testConnection(); // answers via connectionTestResult, connected once in the constructor
}

void AiCenterBridge::detectModels(const QString& name)
{
    if (!m_client || !m_presetManager->exists(name))
        return;
    m_pendingTestName = name;
    m_client->applyConfig(m_presetManager->preset(name));
    m_client->fetchModels(); // answers via modelsFetched, connected once in the constructor
}

void AiCenterBridge::testPrompt(const QString& name, const QString& prompt)
{
    if (!m_client || !m_presetManager->exists(name))
        return;
    const QString presetName = name;
    m_client->applyConfig(m_presetManager->preset(name));

    auto timer = std::make_shared<QElapsedTimer>();
    timer->start();

    // One-shot, same idiom as AgentConfigWidget::onTestPrompt: responseReceived/
    // errorOccurred are reused for every sendAsync() call, so each call gets its
    // own pair of self-disconnecting handlers rather than one persistent slot.
    auto okConn = std::make_shared<QMetaObject::Connection>();
    auto errConn = std::make_shared<QMetaObject::Connection>();
    QPointer<AiCenterBridge> self(this);

    *okConn = connect(m_client, &LLMClient::responseReceived, this,
        [self, presetName, timer, okConn, errConn](const QString& response) {
        if (self) {
            QVariantMap result;
            result[QStringLiteral("ok")] = true;
            result[QStringLiteral("message")] = QString();
            result[QStringLiteral("elapsedSeconds")] = timer->elapsed() / 1000.0;
            result[QStringLiteral("response")] = response;
            emit self->promptTestResult(presetName, result);
        }
        QObject::disconnect(*okConn);
        QObject::disconnect(*errConn);
    });
    *errConn = connect(m_client, &LLMClient::errorOccurred, this,
        [self, presetName, timer, okConn, errConn](const QString& error) {
        if (self) {
            QVariantMap result;
            result[QStringLiteral("ok")] = false;
            result[QStringLiteral("message")] = error;
            result[QStringLiteral("elapsedSeconds")] = timer->elapsed() / 1000.0;
            result[QStringLiteral("response")] = QString();
            emit self->promptTestResult(presetName, result);
        }
        QObject::disconnect(*okConn);
        QObject::disconnect(*errConn);
    });

    const AgentConfig cfg = m_presetManager->preset(name);
    m_client->sendAsync(cfg.behavior.systemPrompt, prompt);
}
