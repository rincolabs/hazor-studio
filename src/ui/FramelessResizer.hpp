#pragma once

#include <QObject>
#include <Qt>

class QPoint;
class QWidget;

// Gives a frameless window back the edge resizing its native frame provided.
//
// Qt's FramelessWindowHint removes the whole frame, including the invisible
// resize border, so a window that draws its own title bar has to hand those
// pixels back itself. This filter turns the outer `margin` pixels of a widget
// into the eight resize handles, delegating the drag to the window manager
// through startSystemResize().
class FramelessResizer : public QObject {
    Q_OBJECT

public:
    // `frame` must be a widget inside a frameless top-level window whose
    // outermost pixels are free of children — a layout content margin is
    // enough. Install it on every widget that reaches a window edge (the
    // central widget and the title bar, typically); each one only reports the
    // edges it actually touches. The filter is owned by `frame`.
    static void install(QWidget* frame, int margin = 5);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    FramelessResizer(QWidget* frame, int margin);

    Qt::Edges edgesAt(const QPoint& local) const;

    QWidget* m_frame = nullptr;
    int m_margin = 5;
};
