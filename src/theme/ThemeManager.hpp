#pragma once

#include <QObject>

class Theme;

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager* instance();

    Theme* current() const { return m_current; }

    void loadSettings();
    void setTheme(Theme* theme);

signals:
    void themeChanged();

private:
    ThemeManager(QObject* parent = nullptr);
    ~ThemeManager() override;

    Theme* m_current = nullptr;
};
