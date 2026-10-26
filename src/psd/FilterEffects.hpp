#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Compression.hpp"
#include "Error.hpp"
#include "Rect.hpp"

// Smart-filter caches (the global `FEid` / `FXid` block). See PROVENANCE.md
// for origin details.

namespace psd {

// Most slots an item may declare (Photoshop writes 24 channels + 2).
inline constexpr uint32_t kFilterEffectsMaxSlots = 64;

// One compressed plane of a filter-effects item.
struct EffectsPlane {
    Compression compression = Compression::Raw;
    std::vector<uint8_t> data;

    // Encodes one plane of w x h big-endian samples of `depth` bits (RLE uses four-byte counts).
    static Result<EffectsPlane> encode(Compression compression, std::span<const uint8_t> decoded, size_t w, size_t h, uint16_t depth);
    // Decodes into w x h big-endian samples of `depth` bits.
    Result<std::vector<uint8_t>> decode(size_t w, size_t h, uint16_t depth) const;

    bool operator==(const EffectsPlane&) const = default;
};

// The cache of one smart object's filters.
struct FilterEffectsItem {
    std::string id;     // The smart object's `placed` id.
    uint32_t version = 1;
    Rect rect;          // Bounds of the cached planes.
    uint32_t depth = 0; // Bits per sample.
    uint32_t maxChannels = 0; // Declared channel count; there are maxChannels + 2 slots.
    std::vector<std::optional<EffectsPlane>> slots;
    std::optional<std::pair<Rect, EffectsPlane>> mask; // The filter mask: bounds and plane.

    // The decoded filter mask (8/16/32-bit big-endian samples) with its bounds.
    std::optional<std::pair<Rect, std::vector<uint8_t>>> decodedMask() const;

    bool operator==(const FilterEffectsItem&) const = default;
};

// A parsed FEid/FXid block.
struct FilterEffects {
    uint32_t version = 1; // Block version (1..=3).
    std::vector<FilterEffectsItem> items;

    // Parses block data. Trailing zero padding is allowed.
    static Result<FilterEffects> parse(std::span<const uint8_t> data);
    // Serializes block data as Photoshop writes it: one section per item, each padded to 4 bytes.
    std::vector<uint8_t> toBytes() const;

    bool operator==(const FilterEffects&) const = default;
};

// Debug-style printers (see Error.hpp).
std::ostream& operator<<(std::ostream& os, const EffectsPlane& p);
std::ostream& operator<<(std::ostream& os, const FilterEffectsItem& it);
std::ostream& operator<<(std::ostream& os, const FilterEffects& fx);

} // namespace psd
