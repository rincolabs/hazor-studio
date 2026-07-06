#pragma once

#include <QWidget>

class QSlider;
class QDoubleSpinBox;
class QLabel;

class ParamRow : public QWidget {
    Q_OBJECT
public:
    explicit ParamRow(const QString& label, double min, double max,
                      double defaultValue, int decimals = 2,
                      bool logarithmic = false, QWidget* parent = nullptr);

    double value() const;
    void setValue(double v);
    void resetToDefault();
    void setRange(double min, double max);
    void setDecimals(int decimals);

signals:
    void valueChanged(double value);

private slots:
    void onSliderChanged(int pos);
    void onSpinChanged(double val);

private:
    void updateSliderFromSpin();
    void updateSpinFromSlider();
    double sliderToValue(int sliderPos) const;
    int valueToSlider(double val) const;

    QLabel* m_label = nullptr;
    QSlider* m_slider = nullptr;
    QDoubleSpinBox* m_spin = nullptr;

    double m_min = 0.0;
    double m_max = 1.0;
    double m_defaultValue = 0.0;
    int m_decimals = 2;
    bool m_logarithmic = false;

    static constexpr int kSliderRange = 10000;
};
