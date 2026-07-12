#include "RasterCelModel.hpp"

#include <utility>

namespace anim {

CelStorage::CelStorage(const CelStorage& other)
{
    *this = other;
}

CelStorage& CelStorage::operator=(const CelStorage& other)
{
    if (this == &other)
        return *this;
    m_cels.clear();
    for (auto it = other.m_cels.constBegin(); it != other.m_cels.constEnd(); ++it) {
        if (it.value())
            m_cels.insert(it.key(),
                std::make_shared<RasterCelContent>(deepCopy(*it.value())));
    }
    return *this;
}

bool RasterCelTrack::hasKeyframe(Frame frame) const
{
    return m_keyframes.find(frame) != m_keyframes.end();
}

std::optional<CelId> RasterCelTrack::keyframeCel(Frame frame) const
{
    const auto it = m_keyframes.find(frame);
    return it == m_keyframes.end() ? std::nullopt : it->second;
}

std::optional<CelId> RasterCelTrack::celAt(Frame frame) const
{
    auto it = m_keyframes.upper_bound(frame);
    if (it == m_keyframes.begin())
        return std::nullopt;
    --it;
    return it->second;
}

void RasterCelTrack::setCel(Frame frame, std::optional<CelId> celId)
{
    if (celId && celId->isNull())
        celId.reset();
    m_keyframes[frame] = std::move(celId);
}

bool RasterCelTrack::removeKeyframe(Frame frame)
{
    return m_keyframes.erase(frame) > 0;
}

bool RasterCelTrack::moveKeyframe(Frame from, Frame to)
{
    const auto it = m_keyframes.find(from);
    if (it == m_keyframes.end() || from == to)
        return false;
    std::optional<CelId> celId = it->second;
    m_keyframes.erase(it);
    m_keyframes[to] = std::move(celId);
    return true;
}

bool CelStorage::contains(const CelId& id) const
{
    return !id.isNull() && m_cels.contains(id);
}

const RasterCelContent* CelStorage::content(const CelId& id) const
{
    const auto it = m_cels.constFind(id);
    return it == m_cels.constEnd() ? nullptr : it.value().get();
}

RasterCelContent* CelStorage::content(const CelId& id)
{
    auto it = m_cels.find(id);
    return it == m_cels.end() ? nullptr : it.value().get();
}

std::shared_ptr<const RasterCelContent> CelStorage::contentHandle(const CelId& id) const
{
    const auto it = m_cels.constFind(id);
    return it == m_cels.constEnd() ? nullptr : it.value();
}

std::shared_ptr<RasterCelContent> CelStorage::contentHandle(const CelId& id)
{
    const auto it = m_cels.find(id);
    return it == m_cels.end() ? nullptr : it.value();
}

CelId CelStorage::createCel(RasterCelContent content)
{
    CelId id;
    do {
        id = QUuid::createUuid();
    } while (m_cels.contains(id));
    m_cels.insert(id, std::make_shared<RasterCelContent>(std::move(content)));
    return id;
}

bool CelStorage::insertCel(const CelId& id, RasterCelContent content)
{
    if (id.isNull() || m_cels.contains(id))
        return false;
    m_cels.insert(id, std::make_shared<RasterCelContent>(std::move(content)));
    return true;
}

RasterCelContent CelStorage::deepCopy(const RasterCelContent& source)
{
    RasterCelContent copy;
    copy.cpuImage = source.cpuImage.copy();
    copy.bounds = source.bounds;
    copy.metadata = source.metadata;

    if (source.rasterStorage.isEnabled()) {
        QRect tileBounds;
        const QImage tiles = source.rasterStorage.toImage(&tileBounds);
        copy.rasterStorage.replaceWithImage(
            tiles, tileBounds.topLeft(), source.rasterStorage.tileSize());
        copy.rasterStorage.setBaseSize(source.rasterStorage.baseSize());
        copy.rasterStorage.markAllGpuDirty();
    }
    return copy;
}

CelId CelStorage::cloneCel(const CelId& sourceId)
{
    const RasterCelContent* source = content(sourceId);
    return source ? createCel(deepCopy(*source)) : CelId{};
}

bool CelStorage::removeCel(const CelId& id)
{
    return m_cels.remove(id) > 0;
}

} // namespace anim
