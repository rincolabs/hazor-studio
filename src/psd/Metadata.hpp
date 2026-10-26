#pragma once

#include <array>
#include <cstdint>
#include <ostream>
#include <vector>

#include "Error.hpp"
#include "Io.hpp"

// The `shmd` (metadata setting) layer block. See PROVENANCE.md for origin
// details.

namespace psd {

// One `shmd` item.
struct MetadataItem {
    std::array<uint8_t, 4> signature = {'8', 'B', 'I', 'M'};
    std::array<uint8_t, 4> key{}; // e.g. `cmls`.
    bool copy = false;            // Copy on sheet duplication.
    std::array<uint8_t, 3> reserved{};
    std::vector<uint8_t> data; // Data including any padding counted in the stored length.

    // A new item; `data` is zero-padded to an even length as Photoshop writes it.
    static MetadataItem make(std::array<uint8_t, 4> key, std::vector<uint8_t> data);

    bool operator==(const MetadataItem&) const = default;
};
std::ostream& operator<<(std::ostream& os, const MetadataItem& it);

// Parses `shmd` block data.
Result<std::vector<MetadataItem>> parseShmd(std::span<const uint8_t> data);
// Serializes `shmd` block data.
std::vector<uint8_t> writeShmd(const std::vector<MetadataItem>& items);

} // namespace psd
