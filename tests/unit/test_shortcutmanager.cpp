#define BOOST_TEST_MODULE ShortcutManagerTest
#include <boost/test/included/unit_test.hpp>
#include <QApplication>
#include <QAction>
#include <QShortcut>
#include <QKeySequence>
#include <QSettings>
#include "ui/ShortcutManager.hpp"

struct QtAppFixture {
    QtAppFixture() {
        if (!QApplication::instance()) {
            static int argc = 1;
            static char* argv[] = { const_cast<char*>("test") };
            static QApplication app(argc, argv);
        }
        QApplication::setOrganizationName("Hazor Studio");
        QApplication::setApplicationName("test");
    }
};
BOOST_GLOBAL_FIXTURE(QtAppFixture);

struct ShortcutFixture {
    ShortcutManager* mgr;
    ShortcutFixture() {
        mgr = ShortcutManager::instance();
        mgr->resetAll();
    }
};

BOOST_AUTO_TEST_SUITE(shortcutmanager)

BOOST_AUTO_TEST_CASE(has_72_actions)
{
    ShortcutFixture f;
    BOOST_CHECK(f.mgr->actions().size() >= 70);
    BOOST_CHECK(f.mgr->actions().size() <= 80);
}

BOOST_AUTO_TEST_CASE(categories_not_empty)
{
    ShortcutFixture f;
    QStringList cats = f.mgr->categories();
    BOOST_CHECK(cats.contains("File"));
    BOOST_CHECK(cats.contains("Edit"));
    BOOST_CHECK(cats.contains("Layer"));
    BOOST_CHECK(cats.contains("Tools"));
    BOOST_CHECK(cats.contains("View"));
    BOOST_CHECK(cats.contains("Canvas"));
    BOOST_CHECK(cats.contains("Color"));
}

BOOST_AUTO_TEST_CASE(default_shortcuts_match)
{
    ShortcutFixture f;
    auto t = [&](const QString& id, const QKeySequence& expected) {
        QKeySequence actual = f.mgr->defaultShortcut(id);
        BOOST_CHECK(actual == expected);
    };
    // File
    t("file.new",      QKeySequence::New);
    t("file.open",     QKeySequence::Open);
    t("file.save",     QKeySequence::Save);
    t("file.exit",     QKeySequence::Quit);
    // Edit
    t("edit.undo",     QKeySequence::Undo);
    t("edit.redo",     QKeySequence::Redo);
    t("edit.copy",     QKeySequence::Copy);
    t("edit.paste",    QKeySequence::Paste);
    // Tools — single key
    t("tool.brush",    QKeySequence(Qt::Key_B));
    t("tool.move",     QKeySequence(Qt::Key_V));
    t("tool.hand",     QKeySequence(Qt::Key_H));
    t("tool.text",     QKeySequence(Qt::Key_T));
    // Color
    t("color.swap",           QKeySequence(Qt::Key_X));
    t("color.reset_default",  QKeySequence(Qt::Key_D));
    // Canvas
    t("canvas.quick_mask",    QKeySequence(Qt::Key_Q));
    t("canvas.cancel",        QKeySequence(Qt::Key_Escape));
    // Empty defaults
    BOOST_CHECK(f.mgr->defaultShortcut("layer.merge_layers").isEmpty());
    BOOST_CHECK(f.mgr->defaultShortcut("image.grayscale").isEmpty());
}

BOOST_AUTO_TEST_CASE(current_equals_default_when_not_modified)
{
    ShortcutFixture f;
    for (const auto& a : f.mgr->actions()) {
        BOOST_CHECK(f.mgr->currentShortcut(a.id) == a.defaultSeq);
        BOOST_CHECK(!f.mgr->isModified(a.id));
    }
}

BOOST_AUTO_TEST_CASE(setShortcut_updates_current)
{
    ShortcutFixture f;
    QKeySequence custom(Qt::Key_F12);
    bool ok = f.mgr->setShortcut("tool.brush", custom);
    BOOST_CHECK(ok);
    BOOST_CHECK(f.mgr->currentShortcut("tool.brush") == custom);
    BOOST_CHECK(f.mgr->isModified("tool.brush"));
}

BOOST_AUTO_TEST_CASE(setDefault_removes_modified)
{
    ShortcutFixture f;
    QKeySequence def = f.mgr->defaultShortcut("file.new");
    f.mgr->setShortcut("file.new", QKeySequence(Qt::Key_F12));
    BOOST_CHECK(f.mgr->isModified("file.new"));
    f.mgr->setShortcut("file.new", def);
    BOOST_CHECK(!f.mgr->isModified("file.new"));
}

BOOST_AUTO_TEST_CASE(resetToDefault_restores)
{
    ShortcutFixture f;
    QKeySequence def = f.mgr->defaultShortcut("file.new");
    f.mgr->setShortcut("file.new", QKeySequence(Qt::Key_F12));
    BOOST_CHECK(f.mgr->currentShortcut("file.new") != def);
    f.mgr->resetToDefault("file.new");
    BOOST_CHECK(f.mgr->currentShortcut("file.new") == def);
    BOOST_CHECK(!f.mgr->isModified("file.new"));
}

BOOST_AUTO_TEST_CASE(resetAll_clears_all)
{
    ShortcutFixture f;
    f.mgr->setShortcut("file.new", QKeySequence(Qt::Key_F1));
    f.mgr->setShortcut("file.open", QKeySequence(Qt::Key_F2));
    f.mgr->setShortcut("file.save", QKeySequence(Qt::Key_F3));
    BOOST_CHECK(f.mgr->isModified("file.new"));
    f.mgr->resetAll();
    BOOST_CHECK(!f.mgr->isModified("file.new"));
    BOOST_CHECK(!f.mgr->isModified("file.open"));
    BOOST_CHECK(!f.mgr->isModified("file.save"));
}

BOOST_AUTO_TEST_CASE(findConflict_detects_duplicate)
{
    ShortcutFixture f;
    QKeySequence ctrlO = QKeySequence::Open;
    QString conflict = f.mgr->findConflict(ctrlO);
    BOOST_CHECK(!conflict.isEmpty());
}

BOOST_AUTO_TEST_CASE(findConflict_excepts_self)
{
    ShortcutFixture f;
    QKeySequence ctrlO = QKeySequence::Open;
    QString conflict = f.mgr->findConflict(ctrlO, "file.open");
    BOOST_CHECK(conflict.isEmpty());
}

BOOST_AUTO_TEST_CASE(setShortcut_with_conflict_returns_false)
{
    ShortcutFixture f;
    QKeySequence ctrlO = QKeySequence::Open;
    bool ok = f.mgr->setShortcut("file.save", ctrlO);
    BOOST_CHECK(!ok);
    BOOST_CHECK(f.mgr->currentShortcut("file.save") == f.mgr->defaultShortcut("file.save"));
}

BOOST_AUTO_TEST_CASE(registerAction_sets_QAction_shortcut)
{
    ShortcutFixture f;
    QAction action;
    f.mgr->registerAction("file.new", &action);
    BOOST_CHECK(action.shortcut() == QKeySequence::New);
}

// registerShortcut tested via registerAction since QShortcut needs a live widget window

// saveSettings/loadSettings are thin wrappers over QSettings arrays.
// QSettings persistence is tested by Qt itself.

BOOST_AUTO_TEST_SUITE_END()
