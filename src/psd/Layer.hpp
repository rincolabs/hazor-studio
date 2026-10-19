#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <utility>
#include <variant>
#include <vector>

#include "Blend.hpp"
#include "Compression.hpp"
#include "Error.hpp"
#include "Header.hpp"
#include "Io.hpp"
#include "Rect.hpp"
#include "Tagged.hpp"

// Layer records, layer masks, blending ranges and per-layer channel data.
// See PROVENANCE.md for origin details.

namespace psd {

inline constexpr int16_t kChannelTransparency = -1; // Channel id of the transparency mask.
inline constexpr int16_t kChannelUserMask = -2;     // Channel id of the user-supplied layer mask.
inline constexpr int16_t kChannelRealUserMask = -3; // Channel id of the real user-supplied layer mask.
inline constexpr uint16_t kMaxLayerChannels = 64;   // Maximum number of channels per layer accepted by the parser.

// Layer record flag byte.
struct LayerFlags {
    static constexpr uint8_t kTransparencyProtected = 1 << 0;
    static constexpr uint8_t kHidden = 1 << 1;
    static constexpr uint8_t kObsolete = 1 << 2;
    static constexpr uint8_t kBit4Useful = 1 << 3;
    static constexpr uint8_t kPixelDataIrrelevant = 1 << 4;

    uint8_t value = 0;

    LayerFlags() = default;
    explicit LayerFlags(uint8_t v) : value(v) {}

    bool transparencyProtected() const { return bit(kTransparencyProtected); }
    bool hidden() const { return bit(kHidden); }
    bool bit4Useful() const { return bit(kBit4Useful); }
    bool pixelDataIrrelevant() const { return bit(kPixelDataIrrelevant); }
    void setHidden(bool v) { set(kHidden, v); }
    void setTransparencyProtected(bool v) { set(kTransparencyProtected, v); }
    // Sets pixel-data-irrelevant (also sets bit 3).
    void setPixelDataIrrelevant(bool v) {
        set(kBit4Useful, true);
        set(kPixelDataIrrelevant, v);
    }

    bool operator==(const LayerFlags&) const = default;

private:
    bool bit(uint8_t b) const { return (value & b) != 0; }
    void set(uint8_t b, bool on) {
        if (on) value = static_cast<uint8_t>(value | b);
        else value = static_cast<uint8_t>(value & ~b);
    }
};
std::ostream& operator<<(std::ostream& os, const LayerFlags& f);

// Optional mask parameters (present when mask flag bit 4 is set).
struct MaskParameters {
    uint8_t flags = 0; // bit 0 user density, 1 user feather, 2 vector density, 3 vector feather.
    std::optional<uint8_t> userDensity;
    std::optional<double> userFeather;
    std::optional<uint8_t> vectorDensity;
    std::optional<double> vectorFeather;

    bool operator==(const MaskParameters&) const = default;
};
std::ostream& operator<<(std::ostream& os, const MaskParameters& p);

// "Real" user mask fields (present in 36+-byte mask records).
struct RealMask {
    uint8_t flags = 0;
    uint8_t background = 0; // 0 or 255.
    Rect rect;

    bool operator==(const RealMask&) const = default;
};
std::ostream& operator<<(std::ostream& os, const RealMask& m);

// Byte length of the mask parameter values announced by parameter flags byte `pf`.
size_t maskParameterBytes(uint8_t pf);

// Layer mask / adjustment layer data.
struct LayerMask {
    static constexpr uint8_t kFlagRelative = 1 << 0;
    static constexpr uint8_t kFlagDisabled = 1 << 1;
    static constexpr uint8_t kFlagInvert = 1 << 2;
    static constexpr uint8_t kFlagParameters = 1 << 4;

    Rect rect;
    uint8_t defaultColor = 0; // 0 or 255.
    uint8_t flags = 0;
    std::optional<MaskParameters> parameters;
    std::optional<RealMask> real;
    std::vector<uint8_t> trailing;
    // When both are present, the real-mask fields come before the mask
    // parameters (Photoshop's order); false keeps a file that uses the
    // spec's order byte-stable.
    bool realFirst = true;

    // The simple 20-byte form.
    static LayerMask make(Rect rect, uint8_t defaultColor, uint8_t flags);
    bool disabled() const { return (flags & kFlagDisabled) != 0; }

    // Whether `d` (a whole mask record with mask parameters) stores the
    // real-mask fields before the parameters, as Photoshop does.
    static bool realBeforeParameters(std::span<const uint8_t> d);

    static Result<LayerMask> parse(std::span<const uint8_t> d);
    uint64_t encodedLen() const;
    void write(std::vector<uint8_t>& out) const;

    bool operator==(const LayerMask&) const = default;
};
std::ostream& operator<<(std::ostream& os, const LayerMask& m);

// The layer mask data field of a layer record.
class MaskData {
public:
    enum class Kind { None, Mask, Raw };
    MaskData() : m_kind(Kind::None), m_payload(std::vector<uint8_t>{}) {}
    static MaskData none() { return MaskData(); }
    static MaskData mask(LayerMask m) { return MaskData(Kind::Mask, std::move(m)); }
    static MaskData raw(std::vector<uint8_t> b) { return MaskData(Kind::Raw, std::move(b)); }

    Kind kind() const { return m_kind; }
    const LayerMask& asMask() const { return std::get<LayerMask>(m_payload); }
    const std::vector<uint8_t>& asRaw() const { return std::get<std::vector<uint8_t>>(m_payload); }

    bool operator==(const MaskData& other) const { return m_kind == other.m_kind && m_payload == other.m_payload; }

private:
    MaskData(Kind kind, std::variant<LayerMask, std::vector<uint8_t>> payload) : m_kind(kind), m_payload(std::move(payload)) {}
    Kind m_kind;
    std::variant<LayerMask, std::vector<uint8_t>> m_payload;
};
std::ostream& operator<<(std::ostream& os, const MaskData& m);

// One blending range entry.
struct BlendRange {
    std::array<uint8_t, 4> source{}; // black low, black high, white low, white high.
    std::array<uint8_t, 4> dest{};
    bool operator==(const BlendRange&) const = default;
};

// Layer blending ranges ("Blend If"), stored raw.
struct BlendingRanges {
    std::vector<uint8_t> data;

    // Default "full" ranges for composite + `channels` channels.
    static BlendingRanges full(size_t channels);
    // Parsed entries (incomplete trailing bytes are ignored).
    std::vector<BlendRange> ranges() const;

    bool operator==(const BlendingRanges&) const = default;
};
std::ostream& operator<<(std::ostream& os, const BlendingRanges& r);

// Encoded image data for one layer channel.
struct ChannelData {
    int16_t id = 0; // 0.. color channels, -1 transparency, -2 user mask, -3 real user mask.
    std::optional<Compression> compression; // nullopt when the stored length was 0.
    std::vector<uint8_t> data;              // Encoded bytes following the compression field, kept verbatim.

    // Encodes planar samples (height x rowBytes).
    static Result<ChannelData> encode(int16_t id, Compression compression, std::span<const uint8_t> decoded, size_t width, size_t height,
                                       uint16_t depth, Version version);
    // Decodes into planar big-endian samples.
    Result<std::vector<uint8_t>> decode(size_t width, size_t height, uint16_t depth, Version version) const;
    // Replaces the channel contents, re-encoding with `compression`.
    Result<void> setDecoded(Compression compression, std::span<const uint8_t> decoded, size_t width, size_t height, uint16_t depth, Version version);

    uint64_t storedLen() const { return compression.has_value() ? 2 + data.size() : 0; }

    bool operator==(const ChannelData&) const = default;
};
std::ostream& operator<<(std::ostream& os, const ChannelData& c);

// A layer record together with its channel image data.
struct LayerRecord {
    Rect rect;
    std::vector<ChannelData> channels; // Channels in stored order, including encoded image data.
    BlendMode blendMode = BlendMode::Normal;
    uint8_t opacity = 255;
    uint8_t clipping = 0; // 0 base, 1 non-base (clipped to layer below).
    LayerFlags flags;
    uint8_t filler = 0;
    MaskData mask = MaskData::none();
    BlendingRanges blendingRanges;
    std::vector<uint8_t> name; // Legacy Pascal name bytes (padded to 4 in the file).
    std::vector<TaggedBlock> blocks;
    std::vector<uint8_t> extraTrailing; // Bytes at the end of the extra-data field that do not form a block.

    // Layer name, preferring the Unicode `luni` block over the legacy name
    // (named resolvedName since the `name` field already holds the legacy
    // Pascal bytes).
    std::string resolvedName() const;
    const TaggedBlock* block(std::array<uint8_t, 4> key) const;
    TaggedBlock* blockMut(std::array<uint8_t, 4> key);
    std::optional<SectionDivider> sectionDivider() const;
    SectionType sectionType() const;
    bool isVisible() const { return !flags.hidden(); }
    std::optional<uint32_t> layerId() const;
    uint8_t fillOpacity() const;
    const LayerMask* layerMask() const;
    const ChannelData* channel(int16_t id) const;
    // Rectangle that a channel's pixels cover.
    Rect channelRect(int16_t id) const;
    // Decodes a channel's samples (planar, big-endian).
    Result<std::vector<uint8_t>> decodeChannel(int16_t id, uint16_t depth, Version version) const;

    static Result<std::pair<LayerRecord, std::vector<uint64_t>>> read(Reader& r, Version version);
    Result<uint64_t> encodedLen(Version version) const;
    Result<void> write(std::vector<uint8_t>& out, Version version) const;

    bool operator==(const LayerRecord&) const = default;
};
std::ostream& operator<<(std::ostream& os, const LayerRecord& r);

// Layer info: layer records plus their channel image data.
struct LayerInfo {
    // The layer count was stored negative: the first alpha channel of the
    // merged image contains the transparency of the merged result.
    bool mergedAlpha = false;
    std::vector<LayerRecord> layers; // bottom-most first (file order).
    std::optional<std::vector<uint8_t>> padding; // nullopt = default (zero pad to even).

    // Parses a layer info body (the bytes covered by its length field).
    static Result<LayerInfo> readBody(std::span<const uint8_t> data, Version version);
    // Length of the body (layer count, records and channel data) without its trailing padding.
    Result<uint64_t> unpaddedLen(Version version) const;
    // Sets the padding so the body length is a multiple of `align`.
    Result<void> padTo(Version version, uint64_t align);
    // Number of zero bytes padTo() would set, without touching `this`.
    Result<size_t> padLen(Version version, uint64_t align) const;
    size_t channelDataLen() const;
    Result<void> writeUnpadded(std::vector<uint8_t>& out, Version version) const;
    Result<void> writeBody(std::vector<uint8_t>& out, Version version) const;

    bool operator==(const LayerInfo&) const = default;
};
std::ostream& operator<<(std::ostream& os, const LayerInfo& info);

} // namespace psd
