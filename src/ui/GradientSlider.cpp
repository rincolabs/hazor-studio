#include "GradientSlider.hpp"
#include "ColorTrack.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QHBoxLayout>
#include <QMouseEvent>
#include <QPainter>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionSlider>

static constexpr int kTrackH    = 8;
static constexpr int kSpinWidth = 56;

// ── GradientSlider::Track ────────────────────────────────────────────────────
// The interactive piece. Value/range/keyboard handling stay QSlider's (every
// consumer's limits, step and precision keep working unchanged); mouse
// handling is replaced outright, because QSlider's own groove click is a page
// step, not a jump to the clicked position — clicking anywhere on the bar must
// move the handle there immediately and keep tracking the drag. The handle's
// own drag already matched this once pressed; valueForPosition() below is the
// same pixel↔value mapping Qt uses internally for that drag, just invoked from
// mousePress too, so a press on the handle reproduces its current value (no
// jump) and extremes stay pixel-accurate. Rendering of the colour composition
// is delegated to ColorTrack, which knows nothing about sliders or values.
class GradientSlider::Track : public QSlider {
public:
    using Stop = GradientSlider::Stop;

    explicit Track(QWidget* parent = nullptr) : QSlider(Qt::Horizontal, parent)
    {
        setMinimumHeight(22);
        setStyleSheet(QStringLiteral(
            "QSlider::sub-page:horizontal { background: transparent; }"));
    }

    void setStops(const QList<Stop>& stops) { m_colorTrack.setStops(stops); update(); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QStyleOptionSlider opt;
        initStyleOption(&opt);

        QPainter p(this);

        const QRect groove = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);

        const QRectF track(groove.left(), groove.center().y() - kTrackH / 2,
                            groove.width(), kTrackH);

        const QColor border = ThemeManager::instance()->current()->colorBorder;
        m_colorTrack.paint(p, track, border);

        opt.subControls = QStyle::SC_SliderHandle;
        style()->drawComplexControl(QStyle::CC_Slider, &opt, &p, this);
    }

    void mousePressEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton) {
            QSlider::mousePressEvent(e);
            return;
        }
        setFocus(Qt::MouseFocusReason);
        setSliderDown(true);
        setValue(valueForPosition(e->pos().x()));
        e->accept();
    }

    void mouseMoveEvent(QMouseEvent* e) override
    {
        if (!isSliderDown()) {
            QSlider::mouseMoveEvent(e);
            return;
        }
        setValue(valueForPosition(e->pos().x()));
        e->accept();
    }

    void mouseReleaseEvent(QMouseEvent* e) override
    {
        if (e->button() != Qt::LeftButton || !isSliderDown()) {
            QSlider::mouseReleaseEvent(e);
            return;
        }
        setSliderDown(false);
        e->accept();
    }

private:
    // Mirrors QSliderPrivate::pixelPosToRangeValue (horizontal case): the span
    // excludes the handle's own width, and QStyle::sliderValueFromPosition maps
    // the click/drag x within it to a value — the same mapping the handle's
    // drag already used, so this works for any range, including negative or
    // asymmetric ones (Hue -180..180, Color Balance -100..100, ...).
    int valueForPosition(int x) const
    {
        QStyleOptionSlider opt;
        initStyleOption(&opt);
        const QRect groove = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderGroove, this);
        const QRect handle = style()->subControlRect(
            QStyle::CC_Slider, &opt, QStyle::SC_SliderHandle, this);
        const int span = groove.width() - handle.width();
        return QStyle::sliderValueFromPosition(
            minimum(), maximum(), x - groove.left(), span, opt.upsideDown);
    }

    ColorTrack m_colorTrack;
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
