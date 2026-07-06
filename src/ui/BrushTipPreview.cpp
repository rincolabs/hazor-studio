#include "BrushTipPreview.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QPainter>
#include <QMouseEvent>
#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

BrushTipPreview::BrushTipPreview(QWidget* parent)
    : QWidget(parent)
{
    setMinimumSize(72, 72);
    setCursor(Qt::SizeAllCursor);
    setToolTip(tr("Drag horizontally for size, vertically for hardness"));
}

void BrushTipPreview::setBrush(const BrushSettings& settings)
{
    m_size = std::clamp(settings.size, kSizeMin, kSizeMax);
    m_hardness = std::clamp(settings.hardness, 0.0f, 1.0f);
    m_type = settings.type;
    m_tipSource = settings.tipSource;
    update();
}

void BrushTipPreview::setSize(float size)
{
    const float s = std::clamp(size, kSizeMin, kSizeMax);
    if (qFuzzyCompare(s, m_size))
        return;
    m_size = s;
    update();
}

void BrushTipPreview::setHardness(float hardness)
{
    const float h = std::clamp(hardness, 0.0f, 1.0f);
    if (qFuzzyCompare(h, m_hardness))
        return;
    m_hardness = h;
    update();
}

void BrushTipPreview::setAngle(float radians)
{
    if (qFuzzyCompare(radians, m_angle))
        return;
    m_angle = radians;
    update();
}

void BrushTipPreview::setRoundness(float roundness)
{
    const float r = std::clamp(roundness, kRoundnessMin, 1.0f);
    if (qFuzzyCompare(r, m_roundness))
        return;
    m_roundness = r;
    update();
}

void BrushTipPreview::setOrientationEditing(bool on)
{
    if (m_orientation == on)
        return;
    m_orientation = on;
    setCursor(on ? Qt::ArrowCursor : Qt::SizeAllCursor);
    setToolTip(on ? tr("Drag the handles to set angle and roundness")
                  : tr("Drag horizontally for size, vertically for hardness"));
    update();
}

bool BrushTipPreview::hasAngleControl() const
{
    // A pure round circle tip has no orientation; hide the angle marker.
    return !(m_type == BrushType::Round && m_tipSource == BrushTipSource::Circle);
}

float BrushTipPreview::displayRadius() const
{
    const float maxR = std::min(width(), height()) * 0.5f - 10.0f;
    if (maxR <= 2.0f)
        return 2.0f;
    // Compress the (potentially huge) size range into the box: ~size 100 fills
    // the preview; larger brushes clamp to the box.
    const float frac = std::clamp(m_size / 100.0f, 0.08f, 1.0f);
    return std::max(3.0f, maxR * frac);
}

float BrushTipPreview::orientationRadius() const
{
    // A fixed reference radius so the angle/roundness handles stay usable
    // regardless of the brush size.
    return std::max(8.0f, std::min(width(), height()) * 0.5f - 14.0f);
}

QPointF BrushTipPreview::angleMarkerPos(float displayRadius) const
{
    const QPointF c(width() * 0.5, height() * 0.5);
    const float maxR = std::min(width(), height()) * 0.5f - 6.0f;
    const float r = std::max(displayRadius + 6.0f, maxR);
    return c + QPointF(std::cos(m_angle) * r, std::sin(m_angle) * r);
}

QPointF BrushTipPreview::roundnessHandlePos(float displayRadius) const
{
    const QPointF c(width() * 0.5, height() * 0.5);
    // Along the minor axis (perpendicular to the angle), at radius * roundness.
    const float a = m_angle + kPi * 0.5f;
    const float r = displayRadius * m_roundness;
    return c + QPointF(std::cos(a) * r, std::sin(a) * r);
}

void BrushTipPreview::paintEvent(QPaintEvent*)
{
    const Theme* t = ThemeManager::instance()->current();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    const QPointF c(width() * 0.5, height() * 0.5);
    const QColor ink = t->colorTextPrimary;

    // Guide cross spanning the widget.
    QPen cross(t->colorBorder);
    cross.setWidthF(1.0);
    p.setPen(cross);
    p.drawLine(QPointF(c.x(), 6), QPointF(c.x(), height() - 6));
    p.drawLine(QPointF(6, c.y()), QPointF(width() - 6, c.y()));

    if (m_orientation) {
        const float R = orientationRadius();

        // Reference (fully round) boundary.
        QPen ref(t->colorBorder);
        ref.setWidthF(1.0);
        ref.setStyle(Qt::DotLine);
        p.setPen(ref);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(c, R, R);

        // The dab ellipse: rotated by angle, flattened by roundness, with the
        // hardness falloff. Drawn in a transformed frame so the radial gradient
        // becomes the same ellipse.
        p.save();
        p.translate(c);
        p.rotate(m_angle * 180.0f / kPi);
        p.scale(1.0, std::max(0.001f, m_roundness));

        QRadialGradient grad(QPointF(0, 0), R);
        QColor solid = ink; solid.setAlphaF(0.85);
        QColor edge = ink;  edge.setAlphaF(0.0);
        grad.setColorAt(0.0, solid);
        grad.setColorAt(std::clamp(m_hardness, 0.0f, 0.98f), solid);
        grad.setColorAt(1.0, edge);
        p.setPen(Qt::NoPen);
        p.setBrush(grad);
        p.drawEllipse(QPointF(0, 0), R, R);

        QPen outline(ink);
        outline.setWidthF(1.0);
        p.setPen(outline);
        p.setBrush(Qt::NoBrush);
        p.drawEllipse(QPointF(0, 0), R, R);
        p.restore();

        // Major-axis handles (angle) and the line between them.
        const QPointF aMarker = c + QPointF(std::cos(m_angle) * R, std::sin(m_angle) * R);
        const QPointF aMarker2 = c - QPointF(std::cos(m_angle) * R, std::sin(m_angle) * R);
        QPen axis(t->colorAccent);
        axis.setWidthF(1.4);
        p.setPen(axis);
        p.drawLine(aMarker2, aMarker);
        p.setBrush(t->colorAccent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(aMarker, 3.5, 3.5);
        p.drawEllipse(aMarker2, 3.0, 3.0);

        // Minor-axis handle (roundness).
        const QPointF rMarker = roundnessHandlePos(R);
        p.setBrush(t->colorTextSecondary);
        p.drawEllipse(rMarker, 3.0, 3.0);

        // Central cross marker.
        QPen centerPen(ink);
        centerPen.setWidthF(1.2);
        p.setPen(centerPen);
        p.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
        p.drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));
        return;
    }

    const float dr = displayRadius();

    // Brush circle with hardness falloff.
    QRadialGradient grad(c, dr);
    QColor solid = ink;
    solid.setAlphaF(0.85);
    QColor edge = ink;
    edge.setAlphaF(0.0);
    grad.setColorAt(0.0, solid);
    grad.setColorAt(std::clamp(m_hardness, 0.0f, 0.98f), solid);
    grad.setColorAt(1.0, edge);
    p.setPen(Qt::NoPen);
    p.setBrush(grad);

    if (m_type == BrushType::Square && m_tipSource == BrushTipSource::Circle)
        p.drawRect(QRectF(c.x() - dr, c.y() - dr, dr * 2, dr * 2));
    else
        p.drawEllipse(c, dr, dr);

    // Outline so a fully-soft brush still reads as a circle.
    QPen outline(ink);
    outline.setWidthF(1.0);
    outline.setStyle(Qt::DotLine);
    p.setPen(outline);
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, dr, dr);

    // Central cross marker.
    QPen centerPen(ink);
    centerPen.setWidthF(1.2);
    p.setPen(centerPen);
    p.drawLine(QPointF(c.x() - 4, c.y()), QPointF(c.x() + 4, c.y()));
    p.drawLine(QPointF(c.x(), c.y() - 4), QPointF(c.x(), c.y() + 4));

    // Angle / direction marker.
    if (hasAngleControl()) {
        const QPointF m = angleMarkerPos(dr);
        QPen anglePen(t->colorAccent);
        anglePen.setWidthF(1.4);
        p.setPen(anglePen);
        p.drawLine(c, m);
        p.setBrush(t->colorAccent);
        p.setPen(Qt::NoPen);
        p.drawEllipse(m, 3.0, 3.0);
    }
}

void BrushTipPreview::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    m_lastPos = event->position();
    m_dragStartPos = m_lastPos;
    m_dragStartSize = m_size;
    m_dragStartHardness = m_hardness;

    if (m_orientation) {
        const float R = orientationRadius();
        const QPointF c(width() * 0.5, height() * 0.5);
        const QPointF rh = roundnessHandlePos(R);
        if (std::hypot((m_lastPos - rh).x(), (m_lastPos - rh).y()) <= 9.0) {
            m_drag = DragMode::Roundness;
            return;
        }
        const QPointF a1 = c + QPointF(std::cos(m_angle) * R, std::sin(m_angle) * R);
        const QPointF a2 = c - QPointF(std::cos(m_angle) * R, std::sin(m_angle) * R);
        if (std::hypot((m_lastPos - a1).x(), (m_lastPos - a1).y()) <= 9.0 ||
            std::hypot((m_lastPos - a2).x(), (m_lastPos - a2).y()) <= 9.0) {
            m_drag = DragMode::Angle;
            return;
        }
        m_drag = DragMode::SizeHardness;
        return;
    }

    if (hasAngleControl()) {
        const QPointF marker = angleMarkerPos(displayRadius());
        const QPointF d = m_lastPos - marker;
        if (std::hypot(d.x(), d.y()) <= 9.0) {
            m_drag = DragMode::Angle;
            return;
        }
    }
    m_drag = DragMode::SizeHardness;
}

void BrushTipPreview::mouseMoveEvent(QMouseEvent* event)
{
    if (m_drag == DragMode::None) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    const QPointF pos = event->position();
    const QPointF c(width() * 0.5, height() * 0.5);

    if (m_drag == DragMode::Angle) {
        const QPointF d = pos - c;
        m_angle = std::atan2(d.y(), d.x());
        update();
        emit angleChanged(m_angle);
        m_lastPos = pos;
        return;
    }

    if (m_drag == DragMode::Roundness) {
        // Project the cursor onto the minor axis (perpendicular to the angle).
        const float a = m_angle + kPi * 0.5f;
        const QPointF d = pos - c;
        const float proj = std::abs(d.x() * std::cos(a) + d.y() * std::sin(a));
        const float R = orientationRadius();
        const float nr = std::clamp(proj / std::max(1.0f, R), kRoundnessMin, 1.0f);
        if (!qFuzzyCompare(nr, m_roundness)) {
            m_roundness = nr;
            emit roundnessChanged(m_roundness);
        }
        update();
        m_lastPos = pos;
        return;
    }

    // Accumulate from the press point (like ScrubbableValueInput) instead of the
    // previous move event: a caller that echoes a rounded value back into
    // setSize()/setHardness() mid-drag must not erase sub-step precision that
    // would otherwise accumulate between move events.
    const QPointF totalDelta = pos - m_dragStartPos;
    m_lastPos = pos;

    // Same fine/coarse modifier scheme as ScrubbableValueInput::updateDrag:
    // Alt = fine (1%), Shift = medium (10%), Ctrl = coarse (10x).
    float multiplier = 1.0f;
    const auto mods = event->modifiers();
    if (mods & Qt::AltModifier)
        multiplier = 0.01f;
    else if (mods & Qt::ShiftModifier)
        multiplier = 0.1f;
    else if (mods & Qt::ControlModifier)
        multiplier = 10.0f;

    // Horizontal → size (scale step with the drag-start size for a natural feel).
    if (std::abs(totalDelta.x()) > 0.0) {
        const float step = std::max(0.5f, m_dragStartSize * 0.01f) * multiplier;
        const float ns = std::clamp(m_dragStartSize + static_cast<float>(totalDelta.x()) * step,
                                    kSizeMin, kSizeMax);
        if (!qFuzzyCompare(ns, m_size)) {
            m_size = ns;
            emit sizeChanged(m_size);
        }
    }
    // Vertical → hardness (drag up = harder).
    if (std::abs(totalDelta.y()) > 0.0) {
        const float nh = std::clamp(m_dragStartHardness - static_cast<float>(totalDelta.y()) * 0.005f * multiplier,
                                    0.0f, 1.0f);
        if (!qFuzzyCompare(nh, m_hardness)) {
            m_hardness = nh;
            emit hardnessChanged(m_hardness);
        }
    }
    update();
}

void BrushTipPreview::mouseReleaseEvent(QMouseEvent* event)
{
    Q_UNUSED(event);
    m_drag = DragMode::None;
}
