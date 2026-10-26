#include "FramelessResizer.hpp"

#include <QEvent>
#include <QMouseEvent>
#include <QWidget>
#include <QWindow>

namespace {

Qt::CursorShape cursorForEdges(Qt::Edges edges)
{
    if ((edges & Qt::TopEdge) && (edges & Qt::LeftEdge))     return Qt::SizeFDiagCursor;
    if ((edges & Qt::BottomEdge) && (edges & Qt::RightEdge)) return Qt::SizeFDiagCursor;
    if ((edges & Qt::TopEdge) && (edges & Qt::RightEdge))    return Qt::SizeBDiagCursor;
    if ((edges & Qt::BottomEdge) && (edges & Qt::LeftEdge))  return Qt::SizeBDiagCursor;
    if (edges & (Qt::LeftEdge | Qt::RightEdge))              return Qt::SizeHorCursor;
    return Qt::SizeVerCursor;
}

} // namespace

void FramelessResizer::install(QWidget* frame, int margin)
{
    if (!frame)
        return;
    frame->setMouseTracking(true);
    frame->installEventFilter(new FramelessResizer(frame, margin));
}

FramelessResizer::FramelessResizer(QWidget* frame, int margin)
    : QObject(frame)
    , m_frame(frame)
    , m_margin(margin)
{
}

Qt::Edges FramelessResizer::edgesAt(const QPoint& local) const
{
    QWidget* window = m_frame->window();
    if (!window || window->isMaximized() || window->isFullScreen())
        return {};

    const QPoint point = m_frame->mapTo(window, local);
    Qt::Edges edges;
    if (point.x() <= m_margin)                         edges |= Qt::LeftEdge;
    if (point.x() >= window->width() - 1 - m_margin)   edges |= Qt::RightEdge;
    if (point.y() <= m_margin)                         edges |= Qt::TopEdge;
    if (point.y() >= window->height() - 1 - m_margin)  edges |= Qt::BottomEdge;
    return edges;
}

bool FramelessResizer::eventFilter(QObject* watched, QEvent* event)
{
    if (watched != m_frame)
        return QObject::eventFilter(watched, event);

    switch (event->type()) {
        case QEvent::MouseMove: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->buttons() != Qt::NoButton)
                break;
            const Qt::Edges edges = edgesAt(mouse->position().toPoint());
            if (edges)
                m_frame->setCursor(cursorForEdges(edges));
            else
                m_frame->unsetCursor();
            break;
        }
        case QEvent::MouseButtonPress: {
            auto* mouse = static_cast<QMouseEvent*>(event);
            if (mouse->button() != Qt::LeftButton)
                break;
            const Qt::Edges edges = edgesAt(mouse->position().toPoint());
            if (!edges)
                break;
            // The window manager owns the drag from here — consuming the press
            // also keeps the title bar from starting a move on the same click.
            if (auto* handle = m_frame->window()->windowHandle()) {
                handle->startSystemResize(edges);
                return true;
            }
            break;
        }
        case QEvent::Leave:
            m_frame->unsetCursor();
            break;
        default:
            break;
    }
    return QObject::eventFilter(watched, event);
}
