#define BOOST_TEST_MODULE theme
#include <boost/test/included/unit_test.hpp>

#include <QRegularExpression>
#include <QSettings>

#include "theme/ThemeManager.hpp"
#include "theme/DarkerTheme.hpp"
#include "theme/ClassicTheme.hpp"

// ── Helper — uses intermediate bools to avoid Boost.Test trying to
//     stringify QColor in macro expansion ──
static void checkTokenCompleteness(const Theme* t)
{
    BOOST_TEST(t->colorBackgroundPrimary.isValid());
    BOOST_TEST(t->colorBackgroundSecondary.isValid());
    BOOST_TEST(t->colorBackgroundTertiary.isValid());

    BOOST_TEST(t->colorSurface.isValid());
    BOOST_TEST(t->colorSurfaceHover.isValid());
    BOOST_TEST(t->colorSurfacePressed.isValid());
    BOOST_TEST(t->colorSurfaceSelected.isValid());
    BOOST_TEST(t->colorSurfaceRow.isValid());

    BOOST_TEST(t->colorTextPrimary.isValid());
    BOOST_TEST(t->colorTextBright.isValid());
    BOOST_TEST(t->colorTextSecondary.isValid());
    BOOST_TEST(t->colorTextDisabled.isValid());
    BOOST_TEST(t->colorTextInverted.isValid());

    BOOST_TEST(t->colorBorder.isValid());
    BOOST_TEST(t->colorBorderFocus.isValid());

    BOOST_TEST(t->colorAccent.isValid());
    BOOST_TEST(t->colorAccentHover.isValid());
    BOOST_TEST(t->colorAccentPressed.isValid());
    BOOST_TEST(t->colorTextSelection.isValid());

    BOOST_TEST(t->colorDanger.isValid());
    BOOST_TEST(t->colorDangerHover.isValid());
    BOOST_TEST(t->colorWarning.isValid());
    BOOST_TEST(t->colorSuccess.isValid());

    BOOST_TEST(t->colorSplashBackground.isValid());
    BOOST_TEST(t->colorSplashBorder.isValid());
    BOOST_TEST(t->colorSplashProgressTrack.isValid());
    BOOST_TEST(t->colorSplashProgressStart.isValid());
    BOOST_TEST(t->colorSplashProgressEnd.isValid());
    BOOST_TEST(t->colorSplashText.isValid());
    BOOST_TEST(t->colorSplashVersion.isValid());

    BOOST_TEST(t->spaceXS > 0);
    BOOST_TEST(t->spaceSM > 0);
    BOOST_TEST(t->spaceMD > 0);
    BOOST_TEST(t->spaceLG > 0);
    BOOST_TEST(t->spaceXL > 0);
    BOOST_TEST(t->spaceXXL > 0);

    BOOST_TEST(t->radiusSM >= 0);
    BOOST_TEST(t->radiusMD >= 0);
    BOOST_TEST(t->radiusLG >= 0);

    BOOST_TEST(t->borderThinWidth >= 0);
    BOOST_TEST(t->borderNormalWidth >= 0);

    BOOST_TEST(!t->fontFamilyMono.isEmpty());
    BOOST_TEST(t->fontSizeXS > 0);
    BOOST_TEST(t->fontSizeSM > 0);
    BOOST_TEST(t->fontSizeMD > 0);
}

// ── Tests ──

BOOST_AUTO_TEST_CASE(theme_manager_singleton)
{
    auto* a = ThemeManager::instance();
    auto* b = ThemeManager::instance();
    BOOST_TEST(a == b);
}

BOOST_AUTO_TEST_CASE(theme_default_is_unity_photo_default)
{
    auto* t = ThemeManager::instance()->current();
    BOOST_TEST(t != nullptr);
    BOOST_TEST(dynamic_cast<ClassicTheme*>(t) != nullptr);
}

BOOST_AUTO_TEST_CASE(default_theme_all_tokens_valid)
{
    ClassicTheme dt;
    checkTokenCompleteness(&dt);
}

BOOST_AUTO_TEST_CASE(classic_theme_all_tokens_valid)
{
    DarkerTheme lt;
    checkTokenCompleteness(&lt);
}

BOOST_AUTO_TEST_CASE(global_stylesheet_non_empty)
{
    ClassicTheme dt;
    QString qss = dt.globalStyleSheet();
    BOOST_TEST(!qss.isEmpty());
    BOOST_TEST(qss.contains("QMainWindow"));
    BOOST_TEST(qss.contains("QPushButton"));
    BOOST_TEST(qss.contains("QComboBox"));
}

BOOST_AUTO_TEST_CASE(combo_box_dropdown_is_flat)
{
    ClassicTheme dt;
    const QString qss = dt.globalStyleSheet();

    BOOST_TEST(qss.contains("QComboBox::drop-down"));
    BOOST_TEST(qss.contains("QComboBox::drop-down { border: none; background: transparent;"));
    BOOST_TEST(qss.contains("QComboBox::down-arrow"));
    BOOST_TEST(qss.contains(":/icons/combo-arrow.xpm"));
    BOOST_TEST(qss.contains("QComboBox QAbstractItemView {"));
    BOOST_TEST(qss.contains("padding: 4px;"));
    BOOST_TEST(qss.contains("outline: none;"));
    BOOST_TEST(qss.contains("QComboBox QAbstractItemView QWidget { background: transparent; border: none; }"));
    BOOST_TEST(qss.contains("QComboBox QAbstractItemView::item {"));
    BOOST_TEST(qss.contains("QComboBox QAbstractItemView::item:selected {"));
}

BOOST_AUTO_TEST_CASE(default_vs_classic_stylesheets_differ)
{
    ClassicTheme dt;
    DarkerTheme lt;
    BOOST_TEST(dt.globalStyleSheet() != lt.globalStyleSheet());
}

BOOST_AUTO_TEST_CASE(widget_stylesheets_non_empty)
{
    ClassicTheme dt;

    BOOST_TEST(!dt.titleBarStyleSheet().isEmpty());
    BOOST_TEST(dt.titleBarStyleSheet().contains("TitleBar"));

    BOOST_TEST(!dt.layerPanelStyleSheet().isEmpty());
    BOOST_TEST(dt.layerPanelStyleSheet().contains("QTreeWidget"));

    BOOST_TEST(!dt.layerItemDelegateEditorStyleSheet().isEmpty());
    BOOST_TEST(dt.layerItemDelegateEditorStyleSheet().contains("QLineEdit"));

    BOOST_TEST(!dt.historyPanelStyleSheet().isEmpty());
    BOOST_TEST(dt.historyPanelStyleSheet().contains("QPushButton"));

    BOOST_TEST(!dt.agentPanelStyleSheet().isEmpty());
    BOOST_TEST(dt.agentPanelStyleSheet().contains("QComboBox"));

    BOOST_TEST(!dt.agentConfigDialogStyleSheet().isEmpty());
    BOOST_TEST(dt.agentConfigDialogStyleSheet().contains("QGroupBox"));

    BOOST_TEST(!dt.brushDynamicsStyleSheet().isEmpty());
    BOOST_TEST(dt.brushDynamicsStyleSheet().contains("QGroupBox"));

    BOOST_TEST(!dt.exportDialogStyleSheet().isEmpty());
    BOOST_TEST(dt.exportDialogStyleSheet().contains("QComboBox"));
}

BOOST_AUTO_TEST_CASE(no_unresolved_placeholders)
{
    ClassicTheme dt;
    QRegularExpression re(QStringLiteral(R"(%[\w]+)"));

    auto check = [&](const QString& qss, const char* name) {
        auto matches = re.globalMatch(qss);
        if (matches.hasNext())
            BOOST_TEST_INFO(name);
        BOOST_TEST(!matches.hasNext());
    };

    check(dt.globalStyleSheet(),             "globalStyleSheet");
    check(dt.titleBarStyleSheet(),           "titleBarStyleSheet");
    check(dt.layerPanelStyleSheet(),         "layerPanelStyleSheet");
    check(dt.layerItemDelegateEditorStyleSheet(), "layerItemDelegateEditorStyleSheet");
    check(dt.historyPanelStyleSheet(),       "historyPanelStyleSheet");
    check(dt.agentPanelStyleSheet(),         "agentPanelStyleSheet");
    check(dt.agentConfigDialogStyleSheet(),  "agentConfigDialogStyleSheet");
    check(dt.brushDynamicsStyleSheet(),      "brushDynamicsStyleSheet");
    check(dt.exportDialogStyleSheet(),       "exportDialogStyleSheet");
}

BOOST_AUTO_TEST_CASE(set_theme_swaps_colors)
{
    ClassicTheme dt;
    DarkerTheme lt;

    BOOST_TEST(dt.colorBackgroundPrimary.name() != lt.colorBackgroundPrimary.name());
    BOOST_TEST(dt.colorTextPrimary.name() != lt.colorTextPrimary.name());
    BOOST_TEST(dt.colorAccent.name() != lt.colorAccent.name());
}

BOOST_AUTO_TEST_CASE(theme_manager_loads_saved_theme_settings)
{
    QSettings settings("Hazor Studio", "General");
    settings.setValue("theme", 1);
    settings.setValue("iconOpacity", 66);
    settings.sync();

    auto* mgr = ThemeManager::instance();
    mgr->loadSettings();

    BOOST_TEST(dynamic_cast<DarkerTheme*>(mgr->current()) != nullptr);
    BOOST_TEST(mgr->current()->iconOpacity == 0.66f, boost::test_tools::tolerance(0.001f));

    settings.clear();
    settings.sync();
    mgr->loadSettings();
}
