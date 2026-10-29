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

// AI Agent, AI / Machine Learning and MCP Server settings moved to the Hazor
// AI Center (src/ui/AiCenterWindow.hpp) — see openAiCenter() in MainWindow.
// This dialog keeps the settings that have no equivalent there.
class AppSettingsDialog : public QDialog
{
    Q_OBJECT
public:
    // Values match the actual sidebar/page order built in setupUi().
    enum PageIndex {
        PageGeneral = 0,
        PageColorManagement = 1,
        PageKeyboardShortcuts = 2,
        PageInterface = 3,
        PageCount
    };

    explicit AppSettingsDialog(int initialPage = PageGeneral, QWidget* parent = nullptr);

    int selectedPage() const;
    // Switches to `page` on an already-open dialog. Needed because Settings is
    // modeless now: a second "Snap Settings..." while it is open should land on
    // that page instead of just raising whatever was showing.
    void setCurrentPage(int page);
    void reject() override;

private slots:
    void onPageSelected(int row);
    void onAccept();
    void onCancel();

private:
    void setupUi();
    void applyStyleSheet();

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
    int m_currentPage = -1;
    bool m_handlingPageChange = false;
};
