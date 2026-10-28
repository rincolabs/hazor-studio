#include "NeuralFlowBridge.hpp"

#include "ai/sd/SdBackend.hpp"
#include "ai/sd/SdCanvasBridge.hpp"
#include "ai/sd/SdModelManager.hpp"
#include "ai/sd/SdNodeCatalog.hpp"
#include "ai/sd/SdWorkflow.hpp"
#include "ai/sd/SdWorkflowExecutor.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/WebThemeVariables.hpp"

#include <QDebug>

namespace {

// Reported to the frontend for a failure that is not about the graph: the
// engine's own error has no SdWorkflowErrorCode, and giving it one would mean
// inventing a validation code for something validation never saw.
const char* kGenerationFailedCode = "generation_failed";

QVariantMap modelToVariant(const sdlocal::SdModelEntry& entry, const QString& defaultModelId)
{
    QVariantMap model;
    model.insert(QStringLiteral("id"), entry.id);
    model.insert(QStringLiteral("label"), entry.label);
    model.insert(QStringLiteral("category"), sdlocal::modelCategoryName(entry.category));
    model.insert(QStringLiteral("encoderRole"),
                 sdlocal::textEncoderRoleName(entry.encoderRole));
    model.insert(QStringLiteral("path"), entry.path);
    // The registry records where weights live without owning them, so a file
    // can disappear between two queries. The frontend shows that rather than
    // offering a model that cannot load.
    model.insert(QStringLiteral("available"), entry.fileExists());
    model.insert(QStringLiteral("discovered"), entry.discovered);
    model.insert(QStringLiteral("isDefault"), !entry.id.isEmpty() && entry.id == defaultModelId);
    return model;
}

} // namespace

NeuralFlowBridge::NeuralFlowBridge(QObject* parent)
    : QObject(parent)
{
    m_canvas = new sdlocal::SdCanvasBridge(this);
    m_executor = new sdlocal::SdWorkflowExecutor(m_canvas, this);

    connect(m_executor, &sdlocal::SdWorkflowExecutor::stateChanged,
            this, &NeuralFlowBridge::onStateChanged);
    connect(m_executor, &sdlocal::SdWorkflowExecutor::progress, this,
            [this](int step, int steps, float seconds) {
        emit workflowProgress(step, steps, static_cast<double>(seconds));
    });
    connect(m_executor, &sdlocal::SdWorkflowExecutor::finished,
            this, &NeuralFlowBridge::onFinished);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        emit themeChanged(webtheme::variables());
    });
}

NeuralFlowBridge::~NeuralFlowBridge() = default;

void NeuralFlowBridge::shutdown()
{
    if (m_executor)
        m_executor->shutdown();
}

bool NeuralFlowBridge::busy() const
{
    return m_executor && m_executor->busy();
}

void NeuralFlowBridge::setController(ImageController* controller)
{
    m_canvas->setController(controller);
    const bool available = m_canvas->hasDocument();
    if (available == m_documentAvailable)
        return;
    m_documentAvailable = available;
    emit documentAvailabilityChanged(available);
}

// ── Queries ─────────────────────────────────────────────────────────────────

QVariantMap NeuralFlowBridge::nodeDefinitions() const
{
    return sdlocal::describeNodeCatalog().toVariantMap();
}

QVariantList NeuralFlowBridge::availableModels() const
{
    const sdlocal::SdModelManager& models = sdlocal::SdModelManager::instance();
    const QString defaultModelId = models.defaultModelId();

    QVariantList out;
    for (const sdlocal::SdModelEntry& entry : models.models())
        out.append(modelToVariant(entry, defaultModelId));
    return out;
}

QVariantList NeuralFlowBridge::refreshModels()
{
    sdlocal::SdModelManager::instance().refresh();
    const QVariantList models = availableModels();
    // Emitted as well as returned: the caller gets its answer, and anything
    // else listening (a second editor instance later on) stays in step.
    emit modelsChanged(models);
    return models;
}

QVariantMap NeuralFlowBridge::initialState()
{
    QVariantMap state;
    state.insert(QStringLiteral("nodeDefinitions"), nodeDefinitions());
    state.insert(QStringLiteral("models"), availableModels());
    state.insert(QStringLiteral("defaultModelId"),
                 sdlocal::SdModelManager::instance().defaultModelId());
    state.insert(QStringLiteral("theme"), webtheme::variables());
    state.insert(QStringLiteral("busy"), busy());
    state.insert(QStringLiteral("hasDocument"), m_canvas->hasDocument());
    // The three layers SdBackend keeps apart — compiled backend, devices found,
    // device in use — as one line, so the editor can say where a run will go
    // instead of implying a GPU it may not have.
    state.insert(QStringLiteral("backend"), sdlocal::describeBackendEnvironment());
    state.insert(QStringLiteral("modelConfigPath"),
                 sdlocal::SdModelManager::instance().configPath());
    return state;
}

// ── Execution ───────────────────────────────────────────────────────────────

QVariantList NeuralFlowBridge::errorsToVariant(const QVector<sdlocal::SdWorkflowError>& errors)
{
    QVariantList out;
    for (const sdlocal::SdWorkflowError& error : errors) {
        QVariantMap entry;
        entry.insert(QStringLiteral("code"), sdlocal::workflowErrorCodeName(error.code));
        // Both can be empty: not every failure is about one node or one port.
        // The frontend highlights a node only when it is named here.
        entry.insert(QStringLiteral("nodeId"), error.nodeId);
        entry.insert(QStringLiteral("portId"), error.portId);
        entry.insert(QStringLiteral("message"), error.message);
        out.append(entry);
    }
    return out;
}

QVariantMap NeuralFlowBridge::executeWorkflow(const QString& workflowJson)
{
    QVariantMap response;

    QVector<sdlocal::SdWorkflowError> errors;
    // Everything this call does — parse, validate, resolve models, capture
    // pixels — happens inside the existing workflow controller. Nothing is
    // started when it refuses, so a bad graph costs milliseconds.
    const bool started = m_executor->run(workflowJson.toUtf8(), &errors);

    response.insert(QStringLiteral("started"), started);
    response.insert(QStringLiteral("errors"), errorsToVariant(errors));

    if (started) {
        qInfo().noquote() << "[NeuralFlow] run started |"
                          << sdlocal::describeBackendEnvironment();
        emit workflowStarted();
    } else {
        // Not also signalled: a refusal is the answer to this call, and the
        // frontend reporting it twice is the kind of difference that turns into
        // two error banners for one mistake.
        qWarning().noquote() << "[NeuralFlow] run refused |"
                             << sdlocal::describeWorkflowErrors(errors);
    }
    return response;
}

void NeuralFlowBridge::cancelWorkflow()
{
    if (m_executor)
        m_executor->cancel();
}

void NeuralFlowBridge::reportError(const QString& context, const QString& message)
{
    qWarning().noquote() << "[NeuralFlow]" << context << "|" << message;
}

void NeuralFlowBridge::onStateChanged(sdlocal::SdGenerationState state, const QString& detail)
{
    emit workflowStateChanged(sdlocal::generationStateName(state), detail);
}

void NeuralFlowBridge::onFinished(const sdlocal::SdWorkflowRunResult& result)
{
    const sdlocal::SdGenerationResult& generation = result.generation;

    if (generation.state == sdlocal::SdGenerationState::Cancelled) {
        // stateChanged already said so; a cancel is not a failure and must not
        // be reported as one.
        return;
    }

    if (!result.ok()) {
        QVariantList errors = errorsToVariant(result.errors);
        if (errors.isEmpty()) {
            QVariantMap entry;
            entry.insert(QStringLiteral("code"), QString::fromUtf8(kGenerationFailedCode));
            entry.insert(QStringLiteral("nodeId"), QString());
            entry.insert(QStringLiteral("portId"), QString());
            entry.insert(QStringLiteral("message"), result.firstError());
            errors.append(entry);
        }
        emit workflowFailed(errors);
        return;
    }

    QVariantMap info;
    info.insert(QStringLiteral("layerName"), result.layerName);
    info.insert(QStringLiteral("width"), generation.size.width());
    info.insert(QStringLiteral("height"), generation.size.height());
    // A string, like the seed in a workflow document: a 64-bit value does not
    // survive the JSON number the channel would turn it into.
    info.insert(QStringLiteral("seed"), QString::number(generation.seed));
    info.insert(QStringLiteral("modelId"), generation.modelId);
    info.insert(QStringLiteral("modelVersion"), generation.modelVersion);
    // The device the run really used, never the one that was asked for: a CPU
    // run on a GPU build has to read as a CPU run.
    info.insert(QStringLiteral("device"), generation.deviceName);
    info.insert(QStringLiteral("gpu"), generation.gpu);
    info.insert(QStringLiteral("cpuFallback"), generation.cpuFallback);
    info.insert(QStringLiteral("backendNote"), generation.backendNote);
    info.insert(QStringLiteral("loadMs"), generation.loadMs);
    info.insert(QStringLiteral("contextReused"), generation.contextReused);
    info.insert(QStringLiteral("generateMs"), generation.generateMs);
    emit workflowCompleted(info);
}
