#include "AppSettingsDialog.hpp"
#include "GeneralSettingsPage.hpp"
#include "ColorSettingsPage.hpp"
#include "ShortcutSettingsPage.hpp"
#include "RulerGuideSettingsPage.hpp"
#include "AgentConfigWidget.hpp"
#include "AiSettingsPage.hpp"
#include "McpSettingsPage.hpp"
#include "ShortcutManager.hpp"
#include "MainWindow.hpp"
#include "agent/AgentPresetManager.hpp"
#include "agent/LLMClient.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QListWidget>
#include <QStackedWidget>
#include <QPushButton>
#include <QLabel>
#include <QScrollArea>
#include <QDebug>

AppSettingsDialog::AppSettingsDialog(int initialPage, AgentPresetManager *presetMgr,
                                     LLMClient *client, QWidget *parent)
    : QDialog(parent), m_presetManager(presetMgr), m_client(client)
{
    setWindowTitle(tr("Settings"));
    resize(850, 600);
    setMinimumSize(700, 400);

    setupUi();
    applyStyleSheet();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &AppSettingsDialog::applyStyleSheet);

    if (m_sidebar->count() > 0 && initialPage >= 0 && initialPage < m_sidebar->count())
    {
        m_sidebar->setCurrentRow(initialPage);
        onPageSelected(initialPage);
    }
}

int AppSettingsDialog::selectedPage() const
{
    return m_sidebar ? m_sidebar->currentRow() : 0;
}

void AppSettingsDialog::setupUi()
{
    auto *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(0, 0, 0, 0);
    mainLay->setSpacing(0);

    // ── Content area: sidebar + pages ──
    auto *content = new QWidget(this);
    auto *contentLay = new QHBoxLayout(content);
    contentLay->setContentsMargins(0, 0, 0, 0);
    contentLay->setSpacing(0);

    // Sidebar
    m_sidebar = new QListWidget(this);
    m_sidebar->setFixedWidth(180);
    m_sidebar->setIconSize(QSize(16, 16));
    m_sidebar->setSpacing(0);

    m_sidebar->addItem(tr("General"));
    m_sidebar->addItem(tr("Color Management"));
    m_sidebar->addItem(tr("Keyboard Shortcuts"));
    // m_sidebar->addItem(tr("Performance"));
    m_sidebar->addItem(tr("AI Agent"));
    m_sidebar->addItem(tr("Interface"));
    m_sidebar->addItem(tr("AI / Machine Learning"));
    m_sidebar->addItem(tr("MCP Server"));

    m_sidebar->setCurrentRow(PageKeyboardShortcuts);

    contentLay->addWidget(m_sidebar);

    // Separator line
    m_separator = new QFrame(this);
    m_separator->setFrameShape(QFrame::VLine);
    m_separator->setFixedWidth(1);
    // contentLay->addWidget(m_separator);

    // Pages
    m_pages = new QStackedWidget(this);

    m_generalPage = new GeneralSettingsPage(this);
    m_pages->addWidget(m_generalPage);

    m_colorSettingsPage = new ColorSettingsPage(this);
    m_pages->addWidget(m_colorSettingsPage);

    m_shortcutPage = new ShortcutSettingsPage(this);
    m_pages->addWidget(m_shortcutPage);

    auto *th = ThemeManager::instance()->current();
    QString pageBg = QStringLiteral("QWidget { background: transparent; }").arg(th->colorSurface.name());
    QString labelColor = QStringLiteral("color: %1;").arg(th->colorTextPrimary.name());

    // Performance placeholder
    auto *perfPage = new QWidget(this);
    perfPage->setStyleSheet(pageBg);
    auto *perfLay = new QVBoxLayout(perfPage);
    perfLay->setContentsMargins(24, 16, 24, 16);
    auto *perfLabel = new QLabel(tr("Performance settings available in a future update."), perfPage);
    perfLabel->setWordWrap(true);
    perfLabel->setStyleSheet(labelColor);
    perfLay->addWidget(perfLabel);
    perfLay->addStretch();
    // m_pages->addWidget(perfPage);

    // AI Agent page
    m_agentConfigWidget = new AgentConfigWidget(m_presetManager, m_client,
                                                m_presetManager ? m_presetManager->activePresetName() : QString(), this);
    m_agentConfigWidget->load();
    m_pages->addWidget(m_agentConfigWidget);

    m_rulerGuidePage = new RulerGuideSettingsPage(this);
    m_pages->addWidget(m_rulerGuidePage);

    // AI / Machine Learning page (ONNX runtime, providers, performance, models).
    m_aiPage = new AiSettingsPage(this);
    m_pages->addWidget(m_aiPage);

    // MCP Server page (embedded local tool server for external AI agents).
    m_mcpPage = new McpSettingsPage(this);
    m_pages->addWidget(m_mcpPage);

    contentLay->addWidget(m_pages, 1);

    mainLay->addWidget(content, 1);

    // ── Bottom buttons ──
    m_btnBar = new QWidget(this);
    auto *btnLay = new QHBoxLayout(m_btnBar);
    btnLay->setContentsMargins(12, 8, 12, 8);

    btnLay->addStretch();
    m_acceptBtn = new QPushButton(tr("Ok"), this);
    m_acceptBtn->setFixedWidth(100);
    m_cancelBtn = new QPushButton(tr("Cancel"), this);
    m_cancelBtn->setFixedWidth(100);
    btnLay->addWidget(m_cancelBtn);
    btnLay->addWidget(m_acceptBtn);

    mainLay->addWidget(m_btnBar);

    // Connections
    connect(m_sidebar, &QListWidget::currentRowChanged, this, &AppSettingsDialog::onPageSelected);
    connect(m_acceptBtn, &QPushButton::clicked, this, &AppSettingsDialog::onAccept);
    connect(m_cancelBtn, &QPushButton::clicked, this, &AppSettingsDialog::onCancel);

    m_currentPage = m_sidebar->currentRow();
}

void AppSettingsDialog::onPageSelected(int row)
{
    if (m_handlingPageChange) {
        if (row >= 0 && row < m_pages->count())
            m_pages->setCurrentIndex(row);
        m_currentPage = row;
        return;
    }

    if (row < 0 || row >= m_pages->count())
        return;

    const int agentPage = m_agentConfigWidget ? m_pages->indexOf(m_agentConfigWidget) : -1;
    if (m_currentPage == agentPage && row != agentPage && !confirmAgentPendingChanges()) {
        m_handlingPageChange = true;
        m_sidebar->setCurrentRow(m_currentPage);
        m_handlingPageChange = false;
        return;
    }

    m_pages->setCurrentIndex(row);
    m_currentPage = row;
}

void AppSettingsDialog::onAccept()
{
    if (!confirmAgentPendingChanges())
        return;

    m_generalPage->saveSettings();
    if (m_colorSettingsPage)
        m_colorSettingsPage->saveSettings();
    m_shortcutPage->apply();
    if (m_rulerGuidePage)
        m_rulerGuidePage->saveSettings();
    if (m_aiPage)
        m_aiPage->saveSettings();
    if (m_mcpPage) {
        m_mcpPage->saveSettings();
        if (auto* mw = qobject_cast<MainWindow*>(parentWidget()))
            mw->applyMcpSettings();
    }
    ShortcutManager::instance()->saveSettings();
    accept();
}

void AppSettingsDialog::onCancel()
{
    reject();
}

void AppSettingsDialog::reject()
{
    if (!confirmAgentPendingChanges())
        return;

    // Reload original settings to undo any changes made
    ShortcutManager::instance()->loadSettings();
    QDialog::reject();
}

bool AppSettingsDialog::confirmAgentPendingChanges()
{
    return !m_agentConfigWidget || m_agentConfigWidget->confirmPendingChanges();
}

void AppSettingsDialog::applyStyleSheet()
{
    auto *th = ThemeManager::instance()->current();
    const auto &bgPrim = th->colorBackgroundTertiary.name();
    const auto &border = th->colorBorder.name();
    const auto &text = th->colorTextPrimary.name();
    const auto &textBr = th->colorTextBright.name();
    const auto &hover = th->colorSurfaceHover.name();
    const auto &press = th->colorSurfacePressed.name();

    // QStackedWidget background
    m_pages->setStyleSheet(QStringLiteral(
                               "QStackedWidget { background: %1; }")
                               .arg(th->colorSurface.name()));

    // Sidebar — override ListWidget with custom item colors
    const QString sidebarStyle = QStringLiteral(
                                     "QListWidget { background: %1; border: none; outline: none; padding: 1px; margin: 0; }"
                                     "QListWidget::item {background: %3; color: %2; border: 1px solid %5; margin: 1px; border-radius: 0px; padding: %7px %7px; }"
                                     "QListWidget::item:hover { background: %4; }"
                                     "QListWidget::item:selected { background: %5; color: %2; }")
                                     .arg(th->colorBackgroundTertiary.name(), th->colorTextPrimary.name())
                                     .arg(th->colorSurface.name(), th->colorSurfaceHover.name())
                                     .arg(th->colorSurfacePressed.name())
                                     .arg(th->spaceSM);
    // qDebug() << "AppSettingsDialog sidebarStyle:" << sidebarStyle;
    m_sidebar->setStyleSheet(sidebarStyle);

    // Bottom bar
    if (m_btnBar)
    {
        m_btnBar->setStyleSheet(QStringLiteral(
                                    "QWidget { background: %1; border-top: 1px solid %2; }")
                                    .arg(bgPrim, border));
    }

    // Separator
    if (m_separator)
    {
        m_separator->setStyleSheet(QStringLiteral(
                                       "QFrame { color: %1; background: %1; }")
                                       .arg(border));
    }
}
