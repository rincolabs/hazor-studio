#include "theme/WebThemeVariables.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

namespace webtheme {

QVariantMap variables()
{
    const auto* theme = ThemeManager::instance()->current();
    QVariantMap values;
    if (!theme)
        return values;

    values.insert(QStringLiteral("background"), theme->colorPanelBackground.name());
    values.insert(QStringLiteral("surface"), theme->colorSurface.name());
    values.insert(QStringLiteral("surfaceDark"), theme->colorSurfaceDark.name());
    values.insert(QStringLiteral("surfaceHover"), theme->colorSurfaceHover.name());
    values.insert(QStringLiteral("surfaceSelected"), theme->colorSurfaceSelected.name());
    values.insert(QStringLiteral("text"), theme->colorTextPrimary.name());
    values.insert(QStringLiteral("textBright"), theme->colorTextBright.name());
    values.insert(QStringLiteral("textSecondary"), theme->colorTextSecondary.name());
    values.insert(QStringLiteral("textDisabled"), theme->colorTextDisabled.name());
    values.insert(QStringLiteral("border"), theme->colorBorder.name());
    values.insert(QStringLiteral("borderFocus"), theme->colorBorderFocus.name());
    values.insert(QStringLiteral("accent"), theme->colorAccent.name());
    values.insert(QStringLiteral("accentHover"), theme->colorAccentHover.name());
    values.insert(QStringLiteral("danger"), theme->colorDanger.name());
    values.insert(QStringLiteral("success"), theme->colorSuccess.name());
    values.insert(QStringLiteral("warning"), theme->colorWarning.name());
    values.insert(QStringLiteral("fontFamily"), theme->fontFamily);
    values.insert(QStringLiteral("fontFamilyMono"), theme->fontFamilyMono);
    values.insert(QStringLiteral("fontSize"), theme->fontSizeMD);
    values.insert(QStringLiteral("radius"), theme->radiusMD);
    return values;
}

} // namespace webtheme
