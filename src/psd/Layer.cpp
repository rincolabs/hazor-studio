#include "Layer.hpp"

#include <algorithm>

namespace psd {

std::ostream& operator<<(std::ostream& os, const LayerFlags& f) { return os << "LayerFlags(" << int(f.value) << ")"; }

std::ostream& operator<<(std::ostream& os, const MaskParameters& p) {
    return os << "MaskParameters{flags=" << int(p.flags) << ", userDensity=" << p.userDensity << ", userFeather=" << p.userFeather
              << ", vectorDensity=" << p.vectorDensity << ", vectorFeather=" << p.vectorFeather << "}";
}

std::ostream& operator<<(std::ostream& os, const RealMask& m) {
    return os << "RealMask{flags=" << int(m.flags) << ", background=" << int(m.background) << ", rect=" << m.rect << "}";
}

size_t maskParameterBytes(uint8_t pf) {
    size_t n = 0;
    if (pf & 1) n += 1;
    if (pf & 2) n += 8;
    if (pf & 4) n += 1;
    if (pf & 8) n += 8;
    return n;
}

LayerMask LayerMask::make(Rect rect, uint8_t defaultColor, uint8_t flags) {
    LayerMask m;
    m.rect = rect;
    m.defaultColor = defaultColor;
    m.flags = flags & ~kFlagParameters;
    m.parameters = std::nullopt;
    m.real = std::nullopt;
    m.trailing = {0, 0};
    m.realFirst = true;
    return m;
}

namespace {
// Reads the "real" mask fields (flags, background, rect) if at least 18
// bytes remain, matching LayerMask::parse's read_real closure.
Result<std::optional<RealMask>> readRealMask(Reader& r) {
    if (r.remaining() < 18) return std::optional<RealMask>(std::nullopt);
    RealMask m;
    PSD_TRY_ASSIGN(m.flags, r.u8());
    PSD_TRY_ASSIGN(m.background, r.u8());
    PSD_TRY_ASSIGN(m.rect, Rect::read(r));
    return std::optional<RealMask>(m);
}
} // namespace

bool LayerMask::realBeforeParameters(std::span<const uint8_t> d) {
    auto fits = [&](size_t pfAt) -> bool {
        if (pfAt >= d.size()) return false;
        uint8_t pf = d[pfAt];
        size_t need = maskParameterBytes(pf);
        size_t lo = pfAt + 1 + need;
        size_t hi = pfAt + 4 + need;
        return d.size() >= lo && d.size() <= hi;
    };
    if (fits(18)) return false;
    bool photoshop = fits(36);
    if (!photoshop) return false;
    auto knownFlags = [](uint8_t pf) { return (pf & 0xF0) == 0; };
    auto realBackgroundOk = [&](size_t at) { return at + 1 < d.size() && (d[at + 1] == 0 || d[at + 1] == 255); };
    bool photoshopPlausible = realBackgroundOk(18) && 36 < d.size() && knownFlags(d[36]);
    bool specPlausible = [&] {
        if (18 >= d.size()) return false;
        uint8_t pf = d[18];
        size_t realAt = 19 + maskParameterBytes(pf);
        bool lenOk = d.size() >= realAt + 18 && d.size() <= realAt + 21;
        return knownFlags(pf) && lenOk && realBackgroundOk(realAt);
    }();
    return photoshopPlausible || !specPlausible;
}

Result<LayerMask> LayerMask::parse(std::span<const uint8_t> d) {
    Reader r(d);
    LayerMask m;
    PSD_TRY_ASSIGN(m.rect, Rect::read(r));
    PSD_TRY_ASSIGN(m.defaultColor, r.u8());
    PSD_TRY_ASSIGN(m.flags, r.u8());
    bool hasParameters = (m.flags & kFlagParameters) != 0;
    bool realFirst = hasParameters && realBeforeParameters(d);
    std::optional<RealMask> real;
    if (realFirst) {
        PSD_TRY_ASSIGN(real, readRealMask(r));
    }
    std::optional<MaskParameters> parameters;
    if (hasParameters) {
        MaskParameters p;
        PSD_TRY_ASSIGN(p.flags, r.u8());
        if (p.flags & 1) {
            PSD_TRY_ASSIGN(auto v, r.u8());
            p.userDensity = v;
        }
        if (p.flags & 2) {
            PSD_TRY_ASSIGN(auto v, r.f64());
            p.userFeather = v;
        }
        if (p.flags & 4) {
            PSD_TRY_ASSIGN(auto v, r.u8());
            p.vectorDensity = v;
        }
        if (p.flags & 8) {
            PSD_TRY_ASSIGN(auto v, r.f64());
            p.vectorFeather = v;
        }
        parameters = p;
    }
    if (!realFirst) {
        PSD_TRY_ASSIGN(real, readRealMask(r));
    }
    // The order only matters when both are present; otherwise default to Photoshop's.
    realFirst = realFirst || !real.has_value() || !parameters.has_value();
    m.parameters = parameters;
    m.real = real;
    auto rest = r.peekRest();
    m.trailing = std::vector<uint8_t>(rest.begin(), rest.end());
    m.realFirst = realFirst;
    return m;
}

uint64_t LayerMask::encodedLen() const {
    uint64_t real = this->real.has_value() ? 18 : 0;
    uint64_t parameters = 0;
    if (this->parameters.has_value()) {
        const auto& p = *this->parameters;
        parameters = 1 + (p.userDensity.has_value() ? 1 : 0) + (p.userFeather.has_value() ? 8 : 0) + (p.vectorDensity.has_value() ? 1 : 0) +
                     (p.vectorFeather.has_value() ? 8 : 0);
    }
    return 18 + real + parameters + trailing.size();
}

void LayerMask::write(std::vector<uint8_t>& out) const {
    rect.write(out);
    putU8(out, defaultColor);
    putU8(out, flags);
    auto writeReal = [&]() {
        if (real.has_value()) {
            putU8(out, real->flags);
            putU8(out, real->background);
            real->rect.write(out);
        }
    };
    if (realFirst) writeReal();
    if (parameters.has_value()) {
        const auto& p = *parameters;
        putU8(out, p.flags);
        if (p.userDensity.has_value()) putU8(out, *p.userDensity);
        if (p.userFeather.has_value()) putF64(out, *p.userFeather);
        if (p.vectorDensity.has_value()) putU8(out, *p.vectorDensity);
        if (p.vectorFeather.has_value()) putF64(out, *p.vectorFeather);
    }
    if (!realFirst) writeReal();
    put(out, trailing);
}

std::ostream& operator<<(std::ostream& os, const LayerMask& m) {
    return os << "LayerMask{rect=" << m.rect << ", flags=" << int(m.flags) << ", parameters=" << m.parameters << ", real=" << m.real
              << ", realFirst=" << m.realFirst << "}";
}

std::ostream& operator<<(std::ostream& os, const MaskData& m) {
    switch (m.kind()) {
    case MaskData::Kind::None: return os << "None";
    case MaskData::Kind::Mask: return os << "Mask(" << m.asMask() << ")";
    case MaskData::Kind::Raw: return os << "Raw(n=" << m.asRaw().size() << ")";
    }
    return os << "?";
}

BlendingRanges BlendingRanges::full(size_t channels) {
    BlendingRanges br;
    br.data.reserve((channels + 1) * 8);
    for (size_t i = 0; i <= channels; ++i) {
        uint8_t row[8] = {0, 0, 255, 255, 0, 0, 255, 255};
        br.data.insert(br.data.end(), row, row + 8);
    }
    return br;
}

std::vector<BlendRange> BlendingRanges::ranges() const {
    std::vector<BlendRange> out;
    size_t n = data.size() / 8;
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const uint8_t* c = data.data() + i * 8;
        BlendRange br;
        br.source = {c[0], c[1], c[2], c[3]};
        br.dest = {c[4], c[5], c[6], c[7]};
        out.push_back(br);
    }
    return out;
}

std::ostream& operator<<(std::ostream& os, const BlendingRanges& r) { return os << "BlendingRanges(n=" << r.data.size() << ")"; }

Result<ChannelData> ChannelData::encode(int16_t id, Compression compression, std::span<const uint8_t> decoded, size_t width, size_t height,
                                         uint16_t depth, Version version) {
    PlaneLayout layout;
    layout.planes = 1;
    layout.width = width;
    layout.height = height;
    layout.depth = depth;
    layout.version = version;
    PSD_TRY_ASSIGN(auto data, encodePlanes(compression, decoded, layout));
    ChannelData cd;
    cd.id = id;
    cd.compression = compression;
    cd.data = std::move(data);
    return cd;
}

Result<std::vector<uint8_t>> ChannelData::decode(size_t width, size_t height, uint16_t depth, Version version) const {
    PlaneLayout layout;
    layout.planes = 1;
    layout.width = width;
    layout.height = height;
    layout.depth = depth;
    layout.version = version;
    if (compression.has_value()) return decodePlanes(*compression, data, layout);
    PSD_TRY_ASSIGN(auto n, layout.decodedLen());
    if (n == 0) return std::vector<uint8_t>{};
    return PsdError::invalid("channel has no data");
}

Result<void> ChannelData::setDecoded(Compression compression_, std::span<const uint8_t> decoded, size_t width, size_t height, uint16_t depth,
                                      Version version) {
    PSD_TRY_ASSIGN(*this, encode(id, compression_, decoded, width, height, depth, version));
    return {};
}

std::ostream& operator<<(std::ostream& os, const ChannelData& c) { return os << "ChannelData{id=" << c.id << ", n=" << c.data.size() << "}"; }

std::string LayerRecord::resolvedName() const {
    for (const auto& b : blocks) {
        if (b.key == std::array<uint8_t, 4>{'l', 'u', 'n', 'i'}) {
            auto parsed = b.parsed();
            if (parsed.has_value() && bool(*parsed) && parsed->value().kind() == TaggedBlock::BlockData::Kind::UnicodeName) {
                return parsed->value().asUnicodeName();
            }
        }
    }
    return decodeLegacyName(name);
}

const TaggedBlock* LayerRecord::block(std::array<uint8_t, 4> key) const {
    for (auto& b : blocks) {
        if (b.key == key) return &b;
    }
    return nullptr;
}

TaggedBlock* LayerRecord::blockMut(std::array<uint8_t, 4> key) {
    for (auto& b : blocks) {
        if (b.key == key) return &b;
    }
    return nullptr;
}

std::optional<SectionDivider> LayerRecord::sectionDivider() const {
    for (auto key : {std::array<uint8_t, 4>{'l', 's', 'c', 't'}, std::array<uint8_t, 4>{'l', 's', 'd', 'k'}}) {
        const TaggedBlock* b = block(key);
        if (!b) continue;
        auto parsed = b->parsed();
        if (parsed.has_value() && bool(*parsed) && parsed->value().kind() == TaggedBlock::BlockData::Kind::SectionDivider) {
            return parsed->value().asSectionDivider();
        }
    }
    return std::nullopt;
}

SectionType LayerRecord::sectionType() const {
    auto sd = sectionDivider();
    return sd.has_value() ? sd->kind : SectionType::Other;
}

std::optional<uint32_t> LayerRecord::layerId() const {
    const TaggedBlock* b = block({'l', 'y', 'i', 'd'});
    if (!b) return std::nullopt;
    auto parsed = b->parsed();
    if (!parsed.has_value() || !bool(*parsed)) return std::nullopt;
    if (parsed->value().kind() != TaggedBlock::BlockData::Kind::LayerId) return std::nullopt;
    return parsed->value().asLayerId();
}

uint8_t LayerRecord::fillOpacity() const {
    const TaggedBlock* b = block({'i', 'O', 'p', 'a'});
    if (!b) return 255;
    auto parsed = b->parsed();
    if (!parsed.has_value() || !bool(*parsed)) return 255;
    if (parsed->value().kind() != TaggedBlock::BlockData::Kind::FillOpacity) return 255;
    return parsed->value().asFillOpacity();
}

const LayerMask* LayerRecord::layerMask() const {
    if (mask.kind() == MaskData::Kind::Mask) return &mask.asMask();
    return nullptr;
}

const ChannelData* LayerRecord::channel(int16_t id) const {
    for (auto& c : channels) {
        if (c.id == id) return &c;
    }
    return nullptr;
}

Rect LayerRecord::channelRect(int16_t id) const {
    if (id == kChannelUserMask && mask.kind() == MaskData::Kind::Mask) return mask.asMask().rect;
    if (id == kChannelRealUserMask && mask.kind() == MaskData::Kind::Mask) {
        const auto& m = mask.asMask();
        return m.real.has_value() ? m.real->rect : m.rect;
    }
    return rect;
}

Result<std::vector<uint8_t>> LayerRecord::decodeChannel(int16_t id, uint16_t depth, Version version) const {
    const ChannelData* ch = channel(id);
    if (!ch) return PsdError::invalid("layer has no channel " + std::to_string(id));
    PSD_TRY_ASSIGN(auto wh, channelRect(id).size());
    return ch->decode(wh.first, wh.second, depth, version);
}

namespace {
Result<uint64_t> readCountField(Reader& r, bool isLong) { return r.lenField(isLong); }
} // namespace

Result<std::pair<LayerRecord, std::vector<uint64_t>>> LayerRecord::read(Reader& r, Version version) {
    LayerRecord rec;
    PSD_TRY_ASSIGN(rec.rect, Rect::read(r));
    PSD_TRY_ASSIGN(auto nch, r.u16());
    if (nch > kMaxLayerChannels) return PsdError::limitExceeded("too many channels in layer");
    std::vector<uint64_t> lens;
    lens.reserve(nch);
    for (uint16_t i = 0; i < nch; ++i) {
        PSD_TRY_ASSIGN(auto id, r.i16());
        PSD_TRY_ASSIGN(auto len, readCountField(r, isPsb(version)));
        ChannelData cd;
        cd.id = id;
        rec.channels.push_back(std::move(cd));
        lens.push_back(len);
    }
    PSD_TRY_ASSIGN(auto sig, r.array<4>());
    if (sig != std::array<uint8_t, 4>{'8', 'B', 'I', 'M'}) {
        return PsdError::invalidSignature("8BIM (layer blend mode)", sig);
    }
    PSD_TRY_ASSIGN(auto blendKey, r.array<4>());
    rec.blendMode = BlendMode::fromKey(blendKey);
    PSD_TRY_ASSIGN(rec.opacity, r.u8());
    PSD_TRY_ASSIGN(rec.clipping, r.u8());
    PSD_TRY_ASSIGN(auto flagsByte, r.u8());
    rec.flags = LayerFlags(flagsByte);
    PSD_TRY_ASSIGN(rec.filler, r.u8());
    PSD_TRY_ASSIGN(auto extraLen, r.u32());
    PSD_TRY_ASSIGN(auto x, r.sub(extraLen));
    PSD_TRY_ASSIGN(auto maskLen, x.u32());
    PSD_TRY_ASSIGN(auto maskBytes, x.bytesU64(maskLen));
    if (maskBytes.empty()) {
        rec.mask = MaskData::none();
    } else {
        auto m = LayerMask::parse(maskBytes);
        if (m) rec.mask = MaskData::mask(m.value());
        else rec.mask = MaskData::raw(std::vector<uint8_t>(maskBytes.begin(), maskBytes.end()));
    }
    PSD_TRY_ASSIGN(auto brLen, x.u32());
    PSD_TRY_ASSIGN(auto brBytes, x.bytesU64(brLen));
    rec.blendingRanges = BlendingRanges{std::vector<uint8_t>(brBytes.begin(), brBytes.end())};
    PSD_TRY_ASSIGN(rec.name, readPascal(x, 4));
    PSD_TRY_ASSIGN(auto blocksResult, readBlocks(x, version));
    rec.blocks = std::move(blocksResult.first);
    rec.extraTrailing = std::move(blocksResult.second);
    return std::make_pair(std::move(rec), std::move(lens));
}

Result<uint64_t> LayerRecord::encodedLen(Version version) const {
    auto tooBig = []() { return PsdError::limitExceeded("section too large for 32-bit length (use PSB)"); };
    if (channels.size() > UINT16_MAX) return PsdError::limitExceeded("too many channels");
    uint64_t channelLen = isPsb(version) ? 8 : 4;
    if (!isPsb(version)) {
        for (auto& c : channels) {
            if (c.storedLen() > UINT32_MAX) return tooBig();
        }
    }
    uint64_t maskLen;
    switch (mask.kind()) {
    case MaskData::Kind::None: maskLen = 0; break;
    case MaskData::Kind::Mask: maskLen = mask.asMask().encodedLen(); break;
    case MaskData::Kind::Raw: maskLen = mask.asRaw().size(); break;
    default: maskLen = 0; break;
    }
    if (maskLen > UINT32_MAX) return tooBig();
    uint64_t ranges = blendingRanges.data.size();
    if (ranges > UINT32_MAX) return tooBig();
    uint64_t nameLen = 1 + std::min<uint64_t>(name.size(), 255);
    nameLen = ((nameLen + 3) / 4) * 4;
    uint64_t extra = (4 + maskLen) + (4 + ranges) + nameLen + extraTrailing.size();
    for (auto& b : blocks) {
        if (!usesLongLength(version, b.key) && b.data.size() > UINT32_MAX) return tooBig();
        extra += b.encodedLen(version);
    }
    if (extra > UINT32_MAX) return tooBig();
    // rect, channel count, channel entries, signature + blend key, four flag bytes, extra length.
    return (16 + 2 + static_cast<uint64_t>(channels.size()) * (2 + channelLen) + 8 + 4 + 4) + extra;
}

Result<void> LayerRecord::write(std::vector<uint8_t>& out, Version version) const {
    rect.write(out);
    if (channels.size() > UINT16_MAX) return PsdError::limitExceeded("too many channels");
    putU16(out, static_cast<uint16_t>(channels.size()));
    for (auto& c : channels) {
        putI16(out, c.id);
        PSD_TRY(putLen(out, c.storedLen(), isPsb(version)));
    }
    put(out, std::array<uint8_t, 4>{'8', 'B', 'I', 'M'});
    put(out, blendMode.key());
    putU8(out, opacity);
    putU8(out, clipping);
    putU8(out, flags.value);
    putU8(out, filler);
    size_t at = beginLen(out, false);
    size_t m = beginLen(out, false);
    switch (mask.kind()) {
    case MaskData::Kind::None: break;
    case MaskData::Kind::Mask: mask.asMask().write(out); break;
    case MaskData::Kind::Raw: put(out, mask.asRaw()); break;
    }
    PSD_TRY(endLen(out, m, false));
    PSD_TRY(putLen(out, blendingRanges.data.size(), false));
    put(out, blendingRanges.data);
    writePascal(out, name, 4);
    PSD_TRY(writeBlocks(out, blocks, version, 2));
    put(out, extraTrailing);
    return endLen(out, at, false);
}

std::ostream& operator<<(std::ostream& os, const LayerRecord& r) { return os << "LayerRecord{name=\"" << r.resolvedName() << "\", rect=" << r.rect << "}"; }

Result<LayerInfo> LayerInfo::readBody(std::span<const uint8_t> data, Version version) {
    Reader r(data);
    PSD_TRY_ASSIGN(auto count, r.i16());
    bool mergedAlpha = count < 0;
    uint16_t n = static_cast<uint16_t>(count < 0 ? -int32_t(count) : count);
    // Minimum record size: 16 rect + 2 + 4 sig + 4 key + 4 + 4 extra len.
    PSD_TRY(r.checkCount(n, 34));
    std::vector<LayerRecord> layers;
    layers.reserve(n);
    std::vector<std::vector<uint64_t>> allLens;
    allLens.reserve(n);
    for (uint16_t i = 0; i < n; ++i) {
        PSD_TRY_ASSIGN(auto recAndLens, LayerRecord::read(r, version));
        layers.push_back(std::move(recAndLens.first));
        allLens.push_back(std::move(recAndLens.second));
    }
    for (size_t i = 0; i < layers.size(); ++i) {
        auto& rec = layers[i];
        auto& lens = allLens[i];
        for (size_t c = 0; c < rec.channels.size() && c < lens.size(); ++c) {
            uint64_t len = lens[c];
            if (len == 0) continue;
            if (len == 1) return PsdError::invalid("channel data length 1");
            PSD_TRY_ASSIGN(auto compRaw, r.u16());
            rec.channels[c].compression = Compression::fromU16(compRaw);
            PSD_TRY_ASSIGN(auto chData, r.bytesU64(len - 2));
            rec.channels[c].data = std::vector<uint8_t>(chData.begin(), chData.end());
        }
    }
    size_t bodyLen = r.pos();
    auto rest = r.peekRest();
    std::optional<std::vector<uint8_t>> padding;
    bool restIsDefault = rest.size() == bodyLen % 2 && std::all_of(rest.begin(), rest.end(), [](uint8_t b) { return b == 0; });
    if (!restIsDefault) padding = std::vector<uint8_t>(rest.begin(), rest.end());
    LayerInfo info;
    info.mergedAlpha = mergedAlpha;
    info.layers = std::move(layers);
    info.padding = padding;
    return info;
}

Result<uint64_t> LayerInfo::unpaddedLen(Version version) const {
    uint64_t n = 2;
    for (auto& l : layers) {
        PSD_TRY_ASSIGN(auto len, l.encodedLen(version));
        n += len;
    }
    for (auto& l : layers) {
        for (auto& c : l.channels) {
            if (c.compression.has_value()) n += 2 + c.data.size();
        }
    }
    return n;
}

Result<void> LayerInfo::padTo(Version version, uint64_t align) {
    PSD_TRY_ASSIGN(auto len, padLen(version, align));
    padding = std::vector<uint8_t>(len, 0);
    return {};
}

Result<size_t> LayerInfo::padLen(Version version, uint64_t align) const {
    align = std::max<uint64_t>(align, 1);
    PSD_TRY_ASSIGN(auto len, unpaddedLen(version));
    return static_cast<size_t>((align - len % align) % align);
}

size_t LayerInfo::channelDataLen() const {
    size_t n = 0;
    for (auto& l : layers) {
        for (auto& c : l.channels) {
            if (c.compression.has_value()) n += 2 + c.data.size();
        }
    }
    return n;
}

Result<void> LayerInfo::writeUnpadded(std::vector<uint8_t>& out, Version version) const {
    if (layers.size() > INT16_MAX) return PsdError::limitExceeded("more than 32767 layers");
    int16_t n = static_cast<int16_t>(layers.size());
    putI16(out, mergedAlpha ? static_cast<int16_t>(-n) : n);
    for (auto& l : layers) {
        PSD_TRY(l.write(out, version));
    }
    for (auto& l : layers) {
        for (auto& c : l.channels) {
            if (c.compression.has_value()) {
                putU16(out, c.compression->asU16());
                put(out, c.data);
            }
        }
    }
    return {};
}

Result<void> LayerInfo::writeBody(std::vector<uint8_t>& out, Version version) const {
    size_t start = out.size();
    PSD_TRY(writeUnpadded(out, version));
    if (padding.has_value()) {
        put(out, *padding);
    } else if ((out.size() - start) % 2 == 1) {
        putU8(out, 0);
    }
    return {};
}

std::ostream& operator<<(std::ostream& os, const LayerInfo& info) { return os << "LayerInfo{layers=" << info.layers.size() << "}"; }

} // namespace psd
