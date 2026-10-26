#pragma once

#include <QWidget>

class QSlider;
class QDoubleSpinBox;
class QLabel;

class ParamRow : public QWidget {
    Q_OBJECT
public:
    // centerZeroLog gives a bipolar range (min < 0 < max) a zero that always
    // lands exactly at the slider's visual middle, each half (min..0 and
    // 0..max) independently log-curved by logBase — useful for a control
    // whose valid range is asymmetric but whose neutral value should still
    // sit centered. Requires min < 0 < max; appended after `parent` (rather
    // than before it) so every existing positional call site — which passes
    // `parent` 7th and nothing after — keeps compiling unchanged.
    explicit ParamRow(const QString& label, double min, double max,
                      double defaultValue, int decimals = 2,
                      bool logarithmic = false, QWidget* parent = nullptr,
                      bool centerZeroLog = false, double logBase = 2.0);

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
    bool m_centerZeroLog = false;
    double m_logBase = 2.0;

    static constexpr int kSliderRange = 10000;
};
