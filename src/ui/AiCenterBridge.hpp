#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

class AgentPresetManager;
class LLMClient;
class McpServer;
class QWidget;
struct AgentConfig;
struct AiModelDescriptor;
struct AiRuntimeSettings;

// The QWebChannel surface of the AI Center: the only thing the Vue application
// can reach inside Hazor.
//
// Narrow on purpose, same spirit as NeuralFlowBridge: every operation here is a
// thin adapter onto something that already exists — AiRuntimeManager,
// AiModelRegistry, AgentPresetManager, sdlocal::SdModelManager, McpSettings.
// There is no second settings store, no second preset format and no second
// model registry behind this class. C++ remains the source of truth; this
// class only shapes calls and replies as QVariant trees that map onto JSON.
//
// Split across several .cpp files by concern (AiCenterBridge_Runtime.cpp,
// _Models.cpp, _Presets.cpp, _StableDiffusion.cpp, _Mcp.cpp), one class.
//
// Lives on the GUI thread, like NeuralFlowBridge. LLMClient's network replies
// arrive on this thread already.
class AiCenterBridge : public QObject {
    Q_OBJECT

public:
    explicit AiCenterBridge(AgentPresetManager* presetManager, McpServer* mcpServer,
                            QObject* parent = nullptr);
    ~AiCenterBridge() override;

    // Dialogs (Browse..., file pickers) parent themselves to this.
    void setDialogParent(QWidget* parent) { m_dialogParent = parent; }

    // Set once by AiCenterWindow before the page's first navigation, and again
    // for every later openSection() call while the page is already loaded.
    void setNavigationTarget(const QString& section, const QString& subsection);

    // ── Invoked from JavaScript — shell ──────────────────────────────────────

    // Where to land on first paint; null outside a deep link. Consumed once:
    // after this call, further navigation arrives only through
    // navigationRequested (see AiCenterWindow::openSection).
    Q_INVOKABLE QVariant initialTarget();

    Q_INVOKABLE QVariantList toolCatalog() const;

    Q_INVOKABLE void reportError(const QString& context, const QString& message);

    Q_INVOKABLE QVariantMap overviewState() const;

    // ── Runtime / Performance / Diagnostics (AiRuntimeManager) ───────────────

    Q_INVOKABLE QVariantMap runtimeState() const;
    Q_INVOKABLE QVariantMap applyRuntimeSettings(const QVariantMap& patch);
    Q_INVOKABLE QVariantMap clearAiCache();

    // The Real-ESRGAN executable override ("ai/upscale/realesrganExecutable"),
    // the one AI preference that is a QSettings key rather than an
    // AiRuntimeSettings field. An empty path clears the override and goes back
    // to the packaged backend — AiUpscaleDialog can only set one, so without
    // this there is no way back from a bad path.
    Q_INVOKABLE QVariantMap applyRealEsrganPath(const QString& path);
    Q_INVOKABLE QString browseForRealEsrganExecutable();

    // ── Models — Vision & Editing (AiModelRegistry) ──────────────────────────

    Q_INVOKABLE QVariantMap modelsVisionState() const;
    Q_INVOKABLE QVariantMap setDefaultModel(const QString& task, const QString& modelId);
    Q_INVOKABLE QVariantMap removeInstalledModel(const QString& modelId);
    Q_INVOKABLE void showModelFiles(const QString& modelId) const;
    Q_INVOKABLE void openModelLicense(const QString& modelId) const;
    // No downloadManifestModel(): wiring real downloads (progress, cancel) is
    // a separate, larger change — see AiModelDownloadManager / AiSettingsPage's
    // existing onModelAction/onDownloadStarted family. Out of scope here; the
    // manifest is only surfaced for display (manifestConfigured/manifestEntries).

    // ── Models — Stable Diffusion (sdlocal::SdModelManager) ──────────────────

    Q_INVOKABLE QVariantMap sdModelsState() const;
    Q_INVOKABLE QVariantMap registerSdFile(const QString& path, const QString& category,
                                           const QString& encoderRole, const QString& id);
    Q_INVOKABLE QVariantMap registerSdDirectory(const QString& path, const QString& category,
                                                const QString& encoderRole, bool recursive);
    Q_INVOKABLE QVariantMap unregisterSdEntry(const QString& id);
    Q_INVOKABLE QVariantMap unregisterSdDirectory(const QString& path);
    Q_INVOKABLE QVariantMap setSdDefaultModel(const QString& id);
    Q_INVOKABLE QVariantMap rescanSdDirectories();
    Q_INVOKABLE QString browseForSdFile();
    Q_INVOKABLE QString browseForSdDirectory();

    // ── Presets — Assistants / Image Generation (AgentPresetManager) ────────

    Q_INVOKABLE QVariantMap presetLists() const;
    Q_INVOKABLE QVariantMap getPreset(const QString& name) const;

    // The defaults a fresh provider of `type` gets for `kind`. Read from
    // ProviderConfig so the page never carries its own copy of them — "Local"
    // means Ollama for an assistant and an A1111 server for image generation.
    Q_INVOKABLE QVariantMap providerDefaults(const QString& kind, const QString& type) const;

    // createPreset/savePreset answer with { ok, error, preset }: a name
    // collision or an empty name has to reach the user as a message, not as a
    // silently overwritten preset.
    Q_INVOKABLE QVariantMap createPreset(const QString& kind, const QString& name,
                                         const QString& description, const QString& providerType);
    Q_INVOKABLE QString duplicatePreset(const QString& name);
    Q_INVOKABLE QVariantMap deletePreset(const QString& name);
    // `name` is the preset as it is stored; `preset["name"]` is what the user
    // typed. When they differ this renames instead of writing a second preset.
    Q_INVOKABLE QVariantMap savePreset(const QString& name, const QVariantMap& preset);
    Q_INVOKABLE QVariantMap setActivePreset(const QString& name, const QString& kind);

    // Fire-and-forget: LLMClient's replies are genuinely asynchronous, so these
    // start the call and answer later through the matching signal below.
    //
    // They take the editor's draft rather than reading the stored preset: the
    // whole point of a test is to try the values currently on screen, including
    // unsaved ones. `name` only labels the reply.
    Q_INVOKABLE void testConnection(const QString& name, const QVariantMap& preset);
    Q_INVOKABLE void detectModels(const QString& name, const QVariantMap& preset);
    Q_INVOKABLE void testPrompt(const QString& name, const QVariantMap& preset,
                                const QString& prompt);

    // ── MCP (McpSettings / McpServer) ────────────────────────────────────────

    Q_INVOKABLE QVariantMap mcpState() const;
    Q_INVOKABLE QVariantMap applyMcpState(bool enabled, int port);

signals:
    void navigationRequested(const QString& section, const QString& subsection);
    void overviewChanged(const QVariantMap& state);
    void runtimeStateChanged(const QVariantMap& state);
    void modelsVisionChanged(const QVariantMap& state);
    void sdModelsChanged(const QVariantMap& state);
    void presetsChanged(const QVariantMap& lists);
    void mcpStateChanged(const QVariantMap& state);

    void connectionTestResult(const QString& name, const QVariantMap& result);
    void modelsDetected(const QString& name, const QVariantList& models);
    void promptTestResult(const QString& name, const QVariantMap& result);

    // Owned by MainWindow: McpServer's lifecycle (start/stop on port/enabled
    // change) is MainWindow::applyMcpSettings()'s job already, for every other
    // caller of the MCP settings — this reuses that instead of restarting the
    // server here too.
    void mcpRestartRequested();

private:
    // Shared by the dedicated "xState()" call and overviewState(), so Overview
    // never recomputes anything the System/Models/Assistants pages don't also see.
    QVariantMap runtimeStateVariant() const;
    QVariantMap modelsVisionStateVariant() const;
    QVariantMap sdModelsStateVariant() const;
    QVariantMap mcpStateVariant() const;
    QVariantMap presetSummary(const AgentConfig& config) const;
    QVariantMap presetListsVariant() const;
    // { ok, error, preset } — the one reply shape createPreset/savePreset use.
    QVariantMap presetMutation(bool ok, const QString& error, const AgentConfig& config) const;
    // Resolves the draft the three test calls were handed into a config, and
    // points m_testClient at it. Returns false when the draft is unusable.
    bool prepareTestClient(const QString& name, const QVariantMap& preset, AgentConfig* out);
    void emitPresetsChanged();
    void emitOverviewChanged();

    QVariantMap toVariant(const AgentConfig& config) const;
    AgentConfig fromVariant(const QVariantMap& map, const AgentConfig& base) const;
    QVariantMap toVariant(const AiModelDescriptor& model, bool isDefault) const;
    QVariantMap toVariant(const AiRuntimeSettings& settings) const;

    AgentPresetManager* m_presetManager = nullptr;
    // The bridge's own client, never the chat agent's. Test Connection / Detect
    // Models / Test Prompt reconfigure whatever client they run on and their
    // replies are broadcast on LLMClient's signals, so sharing AgentController's
    // would repoint the live chat at the preset being tested and feed it the
    // test's answer — tool calls included.
    LLMClient* m_testClient = nullptr;
    McpServer* m_mcpServer = nullptr;
    QWidget* m_dialogParent = nullptr;

    QString m_targetSection;
    QString m_targetSubsection;
    bool m_targetConsumed = false;

    // The preset a fire-and-forget LLMClient call (testConnection/detectModels/
    // testPrompt) is currently running for, so the one-shot reply handlers know
    // which name to put on the result signal.
    QString m_pendingTestName;

    // SdModelManager keeps no "last scan" timestamp; tracked here, and only
    // once a manual Rescan actually ran (not for the transparent first scan
    // ensureScanned() does lazily on first query).
    QString m_sdLastScanAt;
};
