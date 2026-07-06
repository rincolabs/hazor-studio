#include <QApplication>
#include <QMenu>
#include <QToolButton>
#include <QtTest>
#include "core/ColorEngine.hpp"
#include "ui/ToolsPanel.hpp"
#include "ui/ColorSwapWidget.hpp"

class TestToolsPanel : public QObject {
    Q_OBJECT

private slots:

    void test_creates_grouped_tool_buttons()
    {
        ColorEngine engine;
        ToolsPanel panel(&engine);
        auto btns = panel.findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        QCOMPARE(btns.size(), 13);

        auto menus = panel.findChildren<QMenu*>();
        QCOMPARE(menus.size(), 3);
        QCOMPARE(menus[0]->actions().size(), 2);
        QCOMPARE(menus[1]->actions().size(), 3);
        QCOMPARE(menus[2]->actions().size(), 2);
        for (auto* menu : menus) {
            QVERIFY(menu->styleSheet().contains("border: 1px solid"));
            QVERIFY(menu->styleSheet().contains("margin: 0px"));
            QVERIFY(menu->testAttribute(Qt::WA_TranslucentBackground));
            QVERIFY(!menu->graphicsEffect());
            QVERIFY(menu->windowFlags() & Qt::FramelessWindowHint);
            QVERIFY(menu->windowFlags() & Qt::NoDropShadowWindowHint);
        }

        for (int index : {1, 2, 3}) {
            QVERIFY(!btns[index]->menu());
            QVERIFY(btns[index]->popupMode() != QToolButton::MenuButtonPopup);
            QVERIFY(btns[index]->popupMode() != QToolButton::InstantPopup);
            QCOMPARE(btns[index]->minimumSize(), QSize(34, 34));
            QCOMPARE(btns[index]->maximumSize(), QSize(34, 34));
        }
    }

    void test_button_tooltips()
    {
        ColorEngine engine;
        ToolsPanel panel(&engine);
        auto btns = panel.findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(btns.size() >= 13);
        QVERIFY(btns[0]->toolTip().contains("Move"));
        QVERIFY(btns[1]->toolTip().contains("Rectangular"));
        QVERIFY(btns[2]->toolTip().contains("Lasso"));
        QVERIFY(btns[3]->toolTip().contains("Magic"));
        QVERIFY(btns[7]->toolTip().contains("Fill"));
        QVERIFY(btns[8]->toolTip().contains("Eye"));
    }

    void test_set_active_tool()
    {
        ColorEngine engine;
        ToolsPanel panel(&engine);
        panel.setActiveTool(3);
        auto btns = panel.findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(btns[1]->isChecked());
    }

    void test_set_active_sub_tool_updates_group()
    {
        ColorEngine engine;
        ToolsPanel panel(&engine);
        panel.setActiveSubTool(3, 6);

        auto btns = panel.findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);
        QVERIFY(btns[2]->isChecked());
        QVERIFY(btns[2]->toolTip().contains("Polygonal"));
        QCOMPARE(panel.activeSubToolForTool(3), 6);
        QCOMPARE(panel.activeSubToolsForTool(3), QVector<int>({2, 6, 5}));
    }

    void test_select_tool_remembers_clicked_group_after_simple_tool()
    {
        ColorEngine engine;
        ToolsPanel panel(&engine);
        auto btns = panel.findChildren<QToolButton*>(QString(), Qt::FindDirectChildrenOnly);

        panel.setActiveTool(ToolsPanel::Crop);
        QTest::mouseClick(btns[3], Qt::LeftButton);
        panel.setActiveTool(ToolsPanel::Select);

        QVERIFY(btns[3]->isChecked());
        QCOMPARE(panel.activeSubToolForTool(ToolsPanel::Select), 3);
        QCOMPARE(panel.activeSubToolsForTool(ToolsPanel::Select), QVector<int>({3, 4}));
    }

    void test_contains_color_swap_widget()
    {
        ColorEngine engine;
        ToolsPanel panel(&engine);
        auto swapWidgets = panel.findChildren<ColorSwapWidget*>();
        QCOMPARE(swapWidgets.size(), 1);
    }
};

QTEST_MAIN(TestToolsPanel)
#include "test_toolspanel.moc"
