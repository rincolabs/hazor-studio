#pragma once

#include <QRect>
#include <QPoint>
#include <vector>

namespace core {

class DirtyRegion {
public:
    DirtyRegion() = default;

    void addRect(const QRect& rect);
    void addCircle(const QPoint& center, int radius);

    QRect boundingRect() const;
    bool isEmpty() const { return m_rects.empty(); }
    void clear() { m_rects.clear(); }

    // Merge overlapping / nearby rects to reduce count.
    void consolidate(int maxRects = 16);

private:
    std::vector<QRect> m_rects;

    static bool shouldMerge(const QRect& a, const QRect& b);
    static QRect merged(const QRect& a, const QRect& b);
    static int distance(const QRect& a, const QRect& b);
};

} // namespace core
