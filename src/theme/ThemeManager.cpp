#include "ThemeManager.hpp"
#include "DarkerTheme.hpp"
#include "ClassicTheme.hpp"

#include <QSettings>

namespace {
Theme* createThemeFromIndex(int index, QObject* parent)
{
    return index == 1
        ? static_cast<Theme*>(new DarkerTheme(parent))
        : static_cast<Theme*>(new ClassicTheme(parent));
}
} // namespace

ThemeManager* ThemeManager::instance()
{
    static ThemeManager inst;
    return &inst;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    loadSettings();
}

ThemeManager::~ThemeManager()
{
}

void ThemeManager::setTheme(Theme* theme)
{
    if (m_current && m_current != theme) {
        m_current->deleteLater();
    }
    m_current = theme;
    emit themeChanged();
}

void ThemeManager::loadSettings()
{
    QSettings settings;
    const int savedTheme = settings.value("theme", 0).toInt();
    auto* theme = createThemeFromIndex(savedTheme, this);

    const int savedOpacity = settings.value("iconOpacity", -1).toInt();
    if (savedOpacity >= 0)
        theme->iconOpacity = savedOpacity / 100.0f;

    if (m_current && m_current != theme)
        m_current->deleteLater();
    m_current = theme;
}
