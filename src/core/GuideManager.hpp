#pragma once

#include "GuideTypes.hpp"

#include <vector>

class GuideManager {
public:
    const std::vector<Guide>& guides() const { return m_guides; }
    std::vector<Guide>& guides() { return m_guides; }
    int guideCount() const { return static_cast<int>(m_guides.size()); }
    bool empty() const { return m_guides.empty(); }

    Guide guideAt(int index) const;
    int addGuide(GuideOrientation orientation, qreal position);
    void insertGuide(int index, const Guide& guide);
    void moveGuide(int index, qreal position);
    Guide removeGuide(int index);
    void clear();
    void setGuides(std::vector<Guide> guides);

private:
    int nextGuideId();

    std::vector<Guide> m_guides;
    int m_nextId = 1;
};

