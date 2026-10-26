#include "Testgen.hpp"

#include <array>
#include <cstring>
#include <sstream>

#include "psd/Builder.hpp"
#include "psd/Io.hpp"
#include "psd/Layer.hpp"
#include "psd/Resources.hpp"
#include "psd/Tagged.hpp"

namespace psd::testgen {

std::vector<uint8_t> patternPlane(size_t w, size_t h, uint16_t depth, uint32_t seed) {
    std::vector<uint8_t> out;
    out.reserve(rowBytes(w, depth) * h);
    size_t s = seed;
    for (size_t y = 0; y < h; ++y) {
        switch (depth) {
        case 1: {
            size_t rb = rowBytes(w, 1);
            for (size_t bx = 0; bx < rb; ++bx) {
                out.push_back(static_cast<uint8_t>(bx * 37 + y * 11 + s) ^ 0x5a);
            }
            break;
        }
        case 8: {
            for (size_t x = 0; x < w; ++x) {
                // Mix smooth gradients and runs so RLE sees both.
                uint8_t v = ((x / 4 + y) % 3 == 0) ? static_cast<uint8_t>(s * 17) : static_cast<uint8_t>(x * 3 + y * 7 + s);
                out.push_back(v);
            }
            break;
        }
        case 16: {
            for (size_t x = 0; x < w; ++x) {
                uint16_t v = static_cast<uint16_t>((x * 997 + y * 131 + s * 7919) % 65536);
                out.push_back(static_cast<uint8_t>(v >> 8));
                out.push_back(static_cast<uint8_t>(v));
            }
            break;
        }
        default: {
            for (size_t x = 0; x < w; ++x) {
                float v = static_cast<float>((x + y + s) % 17) / 16.0f;
                uint32_t bits;
                std::memcpy(&bits, &v, 4);
                out.push_back(static_cast<uint8_t>(bits >> 24));
                out.push_back(static_cast<uint8_t>(bits >> 16));
                out.push_back(static_cast<uint8_t>(bits >> 8));
                out.push_back(static_cast<uint8_t>(bits));
            }
            break;
        }
        }
    }
    return out;
}

uint16_t modeChannels(ColorMode mode) {
    if (mode.kind() == ColorMode::Kind::Multichannel) return 2;
    auto cc = mode.colorChannels();
    return cc.has_value() ? static_cast<uint16_t>(*cc) : uint16_t{1};
}

std::vector<uint16_t> modeDepths(ColorMode mode) {
    switch (mode.kind()) {
    case ColorMode::Kind::Bitmap: return {1};
    case ColorMode::Kind::Indexed:
    case ColorMode::Kind::Duotone:
        return {8};
    case ColorMode::Kind::Multichannel:
    case ColorMode::Kind::Cmyk:
    case ColorMode::Kind::Lab:
        return {8, 16};
    default:
        return {8, 16, 32};
    }
}

const std::array<ColorMode, 8>& modes() {
    static const std::array<ColorMode, 8> kModes = {ColorMode::Bitmap,  ColorMode::Grayscale, ColorMode::Indexed,     ColorMode::Rgb,
                                                      ColorMode::Cmyk,    ColorMode::Lab,       ColorMode::Multichannel, ColorMode::Duotone};
    return kModes;
}

namespace {

std::vector<uint8_t> colorModeData(ColorMode mode) {
    if (mode.kind() == ColorMode::Kind::Indexed) {
        std::vector<uint8_t> v(768);
        for (uint32_t i = 0; i < 768; ++i) v[i] = static_cast<uint8_t>(i * 7 % 256);
        return v;
    }
    if (mode.kind() == ColorMode::Kind::Duotone) {
        // Duotone data is undocumented; any bytes are preserved.
        std::vector<uint8_t> v(24);
        for (uint8_t i = 0; i < 24; ++i) v[i] = i;
        return v;
    }
    return {};
}

std::vector<ImageResource> baseResources() {
    auto named = ImageResource::make(2000, {1, 2, 3});
    named.name = {'P', 'a', 't', 'h', ' ', '1'};
    std::vector<uint8_t> xmp = {'<', 'x', ':', 'x', 'm', 'p', 'm', 'e', 't', 'a', ' ', 'x', 'm', 'l', 'n', 's', ':', 'x', '=',
                                '\'', 'a', 'd', 'o', 'b', 'e', ':', 'n', 's', ':', 'm', 'e', 't', 'a', '/', '\'', '/', '>'};
    std::vector<uint8_t> angle = {0, 0, 0, 30};
    std::vector<uint8_t> altitude = {0, 0, 0, 30};
    return {
        ImageResource::make(ids::kResolutionInfo, ResolutionInfo::fromDpi(72.0).toBytes()),
        ImageResource::make(ids::kGlobalAngle, angle),
        ImageResource::make(ids::kGlobalAltitude, altitude),
        ImageResource::make(ids::kIccProfile, std::vector<uint8_t>(13, 0x42)),
        ImageResource::make(ids::kXmp, xmp),
        named,
        versionInfoResource(true),
    };
}

} // namespace

PsdFile mergedOnly(Version version, ColorMode mode, uint16_t depth, Compression compression, uint32_t w, uint32_t h) {
    uint16_t channels = modeChannels(mode);
    Header header(version, w, h, channels, depth, mode);
    std::vector<uint8_t> planes;
    for (uint16_t c = 0; c < channels; ++c) {
        auto p = patternPlane(w, h, depth, c);
        planes.insert(planes.end(), p.begin(), p.end());
    }
    auto encoded = ImageData::encode(compression, planes, header);
    ImageData imageData = encoded ? encoded.value() : ImageData{compression, {}};
    PsdFile file;
    file.header = header;
    file.colorModeData = colorModeData(mode);
    file.resources = baseResources();
    file.layerInfo = std::nullopt;
    file.layerInfoPlacement = LayerInfoPlacement::section();
    file.globalLayerMask = std::nullopt;
    file.globalBlocks = {};
    file.layerMaskTrailing = {};
    file.imageData = imageData;
    return file;
}

namespace {

// Parameters for a generated raster layer.
struct LayerGen {
    const Header& header;
    Compression compression;
    Rect rect;
    std::string name;
    BlendMode blend;
    std::optional<LayerMask> mask;
    uint32_t seed;
};

LayerRecord rasterLayer(const LayerGen& g) {
    auto wh = g.rect.size();
    size_t w = wh ? wh.value().first : 0, h = wh ? wh.value().second : 0;
    uint16_t depth = g.header.depth;
    Version v = g.header.version;
    int16_t cc = static_cast<int16_t>(modeChannels(g.header.colorMode));
    std::vector<ChannelData> channels;
    for (int16_t id = -1; id < cc; ++id) {
        auto plane = patternPlane(w, h, depth, g.seed + static_cast<uint32_t>(id + 1));
        Compression comp = (id == -1 && g.compression == Compression::Rle) ? Compression::Zip : g.compression;
        auto cd = ChannelData::encode(id, comp, plane, w, h, depth, v);
        channels.push_back(cd.value());
    }
    MaskData maskData = MaskData::none();
    if (g.mask.has_value()) {
        const LayerMask& m = *g.mask;
        auto mwh = m.rect.size();
        size_t mw = mwh ? mwh.value().first : 0, mh = mwh ? mwh.value().second : 0;
        auto maskPlane = patternPlane(mw, mh, depth, g.seed + 99);
        auto maskCd = ChannelData::encode(-2, g.compression, maskPlane, mw, mh, depth, v);
        channels.push_back(maskCd.value());
        if (m.real.has_value()) {
            auto rwh = m.real->rect.size();
            size_t rw = rwh ? rwh.value().first : 0, rh = rwh ? rwh.value().second : 0;
            auto realPlane = patternPlane(rw, rh, depth, g.seed + 7);
            auto realCd = ChannelData::encode(-3, g.compression, realPlane, rw, rh, depth, v);
            channels.push_back(realCd.value());
        }
        maskData = MaskData::mask(m);
    }
    LayerFlags flags;
    flags.setHidden(g.seed % 5 == 4);
    LayerRecord rec;
    rec.rect = g.rect;
    rec.channels = std::move(channels);
    rec.blendMode = g.blend;
    rec.opacity = static_cast<uint8_t>(255 - (g.seed * 13) % 128);
    rec.clipping = (g.seed % 7 == 3) ? 1 : 0;
    rec.flags = flags;
    rec.filler = 0;
    rec.mask = maskData;
    rec.blendingRanges = BlendingRanges::full(static_cast<size_t>(cc));
    rec.name = encodeLegacyName(g.name);
    rec.blocks = {TaggedBlock::unicodeName(g.name), TaggedBlock::layerId(g.seed + 1)};
    rec.extraTrailing = {};
    return rec;
}

LayerRecord groupRecord(const Header& header, const std::string& name, SectionType kind, BlendMode blend, uint32_t id) {
    int16_t cc = static_cast<int16_t>(modeChannels(header.colorMode));
    std::vector<ChannelData> channels;
    for (int16_t c = -1; c < cc; ++c) {
        ChannelData cd;
        cd.id = c;
        cd.compression = Compression::Raw;
        cd.data = {};
        channels.push_back(cd);
    }
    TaggedBlock lsct = (kind == SectionType::BoundingDivider) ? TaggedBlock::sectionDivider(kind, std::nullopt, std::nullopt)
                                                               : TaggedBlock::sectionDivider(kind, blend, uint32_t{0});
    LayerRecord rec;
    rec.channels = std::move(channels);
    rec.blendMode = blend;
    rec.name = encodeLegacyName(name);
    rec.blendingRanges = BlendingRanges::full(static_cast<size_t>(cc));
    rec.blocks = {TaggedBlock::unicodeName(name), TaggedBlock::layerId(id), lsct};
    return rec;
}

} // namespace

VersionedDescriptor sampleDescriptor() {
    Descriptor drSh("DrSh");
    drSh.with("enab", Value::boolean(true))
        .with("Md  ", Value::enumerated(Id::fromString("BlnM"), Id::fromString("Mltp")))
        .with("Clr ", Value::descriptor(Descriptor("RGBC").with("Rd  ", Value::doubleValue(0.0))))
        .with("lagl", Value::unitFloat({'#', 'A', 'n', 'g'}, 120.0))
        .with("Nm  ", Value::text(UnicodeString::fromUtf8Nul("Shadow")));

    Descriptor d("null");
    d.with("Scl ", Value::unitFloat({'#', 'P', 'r', 'c'}, 100.0))
        .with("masterFXSwitch", Value::boolean(true))
        .with("DrSh", Value::descriptor(std::move(drSh)))
        .with("list", Value::list({Value::integer(1), Value::largeInteger(-2)}));
    return VersionedDescriptor::wrap(std::move(d));
}

namespace {

std::vector<TaggedBlock> extraBlocks(uint32_t seed) {
    std::vector<uint8_t> lfx2 = {0, 0, 0, 0};
    auto desc = sampleDescriptor().toBytes();
    lfx2.insert(lfx2.end(), desc.begin(), desc.end());
    TaggedBlock odd = TaggedBlock::make({'z', 'O', 'd', 'd'}, {1, 2, 3});
    odd.padding = std::vector<uint8_t>{0, 0, 0};
    TaggedBlock unpadded = TaggedBlock::make({'z', 'N', 'o', 'p'}, {9});
    unpadded.padding = std::vector<uint8_t>{};
    return {
        TaggedBlock::nameSource({'c', 'o', 'n', 't'}),
        TaggedBlock::blendClippedAsGroup(true),
        TaggedBlock::blendInteriorElements(false),
        TaggedBlock::knockout(0),
        TaggedBlock::protection(seed % 8),
        TaggedBlock::sheetColor(static_cast<uint16_t>(seed % 8)),
        TaggedBlock::fillOpacity(200),
        TaggedBlock::make({'s', 'h', 'm', 'd'}, {0, 0, 0, 0}),
        TaggedBlock::make({'l', 'f', 'x', '2'}, lfx2),
        TaggedBlock::make({'f', 'x', 'r', 'p'}, std::vector<uint8_t>(16, 0)),
        odd,
        unpadded,
    };
}

} // namespace

PsdFile layered(Version version, ColorMode mode, uint16_t depth, Compression compression) {
    uint32_t w = 24, h = 16;
    uint16_t channels = static_cast<uint16_t>(modeChannels(mode) + 1);
    Header header(version, w, h, channels, depth, mode);
    std::vector<LayerRecord> layers;
    uint32_t seed = 0;
    auto next = [&](Rect rect, const std::string& name, BlendMode blend, std::optional<LayerMask> mask) {
        seed += 1;
        LayerGen g{header, compression, rect, name, blend, mask, seed};
        LayerRecord rec = rasterLayer(g);
        if (seed % 3 == 0) {
            auto extra = extraBlocks(seed);
            rec.blocks.insert(rec.blocks.end(), extra.begin(), extra.end());
        }
        layers.push_back(std::move(rec));
    };
    // Background.
    next(Rect::fromXywh(0, 0, w, h), "Background", BlendMode::Normal, std::nullopt);
    // Negative offsets + simple mask.
    next(Rect::fromXywh(-5, -3, 10, 7), "Neg offset \xf0\x9f\x98\x80", BlendMode::Multiply,
         LayerMask::make(Rect::fromXywh(-2, -2, 6, 5), 0, LayerMask::kFlagRelative));
    // Empty layer.
    next(Rect{}, "Empty", BlendMode::Screen, std::nullopt);
    // Large-ish bounds beyond the canvas.
    next(Rect::fromXywh(-100, 5, 400, 2), "Wide", BlendMode::Overlay, std::nullopt);
    // Nested groups: outer [ layer, inner [ masked layer (real mask + params) ] ].
    uint32_t id0 = 1000;
    layers.push_back(groupRecord(header, "</Layer group>", SectionType::BoundingDivider, BlendMode::Normal, id0));
    next(Rect::fromXywh(2, 2, 5, 5), "In outer", BlendMode::Darken, std::nullopt);
    layers.push_back(groupRecord(header, "</Layer group>", SectionType::BoundingDivider, BlendMode::Normal, id0 + 1));
    LayerMask realMask;
    realMask.rect = Rect::fromXywh(1, 1, 4, 3);
    realMask.defaultColor = 255;
    realMask.flags = LayerMask::kFlagParameters;
    MaskParameters mp;
    mp.flags = 0b0011;
    mp.userDensity = 200;
    mp.userFeather = 1.5;
    realMask.parameters = mp;
    realMask.real = RealMask{0, 255, Rect::fromXywh(0, 0, 3, 3)};
    realMask.trailing = {};
    realMask.realFirst = true;
    next(Rect::fromXywh(3, 3, 6, 4), "Gr\xc3\xbc\xc3\x9f" "e", BlendMode::ColorDodge, realMask); // "Grüße"
    layers.push_back(groupRecord(header, "Inner", SectionType::ClosedFolder, BlendMode::Normal, id0 + 2));
    layers.push_back(groupRecord(header, "Outer", SectionType::OpenFolder, BlendMode::PassThrough, id0 + 3));
    // One small layer per blend mode.
    const auto& allModes = BlendMode::all();
    for (size_t i = 0; i < allModes.size(); ++i) {
        std::ostringstream name;
        name << "blend " << allModes[i];
        next(Rect::fromXywh(static_cast<int32_t>(i % 20), static_cast<int32_t>(i % 12), 3, 2), name.str(), allModes[i], std::nullopt);
    }
    next(Rect::fromXywh(0, 0, 2, 2), "unknown blend", BlendMode::fromKey({'z', 'z', 'z', 'z'}), std::nullopt);

    std::vector<uint8_t> planes;
    for (uint16_t c = 0; c < channels; ++c) {
        auto p = patternPlane(w, h, depth, 50 + c);
        planes.insert(planes.end(), p.begin(), p.end());
    }
    auto imageData = ImageData::encode(compression, planes, header);
    LayerInfo layerInfo;
    layerInfo.mergedAlpha = true;
    layerInfo.layers = std::move(layers);
    layerInfo.padding = std::nullopt;

    LayerInfoPlacement placement;
    std::vector<TaggedBlock> blocks;
    if (depth == 16 || depth == 32) {
        LayerInfoPlacement::GlobalBlockInfo gbi;
        gbi.index = 1;
        gbi.signature = {'8', 'B', 'I', 'M'};
        gbi.key = depth == 16 ? std::array<uint8_t, 4>{'L', 'r', '1', '6'} : std::array<uint8_t, 4>{'L', 'r', '3', '2'};
        gbi.padding = std::nullopt;
        placement = LayerInfoPlacement::globalBlock(gbi);
        blocks = {TaggedBlock::make({'P', 'a', 't', 't'}, {}), TaggedBlock::make({'T', 'x', 't', '2'}, {0, 1, 2, 3})};
    } else {
        placement = LayerInfoPlacement::section();
        blocks = {TaggedBlock::make({'P', 'a', 't', 't'}, {}), TaggedBlock::make({'F', 'M', 's', 'k'}, std::vector<uint8_t>(10, 0))};
    }

    PsdFile file;
    file.header = header;
    file.colorModeData = colorModeData(mode);
    file.resources = baseResources();
    file.layerInfo = std::move(layerInfo);
    file.layerInfoPlacement = placement;
    GlobalLayerMask glm;
    glm.data = {0, 0, 0xff, 0xff, 0, 0, 0, 0, 0, 0, 0, 50, 128, 0};
    file.globalLayerMask = glm;
    file.globalBlocks = std::move(blocks);
    file.layerMaskTrailing = {};
    file.imageData = imageData.value();
    return file;
}

PsdFile small(Version version, Compression compression) {
    Header header(version, 4, 3, 4, 8, ColorMode::Rgb);
    auto mask = LayerMask::make(Rect::fromXywh(0, 0, 2, 2), 0, 0);
    std::vector<LayerRecord> layers;
    layers.push_back(rasterLayer(LayerGen{header, compression, Rect::fromXywh(0, 0, 4, 3), "a", BlendMode::Normal, mask, 1}));
    layers.push_back(groupRecord(header, "</Layer group>", SectionType::BoundingDivider, BlendMode::Normal, 9));
    layers.push_back(rasterLayer(LayerGen{header, compression, Rect::fromXywh(-1, 1, 2, 2), "b", BlendMode::Screen, std::nullopt, 2}));
    layers.push_back(groupRecord(header, "g", SectionType::OpenFolder, BlendMode::PassThrough, 10));

    std::vector<uint8_t> planes;
    for (uint16_t c = 0; c < 4; ++c) {
        auto p = patternPlane(4, 3, 8, c);
        planes.insert(planes.end(), p.begin(), p.end());
    }
    PsdFile file;
    file.header = header;
    file.colorModeData = {};
    file.resources = {ImageResource::make(ids::kResolutionInfo, ResolutionInfo::fromDpi(72.0).toBytes())};
    LayerInfo layerInfo;
    layerInfo.mergedAlpha = true;
    layerInfo.layers = std::move(layers);
    layerInfo.padding = std::nullopt;
    file.layerInfo = std::move(layerInfo);
    file.layerInfoPlacement = LayerInfoPlacement::section();
    file.globalLayerMask = GlobalLayerMask{};
    file.globalBlocks = {TaggedBlock::make({'P', 'a', 't', 't'}, {1, 2})};
    file.layerMaskTrailing = {};
    auto imageData = ImageData::encode(compression, planes, header);
    file.imageData = imageData.value();
    return file;
}

std::vector<Case> allCases() {
    std::vector<Case> v;
    for (Version version : {Version::Psd, Version::Psb}) {
        for (ColorMode mode : modes()) {
            for (uint16_t depth : modeDepths(mode)) {
                for (Compression c : Compression::all()) {
                    std::ostringstream name;
                    name << "merged " << version << " " << mode << " " << depth << "bit " << c;
                    v.push_back(Case{name.str(), mergedOnly(version, mode, depth, c, 13, 7)});
                }
            }
        }
        for (ColorMode mode : {ColorMode::Grayscale, ColorMode::Rgb, ColorMode::Cmyk, ColorMode::Lab}) {
            for (uint16_t depth : modeDepths(mode)) {
                if (depth < 8) continue;
                for (Compression c : Compression::all()) {
                    std::ostringstream name;
                    name << "layered " << version << " " << mode << " " << depth << "bit " << c;
                    v.push_back(Case{name.str(), layered(version, mode, depth, c)});
                }
            }
        }
        for (Compression c : Compression::all()) {
            std::ostringstream name;
            name << "small " << version << " " << c;
            v.push_back(Case{name.str(), small(version, c)});
        }
        std::ostringstream name;
        name << "1x1 canvas " << version;
        v.push_back(Case{name.str(), mergedOnly(version, ColorMode::Rgb, 8, Compression::Rle, 1, 1)});
    }
    return v;
}

} // namespace psd::testgen
