#pragma once

#include "core/GuideTypes.hpp"

#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QPushButton;
class QSlider;
class QSpinBox;

class RulerGuideSettingsPage : public QWidget {
public:
    explicit RulerGuideSettingsPage(QWidget* parent = nullptr);

    void loadSettings();
    void saveSettings();

private:
    QWidget* makeOpacityRow();
    void applyStyleSheet();
    void updateColorButton();

    QSlider*   m_iconOpacitySlider = nullptr;
    QLabel*    m_opacityLabel = nullptr;
    QCheckBox* m_showColorPaletteCb = nullptr;
    QCheckBox* m_showRulersCb = nullptr;
    QComboBox* m_unitCombo = nullptr;
    QSpinBox* m_majorTickSpin = nullptr;
    QSpinBox* m_minorDivSpin = nullptr;
    QSpinBox* m_rulerSizeSpin = nullptr;
    QCheckBox* m_mouseIndicatorCb = nullptr;

    QCheckBox* m_showGuidesCb = nullptr;
    QCheckBox* m_lockGuidesCb = nullptr;
    QCheckBox* m_guidesSnapCb = nullptr;
    QPushButton* m_guideColorBtn = nullptr;
    QSlider* m_guideOpacitySlider = nullptr;

    QCheckBox* m_snapEnabledCb = nullptr;
    QCheckBox* m_snapCanvasBoundsCb = nullptr;
    QCheckBox* m_snapCanvasCenterCb = nullptr;
    QCheckBox* m_snapGuidesCb = nullptr;
    QSpinBox* m_snapToleranceSpin = nullptr;
    QCheckBox* m_snapIndicatorsCb = nullptr;

    QColor m_guideColor;
};
