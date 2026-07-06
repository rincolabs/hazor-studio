#pragma once

#include <QPointF>
#include <QSize>

class QTransform;

class MoveTool {
public:
    void beginDrag(QTransform* transform, const QPointF& mousePos, float zoom, const QSize& viewportSize);
    void drag(const QPointF& mousePos);
    void endDrag();

    bool isDragging() const { return m_active && m_transform; }

private:
    QTransform* m_transform = nullptr;
    QPointF m_lastPos;
    float m_zoom = 1.0f;
    QSize m_viewport;
    bool m_active = false;
};
