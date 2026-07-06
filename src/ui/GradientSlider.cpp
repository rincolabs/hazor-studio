#include "GradientSlider.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QHBoxLayout>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>

static constexpr int kTrackH    = 8;
static constexpr int kSpinWidth = 56;

// ── GradientSlider::Track ────────────────────────────────────────────────────

class GradientSlider::Track : public QSlider {
public:
    using Stop = GradientSlider::Stop;

    explicit Track(QWidget* parent = nullptr) : QSlider(Qt::Horizontal, parent)
    {
        setMinimumHeight(22);
        setStyleSheet(QStringLiteral(
            "QSlider::sub-page:horizontal { background: transparent; }"));
    }

    void setStops(const QList<Stop>& stops) { m_stops = stops; update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionSlider opt;
        initStyleOption(&opt);

        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRect groove = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

        QRect track(groove.left(), groove.center().y() - kTrackH / 2,
                    groove.width(), kTrackH);

        QLinearGradient grad(track.left(), 0, track.right(), 0);
        for (const Stop& s : m_stops)
            grad.setColorAt(s.pos, s.color);

        const QColor border = ThemeManager::instance()->current()->colorBorder;
        p.setPen(QPen(border, 1));
        p.setBrush(m_stops.isEmpty() ? Qt::NoBrush : QBrush(grad));
        p.drawRoundedRect(QRectF(track).adjusted(0.5, 0.5, -0.5, -0.5), 3, 3);

        opt.subControls = QStyle::SC_SliderHandle;
        style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);
    }

private:
    QList<Stop> m_stops;
};

// ── GradientSlider ───────────────────────────────────────────────────────────

GradientSlider::GradientSlider(QWidget* parent)
    : QWidget(parent)
    , m_track(new Track(this))
    , m_spin(new QSpinBox(this))
{
    m_spin->setFixedWidth(kSpinWidth);
    m_spin->setKeyboardTracking(false);

    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(8);
    layout->addWidget(m_track, 1);
    layout->addWidget(m_spin);

    connect(m_track, &QSlider::valueChanged, this, [this](int v) {
        m_spin->blockSignals(true);
        m_spin->setValue(v);
        m_spin->blockSignals(false);
        emit valueChanged(v);
    });
    connect(m_track, &QSlider::sliderReleased, this, &GradientSlider::sliderReleased);
    connect(m_spin, qOverload<int>(&QSpinBox::valueChanged), this, [this](int v) {
        m_track->blockSignals(true);
        m_track->setValue(v);
        m_track->blockSignals(false);
        emit spinEdited(v);
    });
}

void GradientSlider::setRange(int min, int max)
{
    m_track->setRange(min, max);
    m_spin->setRange(min, max);
}

void GradientSlider::setStops(const QList<Stop>& stops)
{
    m_track->setStops(stops);
}

void GradientSlider::setValue(int value)
{
    m_track->blockSignals(true);
    m_track->setValue(value);
    m_track->blockSignals(false);
    m_spin->blockSignals(true);
    m_spin->setValue(value);
    m_spin->blockSignals(false);
}

int GradientSlider::value() const
{
    return m_track->value();
}

bool GradientSlider::isSliderDown() const
{
    return m_track->isSliderDown();
}
