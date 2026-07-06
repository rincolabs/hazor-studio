#include "ToolsPanel.hpp"
#include "ColorSwapWidget.hpp"
#include "IconUtils.hpp"
#include "ShortcutManager.hpp"
#include "core/ColorEngine.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QToolButton>
#include <QButtonGroup>
#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMouseEvent>
#include <QPainter>
#include <QTimer>

namespace {
constexpr int kGroupedButtonBase = 1000;
constexpr int kLongPressMenuDelayMs = 320;

QString toolTipFor(const QString& name, const QString& shortcut)
{
    return shortcut.isEmpty()
        ? name
        : name + QStringLiteral(" (") + shortcut + QStringLiteral(")");
}

QString shortcutTextForTool(const QString& actionId, const QString& fallback)
{
    if (!actionId.isEmpty()) {
        auto* shortcuts = ShortcutManager::instance();
        if (shortcuts->hasAction(actionId)) {
            const auto seqs = shortcuts->currentShortcuts(actionId);
            return seqs.isEmpty() ? QString() : shortcuts->shortcutDisplayText(actionId);
        }
    }
    return fallback;
}

QString toolGroupMenuStyleSheet()
{
    auto* t = ThemeManager::instance()->current();
    return QStringLiteral(
        "QMenu {"
        "  background: %1;"
        "  color: %2;"
        "  border: 1px solid %3;"
        "  padding: %4px;"
        "  margin: 0px;"
        "  icon-size: 24px;"   // ← adicionar aqui
        "}"
        "QMenu::item { padding: %5px %6px; }"
        "QMenu::item:selected { background: %7; }")
        .arg(t->colorSurface.name())
        .arg(t->colorTextPrimary.name())
        .arg(t->colorBorder.name())
        .arg(t->spaceSM)
        .arg(t->spaceSM)
        .arg(t->spaceXL)
        .arg(t->colorSurfaceHover.name());
}

class ToolGroupButton : public QToolButton {
public:
    using QToolButton::QToolButton;

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event);

        auto* t = ThemeManager::instance()->current();
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setPen(Qt::NoPen);
        painter.setBrush(t->colorTextSecondary);

        const int marker = qMax(4, qRound(width() * 0.14));
        const int inset = qMax(4, qRound(width() * 0.12));
        const QPoint bottomRight(width() - inset, height() - inset);

        QPolygonF triangle;
        triangle << QPointF(bottomRight.x() - marker, bottomRight.y())
                 << QPointF(bottomRight.x(), bottomRight.y())
                 << QPointF(bottomRight.x(), bottomRight.y() - marker);
        painter.drawPolygon(triangle);
    }
};
}

ToolsPanel::ToolsPanel(ColorEngine* colorEngine, QWidget* parent)
    : QWidget(parent)
{
    auto* t = ThemeManager::instance()->current();
    setObjectName(QStringLiteral("toolsPanel"));
    setFixedWidth(kPanelWidth);
    setAutoFillBackground(true);
    setAttribute(Qt::WA_StyledBackground, true);

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(t->spaceXS, t->spaceXS, t->spaceXS, t->spaceXS);
    lay->setSpacing(t->spaceXS);

    const ToolDef tools[] = {
        { Move, tr("Move"), tr("V"), makeIcon(":/icons/move.png"), QStringLiteral("tool.move") },
        { Brush, tr("Brush"), tr("B"), makeIcon(":/icons/brush.png"), QStringLiteral("tool.brush") },
        { Eraser, tr("Eraser"), tr("E"), makeIcon(":/icons/eraser.png"), QStringLiteral("tool.eraser") },
        { Zoom, tr("Zoom"), tr("Z"), makeIcon(":/icons/zoom.png"), QStringLiteral("tool.zoom") },
        { Hand, tr("Hand"), tr("H"), makeIcon(":/icons/hand.png"), QStringLiteral("tool.hand") },
        { Text, tr("Text"), tr("T"), makeIcon(":/icons/text.png"), QStringLiteral("tool.text") },
        { Crop, tr("Crop"), tr("C"), makeIcon(":/icons/crop.png"), QStringLiteral("tool.crop") },
        { FillBucket, tr("Fill Bucket"), tr("Shift+G"), makeIcon(":/icons/fillbucket.png"), QStringLiteral("tool.fill_bucket") },
        { Gradient, tr("Gradient"), tr("G"), makeIcon(":/icons/gradient.png"), QStringLiteral("tool.gradient") },
        { Eyedropper, tr("Eyedropper"), tr("I"), makeIcon(":/icons/eyedropper.png"), QStringLiteral("tool.eyedropper") },
        { Shape, tr("Shape"), tr("U"), makeIcon(":/icons/shape.png"), QStringLiteral("tool.shape") },
    };

    m_toolGroups = {
        {
            QStringLiteral("selection.marquee"),
            Select,
            0,
            {
                {0, tr("Rectangular Selection"), QString(), makeIcon(":/icons/select-rect.png"), QStringLiteral("tool.selection.marquee_rect")},
                {1, tr("Elliptical Selection"), QString(), makeIcon(":/icons/select-ellipse.png"), QStringLiteral("tool.selection.marquee_ellipse")},
            },
            nullptr,
            nullptr
        },
        {
            QStringLiteral("selection.lasso"),
            Select,
            2,
            {
                {2, tr("Lasso Selection"), QString(), makeIcon(":/icons/select-lasso.png"), QStringLiteral("tool.selection.lasso")},
                {6, tr("Polygonal Lasso"), QString(), makeIcon(":/icons/select-polylasso.png"), QStringLiteral("tool.selection.polygonal_lasso")},
                {5, tr("Magnetic Lasso"), QString(), makeIcon(":/icons/select-magnet.png"), QStringLiteral("tool.selection.magnetic_lasso")},
            },
            nullptr,
            nullptr
        },
        {
            QStringLiteral("selection.wand"),
            Select,
            3,
            {
                {3, tr("Magic Wand"), QString(), makeIcon(":/icons/select-wand.png"), QStringLiteral("tool.selection.magic_wand")},
                {4, tr("Quick Selection"), QString(), makeIcon(":/icons/select-quick.png"), QStringLiteral("tool.selection.quick_selection")},
            },
            nullptr,
            nullptr
        },
        {
            // Clone Stamp flyout: Clone Stamp + Healing Brush share Tool::CloneStamp
            // and differ only by stamp sub-mode (StampMode). Sub-tool ids map to
            // StampMode (0 = Clone, 1 = Healing).
            QStringLiteral("retouch.stamp"),
            CloneStamp,
            0,
            {
                {0, tr("Clone Stamp"), QString(), makeIcon(":/icons/clone-stamp.png"), QStringLiteral("tool.clone_stamp")},
                {1, tr("Healing Brush"), QString(), makeIcon(":/icons/healing-brush.png"), QStringLiteral("tool.healing_brush")},
            },
            nullptr,
            nullptr
        },
        {
            // Fill Bucket flyout: Paint Bucket and Gradient are sibling tools,
            // but share the same toolbar slot.
            QStringLiteral("fill.gradient"),
            FillBucket,
            FillBucket,
            {
                {FillBucket, tr("Fill Bucket"), tr("Shift+G"), makeIcon(":/icons/fillbucket.png"), QStringLiteral("tool.fill_bucket")},
                {Gradient, tr("Gradient"), tr("G"), makeIcon(":/icons/gradient.png"), QStringLiteral("tool.gradient")},
            },
            nullptr,
            nullptr
        },
        {
            QStringLiteral("ai.tools"),
            AiSelect,
            AiSelect,
            {
                {AiSelect, tr("AI Object Selection"), QString(), makeIcon(":/icons/select-ai.png"), QStringLiteral("tool.ai_select")},
                // Temporarily hidden: AI Remove Tool will be released later.
                // Do not remove the implementation. Only the user-facing UI is disabled.
                // {AiRemove, tr("AI Remove"), QString(), makeIcon(":/icons/ai-remove.png"), QStringLiteral("tool.ai_remove")},
            },
            nullptr,
            nullptr
        },
        {
            // Move flyout: Move + Skew share the Move toolbar slot (and the Move
            // canvas tool). Skew only swaps the options page to Distort/Perspective.
            QStringLiteral("transform.skew"),
            Move,
            Move,
            {
                {Move, tr("Move"), tr("V"), makeIcon(":/icons/move.png"), QStringLiteral("tool.move")},
                {Skew, tr("Skew"), QString(), makeIcon(":/icons/skew.png"), QStringLiteral("tool.skew")},
            },
            nullptr,
            nullptr
        },
    };

    m_buttons = QVector<QToolButton*>(AiRemove + 1, nullptr);
    m_activeGroupForTool = QVector<int>(AiRemove + 1, -1);
    for (int i = 0; i < m_toolGroups.size(); ++i) {
        const int hostTool = m_toolGroups[i].hostTool;
        if (hostTool >= 0 && hostTool < m_activeGroupForTool.size()
            && m_activeGroupForTool[hostTool] < 0) {
            m_activeGroupForTool[hostTool] = i;
        }
    }
    m_group = new QButtonGroup(this);
    m_group->setExclusive(true);

    const int visualOrder[] = {
        kGroupedButtonBase + 6,  // Move / Skew flyout
        kGroupedButtonBase + 0,
        kGroupedButtonBase + 1,
        kGroupedButtonBase + 2,
        kGroupedButtonBase + 5,
        -1,
        Crop,
        -1,
        Brush, Eraser, kGroupedButtonBase + 3, kGroupedButtonBase + 4, Eyedropper,
        -1,
        Shape, Text,
        -1,
        Zoom, Hand
    };

    for (int orderedId : visualOrder) {
        if (orderedId < 0)
            continue;

        const bool isGroupedButton = orderedId >= kGroupedButtonBase;
        const int groupIndex = isGroupedButton ? orderedId - kGroupedButtonBase : -1;

        ToolDef def;
        if (isGroupedButton) {
            if (groupIndex < 0 || groupIndex >= m_toolGroups.size())
                continue;
            const auto& group = m_toolGroups[groupIndex];
            const ToolDef* activeTool = nullptr;
            for (const auto& tool : group.tools) {
                if (tool.id == group.activeSubTool) {
                    activeTool = &tool;
                    break;
                }
            }
            def = activeTool ? *activeTool : group.tools.first();
        } else {
            const ToolDef* found = nullptr;
            for (const auto& tool : tools) {
                if (tool.id == orderedId) {
                    found = &tool;
                    break;
                }
            }
            if (!found)
                continue;
            def = *found;
        }

        auto* btn = isGroupedButton
            ? static_cast<QToolButton*>(new ToolGroupButton(this))
            : new QToolButton(this);
        const QString shortcut = shortcutTextForTool(def.shortcutId, def.shortcut);
        btn->setIcon(def.icon);
        btn->setIconSize(QSize(24, 24));
        btn->setToolTip(toolTipFor(def.name, shortcut));
        btn->setCheckable(true);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        btn->setMinimumHeight(34);
        btn->setMinimumWidth(36);
        btn->setMaximumWidth(36);
        btn->setMaximumHeight(34);
        btn->setToolButtonStyle(Qt::ToolButtonIconOnly);
        m_group->addButton(btn, orderedId);
        lay->addWidget(btn, 0, Qt::AlignCenter);

        if (isGroupedButton) {
            auto& group = m_toolGroups[groupIndex];
            group.button = btn;
            auto* menu = new QMenu(btn);
            menu->setAttribute(Qt::WA_TranslucentBackground, true);
            menu->setAttribute(Qt::WA_StyledBackground, true);
            menu->setAutoFillBackground(false);
            menu->setWindowFlag(Qt::FramelessWindowHint, true);
            menu->setWindowFlag(Qt::NoDropShadowWindowHint, true);
            group.menu = menu;

            auto* longPressTimer = new QTimer(btn);
            longPressTimer->setSingleShot(true);
            longPressTimer->setInterval(kLongPressMenuDelayMs);
            connect(btn, &QToolButton::pressed, longPressTimer, qOverload<>(&QTimer::start));
            connect(btn, &QToolButton::released, longPressTimer, &QTimer::stop);
            connect(btn, &QToolButton::destroyed, longPressTimer, &QTimer::stop);
            connect(longPressTimer, &QTimer::timeout, btn, [this, groupIndex]() {
                showGroupMenu(groupIndex);
            });
            btn->installEventFilter(this);

            refreshGroupButton(groupIndex);
        } else {
            m_buttons[orderedId] = btn;
        }
    }

    m_colorSwapWidget = new ColorSwapWidget(colorEngine, this);
    lay->addWidget(m_colorSwapWidget, 0, Qt::AlignCenter);

    lay->addStretch();

    connect(m_group, &QButtonGroup::idClicked, this, &ToolsPanel::activateToolButton);
    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ToolsPanel::applyTheme);
    applyTheme();

    const int moveGroup = groupIndexForTool(Move);
    if (moveGroup >= 0 && m_toolGroups[moveGroup].button)
        m_toolGroups[moveGroup].button->setChecked(true);
    else if (Move >= 0 && Move < m_buttons.size() && m_buttons[Move])
        m_buttons[Move]->setChecked(true);
}

void ToolsPanel::setActiveTool(int tool)
{
    const int groupIndex = groupIndexForTool(tool);
    if (groupIndex >= 0) {
        auto& group = m_toolGroups[groupIndex];
        for (const auto& subTool : group.tools) {
            if (subTool.id == tool) {
                group.activeSubTool = tool;
                refreshGroupButton(groupIndex);
                break;
            }
        }
        if (auto* btn = m_toolGroups[groupIndex].button)
            btn->setChecked(true);
        return;
    }

    for (int i = 0; i < m_toolGroups.size(); ++i) {
        auto& group = m_toolGroups[i];
        if (group.hostTool == Select || group.hostTool == CloneStamp)
            continue;
        for (const auto& subTool : group.tools) {
            if (subTool.id == tool) {
                group.activeSubTool = tool;
                if (group.hostTool >= 0 && group.hostTool < m_activeGroupForTool.size())
                    m_activeGroupForTool[group.hostTool] = i;
                refreshGroupButton(i);
                if (group.button)
                    group.button->setChecked(true);
                return;
            }
        }
    }

    if (tool >= 0 && tool < m_buttons.size() && m_buttons[tool])
        m_buttons[tool]->setChecked(true);
}

bool ToolsPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::MouseButtonPress) {
        auto* btn = qobject_cast<QToolButton*>(watched);
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (btn && mouseEvent && mouseEvent->button() == Qt::RightButton) {
            for (int i = 0; i < m_toolGroups.size(); ++i) {
                if (m_toolGroups[i].button == btn) {
                    showGroupMenu(i);
                    return true;
                }
            }
        }
    }

    return QWidget::eventFilter(watched, event);
}

void ToolsPanel::setActiveSubTool(int tool, int subTool)
{
    const int groupIndex = groupIndexForSubTool(tool, subTool);
    if (groupIndex < 0)
        return;

    auto& group = m_toolGroups[groupIndex];
    if (tool >= 0 && tool < m_activeGroupForTool.size())
        m_activeGroupForTool[tool] = groupIndex;
    group.activeSubTool = subTool;
    refreshGroupButton(groupIndex);
    if (group.button)
        group.button->setChecked(true);
}

int ToolsPanel::activeSubToolForTool(int tool) const
{
    const int groupIndex = groupIndexForTool(tool);
    return groupIndex >= 0 ? m_toolGroups[groupIndex].activeSubTool : -1;
}

QVector<int> ToolsPanel::activeSubToolsForTool(int tool) const
{
    return subToolsForGroup(groupIndexForTool(tool));
}

void ToolsPanel::activateToolButton(int buttonId)
{
    if (buttonId >= kGroupedButtonBase) {
        activateToolGroup(buttonId - kGroupedButtonBase);
        return;
    }

    emit toolSelected(buttonId);
}

void ToolsPanel::activateToolGroup(int groupIndex)
{
    if (groupIndex < 0 || groupIndex >= m_toolGroups.size())
        return;

    auto& group = m_toolGroups[groupIndex];
    if (group.hostTool >= 0 && group.hostTool < m_activeGroupForTool.size())
        m_activeGroupForTool[group.hostTool] = groupIndex;
    if (group.button)
        group.button->setChecked(true);

    emit toolGroupActivated(group.hostTool, group.id, group.activeSubTool, subToolsForGroup(groupIndex));
}

void ToolsPanel::selectSubTool(int groupIndex, int subTool)
{
    if (groupIndex < 0 || groupIndex >= m_toolGroups.size())
        return;

    auto& group = m_toolGroups[groupIndex];
    group.activeSubTool = subTool;
    refreshGroupButton(groupIndex);
    activateToolGroup(groupIndex);
}

void ToolsPanel::showGroupMenu(int groupIndex)
{
    if (groupIndex < 0 || groupIndex >= m_toolGroups.size())
        return;

    auto& group = m_toolGroups[groupIndex];
    if (!group.menu || !group.button)
        return;

    group.menu->popup(group.button->mapToGlobal(QPoint(group.button->width() + 2, 0)));
}

void ToolsPanel::refreshToolGroupMenuStyles()
{
    const QString style = toolGroupMenuStyleSheet();
    for (auto& group : m_toolGroups) {
        if (group.menu)
            group.menu->setStyleSheet(style);
    }
}

void ToolsPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    QPalette pal = palette();
    pal.setColor(QPalette::Window, t->colorSurface);
    setPalette(pal);
    setStyleSheet(QStringLiteral("#toolsPanel { background: %1; }")
        .arg(t->colorSurface.name()));
    refreshToolGroupMenuStyles();
    update();
}

void ToolsPanel::refreshGroupButton(int groupIndex)
{
    if (groupIndex < 0 || groupIndex >= m_toolGroups.size())
        return;

    auto& group = m_toolGroups[groupIndex];
    const ToolDef* activeTool = nullptr;
    for (const auto& tool : group.tools) {
        if (tool.id == group.activeSubTool) {
            activeTool = &tool;
            break;
        }
    }
    if (!activeTool && !group.tools.isEmpty())
        activeTool = &group.tools.first();
    if (!activeTool)
        return;

    if (group.button) {
        const QString shortcut = shortcutTextForTool(activeTool->shortcutId, activeTool->shortcut);
        group.button->setIcon(activeTool->icon);
        group.button->setToolTip(toolTipFor(activeTool->name, shortcut));
    }

    if (!group.menu)
        return;

    group.menu->clear();
    for (const auto& tool : group.tools) {
        auto* action = group.menu->addAction(tool.icon, tool.name);
        action->setCheckable(true);
        action->setChecked(tool.id == group.activeSubTool);
        if (!tool.shortcutId.isEmpty() && ShortcutManager::instance()->hasAction(tool.shortcutId)) {
            action->setShortcut(ShortcutManager::instance()->currentShortcut(tool.shortcutId));
        } else if (!tool.shortcut.isEmpty()) {
            action->setShortcut(QKeySequence(tool.shortcut));
        }
        connect(action, &QAction::triggered, this, [this, groupIndex, id = tool.id]() {
            selectSubTool(groupIndex, id);
        });
    }
}

int ToolsPanel::groupIndexForTool(int tool) const
{
    if (tool >= 0 && tool < m_activeGroupForTool.size()) {
        const int activeGroup = m_activeGroupForTool[tool];
        if (activeGroup >= 0 && activeGroup < m_toolGroups.size()
            && m_toolGroups[activeGroup].hostTool == tool) {
            return activeGroup;
        }
    }

    for (int i = 0; i < m_toolGroups.size(); ++i) {
        if (m_toolGroups[i].hostTool == tool) {
            if (m_toolGroups[i].button && m_toolGroups[i].button->isChecked())
                return i;
        }
    }

    for (int i = 0; i < m_toolGroups.size(); ++i) {
        if (m_toolGroups[i].hostTool == tool)
            return i;
    }

    return -1;
}

int ToolsPanel::groupIndexForSubTool(int tool, int subTool) const
{
    for (int i = 0; i < m_toolGroups.size(); ++i) {
        const auto& group = m_toolGroups[i];
        if (group.hostTool != tool)
            continue;
        for (const auto& item : group.tools) {
            if (item.id == subTool)
                return i;
        }
    }
    return -1;
}

QVector<int> ToolsPanel::subToolsForGroup(int groupIndex) const
{
    QVector<int> ids;
    if (groupIndex < 0 || groupIndex >= m_toolGroups.size())
        return ids;

    for (const auto& tool : m_toolGroups[groupIndex].tools)
        ids.append(tool.id);
    return ids;
}
