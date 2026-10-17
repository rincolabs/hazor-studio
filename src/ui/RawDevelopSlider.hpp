#pragma once

#include "ui/ColorTrack.hpp"

#include <QWidget>

class QDoubleSpinBox;
class QLabel;
class QSlider;

// Compact two-line adjustment control used only by Hazor Develop. The value
// editor shares the label line while the full-width track sits underneath,
// matching the dense RAW-development layout without changing the application's
// general-purpose ParamRow component.
class RawDevelopSlider final : public QWidget {
    Q_OBJECT
public:
    using Stop = ColorTrack::Stop;

    explicit RawDevelopSlider(const QString& label, double min, double max,
                              double defaultValue, int decimals = 2,
                              bool logarithmic = false,
                              bool centerZeroLog = false, double logBase = 2.0,
                              QWidget* parent = nullptr);

    double value() const;
    void setValue(double value);
    void setStops(const QList<Stop>& stops);

signals:
    void valueChanged(double value);

private:
    class Track;

    double sliderToValue(int sliderPosition) const;
    int valueToSlider(double value) const;
    void resetToDefault();

    QLabel* m_label = nullptr;
    Track* m_track = nullptr;
    QDoubleSpinBox* m_spin = nullptr;

    double m_min = 0.0;
    double m_max = 1.0;
    double m_defaultValue = 0.0;
    bool m_logarithmic = false;
    bool m_centerZeroLog = false;
    double m_logBase = 2.0;

    static constexpr int kSliderRange = 10000;
};
