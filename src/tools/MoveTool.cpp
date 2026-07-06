#include "MoveTool.hpp"

#include <QTransform>

void MoveTool::beginDrag(QTransform* transform, const QPointF& mousePos, float zoom, const QSize& viewportSize)
{
    m_transform = transform;
    m_lastPos = mousePos;
    m_zoom = zoom;
    m_viewport = viewportSize;
    m_active = true;
}

void MoveTool::drag(const QPointF& mousePos)
{
    if (!m_active || !m_transform) return;

    QPointF delta = mousePos - m_lastPos;

    float ndcX = 2.0f * static_cast<float>(delta.x()) / m_viewport.width();
    float ndcY = -2.0f * static_cast<float>(delta.y()) / m_viewport.height();

    float dx = ndcX / m_zoom;
    float dy = ndcY / m_zoom;

    m_transform->setMatrix(m_transform->m11(), m_transform->m12(), m_transform->m13(),
                           m_transform->m21(), m_transform->m22(), m_transform->m23(),
                           m_transform->m31() + dx, m_transform->m32() + dy, m_transform->m33());

    m_lastPos = mousePos;
}

void MoveTool::endDrag()
{
    m_active = false;
    m_transform = nullptr;
}
