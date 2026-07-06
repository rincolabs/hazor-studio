#pragma once

#include <QColor>
#include <QList>
#include <QWidget>

class QSpinBox;

// Horizontal gradient slider with an integrated spin box. Gradient stops are
// fully configurable; dimensions and handle style follow the SpectrumSlider
// standard (track height 8 px, min widget height 22 px).
//
// setValue() is always silent (no signals emitted). Signals:
//   valueChanged(int)  – slider position changed (use for live preview)
//   sliderReleased()   – drag ended (use to commit)
//   spinEdited(int)    – spin box committed a value (use to commit)
class GradientSlider : public QWidget {
    Q_OBJECT
public:
    struct Stop { qreal pos; QColor color; };

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
