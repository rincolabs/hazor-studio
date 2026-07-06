#include <QApplication>
#include <QtTest>
#include <QComboBox>
#include <QSlider>
#include <QSpinBox>
#include <QLineEdit>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QListWidget>
#include <QStackedWidget>
#include <QSettings>

#include "ui/ShortcutManager.hpp"
#include "ui/GeneralSettingsPage.hpp"
#include "ui/ShortcutSettingsPage.hpp"
#include "ui/AppSettingsDialog.hpp"
#include "ui/AgentConfigWidget.hpp"
#include "agent/AgentPresetManager.hpp"
#include "theme/ThemeManager.hpp"
#include "theme/DarkerTheme.hpp"
#include "theme/ClassicTheme.hpp"

// ============================================================
// TestGeneralSettingsPage
// ============================================================

class TestGeneralSettingsPage : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        {
            QSettings s("Hazor Studio", "General");
            s.clear();
            s.sync();
        }
        {
            QSettings s("Hazor Studio", "Shortcuts");
            s.clear();
            s.sync();
        }
    }

    void cleanup()
    {
        auto* th = ThemeManager::instance()->current();
        th->iconOpacity = 0.85f;
        if (dynamic_cast<DarkerTheme*>(th))
            ThemeManager::instance()->setTheme(new ClassicTheme(ThemeManager::instance()));
    }

    void has_theme_combo()
    {
        GeneralSettingsPage page;
        auto* combo = page.findChild<QComboBox*>();
        QVERIFY(combo != nullptr);
    }

    void theme_combo_has_default_and_classic()
    {
        GeneralSettingsPage page;
        auto* combo = page.findChild<QComboBox*>();
        QVERIFY(combo != nullptr);
        QCOMPARE(combo->count(), 2);
        QCOMPARE(combo->itemText(0), QStringLiteral("Unity Photo Default"));
        QCOMPARE(combo->itemText(1), QStringLiteral("Unity Photo Classic"));
    }

    void opacity_slider_default_85()
    {
        GeneralSettingsPage page;
        auto* slider = page.findChild<QSlider*>();
        QVERIFY(slider != nullptr);
        QCOMPARE(slider->value(), 85);
    }

    void undo_limit_default_500()
    {
        GeneralSettingsPage page;
        auto spins = page.findChildren<QSpinBox*>();
        bool found = false;
        for (auto* spin : spins) {

            if (spin->suffix().contains("step")) {
                QCOMPARE(spin->value(), 500);
                found = true;
            }
        }
        QVERIFY(found);
    }

    void loadSettings_reads_saved_theme()
    {
        QSettings("Hazor Studio", "General").setValue("theme", 1);

        GeneralSettingsPage page;
        auto* combo = page.findChild<QComboBox*>();
        QCOMPARE(combo->currentIndex(), 1);
    }

    void saveSettings_persists_theme()
    {
        GeneralSettingsPage page;
        auto* combo = page.findChild<QComboBox*>();
        combo->setCurrentIndex(1);

        page.saveSettings();

        QSettings settings("Hazor Studio", "General");
        QCOMPARE(settings.value("theme").toInt(), 1);
    }

    void saveSettings_persists_opacity()
    {
        GeneralSettingsPage page;
        auto* slider = page.findChild<QSlider*>();
        slider->setValue(72);

        page.saveSettings();

        QSettings settings("Hazor Studio", "General");
        QCOMPARE(settings.value("iconOpacity").toInt(), 72);
    }
};

// ============================================================
// TestShortcutSettingsPage
// ============================================================

class TestShortcutSettingsPage : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        ShortcutManager::instance()->resetAll();
    }

    void tree_has_all_categories()
    {
        ShortcutSettingsPage page;
        auto* tree = page.findChild<QTreeWidget*>();
        QVERIFY(tree != nullptr);
        QVERIFY(tree->topLevelItemCount() >= 8);

        QStringList catNames;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            catNames << tree->topLevelItem(i)->text(0);
        QVERIFY(catNames.contains("File"));
        QVERIFY(catNames.contains("Edit"));
        QVERIFY(catNames.contains("Tools"));
        QVERIFY(catNames.contains("Canvas"));
        QVERIFY(catNames.contains("Color"));
    }

    void categories_are_expanded()
    {
        ShortcutSettingsPage page;
        auto* tree = page.findChild<QTreeWidget*>();
        QVERIFY(tree->topLevelItemCount() > 0);
        QVERIFY(tree->topLevelItem(0)->isExpanded());
    }

    void tree_has_two_columns()
    {
        ShortcutSettingsPage page;
        auto* tree = page.findChild<QTreeWidget*>();
        QCOMPARE(tree->columnCount(), 2);
        QCOMPARE(tree->headerItem()->text(0), QStringLiteral("Action"));
        QCOMPARE(tree->headerItem()->text(1), QStringLiteral("Shortcut"));
    }

    void search_filters_items()
    {
        ShortcutSettingsPage page;
        auto* tree = page.findChild<QTreeWidget*>();
        auto* search = page.findChild<QLineEdit*>();
        QVERIFY(search != nullptr);

        int totalBefore = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j)
                if (!tree->topLevelItem(i)->child(j)->isHidden())
                    ++totalBefore;

        QVERIFY(totalBefore > 0);

        search->setText("XYZ_IMPOSSIBLE_ZZZ");
        QApplication::processEvents();

        int totalAfter = 0;
        for (int i = 0; i < tree->topLevelItemCount(); ++i)
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j)
                if (!tree->topLevelItem(i)->child(j)->isHidden())
                    ++totalAfter;

        QCOMPARE(totalAfter, 0);
    }

    void search_empty_shows_all()
    {
        ShortcutSettingsPage page;
        auto* tree = page.findChild<QTreeWidget*>();
        auto* search = page.findChild<QLineEdit*>();

        search->setText("Filtered");
        QApplication::processEvents();
        search->clear();
        QApplication::processEvents();

        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            QVERIFY(!tree->topLevelItem(i)->isHidden());
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j)
                QVERIFY(!tree->topLevelItem(i)->child(j)->isHidden());
        }
    }

    void resetAll_restores_defaults()
    {
        auto* mgr = ShortcutManager::instance();
        mgr->setShortcut("tool.brush", QKeySequence(Qt::Key_F12));
        QVERIFY(mgr->isModified("tool.brush"));

        ShortcutSettingsPage page;
        auto btns = page.findChildren<QPushButton*>();
        QPushButton* resetBtn = nullptr;
        for (auto* btn : btns) {
            if (btn->text() == "Reset All") { resetBtn = btn; break; }
        }
        QVERIFY(resetBtn != nullptr);

        QTest::mouseClick(resetBtn, Qt::LeftButton);
        QCoreApplication::processEvents();

        QVERIFY(!mgr->isModified("tool.brush"));
    }

    void keyCapture_shows_dots()
    {
        ShortcutSettingsPage page;
        auto* tree = page.findChild<QTreeWidget*>();
        QVERIFY(tree != nullptr);

        QTreeWidgetItem* moveItem = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
                if (tree->topLevelItem(i)->child(j)->text(0).contains("Move")) {
                    moveItem = tree->topLevelItem(i)->child(j);
                    break;
                }
            }
        }
        QVERIFY(moveItem != nullptr);
        QVERIFY(!moveItem->text(1).isEmpty());

        emit tree->itemClicked(moveItem, 1);
        QCoreApplication::processEvents();

        QCOMPARE(moveItem->text(1), QStringLiteral("..."));

        emit tree->itemClicked(moveItem, 0);
        QCoreApplication::processEvents();
    }

    void keyCapture_Escape_cancels()
    {
        ShortcutSettingsPage page;
        auto* tree = page.findChild<QTreeWidget*>();

        QTreeWidgetItem* moveItem = nullptr;
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            for (int j = 0; j < tree->topLevelItem(i)->childCount(); ++j) {
                if (tree->topLevelItem(i)->child(j)->text(0).contains("Move")) {
                    moveItem = tree->topLevelItem(i)->child(j);
                    break;
                }
            }
        }
        QVERIFY(moveItem != nullptr);

        QString before = moveItem->text(1);

        emit tree->itemClicked(moveItem, 1);
        QCoreApplication::processEvents();
        QCOMPARE(moveItem->text(1), QStringLiteral("..."));

        QTest::keyPress(&page, Qt::Key_Escape);
        QCoreApplication::processEvents();

        QCOMPARE(moveItem->text(1), before);
    }
};

// ============================================================
// TestAppSettingsDialog
// ============================================================

class TestAppSettingsDialog : public QObject
{
    Q_OBJECT
private slots:
    void init()
    {
        QSettings("Hazor Studio", "General").setValue("undoLimit", 333);
    }

    void cleanup()
    {
        auto* th = ThemeManager::instance()->current();
        th->iconOpacity = 0.85f;
        if (dynamic_cast<DarkerTheme*>(th))
            ThemeManager::instance()->setTheme(new ClassicTheme(ThemeManager::instance()));
    }

    void has_five_sessions()
    {
        AppSettingsDialog dlg(0, nullptr, nullptr);
        auto* sidebar = dlg.findChild<QListWidget*>();
        QVERIFY(sidebar != nullptr);
        QCOMPARE(sidebar->count(), 5);
        QCOMPARE(sidebar->item(0)->text(), QStringLiteral("General"));
    }

    void opens_at_correct_page()
    {
        AppSettingsDialog dlg(AppSettingsDialog::PagePerformance, nullptr, nullptr);
        auto* sidebar = dlg.findChild<QListWidget*>();
        QCOMPARE(sidebar->currentRow(), 2);
    }

    void sidebar_navigation_switches_page()
    {
        AppSettingsDialog dlg(0, nullptr, nullptr);
        auto* sidebar = dlg.findChild<QListWidget*>();
        auto* pages = dlg.findChild<QStackedWidget*>();
        QVERIFY(pages != nullptr);

        sidebar->setCurrentRow(2);
        QCOMPARE(pages->currentIndex(), 2);

        sidebar->setCurrentRow(4);
        QCOMPARE(pages->currentIndex(), 4);

        sidebar->setCurrentRow(0);
        QCOMPARE(pages->currentIndex(), 0);
    }

    void onAccept_accepted()
    {
        AppSettingsDialog dlg(0, nullptr, nullptr);

        auto btns = dlg.findChildren<QPushButton*>();
        QPushButton* acceptBtn = nullptr;
        for (auto* btn : btns) {
            if (btn->text() == "Accept") { acceptBtn = btn; break; }
        }
        QVERIFY(acceptBtn != nullptr);

        QSignalSpy spy(&dlg, &QDialog::accepted);
        QTest::mouseClick(acceptBtn, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
    }

    void onCancel_rejected()
    {
        AppSettingsDialog dlg(0, nullptr, nullptr);

        auto btns = dlg.findChildren<QPushButton*>();
        QPushButton* cancelBtn = nullptr;
        for (auto* btn : btns) {
            if (btn->text() == "Cancel") { cancelBtn = btn; break; }
        }
        QVERIFY(cancelBtn != nullptr);

        QSignalSpy spy(&dlg, &QDialog::rejected);
        QTest::mouseClick(cancelBtn, Qt::LeftButton);
        QCOMPARE(spy.count(), 1);
    }
};

// ============================================================
// TestAgentConfigWidget
// ============================================================

class TestAgentConfigWidget : public QObject
{
    Q_OBJECT
private slots:
    void has_preset_bar()
    {
        AgentPresetManager mgr;
        AgentConfigWidget w(&mgr, nullptr);

        auto combos = w.findChildren<QComboBox*>();
        QVERIFY(combos.size() >= 1);
    }

    void has_identification_section()
    {
        AgentPresetManager mgr;
        AgentConfigWidget w(&mgr, nullptr);

        auto edits = w.findChildren<QLineEdit*>();
        QVERIFY(edits.size() >= 2);
    }

    void has_provider_section()
    {
        AgentPresetManager mgr;
        AgentConfigWidget w(&mgr, nullptr);

        auto combos = w.findChildren<QComboBox*>();
        QVERIFY(combos.size() >= 2);
    }
};

// ============================================================
// Manual main()
// ============================================================

int main(int argc, char* argv[])
{
    QApplication app(argc, argv);

    ThemeManager::instance();
    ShortcutManager::instance()->resetAll();
    QSettings("Hazor Studio", "General").clear();
    QSettings("Hazor Studio", "Shortcuts").clear();

    int status = 0;
    auto run = [&](QObject* obj) {
        status |= QTest::qExec(obj, argc, argv);
        delete obj;
    };

    run(new TestGeneralSettingsPage);
    run(new TestShortcutSettingsPage);
    run(new TestAppSettingsDialog);
    run(new TestAgentConfigWidget);

    return status;
}
#include "test_appsettings.moc"
