#include "FilterEffects.hpp"

#include <algorithm>

namespace psd {

std::ostream& operator<<(std::ostream& os, const EffectsPlane& p) { return os << "EffectsPlane{compression=" << p.compression << ", n=" << p.data.size() << "}"; }

std::ostream& operator<<(std::ostream& os, const FilterEffectsItem& it) {
    os << "FilterEffectsItem{id=\"" << it.id << "\", version=" << it.version << ", rect=" << it.rect << ", depth=" << it.depth
       << ", maxChannels=" << it.maxChannels << ", slots=" << it.slots.size() << ", hasMask=" << (it.mask.has_value() ? "true" : "false") << "}";
    return os;
}

std::ostream& operator<<(std::ostream& os, const FilterEffects& fx) {
    os << "FilterEffects{version=" << fx.version << ", items=[";
    bool first = true;
    for (auto& it : fx.items) {
        if (!first) os << ", ";
        first = false;
        os << it;
    }
    return os << "]}";
}

Result<EffectsPlane> EffectsPlane::encode(Compression compression, std::span<const uint8_t> decoded, size_t w, size_t h, uint16_t depth) {
    PlaneLayout layout;
    layout.planes = 1;
    layout.width = w;
    layout.height = h;
    layout.depth = depth;
    layout.version = Version::Psb;
    PSD_TRY_ASSIGN(auto data, encodePlanes(compression, decoded, layout));
    return EffectsPlane{compression, std::move(data)};
}

Result<std::vector<uint8_t>> EffectsPlane::decode(size_t w, size_t h, uint16_t depth) const {
    PlaneLayout layout;
    layout.planes = 1;
    layout.width = w;
    layout.height = h;
    layout.depth = depth;
    layout.version = Version::Psb;
    uint64_t n = static_cast<uint64_t>(data.size());
    uint64_t most;
    switch (compression.kind()) {
    case Compression::Kind::Raw: most = n; break;
    case Compression::Kind::Rle: most = n > UINT64_MAX / 64 ? UINT64_MAX : n * 64; break;
    default: {
        uint64_t scaled = n > UINT64_MAX / 1032 ? UINT64_MAX : n * 1032;
        most = scaled > UINT64_MAX - 64 ? UINT64_MAX : scaled + 64;
        break;
    }
    }
    PSD_TRY_ASSIGN(auto total, layout.totalBytes());
    if (total > most) return PsdError::invalid("filter effects: plane larger than its data can hold");
    return decodePlanes(compression, data, layout);
}

std::optional<std::pair<Rect, std::vector<uint8_t>>> FilterEffectsItem::decodedMask() const {
    if (!mask.has_value()) return std::nullopt;
    const auto& [r, p] = *mask;
    auto sz = r.size();
    if (!sz) return std::nullopt;
    if (depth > UINT16_MAX) return std::nullopt;
    auto decoded = p.decode(sz.value().first, sz.value().second, static_cast<uint16_t>(depth));
    if (!decoded) return std::nullopt;
    return std::make_pair(r, std::move(decoded).value());
}

namespace {

Result<EffectsPlane> readPlane(Reader& r) {
    PSD_TRY_ASSIGN(auto len, r.u64());
    if (len < 2) return PsdError::invalid("filter effects: plane shorter than its compression field");
    PSD_TRY_ASSIGN(auto sub, r.sub(len));
    PSD_TRY_ASSIGN(auto compRaw, sub.u16());
    auto rest = sub.peekRest();
    return EffectsPlane{Compression::fromU16(compRaw), std::vector<uint8_t>(rest.begin(), rest.end())};
}

void writePlane(std::vector<uint8_t>& out, const EffectsPlane& p) {
    putU64(out, static_cast<uint64_t>(p.data.size()) + 2);
    putU16(out, p.compression.asU16());
    put(out, p.data);
}

Result<FilterEffectsItem> readItem(Reader& body) {
    PSD_TRY_ASSIGN(auto idBytes, readPascal(body, 1));
    std::string id = utf8BytesToStringLossy(idBytes); // matches Rust's String::from_utf8_lossy
    PSD_TRY_ASSIGN(auto version, body.u32());
    PSD_TRY_ASSIGN(auto itemLen, body.u64());
    PSD_TRY_ASSIGN(auto it, body.sub(itemLen));
    PSD_TRY_ASSIGN(auto rect, Rect::read(it));
    PSD_TRY_ASSIGN(auto depth, it.u32());
    PSD_TRY_ASSIGN(auto maxChannels, it.u32());
    if (maxChannels > kFilterEffectsMaxSlots) return PsdError::limitExceeded("filter effects: too many channels");
    std::vector<std::optional<EffectsPlane>> slots;
    slots.reserve(static_cast<size_t>(maxChannels) + 2);
    for (uint32_t i = 0; i < maxChannels + 2; ++i) {
        PSD_TRY_ASSIGN(auto flag, it.u32());
        if (flag != 0) {
            PSD_TRY_ASSIGN(auto plane, readPlane(it));
            slots.push_back(std::move(plane));
        } else {
            slots.push_back(std::nullopt);
        }
    }
    if (!it.isEmpty()) {
        return PsdError::invalid("filter effects item " + id + ": " + std::to_string(it.remaining()) + " unexpected bytes");
    }
    std::optional<std::pair<Rect, EffectsPlane>> mask;
    if (!body.isEmpty()) {
        PSD_TRY_ASSIGN(auto flag, body.u8());
        if (flag != 0) {
            PSD_TRY_ASSIGN(auto maskRect, Rect::read(body));
            PSD_TRY_ASSIGN(auto maskPlane, readPlane(body));
            mask = std::make_pair(maskRect, std::move(maskPlane));
        }
    }
    return FilterEffectsItem{std::move(id), version, rect, depth, maxChannels, std::move(slots), std::move(mask)};
}

} // namespace

Result<FilterEffects> FilterEffects::parse(std::span<const uint8_t> data) {
    Reader r(data);
    PSD_TRY_ASSIGN(auto version, r.u32());
    if (version < 1 || version > 3) return PsdError::invalid("filter effects version " + std::to_string(version));
    std::vector<FilterEffectsItem> items;
    for (;;) {
        PSD_TRY_ASSIGN(auto len, r.u64());
        PSD_TRY_ASSIGN(auto body, r.sub(len));
        while (!body.isEmpty()) {
            PSD_TRY_ASSIGN(auto item, readItem(body));
            items.push_back(std::move(item));
        }
        uint64_t rem = static_cast<uint64_t>(r.remaining());
        uint64_t pad = std::min<uint64_t>((4 - len % 4) % 4, rem);
        PSD_TRY(r.skip(static_cast<size_t>(pad)));
        auto rest = r.peekRest();
        bool allZero = std::all_of(rest.begin(), rest.end(), [](uint8_t b) { return b == 0; });
        if (allZero && rest.size() <= 3) break;
        if (rest.size() < 8) {
            return PsdError::invalid("filter effects: " + std::to_string(rest.size()) + " unexpected bytes after the data");
        }
    }
    return FilterEffects{version, std::move(items)};
}

std::vector<uint8_t> FilterEffects::toBytes() const {
    std::vector<uint8_t> out;
    putU32(out, version);
    for (const auto& it : items) {
        std::vector<uint8_t> body;
        std::vector<uint8_t> idBytes(it.id.begin(), it.id.end());
        writePascal(body, idBytes, 1);
        putU32(body, it.version);
        size_t at = beginLen(body, true);
        it.rect.write(body);
        putU32(body, it.depth);
        putU32(body, it.maxChannels);
        for (size_t i = 0; i < static_cast<size_t>(it.maxChannels) + 2; ++i) {
            if (i < it.slots.size() && it.slots[i].has_value()) {
                putU32(body, 1);
                writePlane(body, *it.slots[i]);
            } else {
                putU32(body, 0);
            }
        }
        (void)endLen(body, at, true); // a 64-bit length never overflows.
        if (it.mask.has_value()) {
            putU8(body, 1);
            it.mask->first.write(body);
            writePlane(body, it.mask->second);
        } else {
            putU8(body, 0);
        }
        putU64(out, static_cast<uint64_t>(body.size()));
        put(out, body);
        size_t padded = (out.size() + 3) / 4 * 4;
        out.resize(padded, 0);
    }
    if (items.empty()) {
        // One empty section, so the block still parses.
        putU64(out, 0);
    }
    return out;
}

} // namespace psd
