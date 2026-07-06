#pragma once

#include <QPointF>
#include <Qt>

#include <algorithm>

#include "../brush/BrushInputState.hpp"

// Unified pointer-input layer.
//
// Mouse, tablet pen, tablet eraser and (future) touch all enter through their
// own native Qt handlers (mousePressEvent / tabletEvent / ...), but every one of
// them is normalised into a single PointerInputEvent before it reaches the brush
// cursor, the brush engine or any tool. This guarantees the visual brush cursor
// and the actual dab always read from the *same* normalised position — the pen no
// longer relies on the synthetic QMouseEvent the OS may emit, so the cursor never
// diverges from the stroke.
//
// See tmp/feature-tablet-input.md for the full design.

enum class PointerSource {
    Mouse,
    TabletPen,
    TabletEraser,
    Touch,
    Unknown
};

enum class PointerPhase {
    Enter,    // proximity in / cursor entered the canvas
    Leave,    // proximity out / cursor left the canvas
    Hover,    // moving without a button / pen hovering above the tablet
    Press,    // button down / pen touched the tablet
    Move,     // moving with a button / pen dragging on the tablet
    Release,  // button up / pen lifted
    Cancel    // gesture aborted
};

struct PointerInputEvent {
    PointerSource source = PointerSource::Unknown;
    PointerPhase phase = PointerPhase::Hover;

    // Coordinates. widgetPos/globalPos come straight from the Qt event; canvasPos
    // (document pixels) and imagePos (active-layer local pixels) are filled in by
    // CanvasView using its single coordinate-conversion path so zoom/pan/HiDPI are
    // handled in exactly one place. Never mix globalPos with widget-local logic
    // without going back through that conversion.
    QPointF widgetPos;     // widget-local pixels (Qt logical, pre-DPR)
    QPointF globalPos;     // screen-global pixels
    QPointF canvasPos;     // document pixels (filled by CanvasView)
    QPointF imagePos;      // active-layer local pixels (filled by CanvasView)

    Qt::MouseButtons buttons = Qt::NoButton;
    Qt::MouseButton button = Qt::NoButton;
    Qt::KeyboardModifiers modifiers = Qt::NoModifier;

    // Pen sensors. Defaults are safe for a plain mouse / unsupported hardware so a
    // missing sensor never breaks a stroke.
    double pressure = 1.0;
    double tangentialPressure = 0.0;
    double xTilt = 0.0;
    double yTilt = 0.0;
    double rotation = 0.0;

    qint64 timestamp = 0;

    bool isPrimary = true;
    bool isHovering = false;   // pen above tablet / mouse moving with no button
    bool isDrawing = false;    // pen touching / left button held
    bool isEraser = false;     // tablet eraser tip

    bool isTablet() const {
        return source == PointerSource::TabletPen
            || source == PointerSource::TabletEraser;
    }

    // Bridge into the existing brush pipeline. imagePos must already be filled by
    // CanvasView (it needs the active layer); velocity/direction stay 0 here and
    // are computed by the stroke orchestrator from successive samples, exactly as
    // the legacy mouse/tablet paths did.
    BrushInputState toBrushInput() const {
        BrushInputState s;
        s.imagePos = imagePos;
        s.pressure = static_cast<float>(pressure);
        // Normalise the pen sensors into the units the brush engine expects (Krita's
        // convention). QTabletEvent reports tilt in degrees (±60) and rotation in
        // degrees (0..360); the engine wants tilt as a −1..1 fraction (raw/60, the
        // sensors divide by the max tilt) and rotation in radians (the dynamics
        // normalise it by 2π). Without this, tilt saturated past 1° and a barrel turn
        // cycled the rotation sensor ~57×.
        constexpr double kMaxTiltDeg = 60.0;
        constexpr double kDegToRad = 3.14159265358979323846 / 180.0;
        s.tiltX = std::clamp(static_cast<float>(xTilt / kMaxTiltDeg), -1.0f, 1.0f);
        s.tiltY = std::clamp(static_cast<float>(yTilt / kMaxTiltDeg), -1.0f, 1.0f);
        s.rotation = static_cast<float>(rotation * kDegToRad);
        // Mouse / unsupported hardware report 0 tangential pressure; the brush
        // pipeline treats 1.0 as the neutral default, so map 0 -> 1.0.
        s.tangentialPressure = tangentialPressure > 0.0
            ? static_cast<float>(tangentialPressure)
            : 1.0f;
        return s;
    }
};
