#pragma once

#include <QHash>
#include <QImage>
#include <QRect>
#include <QUuid>
#include <QVariantMap>

#include <map>
#include <optional>

#include "AnimationTypes.hpp"
#include "core/RasterLayerStorage.hpp"

namespace anim {

using CelId = QUuid;

// One independently editable raster payload. cpuImage is the base-sized image;
// rasterStorage carries sparse/out-of-bounds tile edits using the same contract
// as Layer. Bounds and metadata are explicit so future cel thumbnails/onion skin
// do not need to infer them from the currently evaluated Layer.
struct RasterCelContent {
    QImage cpuImage;
    core::RasterLayerStorage rasterStorage;
    QRect bounds;
    QVariantMap metadata;
};

// Sparse hold track. A key maps a frame either to a CelId or to std::nullopt,
// which is an explicit empty frame. The selected value remains active until the
// next key. Frames before the first key are empty.
class RasterCelTrack {
public:
    using Keyframes = std::map<Frame, std::optional<CelId>>;

    bool isEmpty() const { return m_keyframes.empty(); }
    int keyframeCount() const { return static_cast<int>(m_keyframes.size()); }
    const Keyframes& keyframes() const { return m_keyframes; }

    bool hasKeyframe(Frame frame) const;
    std::optional<CelId> keyframeCel(Frame frame) const;
    std::optional<CelId> celAt(Frame frame) const;

    // Adds or updates an exposure. A null CelId is normalized to an explicit
    // empty frame, never stored as a seemingly valid cel reference.
    void setCel(Frame frame, std::optional<CelId> celId);
    bool removeKeyframe(Frame frame);
    bool moveKeyframe(Frame from, Frame to);
    void clear() { m_keyframes.clear(); }

private:
    Keyframes m_keyframes;
};

// Document-owned cel payload repository. Multiple frame keys (and even copied
// layer tracks) may intentionally reference the same CelId. cloneCel performs
// the pixel detachment used by copy-on-write editing.
class CelStorage {
public:
    CelStorage() = default;
    CelStorage(const CelStorage& other);
    CelStorage& operator=(const CelStorage& other);
    CelStorage(CelStorage&&) noexcept = default;
    CelStorage& operator=(CelStorage&&) noexcept = default;
    bool isEmpty() const { return m_cels.isEmpty(); }
    int size() const { return m_cels.size(); }
    QList<CelId> ids() const { return m_cels.keys(); }

    bool contains(const CelId& id) const;
    const RasterCelContent* content(const CelId& id) const;
    RasterCelContent* content(const CelId& id);
    std::shared_ptr<const RasterCelContent> contentHandle(const CelId& id) const;
    std::shared_ptr<RasterCelContent> contentHandle(const CelId& id);

    CelId createCel(RasterCelContent content = {});
    bool insertCel(const CelId& id, RasterCelContent content);
    CelId cloneCel(const CelId& sourceId);
    bool removeCel(const CelId& id);
    void clear() { m_cels.clear(); }

private:
    static RasterCelContent deepCopy(const RasterCelContent& source);

    QHash<CelId, std::shared_ptr<RasterCelContent>> m_cels;
};

} // namespace anim
