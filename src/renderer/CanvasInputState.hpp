#pragma once

#include <QElapsedTimer>
#include <QPointF>

#include "PointerInputEvent.hpp"

// Tracks the live pointer state across native handlers so the brush cursor, the
// stroke and the synthetic-mouse filter all agree on what the pointer is doing.
//
// The key job is de-duplication: many platforms emit a synthetic QMouseEvent for
// every QTabletEvent. Without filtering, the pen would either draw twice or the
// cursor would lag (the synthetic mouse arrives a frame later at a slightly stale
// position). We remember when the last real tablet event happened and suppress
// the brush path for mouse events that land inside a short window after it. Real
// mouse input (no recent tablet activity) is never blocked, and the mouse starts
// working again automatically once the tablet has been idle past the window.
class CanvasInputState {
public:
    // Synthetic-mouse suppression window. Tuned to comfortably exceed the gap
    // between a QTabletEvent and its paired synthetic QMouseEvent while staying
    // short enough that picking the mouse back up feels instant.
    static constexpr qint64 kSyntheticMouseWindowMs = 60;

    void noteTabletEvent(const PointerInputEvent& ev) {
        m_activeSource = ev.source;
        m_lastPos = ev.canvasPos;
        m_lastPressure = ev.pressure;
        m_lastTiltX = ev.xTilt;
        m_lastTiltY = ev.yTilt;
        m_tabletActive = true;
        m_lastTabletTimer.start();
        if (ev.phase == PointerPhase::Press)
            m_drawing = true;
        else if (ev.phase == PointerPhase::Release || ev.phase == PointerPhase::Cancel)
            m_drawing = false;
    }

    void noteMouseEvent(const PointerInputEvent& ev) {
        // A genuine mouse event past the suppression window means the user moved
        // back to the mouse; the tablet is no longer the active device.
        if (!shouldIgnoreSyntheticMouse()) {
            m_activeSource = PointerSource::Mouse;
            m_tabletActive = false;
        }
        m_lastPos = ev.canvasPos;
        if (ev.phase == PointerPhase::Press)
            m_drawing = true;
        else if (ev.phase == PointerPhase::Release || ev.phase == PointerPhase::Cancel)
            m_drawing = false;
    }

    // True while the last tablet event is recent enough that an incoming mouse
    // event is most likely the OS-synthesised twin of a pen event.
    bool shouldIgnoreSyntheticMouse() const {
        if (!m_tabletActive || !m_lastTabletTimer.isValid())
            return false;
        return m_lastTabletTimer.elapsed() < kSyntheticMouseWindowMs;
    }

    // Pen left the tablet's proximity. The device is no longer hovering, but we
    // keep the timestamp so a trailing synthetic mouse-leave is still suppressed.
    void noteTabletLeave() {
        m_tabletActive = false;
        m_drawing = false;
    }

    void reset() {
        m_tabletActive = false;
        m_drawing = false;
        m_activeSource = PointerSource::Unknown;
    }

    PointerSource activeSource() const { return m_activeSource; }
    bool isTabletActive() const { return m_tabletActive; }
    bool isDrawing() const { return m_drawing; }
    QPointF lastPos() const { return m_lastPos; }
    double lastPressure() const { return m_lastPressure; }
    QPointF lastTilt() const { return {m_lastTiltX, m_lastTiltY}; }

private:
    PointerSource m_activeSource = PointerSource::Unknown;
    bool m_tabletActive = false;
    bool m_drawing = false;
    QElapsedTimer m_lastTabletTimer;
    QPointF m_lastPos;
    double m_lastPressure = 1.0;
    double m_lastTiltX = 0.0;
    double m_lastTiltY = 0.0;
};
