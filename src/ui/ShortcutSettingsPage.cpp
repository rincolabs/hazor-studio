#include "ShortcutSettingsPage.hpp"
#include "ShortcutManager.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QHeaderView>
#include <QKeyEvent>
#include <QMenu>
#include <functional>

ShortcutSettingsPage::ShortcutSettingsPage(QWidget* parent)
    : QWidget(parent)
{
    auto* th = ThemeManager::instance()->current();

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 16, 24, 16);
    lay->setSpacing(th->spaceSM);

    // Top row: search + reset all
    auto* topRow = new QHBoxLayout();
    topRow->setSpacing(th->spaceSM);

    m_searchEdit = new QLineEdit(this);
    m_searchEdit->setPlaceholderText(tr("Search actions..."));
    m_searchEdit->setClearButtonEnabled(true);
    topRow->addWidget(m_searchEdit, 1);

    m_resetAllBtn = new QPushButton(tr("Reset All"), this);
    m_resetAllBtn->setToolTip(tr("Reset all shortcuts to defaults"));
    topRow->addWidget(m_resetAllBtn);

    lay->addLayout(topRow);

    // Tree widget
    m_tree = new QTreeWidget(this);
    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Action"), tr("Shortcut")});
    m_tree->setRootIsDecorated(true);
    m_tree->setAnimated(true);
    m_tree->setAlternatingRowColors(false);
    m_tree->setSelectionMode(QAbstractItemView::SingleSelection);
    m_tree->setIndentation(16);
    m_tree->setAllColumnsShowFocus(true);
    m_tree->header()->setStretchLastSection(false);
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setDefaultSectionSize(200);
    m_tree->setContextMenuPolicy(Qt::CustomContextMenu);
    lay->addWidget(m_tree, 1);

    // Warning label
    m_warningLabel = new QLabel(this);
    m_warningLabel->setVisible(false);
    m_warningLabel->setWordWrap(true);
    lay->addWidget(m_warningLabel);

    // Connections
    connect(m_searchEdit, &QLineEdit::textChanged, this, &ShortcutSettingsPage::onSearchTextChanged);
    connect(m_tree, &QTreeWidget::itemClicked, this, &ShortcutSettingsPage::onTreeItemClicked);
    connect(m_resetAllBtn, &QPushButton::clicked, this, &ShortcutSettingsPage::onResetAll);
    connect(m_tree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        auto* item = m_tree->itemAt(pos);
        if (!item || item->childCount() > 0) return;
        auto* th = ThemeManager::instance()->current();
        QMenu menu(this);
        menu.setStyleSheet(QStringLiteral(
            "QMenu { background: %1; color: %2; border: 1px solid %3; padding: %6px; }"
            "QMenu::item { padding: %6px %7px; }"
            "QMenu::item:selected { background: %4; color: %5; }"
            ).arg(th->colorSurface.name(), th->colorTextPrimary.name(), th->colorBorder.name())
            .arg(th->colorSurfaceHover.name(), th->colorTextBright.name())
            .arg(th->spaceSM).arg(th->spaceXL));
        menu.addAction(tr("Reset to Default"), this, &ShortcutSettingsPage::onResetSelected);
        menu.exec(m_tree->mapToGlobal(pos));
    });
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ShortcutSettingsPage::applyTheme);

    applyTheme();
    buildTree();
}

void ShortcutSettingsPage::applyTheme()
{
    auto* th = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral(
        "ShortcutSettingsPage { background: %1; }"
        "QLabel { color: %2; }"
        ).arg(th->colorSurface.name(), th->colorTextPrimary.name()));

    if (m_searchEdit) {
        m_searchEdit->setStyleSheet(QStringLiteral(
            "QLineEdit { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: %4px; padding: %5px %6px; }"
            ).arg(th->colorBackgroundTertiary.name(), th->colorTextPrimary.name())
            .arg(th->colorBorder.name()).arg(th->radiusSM)
            .arg(th->spaceSM).arg(th->spaceMD));
    }

    if (m_resetAllBtn) {
        m_resetAllBtn->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border: 1px solid %3; "
            "border-radius: %4px; padding: %5px %6px; }"
            "QPushButton:hover { background: %7; }"
            "QPushButton:pressed { background: %8; }"
            ).arg(th->colorSurface.name(), th->colorTextPrimary.name(), th->colorBorder.name())
            .arg(th->radiusSM)
            .arg(th->spaceSM).arg(th->spaceLG)
            .arg(th->colorSurfaceHover.name(), th->colorSurfacePressed.name()));
    }

    if (m_tree) {
        m_tree->setStyleSheet(QStringLiteral(
            "QTreeWidget { background: %1; border: 1px solid %2; border-radius: %3px; outline: none; }"
            "QTreeWidget::item { padding: %10px 0; color: %4; }"
            "QTreeWidget::item:hover { background: %5; }"
            "QTreeWidget::item:selected { background: %6; color: %7; }"
            "QHeaderView::section { background: %8; color: %9; border: none; "
            "border-bottom: 1px solid %2; padding: %10px %11px; font-weight: bold; }"
            ).arg(th->colorBackgroundTertiary.name(), th->colorBorder.name())
            .arg(th->radiusMD)
            .arg(th->colorTextPrimary.name())
            .arg(th->colorSurfaceHover.name())
            .arg(th->colorSurfaceSelected.name(), th->colorTextBright.name())
            .arg(th->colorSurface.name(), th->colorTextBright.name())
            .arg(th->spaceXS).arg(th->spaceMD));

        std::function<void(QTreeWidgetItem*)> applyItemColors = [&](QTreeWidgetItem* item) {
            if (!item)
                return;
            const bool category = item->childCount() > 0;
            item->setForeground(0, category ? th->colorTextBright : th->colorTextPrimary);
            if (!category)
                item->setForeground(1, item == m_capturingItem ? th->colorAccent : th->colorTextPrimary);
            for (int i = 0; i < item->childCount(); ++i)
                applyItemColors(item->child(i));
        };
        for (int i = 0; i < m_tree->topLevelItemCount(); ++i)
            applyItemColors(m_tree->topLevelItem(i));
    }

    if (!m_warningText.isEmpty())
        updateWarning(m_warningText);
}

void ShortcutSettingsPage::buildTree()
{
    m_tree->clear();
    auto* mgr = ShortcutManager::instance();
    auto* th = ThemeManager::instance()->current();

    for (const QString& cat : mgr->categories()) {
        auto* catItem = new QTreeWidgetItem(m_tree);
        catItem->setText(0, cat);
        catItem->setFlags(catItem->flags() & ~Qt::ItemIsSelectable);
        QFont boldFont = catItem->font(0);
        boldFont.setBold(true);
        catItem->setFont(0, boldFont);
        catItem->setForeground(0, th->colorTextBright);

        for (const auto& a : mgr->actionsInCategory(cat)) {
            auto* child = new QTreeWidgetItem(catItem);
            child->setText(0, a.label);
            child->setText(1, mgr->shortcutDisplayText(a.id));
            child->setData(0, Qt::UserRole, a.id);
            child->setForeground(0, th->colorTextPrimary);
            child->setForeground(1, th->colorTextPrimary);
        }

        catItem->setExpanded(true);
    }
}

void ShortcutSettingsPage::load()
{
    buildTree();
}

void ShortcutSettingsPage::apply()
{
    // All changes are applied immediately through ShortcutManager::setShortcut
}

void ShortcutSettingsPage::onSearchTextChanged(const QString& text)
{
    for (int i = 0; i < m_tree->topLevelItemCount(); ++i) {
        auto* catItem = m_tree->topLevelItem(i);
        bool catVisible = false;
        for (int j = 0; j < catItem->childCount(); ++j) {
            auto* child = catItem->child(j);
            bool match = text.isEmpty() || child->text(0).contains(text, Qt::CaseInsensitive);
            child->setHidden(!match);
            if (match) catVisible = true;
        }
        catItem->setHidden(!catVisible);
    }
}

void ShortcutSettingsPage::onTreeItemClicked(QTreeWidgetItem* item, int column)
{
    // Only capture on column 1 (shortcut) and only for leaf items
    if (column != 1 || !item || item->childCount() > 0) {
        // Cancel any active capture
        if (m_capturing) cancelCapture();
        return;
    }

    if (m_capturing) {
        if (item == m_capturingItem) {
            // Clicking same item again — confirm the capture
            if (!m_capturePreserved.isEmpty()) {
                finishCapture(m_capturePreserved);
            }
            return;
        }
        cancelCapture();
    }

    startCapture(item);
}

void ShortcutSettingsPage::startCapture(QTreeWidgetItem* item)
{
    auto* th = ThemeManager::instance()->current();
    m_capturing = true;
    m_capturingItem = item;
    m_capturingId = item->data(0, Qt::UserRole).toString();
    m_captureBefore = ShortcutManager::instance()->currentShortcut(m_capturingId);
    m_capturePreserved = {};
    m_capturingItem->setText(1, "...");
    m_capturingItem->setForeground(1, th->colorAccent);
    updateWarning({});
    setFocus();
}

void ShortcutSettingsPage::finishCapture(const QKeySequence& seq)
{
    auto* th = ThemeManager::instance()->current();
    auto* mgr = ShortcutManager::instance();

    if (m_capturingItem) {
        m_capturingItem->setText(1, shortcutText(seq));
        m_capturingItem->setForeground(1, th->colorTextPrimary);
    }

    m_capturing = false;
    m_capturingItem = nullptr;
    m_capturingId.clear();
    m_capturePreserved = {};
}

void ShortcutSettingsPage::cancelCapture()
{
    if (m_capturingItem) {
        auto* th = ThemeManager::instance()->current();
        m_capturingItem->setText(1, shortcutText(m_captureBefore));
        m_capturingItem->setForeground(1, th->colorTextPrimary);
    }
    m_capturing = false;
    m_capturingItem = nullptr;
    m_capturingId.clear();
    m_capturePreserved = {};
    updateWarning({});
}

void ShortcutSettingsPage::keyPressEvent(QKeyEvent* event)
{
    if (!m_capturing || !m_capturingItem) {
        QWidget::keyPressEvent(event);
        return;
    }

    int key = event->key();
    auto mods = event->modifiers();

    // Escape → cancel
    if (key == Qt::Key_Escape) {
        cancelCapture();
        return;
    }

    // Enter/Return → confirm current capture
    if (key == Qt::Key_Return || key == Qt::Key_Enter) {
        if (!m_capturePreserved.isEmpty()) {
            finishCapture(m_capturePreserved);
        }
        return;
    }

    // Backspace/Delete while no other key → clear shortcut
    if ((key == Qt::Key_Backspace || key == Qt::Key_Delete) && mods == Qt::NoModifier) {
        QKeySequence empty;
        bool ok = ShortcutManager::instance()->setShortcut(m_capturingId, empty, true);
        if (ok) {
            m_capturePreserved = empty;
            finishCapture(empty);
            updateWarning({});
        }
        return;
    }

    // Reject modifier-only presses
    if (key == Qt::Key_Control || key == Qt::Key_Shift || key == Qt::Key_Alt
        || key == Qt::Key_Meta) {
        return;
    }

    // Build the key sequence
    int seqKey = key;
    if (mods.testFlag(Qt::ControlModifier)) seqKey |= Qt::CTRL;
    if (mods.testFlag(Qt::ShiftModifier))   seqKey |= Qt::SHIFT;
    if (mods.testFlag(Qt::AltModifier))     seqKey |= Qt::ALT;
    if (mods.testFlag(Qt::MetaModifier))    seqKey |= Qt::META;

    QKeySequence seq(mods | key);

    // Check for conflict
    QString conflict = ShortcutManager::instance()->findConflict(seq, m_capturingId);
    if (!conflict.isEmpty()) {
        updateWarning(tr("This shortcut is already assigned to: %1").arg(conflict));
        return;
    }

    // Check against global defaults (suppress menu QAction conflicts)
    bool ok = ShortcutManager::instance()->setShortcut(m_capturingId, seq, true);
    if (ok) {
        m_capturePreserved = seq;
        // Visually show accepted, but stay in capture mode until Enter/click away
        auto* th = ThemeManager::instance()->current();
        m_capturingItem->setText(1, shortcutText(seq));
        m_capturingItem->setForeground(1, th->colorAccent);
        updateWarning({});
    }
}

QString ShortcutSettingsPage::shortcutText(const QKeySequence& seq) const
{
    if (seq.isEmpty())
        return QStringLiteral("\u2014");  // em dash
    return seq.toString(QKeySequence::NativeText);
}

void ShortcutSettingsPage::updateWarning(const QString& text)
{
    m_warningText = text;
    auto* th = ThemeManager::instance()->current();
    if (text.isEmpty()) {
        m_warningLabel->setVisible(false);
    } else {
        m_warningLabel->setText(QStringLiteral("<span style='color:%1;'>%2</span>")
            .arg(th->colorDanger.name(), text.toHtmlEscaped()));
        m_warningLabel->setVisible(true);
    }
}

void ShortcutSettingsPage::onResetAll()
{
    ShortcutManager::instance()->resetAll();
    buildTree();
    emit shortcutsChanged();
}

void ShortcutSettingsPage::onResetSelected()
{
    auto* item = m_tree->currentItem();
    if (!item || item->childCount() > 0) return;
    QString id = item->data(0, Qt::UserRole).toString();
    if (id.isEmpty()) return;
    ShortcutManager::instance()->resetToDefault(id);
    auto* th = ThemeManager::instance()->current();
    item->setText(1, ShortcutManager::instance()->shortcutDisplayText(id));
    item->setForeground(1, th->colorTextPrimary);
    emit shortcutsChanged();
}
