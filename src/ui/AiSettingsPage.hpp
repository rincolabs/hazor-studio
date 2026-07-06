#pragma once

#include <QWidget>
#include <QHash>
#include <QString>

class QComboBox;
class QSpinBox;
class QLabel;
class QPushButton;
class QProgressBar;
class QVBoxLayout;
class AppCheckBox;
class AppComboBox;
struct AiModelDescriptor;
struct AiManifestEntry;

// Settings > AI / Machine Learning page. Drives AiRuntimeManager (runtime +
// providers + performance), AiModelRegistry (installed models) and
// AiModelDownloadManager (downloads). Reuses the app's themed widgets and the
// same load/saveSettings contract as the other settings pages: nothing is
// persisted until the dialog is accepted, while model downloads / cache actions
// are immediate.
class AiSettingsPage : public QWidget {
    Q_OBJECT
public:
    explicit AiSettingsPage(QWidget* parent = nullptr);

    void loadSettings();
    void saveSettings();

private:
    QWidget* buildCompatibilityGroup();
    QWidget* buildRuntimeGroup();
    QWidget* buildProvidersGroup();
    QWidget* buildPerformanceGroup();
    QWidget* buildModelsGroup();
    QWidget* buildDiagnosticsGroup();

    // Fills the Execution Provider combobox with Auto + only the providers that
    // are actually selectable on this platform/runtime (spec §9).
    void populateExecutionProviderCombo();
    void refreshProviderStatuses();
    void refreshModelList();
    void refreshDiagnostics();
    // Refreshes the Compatibility summary + per-provider cards + the combobox.
    void refreshCompatibility();
    void applyTheme();

    // Model card builders (rebuilt on every refresh, since discovery is dynamic).
    QWidget* buildDiscoveredCard(const AiModelDescriptor& model);
    QWidget* buildDownloadCard(const AiManifestEntry& entry);
    QWidget* buildInvalidCard(const AiModelDescriptor& model);

    void onChangeModelsDir();
    void onOpenModelsFolder();
    void onClearCache();
    void onModelAction(const QString& modelId);     // download / cancel
    void onUseAsDefault(const QString& task, const QString& modelId);
    void onRemoveModel(const QString& modelId);
    void onRedownload(const QString& modelId);
    void onShowFiles(const QString& dir);
    void onOpenLicense(const QString& dir, const QString& licenseFile, const QString& fallbackUrl);

    void onDownloadStarted(const QString& modelId);
    void onDownloadProgress(const QString& modelId, qint64 received, qint64 total);
    void onDownloadStatus(const QString& modelId, const QString& text);
    void onDownloadFinished(const QString& modelId, bool success, const QString& error);

    void setControlsEnabledForRuntime();

    // Per-id live widgets used by download signals (the rest of a card is static).
    struct ModelRow {
        QLabel* status = nullptr;
        QPushButton* actionBtn = nullptr;
        QProgressBar* progress = nullptr;
    };
    QHash<QString, ModelRow> m_rows;

    // ── Runtime ──
    AppCheckBox* m_enableCb = nullptr;
    AppComboBox* m_providerCombo = nullptr;
    QLabel* m_providerStatusLabel = nullptr;
    QLabel* m_realesrganPathLabel = nullptr;
    QString m_realesrganExePath;
    QLabel* m_compatLabel = nullptr;            // Compatibility summary (rich text)
    QWidget* m_providersContainer = nullptr;    // Execution Provider cards (rebuilt)
    QVBoxLayout* m_providersLayout = nullptr;
    AppCheckBox* m_cpuFallbackCb = nullptr;
    AppCheckBox* m_fp16Cb = nullptr;
    AppCheckBox* m_graphOptCb = nullptr;
    AppCheckBox* m_memArenaCb = nullptr;
    AppCheckBox* m_memPatternCb = nullptr;
    AppCheckBox* m_preloadCb = nullptr;
    QLabel* m_runtimeBanner = nullptr;

    // ── Performance ──
    QSpinBox* m_cpuThreadsSpin = nullptr;
    QSpinBox* m_concurrentSpin = nullptr;
    QSpinBox* m_maxLoadedModelsSpin = nullptr;
    QSpinBox* m_gpuBudgetSpin = nullptr;
    QSpinBox* m_cpuBudgetSpin = nullptr;
    AppCheckBox* m_embedCacheCb = nullptr;
    QSpinBox* m_maxDocsSpin = nullptr;
    QSpinBox* m_maxEmbedMbSpin = nullptr;
    AppComboBox* m_workingResCombo = nullptr;
    AppComboBox* m_workingResSideCombo = nullptr;

    // ── Models ──
    QLabel* m_modelsDirLabel = nullptr;
    QString m_modelsDir;
    QWidget* m_modelsContainer = nullptr;   // rebuilt on every refreshModelList()
    QVBoxLayout* m_modelsLayout = nullptr;

    // ── Diagnostics ──
    QLabel* m_diagLabel = nullptr;
    QPushButton* m_clearCacheBtn = nullptr;
};
