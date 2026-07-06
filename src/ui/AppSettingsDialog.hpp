#pragma once

#include <QDialog>

class QListWidget;
class QStackedWidget;
class QPushButton;
class QFrame;
class GeneralSettingsPage;
class ColorSettingsPage;
class ShortcutSettingsPage;
class RulerGuideSettingsPage;
class AgentConfigWidget;
class AiSettingsPage;
class AgentPresetManager;
class LLMClient;

class AppSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // Values match the actual sidebar/page order built in setupUi().
    enum PageIndex {
        PageGeneral = 0,
        PageColorManagement = 1,
        PageKeyboardShortcuts = 2,
        PageAIAgent = 3,
        PageInterface = 4,
        PageAIMachineLearning = 5,
        PageCount
    };

    explicit AppSettingsDialog(int initialPage = PageGeneral,
                               AgentPresetManager* presetMgr = nullptr,
                               LLMClient* client = nullptr,
                               QWidget* parent = nullptr);

    int selectedPage() const;
    void reject() override;

private slots:
    void onPageSelected(int row);
    void onAccept();
    void onCancel();

private:
    void setupUi();
    void applyStyleSheet();
    bool confirmAgentPendingChanges();

    QListWidget* m_sidebar = nullptr;
    QStackedWidget* m_pages = nullptr;
    QPushButton* m_acceptBtn = nullptr;
    QPushButton* m_cancelBtn = nullptr;
    QWidget* m_btnBar = nullptr;
    QFrame* m_separator = nullptr;

    GeneralSettingsPage* m_generalPage = nullptr;
    ColorSettingsPage* m_colorSettingsPage = nullptr;
    ShortcutSettingsPage* m_shortcutPage = nullptr;
    RulerGuideSettingsPage* m_rulerGuidePage = nullptr;
    AgentConfigWidget* m_agentConfigWidget = nullptr;
    AiSettingsPage* m_aiPage = nullptr;
    AgentPresetManager* m_presetManager = nullptr;
    LLMClient* m_client = nullptr;
    int m_currentPage = -1;
    bool m_handlingPageChange = false;
};
