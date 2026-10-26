#include "Builder.hpp"

namespace psd {

// --- PixelData ----------------------------------------------------------------

PixelData PixelData::rgba8(std::vector<uint8_t> v) { return PixelData(Kind::Rgba8, std::move(v)); }
PixelData PixelData::rgba16(std::vector<uint16_t> v) { return PixelData(Kind::Rgba16, std::move(v)); }
PixelData PixelData::grayA8(std::vector<uint8_t> v) { return PixelData(Kind::GrayA8, std::move(v)); }
PixelData PixelData::grayA16(std::vector<uint16_t> v) { return PixelData(Kind::GrayA16, std::move(v)); }
PixelData PixelData::cmyka8(std::vector<uint8_t> v) { return PixelData(Kind::Cmyka8, std::move(v)); }
PixelData PixelData::cmyka16(std::vector<uint16_t> v) { return PixelData(Kind::Cmyka16, std::move(v)); }

std::pair<ColorMode, uint16_t> PixelData::modeDepth() const {
    switch (m_kind) {
    case Kind::Rgba8: return {ColorMode::Rgb, 8};
    case Kind::Rgba16: return {ColorMode::Rgb, 16};
    case Kind::GrayA8: return {ColorMode::Grayscale, 8};
    case Kind::GrayA16: return {ColorMode::Grayscale, 16};
    case Kind::Cmyka8: return {ColorMode::Cmyk, 8};
    case Kind::Cmyka16: return {ColorMode::Cmyk, 16};
    }
    return {ColorMode::Rgb, 8};
}

size_t PixelData::spp() const {
    switch (modeDepth().first.kind()) {
    case ColorMode::Kind::Rgb: return 4;
    case ColorMode::Kind::Grayscale: return 2;
    default: return 5;
    }
}

size_t PixelData::len() const {
    switch (m_kind) {
    case Kind::Rgba8:
    case Kind::GrayA8:
    case Kind::Cmyka8:
        return as8().size();
    case Kind::Rgba16:
    case Kind::GrayA16:
    case Kind::Cmyka16:
        return as16().size();
    }
    return 0;
}

std::vector<uint8_t> PixelData::plane(size_t c, size_t n) const {
    size_t sppv = spp();
    bool invert = modeDepth().first == ColorMode::Cmyk && c < 4;
    std::vector<uint8_t> out;
    switch (m_kind) {
    case Kind::Rgba8:
    case Kind::GrayA8:
    case Kind::Cmyka8: {
        const auto& v = as8();
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            uint8_t s = v[i * sppv + c];
            out.push_back(invert ? static_cast<uint8_t>(255 - s) : s);
        }
        return out;
    }
    case Kind::Rgba16:
    case Kind::GrayA16:
    case Kind::Cmyka16: {
        const auto& v = as16();
        out.reserve(n * 2);
        for (size_t i = 0; i < n; ++i) {
            uint16_t s = v[i * sppv + c];
            uint16_t val = invert ? static_cast<uint16_t>(65535 - s) : s;
            out.push_back(static_cast<uint8_t>(val >> 8));
            out.push_back(static_cast<uint8_t>(val));
        }
        return out;
    }
    }
    return out;
}

bool PixelData::alphaIsOpaque(size_t n) const {
    size_t sppv = spp();
    switch (m_kind) {
    case Kind::Rgba8:
    case Kind::GrayA8:
    case Kind::Cmyka8: {
        const auto& v = as8();
        for (size_t i = 0; i < n; ++i) {
            if (v[i * sppv + sppv - 1] != 255) return false;
        }
        return true;
    }
    case Kind::Rgba16:
    case Kind::GrayA16:
    case Kind::Cmyka16: {
        const auto& v = as16();
        for (size_t i = 0; i < n; ++i) {
            if (v[i * sppv + sppv - 1] != 65535) return false;
        }
        return true;
    }
    }
    return true;
}

std::vector<uint8_t> PixelData::compositePlane(size_t c, size_t n) const {
    // Photoshop stores transparent RGB composites against white; layer
    // channels stay straight, and so do Gray and CMYK composites here.
    if (m_kind == Kind::Rgba8 && c < 3) {
        const auto& v = as8();
        std::vector<uint8_t> out;
        out.reserve(n);
        for (size_t i = 0; i < n && i * 4 + 3 < v.size(); ++i) {
            uint32_t color = v[i * 4 + c];
            uint32_t alpha = v[i * 4 + 3];
            out.push_back(alpha == 0 ? static_cast<uint8_t>(color) : static_cast<uint8_t>((color * alpha + 127) / 255 + 255 - alpha));
        }
        return out;
    }
    if (m_kind == Kind::Rgba16 && c < 3) {
        const auto& v = as16();
        std::vector<uint8_t> out;
        out.reserve(n * 2);
        for (size_t i = 0; i < n && i * 4 + 3 < v.size(); ++i) {
            uint32_t color = v[i * 4 + c];
            uint32_t alpha = v[i * 4 + 3];
            uint16_t value =
                alpha == 0 ? static_cast<uint16_t>(color) : static_cast<uint16_t>((color * alpha + 32767) / 65535 + 65535 - alpha);
            out.push_back(static_cast<uint8_t>(value >> 8));
            out.push_back(static_cast<uint8_t>(value));
        }
        return out;
    }
    return plane(c, n);
}

// --- PsdBuilder -----------------------------------------------------------------

struct PsdBuilder::Entry {
    enum class Kind { Layer, GroupStart, GroupEnd };
    Kind kind;
    std::optional<LayerSpec> layerSpec;
    std::optional<GroupSpec> groupSpec;
};

PsdBuilder::PsdBuilder(uint32_t width, uint32_t height) : m_width(width), m_height(height) {}

PsdBuilder& PsdBuilder::colorMode(ColorMode mode) {
    m_colorMode = mode;
    return *this;
}
PsdBuilder& PsdBuilder::depth(uint16_t d) {
    m_depth = d;
    return *this;
}
PsdBuilder& PsdBuilder::version(Version v) {
    m_version = v;
    return *this;
}
PsdBuilder& PsdBuilder::compression(Compression c) {
    m_compression = c;
    return *this;
}
PsdBuilder& PsdBuilder::resolution(double dpi) {
    m_resources.push_back(ImageResource::make(ids::kResolutionInfo, ResolutionInfo::fromDpi(dpi).toBytes()));
    return *this;
}
PsdBuilder& PsdBuilder::iccProfile(std::vector<uint8_t> icc) {
    m_resources.push_back(ImageResource::make(ids::kIccProfile, std::move(icc)));
    return *this;
}
PsdBuilder& PsdBuilder::resource(ImageResource r) {
    m_resources.push_back(std::move(r));
    return *this;
}

PsdBuilder& PsdBuilder::pushLayer(LayerSpec spec) {
    auto e = std::make_shared<Entry>();
    e->kind = Entry::Kind::Layer;
    e->layerSpec = std::move(spec);
    m_entries.push_back(std::move(e));
    return *this;
}

PsdBuilder& PsdBuilder::beginGroup(GroupSpec spec) {
    auto e = std::make_shared<Entry>();
    e->kind = Entry::Kind::GroupStart;
    m_entries.push_back(std::move(e));
    m_openGroups.push_back(std::move(spec));
    return *this;
}

Result<void> PsdBuilder::endGroup() {
    if (m_openGroups.empty()) return PsdError::invalid("end_group without begin_group");
    GroupSpec g = std::move(m_openGroups.back());
    m_openGroups.pop_back();
    auto e = std::make_shared<Entry>();
    e->kind = Entry::Kind::GroupEnd;
    e->groupSpec = std::move(g);
    m_entries.push_back(std::move(e));
    return {};
}

PsdBuilder& PsdBuilder::composite(PixelData pixels) {
    m_composite = std::move(pixels);
    return *this;
}

Result<size_t> PsdBuilder::colorChannels() const {
    switch (m_colorMode.kind()) {
    case ColorMode::Kind::Rgb: return size_t{3};
    case ColorMode::Kind::Grayscale: return size_t{1};
    case ColorMode::Kind::Cmyk: return size_t{4};
    default: return PsdError::unsupported("builder color mode");
    }
}

Result<void> PsdBuilder::checkPixels(const PixelData& p, size_t n) const {
    auto [mode, depthv] = p.modeDepth();
    if (!(mode == m_colorMode && depthv == m_depth)) {
        return PsdError::invalid("pixel format does not match document color mode/depth");
    }
    if (p.len() != n * p.spp()) {
        return PsdError::invalid("pixel buffer has " + std::to_string(p.len()) + " samples, expected " + std::to_string(n * p.spp()));
    }
    return {};
}

Result<ChannelData> PsdBuilder::channel(int16_t id, std::span<const uint8_t> planeData, size_t w, size_t h) const {
    return ChannelData::encode(id, m_compression, planeData, w, h, m_depth, m_version);
}

Result<std::vector<ChannelData>> PsdBuilder::emptyChannels(size_t cc) const {
    std::vector<ChannelData> v;
    PSD_TRY_ASSIGN(auto first, channel(-1, {}, 0, 0));
    v.push_back(std::move(first));
    for (size_t c = 0; c < cc; ++c) {
        PSD_TRY_ASSIGN(auto ch, channel(static_cast<int16_t>(c), {}, 0, 0));
        v.push_back(std::move(ch));
    }
    return v;
}

Result<LayerRecord> PsdBuilder::layerRecord(const LayerSpec& spec, size_t cc, uint32_t id) const {
    size_t w = spec.width, h = spec.height;
    size_t n = w * h;
    PSD_TRY(checkPixels(spec.pixels, n));
    std::vector<ChannelData> channels;
    PSD_TRY_ASSIGN(auto alphaCh, channel(-1, spec.pixels.plane(cc, n), w, h));
    channels.push_back(std::move(alphaCh));
    for (size_t c = 0; c < cc; ++c) {
        PSD_TRY_ASSIGN(auto ch, channel(static_cast<int16_t>(c), spec.pixels.plane(c, n), w, h));
        channels.push_back(std::move(ch));
    }
    MaskData mask = MaskData::none();
    if (spec.mask.has_value()) {
        const auto& m = *spec.mask;
        PSD_TRY_ASSIGN(auto mwh, m.rect.size());
        if (m.data.size() != mwh.first * mwh.second) return PsdError::invalid("mask buffer size mismatch");
        std::vector<uint8_t> planeData;
        if (m_depth == 16) {
            planeData.reserve(m.data.size() * 2);
            for (uint8_t v : m.data) {
                uint16_t scaled = static_cast<uint16_t>(static_cast<uint32_t>(v) * 257);
                planeData.push_back(static_cast<uint8_t>(scaled >> 8));
                planeData.push_back(static_cast<uint8_t>(scaled));
            }
        } else {
            planeData = m.data;
        }
        PSD_TRY_ASSIGN(auto maskCh, channel(-2, planeData, mwh.first, mwh.second));
        channels.push_back(std::move(maskCh));
        uint8_t flags = m.disabled ? LayerMask::kFlagDisabled : 0;
        mask = MaskData::mask(LayerMask::make(m.rect, m.defaultColor, flags));
    }
    LayerFlags flags;
    flags.setHidden(!spec.visible);
    std::vector<TaggedBlock> blocks = {TaggedBlock::unicodeName(spec.name), TaggedBlock::layerId(id)};
    if (spec.fillOpacity.has_value()) blocks.push_back(TaggedBlock::fillOpacity(*spec.fillOpacity));
    blocks.insert(blocks.end(), spec.extraBlocks.begin(), spec.extraBlocks.end());

    LayerRecord rec;
    rec.rect = Rect::fromXywh(spec.left, spec.top, spec.width, spec.height);
    rec.channels = std::move(channels);
    rec.blendMode = spec.blendMode;
    rec.opacity = spec.opacity;
    rec.clipping = spec.clipping ? 1 : 0;
    rec.flags = flags;
    rec.filler = 0;
    rec.mask = mask;
    rec.blendingRanges = BlendingRanges::full(cc);
    rec.name = encodeLegacyName(spec.name);
    rec.blocks = std::move(blocks);
    rec.extraTrailing = {};
    return rec;
}

Result<PsdFile> PsdBuilder::build() const {
    if (!m_openGroups.empty()) return PsdError::invalid("unclosed group");
    if (m_depth != 8 && m_depth != 16) return PsdError::unsupported("builder depth " + std::to_string(m_depth));
    PSD_TRY_ASSIGN(auto cc, colorChannels());

    std::vector<LayerRecord> layers;
    uint32_t id = 1;
    for (const auto& e : m_entries) {
        if (e->kind == Entry::Kind::Layer) {
            PSD_TRY_ASSIGN(auto rec, layerRecord(*e->layerSpec, cc, id));
            layers.push_back(std::move(rec));
        } else if (e->kind == Entry::Kind::GroupStart) {
            PSD_TRY_ASSIGN(auto chans, emptyChannels(cc));
            LayerRecord rec;
            rec.channels = std::move(chans);
            rec.blendingRanges = BlendingRanges::full(cc);
            rec.name = {'<', '/', 'L', 'a', 'y', 'e', 'r', ' ', 'g', 'r', 'o', 'u', 'p', '>'};
            rec.blocks = {TaggedBlock::unicodeName("</Layer group>"), TaggedBlock::layerId(id),
                          TaggedBlock::sectionDivider(SectionType::BoundingDivider, std::nullopt, std::nullopt)};
            layers.push_back(std::move(rec));
        } else {
            const GroupSpec& g = *e->groupSpec;
            SectionType kind = g.open ? SectionType::OpenFolder : SectionType::ClosedFolder;
            LayerFlags flags;
            flags.setHidden(!g.visible);
            PSD_TRY_ASSIGN(auto chans, emptyChannels(cc));
            LayerRecord rec;
            rec.channels = std::move(chans);
            rec.blendMode = g.blendMode;
            rec.opacity = g.opacity;
            rec.flags = flags;
            rec.blendingRanges = BlendingRanges::full(cc);
            rec.name = encodeLegacyName(g.name);
            rec.blocks = {TaggedBlock::unicodeName(g.name), TaggedBlock::layerId(id), TaggedBlock::sectionDivider(kind, g.blendMode, std::nullopt)};
            layers.push_back(std::move(rec));
        }
        ++id;
    }

    size_t w = m_width, h = m_height;
    size_t n = w * h;
    bool real = false, withAlpha = false;
    std::vector<uint8_t> planes;
    if (m_composite.has_value()) {
        PSD_TRY(checkPixels(*m_composite, n));
        withAlpha = !m_composite->alphaIsOpaque(n);
        real = true;
        for (size_t c = 0; c < cc + (withAlpha ? 1 : 0); ++c) {
            auto p = withAlpha ? m_composite->compositePlane(c, n) : m_composite->plane(c, n);
            planes.insert(planes.end(), p.begin(), p.end());
        }
    } else {
        size_t bps = m_depth / 8;
        // White: 0xff in RGB/gray; no ink (stored inverted as 0xff) in CMYK.
        planes.assign(n * bps * cc, 0xff);
    }
    uint16_t channelsCount = static_cast<uint16_t>(cc + (withAlpha ? 1 : 0));
    Header header(m_version, m_width, m_height, channelsCount, m_depth, m_colorMode);
    PSD_TRY(header.validate());
    PSD_TRY_ASSIGN(auto imageData, ImageData::encode(m_compression, planes, header));
    std::vector<ImageResource> resources = m_resources;
    resources.push_back(versionInfoResource(real));

    bool hasLayers = !layers.empty();
    std::optional<LayerInfo> layerInfo;
    if (hasLayers) {
        LayerInfo info;
        info.mergedAlpha = withAlpha;
        info.layers = std::move(layers);
        info.padding = std::nullopt;
        layerInfo = std::move(info);
    }
    LayerInfoPlacement placement = LayerInfoPlacement::section();
    if (hasLayers && m_depth == 16) {
        LayerInfoPlacement::GlobalBlockInfo gbi;
        gbi.index = 0;
        gbi.signature = {'8', 'B', 'I', 'M'};
        gbi.key = {'L', 'r', '1', '6'};
        gbi.padding = std::nullopt;
        placement = LayerInfoPlacement::globalBlock(gbi);
    }

    PsdFile file;
    file.header = header;
    file.colorModeData = {};
    file.resources = std::move(resources);
    file.layerInfo = std::move(layerInfo);
    file.layerInfoPlacement = placement;
    file.globalLayerMask = hasLayers ? std::optional<GlobalLayerMask>(GlobalLayerMask{}) : std::nullopt;
    file.globalBlocks = {};
    file.layerMaskTrailing = {};
    file.imageData = std::move(imageData);
    return file;
}

Result<std::vector<uint8_t>> PsdBuilder::toBytes() const {
    PSD_TRY_ASSIGN(auto file, build());
    return file.toBytes();
}

} // namespace psd
