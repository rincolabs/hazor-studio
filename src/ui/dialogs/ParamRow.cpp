#include "ParamRow.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QHBoxLayout>
#include <QLabel>
#include <QSlider>
#include <QDoubleSpinBox>
#include <cmath>

ParamRow::ParamRow(const QString& label, double min, double max,
                   double defaultValue, int decimals, bool logarithmic,
                   QWidget* parent)
    : QWidget(parent)
    , m_min(min)
    , m_max(max)
    , m_defaultValue(defaultValue)
    , m_decimals(decimals)
    , m_logarithmic(logarithmic)
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    auto* t = ThemeManager::instance()->current();
    layout->setSpacing(t->spaceMD);

    m_label = new QLabel(label, this);
    m_label->setFixedWidth(80);

    m_slider = new QSlider(Qt::Horizontal, this);
    m_slider->setRange(0, kSliderRange);

    m_spin = new QDoubleSpinBox(this);
    m_spin->setDecimals(m_decimals);
    m_spin->setRange(m_min, m_max);
    m_spin->setSingleStep((m_max - m_min) / 100.0);
    m_spin->setFixedWidth(80);

    layout->addWidget(m_label);
    layout->addWidget(m_slider, 1);
    layout->addWidget(m_spin);

    connect(m_slider, &QSlider::valueChanged, this, &ParamRow::onSliderChanged);
    connect(m_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, &ParamRow::onSpinChanged);

    m_slider->blockSignals(true);
    m_spin->blockSignals(true);
    m_slider->setValue(valueToSlider(defaultValue));
    m_spin->setValue(defaultValue);
    m_spin->blockSignals(false);
    m_slider->blockSignals(false);
}

double ParamRow::value() const
{
    return m_spin->value();
}

void ParamRow::setValue(double v)
{
    v = std::clamp(v, m_min, m_max);
    m_spin->blockSignals(true);
    m_slider->blockSignals(true);
    m_spin->setValue(v);
    m_slider->setValue(valueToSlider(v));
    m_slider->blockSignals(false);
    m_spin->blockSignals(false);
}

void ParamRow::resetToDefault()
{
    setValue(m_defaultValue);
}

void ParamRow::setRange(double min, double max)
{
    m_min = min;
    m_max = max;
    m_spin->setRange(min, max);
    m_spin->setSingleStep((max - min) / 100.0);
}

void ParamRow::setDecimals(int decimals)
{
    m_decimals = decimals;
    m_spin->setDecimals(decimals);
}

void ParamRow::onSliderChanged(int pos)
{
    updateSpinFromSlider();
    emit valueChanged(m_spin->value());
}

void ParamRow::onSpinChanged(double val)
{
    updateSliderFromSpin();
    emit valueChanged(val);
}

void ParamRow::updateSliderFromSpin()
{
    m_slider->blockSignals(true);
    m_slider->setValue(valueToSlider(m_spin->value()));
    m_slider->blockSignals(false);
}

void ParamRow::updateSpinFromSlider()
{
    m_spin->blockSignals(true);
    m_spin->setValue(sliderToValue(m_slider->value()));
    m_spin->blockSignals(false);
}

double ParamRow::sliderToValue(int sliderPos) const
{
    double t = static_cast<double>(sliderPos) / kSliderRange;
    if (m_logarithmic) {
        double logMin = std::log(m_min + 1.0);
        double logMax = std::log(m_max + 1.0);
        return std::exp(logMin + t * (logMax - logMin)) - 1.0;
    }
    return m_min + t * (m_max - m_min);
}

int ParamRow::valueToSlider(double val) const
{
    double t;
    if (m_logarithmic) {
        double logMin = std::log(m_min + 1.0);
        double logMax = std::log(m_max + 1.0);
        t = (std::log(val + 1.0) - logMin) / (logMax - logMin);
    } else {
        t = (val - m_min) / (m_max - m_min);
    }
    return static_cast<int>(std::round(t * kSliderRange));
}
