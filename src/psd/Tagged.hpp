#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <utility>
#include <variant>
#include <vector>

#include "Blend.hpp"
#include "Descriptor.hpp"
#include "Error.hpp"
#include "FilterEffects.hpp"
#include "Header.hpp"
#include "Io.hpp"

// Additional layer information ("tagged blocks"). See PROVENANCE.md for
// origin details.
//
// Every block is stored raw (signature, key, data and the exact padding
// that followed it in the file) so unknown and partially-understood blocks
// round-trip byte for byte. TaggedBlock::parsed() offers typed views of
// simple, well-known keys.

namespace psd {

// Keys whose length field is 8 bytes in PSB files: the thirteen the Adobe
// specification lists, plus keys Photoshop also writes with 8-byte lengths
// although the specification omits them.
inline constexpr std::array<std::array<uint8_t, 4>, 21> kPsbLongKeys = {{
    {'L', 'M', 's', 'k'}, {'L', 'r', '1', '6'}, {'L', 'r', '3', '2'}, {'L', 'a', 'y', 'r'}, {'M', 't', '1', '6'}, {'M', 't', '3', '2'},
    {'M', 't', 'r', 'n'}, {'A', 'l', 'p', 'h'}, {'F', 'M', 's', 'k'}, {'l', 'n', 'k', '2'}, {'F', 'E', 'i', 'd'}, {'F', 'X', 'i', 'd'},
    {'P', 'x', 'S', 'D'}, // spec
    {'l', 'n', 'k', '3'}, {'l', 'n', 'k', 'E'}, {'p', 't', 'h', 's'}, {'e', 'x', 't', 'd'}, {'e', 'x', 't', 'n'}, {'F', 'E', 'L', 'S'},
    {'c', 'i', 'n', 'f'}, {'a', 'r', 't', 'd'}, // observed
}};

// Returns true if `key` uses an 8-byte length in `version`.
bool usesLongLength(Version version, std::array<uint8_t, 4> key);

class SectionType; // defined below TaggedBlock

// A tagged block, stored raw.
struct TaggedBlock {
    std::array<uint8_t, 4> signature = {'8', 'B', 'I', 'M'}; // `8BIM` or `8B64`.
    std::array<uint8_t, 4> key{};
    std::vector<uint8_t> data; // Block data exactly as covered by the length field.
    // Bytes that followed the data before the next block. nullopt means
    // "default": zero-pad to a multiple of four for global blocks, two for
    // layer blocks.
    std::optional<std::vector<uint8_t>> padding;

    // A new `8BIM` block with default padding.
    static TaggedBlock make(std::array<uint8_t, 4> key, std::vector<uint8_t> data);

    // true for MIB8 / 46B8: the signatures Tiff.cpp gives a block whose data
    // was kept verbatim from a little-endian TIFF because its layout is
    // unknown.
    static bool isForeignOrderSignature(std::span<const uint8_t> b);
    bool isForeignOrder() const { return isForeignOrderSignature(signature); }

    // Key as a string (lossy).
    std::string keyStr() const;

    class BlockData; // defined below TaggedBlock

    // Typed view for known keys. nullopt for keys without a typed parser.
    std::optional<Result<BlockData>> parsed() const;

    static TaggedBlock unicodeName(const std::string& name);
    static TaggedBlock sectionDivider(SectionType kind, std::optional<BlendMode> blendMode, std::optional<uint32_t> subType);
    static TaggedBlock layerId(uint32_t id);
    static TaggedBlock nameSource(std::array<uint8_t, 4> src);
    static TaggedBlock blendClippedAsGroup(bool v);
    static TaggedBlock blendInteriorElements(bool v);
    static TaggedBlock knockout(uint8_t v);
    static TaggedBlock transparencyShapes(bool v);
    static TaggedBlock layerMaskHidesEffects(bool v);
    static TaggedBlock vectorMaskHidesEffects(bool v);
    static TaggedBlock protection(uint32_t flags);
    static TaggedBlock sheetColor(uint16_t color);
    static TaggedBlock fillOpacity(uint8_t v);

    // Strictly re-parses the internal structure of blocks whose data carries
    // its own sizes (PlLd, SoLd/SoLE, lnk2/lnk3/lnkD, lfx2, FEid/FXid).
    Result<void> checkStructure() const;

    Result<void> write(std::vector<uint8_t>& out, Version version, size_t alignment) const;
    static Result<void> writeParts(std::vector<uint8_t>& out, std::array<uint8_t, 4> signature, std::array<uint8_t, 4> key,
                                    std::span<const uint8_t> data, std::optional<std::span<const uint8_t>> padding, Version version, size_t alignment);
    size_t encodedLen(Version version) const;

    bool operator==(const TaggedBlock&) const = default;
};
std::ostream& operator<<(std::ostream& os, const TaggedBlock& b);

// Layer section divider type (`lsct`).
class SectionType {
public:
    enum class Kind { Other, OpenFolder, ClosedFolder, BoundingDivider, Unknown };
    static const SectionType Other, OpenFolder, ClosedFolder, BoundingDivider;
    SectionType() : SectionType(Kind::Other, 0) {}
    static SectionType fromU32(uint32_t v);
    uint32_t asU32() const;
    Kind kind() const { return m_kind; }
    bool isFolder() const { return m_kind == Kind::OpenFolder || m_kind == Kind::ClosedFolder; }
    bool operator==(const SectionType& other) const { return m_kind == other.m_kind && (m_kind != Kind::Unknown || m_raw == other.m_raw); }
    bool operator!=(const SectionType& other) const { return !(*this == other); }

private:
    explicit SectionType(Kind kind, uint32_t raw) : m_kind(kind), m_raw(raw) {}
    Kind m_kind;
    uint32_t m_raw;
};
std::ostream& operator<<(std::ostream& os, const SectionType& t);

// Section divider setting (`lsct` / `lsdk`).
struct SectionDivider {
    SectionType kind;
    std::optional<BlendMode> blendMode; // present if length >= 12
    std::optional<uint32_t> subType;    // present if length >= 16

    bool operator==(const SectionDivider&) const = default;
};
std::ostream& operator<<(std::ostream& os, const SectionDivider& s);

// Typed view of a known block.
class TaggedBlock::BlockData {
public:
    enum class Kind {
        UnicodeName,            // `luni`
        SectionDivider,         // `lsct` / `lsdk`
        LayerId,                // `lyid`
        NameSource,             // `lnsr`
        BlendClippedAsGroup,    // `clbl`
        BlendInteriorElements,  // `infx`
        Knockout,               // `knko`: 0 none, 1 shallow, 2 deep.
        TransparencyShapes,     // `tsly`
        LayerMaskHidesEffects,  // `lmgm`
        VectorMaskHidesEffects, // `vmgm`
        Protection,             // `lspf`
        SheetColor,             // `lclr`
        FillOpacity,            // `iOpa`
        MetadataSetting,        // `shmd`: raw.
    };

    static BlockData unicodeName(std::string s);
    static BlockData sectionDivider(psd::SectionDivider s);
    static BlockData layerId(uint32_t v);
    static BlockData nameSource(std::array<uint8_t, 4> v);
    static BlockData blendClippedAsGroup(bool v);
    static BlockData blendInteriorElements(bool v);
    static BlockData knockout(uint8_t v);
    static BlockData transparencyShapes(bool v);
    static BlockData layerMaskHidesEffects(bool v);
    static BlockData vectorMaskHidesEffects(bool v);
    static BlockData protection(uint32_t v);
    static BlockData sheetColor(uint16_t v);
    static BlockData fillOpacity(uint8_t v);
    static BlockData metadataSetting(std::vector<uint8_t> v);

    Kind kind() const { return m_kind; }
    const std::string& asUnicodeName() const { return std::get<std::string>(m_payload); }
    const psd::SectionDivider& asSectionDivider() const { return std::get<psd::SectionDivider>(m_payload); }
    uint32_t asLayerId() const { return std::get<uint32_t>(m_payload); }
    std::array<uint8_t, 4> asNameSource() const { return std::get<std::array<uint8_t, 4>>(m_payload); }
    bool asBlendClippedAsGroup() const { return std::get<bool>(m_payload); }
    bool asBlendInteriorElements() const { return std::get<bool>(m_payload); }
    uint8_t asKnockout() const { return std::get<uint8_t>(m_payload); }
    bool asTransparencyShapes() const { return std::get<bool>(m_payload); }
    bool asLayerMaskHidesEffects() const { return std::get<bool>(m_payload); }
    bool asVectorMaskHidesEffects() const { return std::get<bool>(m_payload); }
    uint32_t asProtection() const { return std::get<uint32_t>(m_payload); }
    uint16_t asSheetColor() const { return std::get<uint16_t>(m_payload); }
    uint8_t asFillOpacity() const { return std::get<uint8_t>(m_payload); }
    const std::vector<uint8_t>& asMetadataSetting() const { return std::get<std::vector<uint8_t>>(m_payload); }

    bool operator==(const BlockData& other) const { return m_kind == other.m_kind && m_payload == other.m_payload; }

private:
    using Payload = std::variant<std::string, psd::SectionDivider, uint32_t, std::array<uint8_t, 4>, bool, uint8_t, uint16_t, std::vector<uint8_t>>;
    BlockData(Kind kind, Payload payload) : m_kind(kind), m_payload(std::move(payload)) {}
    Kind m_kind;
    Payload m_payload;
};
std::ostream& operator<<(std::ostream& os, const TaggedBlock::BlockData& b);

// Reads tagged blocks until the reader is exhausted. Bytes that do not form
// a block (fewer than 12 bytes or no valid signature) are returned as
// trailing data.
Result<std::pair<std::vector<TaggedBlock>, std::vector<uint8_t>>> readBlocks(Reader& r, Version version);

// The callback consumes selected payloads before they are copied into owned
// blocks; return true to skip adding a block to the output vector.
using ConsumeBlockFn =
    std::function<bool(size_t index, std::array<uint8_t, 4> signature, std::array<uint8_t, 4> key, std::span<const uint8_t> data, const std::optional<std::vector<uint8_t>>& padding)>;
Result<std::pair<std::vector<TaggedBlock>, std::vector<uint8_t>>> readBlocksWith(Reader& r, Version version, size_t alignment, ConsumeBlockFn consume);

Result<void> writeBlocks(std::vector<uint8_t>& out, const std::vector<TaggedBlock>& blocks, Version version, size_t alignment);

} // namespace psd
