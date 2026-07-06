#include "BrushCurveOptionEditor.hpp"
#include "ui/AppCheckBox.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QComboBox>
#include <QSlider>
#include <QLabel>
#include <QPainter>
#include <QMouseEvent>
#include <QLineF>
#include <algorithm>
#include <cmath>
#include <iterator>

// ── ResponseCurveEditor ───────────────────────────────────────────────────────

ResponseCurveEditor::ResponseCurveEditor(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(140, 90);
    setMouseTracking(true);
}

void ResponseCurveEditor::setCurve(const ResponseCurve& c)
{
    if (c.points.size() >= 2)
        m_points = c.points;
    else
        m_points = { {0.0, 0.0}, {1.0, 1.0} };   // identity display
    sortPoints();
    m_selected = -1;
    update();
}

ResponseCurve ResponseCurveEditor::curve() const
{
    ResponseCurve c;
    // Treat the untouched diagonal as identity (empty) so presets stay clean.
    const bool identity = m_points.size() == 2
        && qFuzzyIsNull(float(m_points.first().x())) && qFuzzyIsNull(float(m_points.first().y()))
        && qFuzzyCompare(float(m_points.last().x()), 1.0f) && qFuzzyCompare(float(m_points.last().y()), 1.0f);
    if (!identity)
        c.points = m_points;
    return c;
}

void ResponseCurveEditor::sortPoints()
{
    std::sort(m_points.begin(), m_points.end(),
              [](const QPointF& a, const QPointF& b) { return a.x() < b.x(); });
}

QRect ResponseCurveEditor::plotRect() const
{
    return rect().adjusted(6, 6, -6, -6);
}

QPointF ResponseCurveEditor::toWidget(const QPointF& p) const
{
    const QRect r = plotRect();
    return QPointF(r.left() + std::clamp(p.x(), 0.0, 1.0) * r.width(),
                   r.bottom() - std::clamp(p.y(), 0.0, 1.0) * r.height());
}

QPointF ResponseCurveEditor::toCurve(const QPointF& w) const
{
    const QRect r = plotRect();
    const double x = r.width()  > 0 ? (w.x() - r.left()) / r.width()  : 0.0;
    const double y = r.height() > 0 ? (r.bottom() - w.y()) / r.height() : 0.0;
    return QPointF(std::clamp(x, 0.0, 1.0), std::clamp(y, 0.0, 1.0));
}

int ResponseCurveEditor::hitTest(const QPointF& w) const
{
    for (int i = 0; i < m_points.size(); ++i) {
        if (QLineF(w, toWidget(m_points[i])).length() <= 8.0)
            return i;
    }
    return -1;
}

void ResponseCurveEditor::paintEvent(QPaintEvent*)
{
    const Theme* t = ThemeManager::instance()->current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);
    const QRect r = plotRect();

    p.fillRect(rect(), t->colorSurfaceDark);
    p.setPen(t->colorBorder);
    p.drawRect(r);

    // Grid quarters + diagonal baseline.
    QPen grid(t->colorBorder);
    grid.setStyle(Qt::DotLine);
    p.setPen(grid);
    for (int i = 1; i < 4; ++i) {
        const int x = r.left() + r.width() * i / 4;
        const int y = r.top() + r.height() * i / 4;
        p.drawLine(x, r.top(), x, r.bottom());
        p.drawLine(r.left(), y, r.right(), y);
    }
    p.setPen(QPen(t->colorTextDisabled, 1, Qt::DashLine));
    p.drawLine(toWidget({0.0, 0.0}), toWidget({1.0, 1.0}));

    // The curve itself: sample the SAME spline ResponseCurve::evaluate() uses, so the
    // drawn curve is exactly what the engine applies (a smooth cubic, not segments).
    ResponseCurve rc;
    rc.points = m_points;
    QPen curvePen(t->colorAccent);
    curvePen.setWidthF(1.6);
    p.setPen(curvePen);
    QPolygonF poly;
    constexpr int kSamples = 96;
    poly.reserve(kSamples + 1);
    for (int i = 0; i <= kSamples; ++i) {
        const double xx = double(i) / kSamples;
        poly << toWidget(QPointF(xx, rc.evaluate(float(xx))));
    }
    p.drawPolyline(poly);

    // Control points.
    for (int i = 0; i < m_points.size(); ++i) {
        const QPointF c = toWidget(m_points[i]);
        const bool sel = (i == m_selected);
        p.setBrush(sel ? t->colorTextBright : t->colorAccent);
        p.setPen(QPen(t->colorSurfaceDark, 1));
        const qreal rad = sel ? 4.5 : 3.5;
        p.drawEllipse(c, rad, rad);
    }
}

void ResponseCurveEditor::mousePressEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton)
        return;
    m_selected = hitTest(e->position());
    if (m_selected >= 0) {
        m_dragging = true;
        emit editBegan();
        update();
    }
}

void ResponseCurveEditor::mouseMoveEvent(QMouseEvent* e)
{
    if (!m_dragging || m_selected < 0)
        return;
    QPointF c = toCurve(e->position());
    const int last = m_points.size() - 1;
    if (m_selected == 0) {
        c.setX(0.0);                       // left endpoint pinned to x=0
    } else if (m_selected == last) {
        c.setX(1.0);                       // right endpoint pinned to x=1
    } else {
        const double lo = m_points[m_selected - 1].x() + 1e-3;
        const double hi = m_points[m_selected + 1].x() - 1e-3;
        c.setX(std::clamp(c.x(), lo, hi));
    }
    m_points[m_selected] = c;
    emit changed();
    update();
}

void ResponseCurveEditor::mouseReleaseEvent(QMouseEvent* e)
{
    if (e->button() != Qt::LeftButton || !m_dragging)
        return;
    m_dragging = false;
    emit editCommitted();
}

void ResponseCurveEditor::mouseDoubleClickEvent(QMouseEvent* e)
{
    const int hit = hitTest(e->position());
    if (hit > 0 && hit < m_points.size() - 1) {
        // Remove an interior point.
        m_points.remove(hit);
        m_selected = -1;
    } else if (hit < 0) {
        // Add a point at the clicked spot.
        const QPointF c = toCurve(e->position());
        m_points.append(c);
        sortPoints();
        m_selected = m_points.indexOf(c);
    } else {
        return;   // double-click on an endpoint: ignore
    }
    emit editBegan();
    emit changed();
    emit editCommitted();
    update();
}

// ── CurveOptionEditor ─────────────────────────────────────────────────────────

namespace {
// Combo rows. The order is decoupled from the SensorType enum value via kSensorOrder
// below, so new sensors (e.g. TiltDirection, which the enum appends at the end to keep
// serialization stable) can be listed wherever reads best without breaking presets.
const char* kSensorNames[] = {
    "Pressure", "Tilt X", "Tilt Y", "Tilt Elev.", "Tilt Dir.", "Speed",
    "Direction", "Rotation", "Distance", "Time", "Fade", "Random"
};
const SensorType kSensorOrder[] = {
    SensorType::Pressure, SensorType::TiltX, SensorType::TiltY, SensorType::TiltElevation,
    SensorType::TiltDirection, SensorType::Speed, SensorType::DrawingAngle,
    SensorType::Rotation, SensorType::Distance, SensorType::Time, SensorType::Fade,
    SensorType::Fuzzy
};
constexpr int kSensorCount = int(std::size(kSensorNames));

int comboIndexForSensor(SensorType t) {
    for (int i = 0; i < kSensorCount; ++i)
        if (kSensorOrder[i] == t) return i;
    return 0;  // Pressure
}
}

CurveOptionEditor::CurveOptionEditor(const QString& title, QWidget* parent)
    : QWidget(parent)
{
    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);

    m_enable = new AppCheckBox(title, this);
    lay->addWidget(m_enable);

    auto* sensorRow = new QWidget(this);
    auto* sl = new QHBoxLayout(sensorRow);
    sl->setContentsMargins(0, 0, 0, 0);
    auto* sLbl = new QLabel(tr("Sensor:"), sensorRow);
    sLbl->setFixedWidth(70);
    m_sensor = new QComboBox(sensorRow);
    for (const char* n : kSensorNames)
        m_sensor->addItem(tr(n));
    sl->addWidget(sLbl);
    sl->addWidget(m_sensor, 1);
    lay->addWidget(sensorRow);

    m_editor = new ResponseCurveEditor(this);
    lay->addWidget(m_editor);

    auto addRange = [&](const QString& label, QSlider*& slider, QLabel*& valueLabel, int def) {
        auto* row = new QWidget(this);
        auto* rl = new QHBoxLayout(row);
        rl->setContentsMargins(0, 0, 0, 0);
        auto* lbl = new QLabel(label, row);
        lbl->setFixedWidth(70);
        slider = new QSlider(Qt::Horizontal, row);
        slider->setRange(0, 100);
        slider->setValue(def);
        valueLabel = new QLabel(QString::number(def) + "%", row);
        valueLabel->setFixedWidth(38);
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        rl->addWidget(lbl);
        rl->addWidget(slider, 1);
        rl->addWidget(valueLabel);
        lay->addWidget(row);
    };
    addRange(tr("Min:"), m_min, m_minLabel, 0);
    addRange(tr("Max:"), m_max, m_maxLabel, 100);

    auto fire = [this]() { if (!m_loading) emit changed(); };
    connect(m_enable, &QCheckBox::toggled, this, [fire](bool) { fire(); });
    connect(m_sensor, &QComboBox::currentIndexChanged, this, [fire](int) { fire(); });
    connect(m_editor, &ResponseCurveEditor::changed, this, [fire]() { fire(); });
    connect(m_editor, &ResponseCurveEditor::editCommitted, this, [fire]() { fire(); });
    connect(m_min, &QSlider::valueChanged, this, [this, fire](int v) {
        m_minLabel->setText(QString::number(v) + "%"); fire();
    });
    connect(m_max, &QSlider::valueChanged, this, [this, fire](int v) {
        m_maxLabel->setText(QString::number(v) + "%"); fire();
    });
}

void CurveOptionEditor::set(const CurveOption& o)
{
    m_loading = true;
    m_opt = o;
    m_enable->setChecked(o.enabled);
    m_min->setValue(std::clamp(int(std::lround(o.minValue * 100.0f)), 0, 100));
    m_max->setValue(std::clamp(int(std::lround(o.maxValue * 100.0f)), 0, 100));
    if (!o.sensors.isEmpty()) {
        m_sensor->setCurrentIndex(comboIndexForSensor(o.sensors.first().type));
        m_editor->setCurve(o.sensors.first().curve);
    } else {
        m_sensor->setCurrentIndex(comboIndexForSensor(SensorType::Pressure));
        m_editor->setCurve(ResponseCurve{});
    }
    m_loading = false;
}

CurveOption CurveOptionEditor::value() const
{
    CurveOption o = m_opt;   // keep combine + any extra (non-primary) sensors
    o.enabled = m_enable->isChecked();
    o.minValue = m_min->value() / 100.0f;
    o.maxValue = m_max->value() / 100.0f;
    if (o.sensors.isEmpty())
        o.sensors.append(Sensor{});
    o.sensors[0].type = kSensorOrder[std::clamp(m_sensor->currentIndex(), 0, kSensorCount - 1)];
    o.sensors[0].curve = m_editor->curve();
    o.sensors[0].enabled = true;
    return o;
}
