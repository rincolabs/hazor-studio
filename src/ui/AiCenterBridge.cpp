#include "AiCenterBridge.hpp"

#include "agent/AgentConfig.hpp"
#include "agent/AgentPresetManager.hpp"
#include "agent/LLMClient.hpp"
#include "ai/models/AiModelDescriptor.hpp"
#include "ai/models/AiModelRegistry.hpp"
#include "ai/runtime/AiRuntimeManager.hpp"
#include "ai/sd/SdModelManager.hpp"
#include "mcp/McpServer.hpp"
#include "tools/ToolCall.hpp"
#include "tools/ToolCatalog.hpp"

#include <QDebug>

AiCenterBridge::AiCenterBridge(AgentPresetManager* presetManager, LLMClient* client,
                               McpServer* mcpServer, QObject* parent)
    : QObject(parent), m_presetManager(presetManager), m_client(client), m_mcpServer(mcpServer)
{
    connect(m_presetManager, &AgentPresetManager::presetsChanged, this, &AiCenterBridge::emitPresetsChanged);
    connect(m_presetManager, &AgentPresetManager::activePresetChanged, this, &AiCenterBridge::emitPresetsChanged);
    connect(m_presetManager, &AgentPresetManager::activeGenerativeChanged, this, &AiCenterBridge::emitPresetsChanged);
    connect(m_presetManager, &AgentPresetManager::presetsChanged, this, &AiCenterBridge::emitOverviewChanged);
    connect(m_presetManager, &AgentPresetManager::activePresetChanged, this, &AiCenterBridge::emitOverviewChanged);
    connect(m_presetManager, &AgentPresetManager::activeGenerativeChanged, this, &AiCenterBridge::emitOverviewChanged);

    connect(AiRuntimeManager::instance(), &AiRuntimeManager::settingsChanged, this, [this] {
        emit runtimeStateChanged(runtimeStateVariant());
        emitOverviewChanged();
    });
    connect(AiRuntimeManager::instance(), &AiRuntimeManager::runtimeStatusChanged, this, [this] {
        emit runtimeStateChanged(runtimeStateVariant());
        emitOverviewChanged();
    });
    connect(AiRuntimeManager::instance(), &AiRuntimeManager::providerChanged, this, [this] {
        emit runtimeStateChanged(runtimeStateVariant());
        emitOverviewChanged();
    });
    connect(AiModelRegistry::instance(), &AiModelRegistry::installedModelsChanged, this, [this] {
        emit modelsVisionChanged(modelsVisionStateVariant());
        emitOverviewChanged();
    });

    if (m_mcpServer) {
        connect(m_mcpServer, &McpServer::serverStarted, this, [this] {
            emit mcpStateChanged(mcpStateVariant());
            emitOverviewChanged();
        });
        connect(m_mcpServer, &McpServer::serverStopped, this, [this] {
            emit mcpStateChanged(mcpStateVariant());
            emitOverviewChanged();
        });
    }

    // Persistent, like AgentConfigWidget's own connections to the same shared
    // LLMClient: these two signals are used only by the test-connection /
    // detect-models path (live chat uses LLMClient::sendPrompt's callback
    // overload instead), so one subscription for the bridge's lifetime is safe.
    if (m_client) {
        connect(m_client, &LLMClient::connectionTestResult, this,
                [this](bool ok, const QString& details, const QStringList& models) {
            QVariantMap result;
            result[QStringLiteral("ok")] = ok;
            result[QStringLiteral("message")] = details;
            result[QStringLiteral("elapsedMs")] = 0;
            result[QStringLiteral("models")] = QVariant(models);
            emit connectionTestResult(m_pendingTestName, result);
            if (ok && !models.isEmpty())
                emit modelsDetected(m_pendingTestName, QVariant(models).toList());
        });
        connect(m_client, &LLMClient::modelsFetched, this, [this](const QStringList& models) {
            emit modelsDetected(m_pendingTestName, QVariant(models).toList());
        });
    }
}

AiCenterBridge::~AiCenterBridge() = default;

void AiCenterBridge::setNavigationTarget(const QString& section, const QString& subsection)
{
    m_targetSection = section;
    m_targetSubsection = subsection;
    m_targetConsumed = false;
    emit navigationRequested(section, subsection);
}

QVariant AiCenterBridge::initialTarget()
{
    if (m_targetConsumed || m_targetSection.isEmpty())
        return QVariant();
    m_targetConsumed = true;
    QVariantMap target;
    target[QStringLiteral("section")] = m_targetSection;
    target[QStringLiteral("subsection")] = m_targetSubsection;
    return target;
}

QVariantList AiCenterBridge::toolCatalog() const
{
    QMap<QString, QVariantList> byCategory; // QMap keeps categories in a stable (alphabetical) order
    for (const ToolDefinition& tool : ToolCatalog::allTools()) {
        const QString category = QString::fromStdString(tool.category);
        byCategory[category].append(QString::fromStdString(tool.name));
    }

    QVariantList groups;
    for (auto it = byCategory.constBegin(); it != byCategory.constEnd(); ++it) {
        QVariantMap group;
        group[QStringLiteral("id")] = it.key();
        group[QStringLiteral("label")] = it.key();
        group[QStringLiteral("tools")] = it.value();
        groups.append(group);
    }
    return groups;
}

void AiCenterBridge::reportError(const QString& context, const QString& message)
{
    qWarning().noquote() << "[AiCenter] js error |" << context << "|" << message;
}

QVariantMap AiCenterBridge::overviewState() const
{
    QVariantMap state;

    const QVariantMap runtime = runtimeStateVariant();
    state[QStringLiteral("runtime")] = runtime;

    const QVariantMap models = modelsVisionStateVariant();
    QStringList invalidNames;
    int totalModels = 0;
    int validModels = 0;
    const QVariantList taskGroups = models.value(QStringLiteral("taskGroups")).toList();
    for (const QVariant& groupVariant : taskGroups) {
        const QVariantList modelsInTask = groupVariant.toMap().value(QStringLiteral("models")).toList();
        for (const QVariant& modelVariant : modelsInTask) {
            const QVariantMap model = modelVariant.toMap();
            ++totalModels;
            if (model.value(QStringLiteral("isValid")).toBool())
                ++validModels;
            else
                invalidNames << model.value(QStringLiteral("displayName")).toString();
        }
    }
    QVariantMap modelsSummary;
    modelsSummary[QStringLiteral("registered")] = totalModels;
    modelsSummary[QStringLiteral("tasks")] = taskGroups.size();
    modelsSummary[QStringLiteral("valid")] = validModels;
    modelsSummary[QStringLiteral("invalid")] = invalidNames.size();
    modelsSummary[QStringLiteral("invalidNames")] = invalidNames;
    state[QStringLiteral("models")] = modelsSummary;

    const QString activeAssistant = m_presetManager->activeAssistantName();
    const QString activeGenerative = m_presetManager->activeGenerativeName();
    state[QStringLiteral("activeAssistant")] = m_presetManager->exists(activeAssistant)
        ? QVariant(presetSummary(m_presetManager->preset(activeAssistant)))
        : QVariant();
    state[QStringLiteral("activeGenerative")] = m_presetManager->exists(activeGenerative)
        ? QVariant(presetSummary(m_presetManager->preset(activeGenerative)))
        : QVariant();

    const QVariantMap sd = sdModelsStateVariant();
    state[QStringLiteral("sd")] = sd.value(QStringLiteral("overviewSummary"));
    state[QStringLiteral("mcp")] = mcpStateVariant();

    QVariantList alerts;
    const QVariantMap diagnostic = runtime.value(QStringLiteral("diagnostic")).toMap();
    if (diagnostic.value(QStringLiteral("status")).toString() == QStringLiteral("error")
        && diagnostic.value(QStringLiteral("selectedProvider")) != diagnostic.value(QStringLiteral("activeProvider"))) {
        QVariantMap alert;
        alert[QStringLiteral("severity")] = QStringLiteral("warn");
        alert[QStringLiteral("title")] = QStringLiteral("Execution provider fell back to CPU");
        alert[QStringLiteral("detail")] = diagnostic.value(QStringLiteral("lastError")).toString();
        alert[QStringLiteral("linkLabel")] = QStringLiteral("Open System › Runtime");
        alert[QStringLiteral("section")] = QStringLiteral("system");
        alert[QStringLiteral("subsection")] = QStringLiteral("runtime");
        alerts.append(alert);
    }
    for (const AiModelDescriptor& model : AiModelRegistry::instance()->invalidModels()) {
        QVariantMap alert;
        alert[QStringLiteral("severity")] = QStringLiteral("err");
        alert[QStringLiteral("title")] = QStringLiteral("Invalid local model: %1").arg(model.displayName);
        alert[QStringLiteral("detail")] = model.statusReason;
        alert[QStringLiteral("linkLabel")] = QStringLiteral("Open Models › Vision & Editing");
        alert[QStringLiteral("section")] = QStringLiteral("models");
        alert[QStringLiteral("subsection")] = QStringLiteral("vision");
        alerts.append(alert);
    }
    const QVariantList sdDirectories = sd.value(QStringLiteral("directories")).toList();
    for (const QVariant& dirVariant : sdDirectories) {
        const QVariantMap dir = dirVariant.toMap();
        if (dir.value(QStringLiteral("available")).toBool())
            continue;
        QVariantMap alert;
        alert[QStringLiteral("severity")] = QStringLiteral("err");
        alert[QStringLiteral("title")] = QStringLiteral("Stable Diffusion directory not found");
        alert[QStringLiteral("detail")] = dir.value(QStringLiteral("path")).toString();
        alert[QStringLiteral("linkLabel")] = QStringLiteral("Open Models › Stable Diffusion");
        alert[QStringLiteral("section")] = QStringLiteral("models");
        alert[QStringLiteral("subsection")] = QStringLiteral("stable-diffusion");
        alerts.append(alert);
    }
    state[QStringLiteral("alerts")] = alerts;

    return state;
}

void AiCenterBridge::emitOverviewChanged()
{
    emit overviewChanged(overviewState());
}

QVariantMap AiCenterBridge::presetSummary(const AgentConfig& config) const
{
    QVariantMap summary;
    summary[QStringLiteral("name")] = config.name;
    summary[QStringLiteral("kind")] = agentKindToString(config.kind);
    summary[QStringLiteral("providerType")] = ProviderConfig::typeToString(config.provider.type);
    summary[QStringLiteral("model")] = config.provider.model;
    const QString activeName = config.kind == Generative
        ? m_presetManager->activeGenerativeName()
        : m_presetManager->activeAssistantName();
    summary[QStringLiteral("isActive")] = (config.name == activeName);
    return summary;
}

QVariantMap AiCenterBridge::presetListsVariant() const
{
    auto buildList = [this](AgentKind kind) {
        QVariantList presets;
        for (const QString& name : m_presetManager->presetNames(kind))
            presets.append(presetSummary(m_presetManager->preset(name)));
        QVariantMap list;
        list[QStringLiteral("presets")] = presets;
        list[QStringLiteral("activeName")] = kind == Generative
            ? m_presetManager->activeGenerativeName()
            : m_presetManager->activeAssistantName();
        return list;
    };

    QVariantMap lists;
    lists[QStringLiteral("assistants")] = buildList(Assistant);
    lists[QStringLiteral("generative")] = buildList(Generative);
    return lists;
}

void AiCenterBridge::emitPresetsChanged()
{
    emit presetsChanged(presetListsVariant());
}

QVariantMap AiCenterBridge::presetLists() const
{
    return presetListsVariant();
}
