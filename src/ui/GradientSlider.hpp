#pragma once

#include "ColorTrack.hpp"

#include <QList>
#include <QWidget>

class QSpinBox;

// Horizontal gradient slider with an integrated spin box. Gradient stops are
// fully configurable (rendering is delegated to ColorTrack); dimensions and
// handle style follow the SpectrumSlider standard (track height 8 px, min
// widget height 22 px). Clicking anywhere on the track jumps the handle to
// that position and keeps dragging while the button stays down — see Track in
// the .cpp for the interaction (QSlider's own groove click is a page step,
// not a jump).
//
// setValue() is always silent (no signals emitted). Signals:
//   valueChanged(int)  – slider position changed (use for live preview)
//   sliderReleased()   – drag ended (use to commit)
//   spinEdited(int)    – spin box committed a value (use to commit)
class GradientSlider : public QWidget {
    Q_OBJECT
public:
    using Stop = ColorTrack::Stop;

    explicit GradientSlider(QWidget* parent = nullptr);

    void setRange(int min, int max);
    void setStops(const QList<Stop>& stops);
    void setValue(int value);   // silent: does not emit any signal
    int  value() const;
    bool isSliderDown() const;

signals:
    void valueChanged(int value);
    void sliderReleased();
    void spinEdited(int value);

private:
    class Track;
    Track*    m_track;
    QSpinBox* m_spin;
};
