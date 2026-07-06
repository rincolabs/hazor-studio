#pragma once

#include <QToolButton>

class QMenu;
class QSlider;
class QSpinBox;
class QWidgetAction;

class DropdownSlider : public QToolButton {
    Q_OBJECT

public:
    explicit DropdownSlider(const QString& label,
                            int minValue,
                            int maxValue,
                            int value,
                            const QString& suffix = QString(),
                            int singleStep = 1,
                            int popupWidth = 220,
                            QWidget* parent = nullptr);

    int value() const;
    void setValue(int value);
    void setRange(int minValue, int maxValue);
    void setSuffix(const QString& suffix);
    void setLabel(const QString& label);

signals:
    void valueChanged(int value);

private:
    void updateButtonText();

    QString m_label;
    QString m_suffix;
    QMenu* m_menu = nullptr;
    QSlider* m_slider = nullptr;
    QSpinBox* m_spin = nullptr;
    QWidgetAction* m_action = nullptr;
};

