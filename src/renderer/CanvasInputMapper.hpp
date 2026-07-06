#pragma once

#include <QEvent>
#include <QMouseEvent>
#include <QPointingDevice>
#include <QTabletEvent>

#include "PointerInputEvent.hpp"

// Pure, stateless conversion of native Qt input events into a PointerInputEvent.
//
// Only the *device* fields are filled here (position in widget/global space,
// buttons, modifiers, pen sensors, source). canvasPos / imagePos are intentionally
// left empty: they require the live zoom/pan/active-layer state and must go through
// CanvasView's single coordinate-conversion path so HiDPI/zoom/pan are handled in
// exactly one place (see PointerInputEvent).
namespace CanvasInputMapper {

inline PointerPhase mapTabletPhase(QEvent::Type type) {
    switch (type) {
    case QEvent::TabletPress:           return PointerPhase::Press;
    case QEvent::TabletMove:            return PointerPhase::Move;
    case QEvent::TabletRelease:         return PointerPhase::Release;
    case QEvent::TabletEnterProximity:  return PointerPhase::Enter;
    case QEvent::TabletLeaveProximity:  return PointerPhase::Leave;
    default:                            return PointerPhase::Hover;
    }
}

inline PointerInputEvent fromMouseEvent(QMouseEvent* e, PointerPhase phase) {
    PointerInputEvent out;
    out.source = PointerSource::Mouse;
    out.phase = phase;

    out.widgetPos = e->position();
    out.globalPos = e->globalPosition();

    out.button = e->button();
    out.buttons = e->buttons();
    out.modifiers = e->modifiers();
    out.timestamp = static_cast<qint64>(e->timestamp());

    // A mouse has no sensors: full pressure while the left button paints, nothing
    // otherwise. Tilt/rotation/tangential stay at their neutral defaults.
    const bool down = e->buttons().testFlag(Qt::LeftButton);
    out.pressure = down ? 1.0 : 0.0;
    out.isDrawing = down;
    out.isHovering = !down && phase == PointerPhase::Hover;
    return out;
}

inline PointerInputEvent fromTabletEvent(QTabletEvent* e, PointerPhase phase) {
    PointerInputEvent out;
    out.phase = phase;

    out.widgetPos = e->position();
    out.globalPos = e->globalPosition();

    out.button = e->button();
    out.buttons = e->buttons();
    out.modifiers = e->modifiers();
    out.timestamp = static_cast<qint64>(e->timestamp());

    out.pressure = e->pressure();
    out.tangentialPressure = e->tangentialPressure();
    out.xTilt = e->xTilt();
    out.yTilt = e->yTilt();
    out.rotation = e->rotation();

    if (e->pointerType() == QPointingDevice::PointerType::Eraser) {
        out.source = PointerSource::TabletEraser;
        out.isEraser = true;
    } else {
        out.source = PointerSource::TabletPen;
        out.isEraser = false;
    }

    // Hover = pen above the tablet (proximity / no contact). Drawing = pen in
    // contact (non-zero pressure) or a side button held.
    out.isHovering = phase == PointerPhase::Hover
                  || phase == PointerPhase::Enter
                  || phase == PointerPhase::Leave;
    out.isDrawing = e->pressure() > 0.0 || e->buttons() != Qt::NoButton;
    if (out.isDrawing)
        out.isHovering = false;
    return out;
}

} // namespace CanvasInputMapper
