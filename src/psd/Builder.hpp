#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "Blend.hpp"
#include "Error.hpp"
#include "File.hpp"
#include "Header.hpp"
#include "Layer.hpp"
#include "Rect.hpp"
#include "Resources.hpp"
#include "Tagged.hpp"

// PsdBuilder: constructing PSD/PSB files from pixel buffers. See
// PROVENANCE.md for origin details.
//
// The builder never composites. Supply the merged image with composite();
// otherwise a white placeholder is written and resource 1057 records
// hasRealMergedData = false.

namespace psd {

// Interleaved pixel data with alpha. CMYK values are *ink amounts* (0 = no
// ink); the builder stores them inverted as Photoshop does.
class PixelData {
public:
    enum class Kind { Rgba8, Rgba16, GrayA8, GrayA16, Cmyka8, Cmyka16 };

    static PixelData rgba8(std::vector<uint8_t> v);
    static PixelData rgba16(std::vector<uint16_t> v);
    static PixelData grayA8(std::vector<uint8_t> v);
    static PixelData grayA16(std::vector<uint16_t> v);
    static PixelData cmyka8(std::vector<uint8_t> v);
    static PixelData cmyka16(std::vector<uint16_t> v);

    Kind kind() const { return m_kind; }
    std::pair<ColorMode, uint16_t> modeDepth() const;
    // Samples per pixel including alpha.
    size_t spp() const;
    // Raw buffer length in samples.
    size_t len() const;
    // Planar big-endian plane for interleaved component `c`, as stored (CMYK inverted).
    std::vector<uint8_t> plane(size_t c, size_t n) const;
    bool alphaIsOpaque(size_t n) const;
    std::vector<uint8_t> compositePlane(size_t c, size_t n) const;

    bool operator==(const PixelData& other) const { return m_kind == other.m_kind && m_payload == other.m_payload; }

private:
    PixelData(Kind kind, std::variant<std::vector<uint8_t>, std::vector<uint16_t>> payload) : m_kind(kind), m_payload(std::move(payload)) {}
    const std::vector<uint8_t>& as8() const { return std::get<std::vector<uint8_t>>(m_payload); }
    const std::vector<uint16_t>& as16() const { return std::get<std::vector<uint16_t>>(m_payload); }
    Kind m_kind;
    std::variant<std::vector<uint8_t>, std::vector<uint16_t>> m_payload;
};

// A layer mask given as 8-bit samples.
struct MaskSpec {
    Rect rect;                 // Mask rectangle in document coordinates.
    std::vector<uint8_t> data; // rect.width x rect.height samples (0 = hidden, 255 = shown).
    uint8_t defaultColor = 0;  // Value outside the rectangle.
    bool disabled = false;

    bool operator==(const MaskSpec&) const = default;
};

// A raster layer.
struct LayerSpec {
    std::string name; // written both as legacy Pascal name and `luni`.
    int32_t left = 0;
    int32_t top = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    PixelData pixels; // must match the builder's color mode and depth.
    BlendMode blendMode = BlendMode::Normal;
    uint8_t opacity = 255;
    std::optional<uint8_t> fillOpacity; // `iOpa`, if not 255.
    bool visible = true;
    bool clipping = false; // clipped to the layer below.
    std::optional<MaskSpec> mask;
    std::vector<TaggedBlock> extraBlocks; // appended verbatim.

    // A visible normal layer at full opacity.
    LayerSpec(std::string name_, int32_t left_, int32_t top_, uint32_t width_, uint32_t height_, PixelData pixels_)
        : name(std::move(name_)), left(left_), top(top_), width(width_), height(height_), pixels(std::move(pixels_)) {}

    bool operator==(const LayerSpec&) const = default;
};

// A group (folder).
struct GroupSpec {
    std::string name;
    BlendMode blendMode = BlendMode::PassThrough;
    uint8_t opacity = 255;
    bool visible = true;
    bool open = true; // expanded in the layers panel.

    explicit GroupSpec(std::string name_) : name(std::move(name_)) {}

    bool operator==(const GroupSpec&) const = default;
};

// Builds PsdFiles. Layers are pushed bottom-to-top.
class PsdBuilder {
public:
    // An 8-bit RGB PSD using RLE compression.
    PsdBuilder(uint32_t width, uint32_t height);

    PsdBuilder& colorMode(ColorMode mode);
    PsdBuilder& depth(uint16_t depth);
    PsdBuilder& version(Version v);
    PsdBuilder& compression(Compression c);
    // Adds a resolution resource.
    PsdBuilder& resolution(double dpi);
    // Adds an ICC profile resource.
    PsdBuilder& iccProfile(std::vector<uint8_t> icc);
    // Adds an arbitrary image resource.
    PsdBuilder& resource(ImageResource r);

    // Pushes a layer above the previous one.
    PsdBuilder& pushLayer(LayerSpec spec);
    // Opens a group; subsequent layers go inside until endGroup().
    PsdBuilder& beginGroup(GroupSpec spec);
    // Closes the innermost open group.
    Result<void> endGroup();
    // Supplies the merged composite (straight samples, same format as
    // layers, full canvas). Transparent RGB is stored against white,
    // quantized at the document depth. Gray and CMYK composites with alpha
    // are stored straight (not matted against white).
    PsdBuilder& composite(PixelData pixels);

    // Builds the file model.
    Result<PsdFile> build() const;
    // Builds and serializes.
    Result<std::vector<uint8_t>> toBytes() const;

private:
    struct Entry;

    Result<size_t> colorChannels() const;
    Result<void> checkPixels(const PixelData& p, size_t n) const;
    Result<ChannelData> channel(int16_t id, std::span<const uint8_t> plane, size_t w, size_t h) const;
    Result<std::vector<ChannelData>> emptyChannels(size_t cc) const;
    Result<LayerRecord> layerRecord(const LayerSpec& spec, size_t cc, uint32_t id) const;

    uint32_t m_width, m_height;
    ColorMode m_colorMode = ColorMode::Rgb;
    uint16_t m_depth = 8;
    Version m_version = Version::Psd;
    Compression m_compression = Compression::Rle;
    std::vector<std::shared_ptr<Entry>> m_entries;
    std::vector<GroupSpec> m_openGroups;
    std::optional<PixelData> m_composite;
    std::vector<ImageResource> m_resources;
};

} // namespace psd
