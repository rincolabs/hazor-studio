#include "DirtyRegion.hpp"
#include <algorithm>
#include <climits>
#include <cmath>

namespace core {

void DirtyRegion::addRect(const QRect& rect)
{
    if (rect.isEmpty()) return;
    m_rects.push_back(rect);
}

void DirtyRegion::addCircle(const QPoint& center, int radius)
{
    if (radius <= 0) return;
    addRect(QRect(center.x() - radius, center.y() - radius,
                  radius * 2, radius * 2));
}

QRect DirtyRegion::boundingRect() const
{
    if (m_rects.empty())
        return {};
    QRect b = m_rects.front();
    for (const auto& r : m_rects)
        b = b.united(r);
    return b;
}

int DirtyRegion::distance(const QRect& a, const QRect& b)
{
    int dx = 0, dy = 0;
    if (a.right() < b.left())
        dx = b.left() - a.right();
    else if (b.right() < a.left())
        dx = a.left() - b.right();
    if (a.bottom() < b.top())
        dy = b.top() - a.bottom();
    else if (b.bottom() < a.top())
        dy = a.top() - b.bottom();
    return std::max(dx, dy);
}

bool DirtyRegion::shouldMerge(const QRect& a, const QRect& b)
{
    // Merge if they overlap or are within 256px of each other
    return distance(a, b) <= 256;
}

QRect DirtyRegion::merged(const QRect& a, const QRect& b)
{
    return a.united(b);
}

void DirtyRegion::consolidate(int maxRects)
{
    if (static_cast<int>(m_rects.size()) <= maxRects)
        return;

    while (static_cast<int>(m_rects.size()) > maxRects) {
        // Find closest pair
        int bestI = 0, bestJ = 1;
        int bestDist = INT_MAX;

        for (int i = 0; i < static_cast<int>(m_rects.size()); ++i) {
            for (int j = i + 1; j < static_cast<int>(m_rects.size()); ++j) {
                int d = distance(m_rects[i], m_rects[j]);
                if (d < bestDist) {
                    bestDist = d;
                    bestI = i;
                    bestJ = j;
                }
            }
        }

        // Merge them
        m_rects[bestI] = merged(m_rects[bestI], m_rects[bestJ]);
        m_rects.erase(m_rects.begin() + bestJ);
    }
}

} // namespace core
