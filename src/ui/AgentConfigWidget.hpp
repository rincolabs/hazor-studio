#pragma once

#include <QWidget>
#include "agent/AgentConfig.hpp"

class QLineEdit;
class QTextEdit;
class QPlainTextEdit;
class QComboBox;
class QDoubleSpinBox;
class QSpinBox;
class QCheckBox;
class QPushButton;
class QLabel;
class QGroupBox;
class QListWidget;
class QStackedWidget;
class QTabWidget;
class LLMClient;
class AgentPresetManager;

class AgentConfigWidget : public QWidget
{
    Q_OBJECT
public:
    explicit AgentConfigWidget(AgentPresetManager* manager, LLMClient* client,
                               const QString& initialPreset = {},
                               QWidget* parent = nullptr);

    QString selectedPresetName() const;
    void apply();
    void load();
    bool hasUnsavedChanges() const;
    bool confirmPendingChanges();

private slots:
    void onKindChanged(int index);
    void onProviderChanged(int index);
    void onPresetChanged(int index);
    void onNewPreset();
    void onDeletePreset();
    void onDuplicatePreset();
    void onTestConnection();
    void onTestPrompt();
    void onConnectionResult(bool ok, const QString& details, const QStringList& models);
    void onModelsFetched(const QStringList& models);
    void onDetectModels();
    void onToolsCheckChanged();
    void onSavePreset();

private:
    enum class PendingChangesChoice {
        Save,
        Discard,
        Cancel
    };

    QWidget* createPresetBar();
    QWidget* createHeaderSection();
    QWidget* createProviderSection();
    QWidget* createModelSettingsSection();
    QWidget* createBehaviorSection();
    QWidget* createGenerativeSection();
    QWidget* createTestSection();
    QWidget* createAdvancedSection();

    AgentKind currentKind() const;
    void applyTheme();
    void repopulateProviderCombo(AgentKind kind);
    void updateKindVisibility(AgentKind kind);
    void setSectionTabVisible(int index, bool visible);
    void ensureCurrentTabVisible();
    void updateProviderFields();
    void loadPresetIntoForm(const AgentConfig& preset);
    void saveFormToPreset(AgentConfig& preset) const;
    void validateInputs();
    void refreshPresetCombo();
    void connectDirtySignals();
    void markDirty();
    void setDirty(bool dirty);
    void updateDirtyUi();
    void resetTestConnectionStatus();
    bool saveChanges();
    void discardChanges();
    PendingChangesChoice promptPendingChanges();
    void setPresetComboByName(const QString& name);

    AgentPresetManager* m_manager = nullptr;
    LLMClient* m_client = nullptr;
    AgentConfig m_currentPreset;
    QString m_loadedPresetName;

    // Preset bar
    QComboBox* m_presetCombo = nullptr;
    QPushButton* m_newBtn = nullptr;
    QPushButton* m_deleteBtn = nullptr;
    QPushButton* m_duplicateBtn = nullptr;
    QPushButton* m_saveBtn = nullptr;
    QPushButton* m_dirtyBtn = nullptr;

    // Header
    QLineEdit* m_nameEdit = nullptr;
    QLineEdit* m_descEdit = nullptr;
    QComboBox* m_kindCombo = nullptr;

    // Provider
    QComboBox* m_providerCombo = nullptr;
    QStackedWidget* m_providerStack = nullptr;

    // Section containers (shown/hidden per kind)
    QTabWidget* m_sectionsTabs = nullptr;
    QWidget* m_modelSettingsSection = nullptr;
    QWidget* m_behaviorSection = nullptr;
    QWidget* m_generativeSection = nullptr;
    int m_modelSettingsTabIndex = -1;
    int m_behaviorTabIndex = -1;
    int m_generativeTabIndex = -1;

    // Generative settings
    QSpinBox* m_stepsSpin = nullptr;
    QDoubleSpinBox* m_strengthSpin = nullptr;
    QSpinBox* m_seedSpin = nullptr;
    QLineEdit* m_negativePromptEdit = nullptr;
    QCheckBox* m_promptAssistCheck = nullptr;
    QComboBox* m_promptAssistCombo = nullptr;

    QLineEdit* m_baseUrlEdit = nullptr;
    QLineEdit* m_apiKeyEdit = nullptr;
    QComboBox* m_modelCombo = nullptr;
    QPushButton* m_detectModelsBtn = nullptr;

    // Model Settings
    QDoubleSpinBox* m_tempSpin = nullptr;
    QSpinBox* m_maxTokensSpin = nullptr;
    QDoubleSpinBox* m_topPSpin = nullptr;
    QDoubleSpinBox* m_freqPenaltySpin = nullptr;
    QDoubleSpinBox* m_presencePenaltySpin = nullptr;

    // Behavior
    QPlainTextEdit* m_systemPromptEdit = nullptr;
    QListWidget* m_toolsList = nullptr;
    QLabel* m_toolsCountLabel = nullptr;

    // Test
    QPushButton* m_testConnBtn = nullptr;
    QLabel* m_testConnStatus = nullptr;
    QLineEdit* m_testPromptInput = nullptr;
    QPushButton* m_testPromptBtn = nullptr;
    QPlainTextEdit* m_testPromptOutput = nullptr;

    // Advanced
    QSpinBox* m_timeoutSpin = nullptr;
    QSpinBox* m_retriesSpin = nullptr;
    QCheckBox* m_streamCheck = nullptr;
    QComboBox* m_logLevelCombo = nullptr;
    QCheckBox* m_cacheCheck = nullptr;

    QLabel* m_validationLabel = nullptr;
    bool m_loadingPreset = false;
    bool m_switchingPreset = false;
    bool m_dirty = false;
};
