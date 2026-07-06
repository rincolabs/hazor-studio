#include <QApplication>
#include <QtTest>
#include <QSignalSpy>
#include <QMenu>
#include <QToolButton>
#include <QPushButton>
#include <QWidgetAction>
#include "ui/BrushPresetPicker.hpp"
#include "brush/BrushPresetManager.hpp"

class TestBrushPresetPicker : public QObject {
    Q_OBJECT

private:
    BrushPresetManager* m_manager = nullptr;
    BrushPresetPicker* m_picker = nullptr;

private slots:

    void init()
    {
        m_manager = new BrushPresetManager();

        BrushPreset p1;
        p1.name = "TestA";
        p1.settings.size = 10;
        p1.settings.hardness = 1.0f;
        p1.settings.color = Qt::red;
        m_manager->addPreset(p1);

        BrushPreset p2;
        p2.name = "TestB";
        p2.settings.size = 20;
        p2.settings.hardness = 0.5f;
        p2.settings.color = Qt::blue;
        m_manager->addPreset(p2);

        m_picker = new BrushPresetPicker();
        m_picker->show();
        QApplication::processEvents();
    }

    void cleanup()
    {
        delete m_picker;
        m_picker = nullptr;
        delete m_manager;
        m_manager = nullptr;
    }

    void test_construct_is_tool_button()
    {
        QVERIFY(m_picker);
        QCOMPARE(m_picker->popupMode(), QToolButton::MenuButtonPopup);
        QVERIFY(m_picker->toolTip().contains("Brush Preset"));
        QVERIFY(m_picker->minimumHeight() >= 34);
    }

    void test_set_preset_manager_creates_menu()
    {
        m_picker->setPresetManager(m_manager);
        QApplication::processEvents();
        QVERIFY(m_picker->menu());
    }

    void test_set_current_preset_updates_icon_and_tooltip()
    {
        m_picker->setPresetManager(m_manager);
        BrushPreset p;
        p.name = "Round 10px";
        p.settings.size = 10;
        p.settings.hardness = 0.8f;
        p.settings.color = Qt::green;
        m_picker->setCurrentPreset(p);

        QVERIFY(!m_picker->icon().isNull());
        QVERIFY(m_picker->toolTip().contains("Round 10px"));
        QVERIFY(m_picker->toolTip().contains("10 px"));
    }

    void test_grid_contains_thumbnails_and_buttons()
    {
        m_picker->setPresetManager(m_manager);
        QApplication::processEvents();

        QMenu* menu = m_picker->menu();
        QVERIFY(menu);
        auto* widgetAction = qobject_cast<QWidgetAction*>(menu->actions().first());
        QVERIFY(widgetAction);
        QWidget* container = widgetAction->defaultWidget();
        QVERIFY(container);

        auto btns = container->findChildren<QPushButton*>();
        // TestA + TestB + Save (+) + Load (PNG) = 4 buttons
        QCOMPARE(btns.size(), 4);
    }

    void test_preset_selected_signal_on_thumbnail_click()
    {
        m_picker->setPresetManager(m_manager);
        QApplication::processEvents();

        QSignalSpy spy(m_picker, &BrushPresetPicker::presetSelected);

        QMenu* menu = m_picker->menu();
        QVERIFY(menu);
        auto* widgetAction = qobject_cast<QWidgetAction*>(menu->actions().first());
        QVERIFY(widgetAction);
        QWidget* container = widgetAction->defaultWidget();
        QVERIFY(container);

        auto btns = container->findChildren<QPushButton*>();
        QPushButton* thumbnailBtn = nullptr;
        for (auto* btn : btns) {
            QString t = btn->text().trimmed();
            if (t != "+" && t != "PNG") {
                thumbnailBtn = btn;
                break;
            }
        }
        QVERIFY2(thumbnailBtn, "Thumbnail button not found");

        thumbnailBtn->click();
        QCOMPARE(spy.count(), 1);
    }

    void test_save_button_emits_signal()
    {
        m_picker->setPresetManager(m_manager);
        QApplication::processEvents();

        QSignalSpy spy(m_picker, &BrushPresetPicker::savePresetRequested);

        QMenu* menu = m_picker->menu();
        QVERIFY(menu);
        auto* widgetAction = qobject_cast<QWidgetAction*>(menu->actions().first());
        QVERIFY(widgetAction);
        QWidget* container = widgetAction->defaultWidget();
        QVERIFY(container);

        auto btns = container->findChildren<QPushButton*>();
        QPushButton* saveBtn = nullptr;
        for (auto* btn : btns) {
            if (btn->text().trimmed() == "+") {
                saveBtn = btn;
                break;
            }
        }
        QVERIFY2(saveBtn, "Save button (+) not found");

        saveBtn->click();
        QCOMPARE(spy.count(), 1);
    }

    void test_render_thumbnail_round()
    {
        BrushPreset p;
        p.name = "Round Test";
        p.settings.size = 20;
        p.settings.hardness = 0.8f;
        p.settings.color = QColor(200, 50, 50);
        p.settings.type = BrushType::Round;

        QPixmap thumb = m_picker->renderThumbnail(p);
        QCOMPARE(thumb.width(), 64);
        QCOMPARE(thumb.height(), 64);
        QVERIFY(!thumb.isNull());
        QVERIFY(thumb.toImage().pixelColor(32, 32).alpha() > 0);
    }

    void test_render_thumbnail_square()
    {
        BrushPreset p;
        p.name = "Square Test";
        p.settings.size = 30;
        p.settings.hardness = 1.0f;
        p.settings.color = Qt::blue;
        p.settings.type = BrushType::Square;

        QPixmap thumb = m_picker->renderThumbnail(p);
        QCOMPARE(thumb.width(), 64);
        QCOMPARE(thumb.height(), 64);
        QVERIFY(thumb.toImage().pixelColor(32, 32).alpha() > 0);
    }

    void test_add_preset_emits_presets_changed_from_manager()
    {
        QSignalSpy spy(m_manager, &BrushPresetManager::presetsChanged);

        BrushPreset p;
        p.name = "NewPreset";
        p.settings.size = 50;
        m_manager->addPreset(p);

        QCOMPARE(spy.count(), 1);
    }

    void test_remove_preset_emits_presets_changed()
    {
        QSignalSpy spy(m_manager, &BrushPresetManager::presetsChanged);
        m_manager->removePreset("TestA");
        QCOMPARE(spy.count(), 1);
    }
};

QTEST_MAIN(TestBrushPresetPicker)
#include "test_brushpresetpicker.moc"
