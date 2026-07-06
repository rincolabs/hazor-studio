#pragma once

#include <QWidget>

class QComboBox;
class QSlider;
class QSpinBox;
class QCheckBox;

class GeneralSettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit GeneralSettingsPage(QWidget* parent = nullptr);

    void loadSettings();
    void saveSettings();

signals:
    void settingsChanged();

private:
    void applyTheme();
    void onThemeChanged(int index);

    QComboBox* m_themeCombo = nullptr;
    QSpinBox* m_undoLimitSpin = nullptr;
    QSpinBox* m_autoSaveSpin = nullptr;
    QSpinBox* m_canvasWSpin = nullptr;
    QSpinBox* m_canvasHSpin = nullptr;
    QComboBox* m_langCombo = nullptr;
};
