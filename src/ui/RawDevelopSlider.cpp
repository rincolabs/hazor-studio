#include "RawDevelopSlider.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QDoubleSpinBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPolygonF>
#include <QSlider>
#include <QStyle>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>

namespace {

constexpr int kTrackMargin = 6;
constexpr int kTrackY = 9;
constexpr int kTrackHeight = 3;

} // namespace

class RawDevelopSlider::Track final : public QSlider {
public:
    explicit Track(QWidget* parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
    {
        setMinimumHeight(15);
        setFocusPolicy(Qt::StrongFocus);
    }

    void setStops(const QList<Stop>& stops)
    {
        m_stops = stops;
        update();
    }

    std::function<void()> resetRequested;

protected:
    void paintEvent(QPaintEvent*) override
    {
        const Theme* theme = ThemeManager::instance()->current();
        if (!theme)
            return;

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QRectF trackRect(kTrackMargin, kTrackY,
                               std::max(1, width() - kTrackMargin * 2),
                               kTrackHeight);
        if (m_stops.isEmpty()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(isEnabled() ? theme->colorBorderLight : theme->colorBorder);
            painter.drawRect(trackRect);

            QRectF adjustedRect = trackRect;
            adjustedRect.setRight(positionForValue(value()));
            painter.setBrush(isEnabled() ? theme->colorTextDisabled : theme->colorBorder);
            painter.drawRect(adjustedRect);
        } else {
            QLinearGradient gradient(trackRect.topLeft(), trackRect.topRight());
            for (const Stop& stop : m_stops)
                gradient.setColorAt(stop.pos, stop.color);
            painter.setPen(Qt::NoPen);
            painter.setBrush(gradient);
            painter.drawRect(trackRect);
        }

        const qreal x = positionForValue(value());
        QPolygonF handle;
        handle << QPointF(x, 3.0)
               << QPointF(x - 5.5, 8.5)
               << QPointF(x - 4.5, 12.5)
               << QPointF(x + 4.5, 12.5)
               << QPointF(x + 5.5, 8.5);
        painter.setPen(QPen(theme->colorBorder, 1));
        painter.setBrush(isEnabled() ? theme->colorTextSecondary : theme->colorTextDisabled);
        painter.drawPolygon(handle);
    }

    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(event);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        setSliderDown(true);
        setValue(valueForPosition(event->position().x()));
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if (!isSliderDown()) {
            QSlider::mouseMoveEvent(event);
            return;
        }
        setValue(valueForPosition(event->position().x()));
        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent* event) override
    {
        if (event->button() != Qt::LeftButton || !isSliderDown()) {
            QSlider::mouseReleaseEvent(event);
            return;
        }
        setSliderDown(false);
        event->accept();
    }

    void mouseDoubleClickEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton && resetRequested) {
            resetRequested();
            event->accept();
            return;
        }
        QSlider::mouseDoubleClickEvent(event);
    }

private:
    qreal positionForValue(int sliderValue) const
    {
        const int span = std::max(1, width() - kTrackMargin * 2);
        return kTrackMargin + QStyle::sliderPositionFromValue(
            minimum(), maximum(), sliderValue, span);
    }

    int valueForPosition(qreal x) const
    {
        const int span = std::max(1, width() - kTrackMargin * 2);
        return QStyle::sliderValueFromPosition(
            minimum(), maximum(), qRound(x) - kTrackMargin, span);
    }

    QList<Stop> m_stops;
};

RawDevelopSlider::RawDevelopSlider(const QString& label, double min, double max,
                                   double defaultValue, int decimals,
                                   bool logarithmic, bool centerZeroLog,
                                   double logBase, QWidget* parent)
    : QWidget(parent)
    , m_min(min)
    , m_max(max)
    , m_defaultValue(defaultValue)
    , m_logarithmic(logarithmic)
    , m_centerZeroLog(centerZeroLog && min < 0.0 && max > 0.0)
    , m_logBase(logBase)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 1, 8, 1);
    layout->setSpacing(0);

    auto* valueRow = new QHBoxLayout;
    valueRow->setContentsMargins(0, 0, 0, 0);
    valueRow->setSpacing(4);
    m_label = new QLabel(label, this);
    valueRow->addWidget(m_label);
    valueRow->addStretch();

    m_spin = new QDoubleSpinBox(this);
    m_spin->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_spin->setKeyboardTracking(false);
    m_spin->setAlignment(Qt::AlignRight);
    m_spin->setDecimals(decimals);
    m_spin->setRange(m_min, m_max);
    m_spin->setSingleStep((m_max - m_min) / 100.0);
    m_spin->setFixedWidth(decimals > 2 ? 64 : 54);
    valueRow->addWidget(m_spin);
    layout->addLayout(valueRow);

    m_track = new Track(this);
    m_track->setRange(0, kSliderRange);
    m_track->setToolTip(tr("Double-click to reset %1").arg(label));
    layout->addWidget(m_track);

    connect(m_track, &QSlider::valueChanged, this, [this](int position) {
        const double newValue = sliderToValue(position);
        m_spin->blockSignals(true);
        m_spin->setValue(newValue);
        m_spin->blockSignals(false);
        emit valueChanged(m_spin->value());
    });
    connect(m_spin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            this, [this](double newValue) {
        m_track->blockSignals(true);
        m_track->setValue(valueToSlider(newValue));
        m_track->blockSignals(false);
        emit valueChanged(newValue);
    });
    m_track->resetRequested = [this]() { resetToDefault(); };

    setValue(defaultValue);
}

double RawDevelopSlider::value() const
{
    return m_spin->value();
}

void RawDevelopSlider::setValue(double value)
{
    value = std::clamp(value, m_min, m_max);
    m_spin->blockSignals(true);
    m_track->blockSignals(true);
    m_spin->setValue(value);
    m_track->setValue(valueToSlider(value));
    m_track->blockSignals(false);
    m_spin->blockSignals(false);
}

void RawDevelopSlider::setStops(const QList<Stop>& stops)
{
    m_track->setStops(stops);
}

double RawDevelopSlider::sliderToValue(int sliderPosition) const
{
    const double t = static_cast<double>(sliderPosition) / kSliderRange;
    if (m_centerZeroLog) {
        if (t >= 0.5) {
            const double u = (t - 0.5) * 2.0;
            return (std::pow(m_logBase, u) - 1.0) / (m_logBase - 1.0) * m_max;
        }
        const double u = (0.5 - t) * 2.0;
        return (std::pow(m_logBase, u) - 1.0) / (m_logBase - 1.0) * m_min;
    }
    if (m_logarithmic) {
        const double logMin = std::log(m_min + 1.0);
        const double logMax = std::log(m_max + 1.0);
        return std::exp(logMin + t * (logMax - logMin)) - 1.0;
    }
    return m_min + t * (m_max - m_min);
}

int RawDevelopSlider::valueToSlider(double value) const
{
    double t = 0.0;
    if (m_centerZeroLog) {
        if (value >= 0.0) {
            const double u = std::log1p(value / m_max * (m_logBase - 1.0))
                / std::log(m_logBase);
            t = 0.5 + u * 0.5;
        } else {
            const double u = std::log1p(value / m_min * (m_logBase - 1.0))
                / std::log(m_logBase);
            t = 0.5 - u * 0.5;
        }
    } else if (m_logarithmic) {
        const double logMin = std::log(m_min + 1.0);
        const double logMax = std::log(m_max + 1.0);
        t = (std::log(value + 1.0) - logMin) / (logMax - logMin);
    } else {
        t = (value - m_min) / (m_max - m_min);
    }
    return qRound(std::clamp(t, 0.0, 1.0) * kSliderRange);
}

void RawDevelopSlider::resetToDefault()
{
    setValue(m_defaultValue);
    emit valueChanged(value());
}
