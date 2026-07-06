#include "GuideManager.hpp"

#include <algorithm>

Guide GuideManager::guideAt(int index) const
{
    if (index < 0 || index >= guideCount())
        return {};
    return m_guides[static_cast<size_t>(index)];
}

int GuideManager::addGuide(GuideOrientation orientation, qreal position)
{
    Guide guide;
    guide.orientation = orientation;
    guide.position = position;
    guide.id = nextGuideId();
    m_guides.push_back(guide);
    return guideCount() - 1;
}

void GuideManager::insertGuide(int index, const Guide& guide)
{
    Guide copy = guide;
    if (copy.id <= 0)
        copy.id = nextGuideId();
    m_nextId = std::max(m_nextId, copy.id + 1);

    const int clamped = std::clamp(index, 0, guideCount());
    m_guides.insert(m_guides.begin() + clamped, copy);
}

void GuideManager::moveGuide(int index, qreal position)
{
    if (index < 0 || index >= guideCount())
        return;
    m_guides[static_cast<size_t>(index)].position = position;
}

Guide GuideManager::removeGuide(int index)
{
    if (index < 0 || index >= guideCount())
        return {};
    auto it = m_guides.begin() + index;
    Guide guide = *it;
    m_guides.erase(it);
    return guide;
}

void GuideManager::clear()
{
    m_guides.clear();
}

void GuideManager::setGuides(std::vector<Guide> guides)
{
    m_guides = std::move(guides);
    m_nextId = 1;
    for (const auto& guide : m_guides)
        m_nextId = std::max(m_nextId, guide.id + 1);
}

int GuideManager::nextGuideId()
{
    return m_nextId++;
}

