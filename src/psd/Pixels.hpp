#pragma once

#include <cstdint>
#include <optional>
#include <ostream>
#include <span>
#include <vector>

#include "Error.hpp"
#include "File.hpp"
#include "Header.hpp"
#include "Layer.hpp"

// Sample helpers and RGBA8 convenience extraction. See PROVENANCE.md for
// origin details.
//
// These are conveniences for previews and tests; they perform no color
// management. 16-bit samples are scaled with rounding, 32-bit float samples
// are clamped to 0..=1 (no tone mapping or gamma), CMYK is converted
// naively (r = c' * k' / 255 using the stored, inverted values).

namespace psd {

// Interprets planar big-endian bytes as u16 samples.
std::vector<uint16_t> samplesU16(std::span<const uint8_t> bytes);
// Interprets planar big-endian bytes as f32 samples.
std::vector<float> samplesF32(std::span<const uint8_t> bytes);
// Encodes u16 samples as big-endian bytes.
std::vector<uint8_t> u16ToBytes(std::span<const uint16_t> samples);
// Encodes f32 samples as big-endian bytes.
std::vector<uint8_t> f32ToBytes(std::span<const float> samples);

// Expands a 1-bit plane (MSB first, rows padded to bytes) to one byte per
// pixel (0 or 1).
Result<std::vector<uint8_t>> unpackBits(std::span<const uint8_t> bytes, size_t width, size_t height);
// Converts one plane of samples at `depth` to 8-bit.
Result<std::vector<uint8_t>> planeToU8(std::span<const uint8_t> bytes, uint16_t depth, size_t width, size_t height);

// An 8-bit RGBA image positioned in document coordinates.
struct RgbaImage {
    int32_t left = 0;
    int32_t top = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data; // Interleaved RGBA, row-major.

    bool operator==(const RgbaImage&) const = default;
};
std::ostream& operator<<(std::ostream& os, const RgbaImage& img);

// An 8-bit single-channel image (e.g. a layer mask).
struct GrayImage {
    int32_t left = 0;
    int32_t top = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
    uint8_t defaultValue = 0; // Value outside the rectangle (masks only).

    bool operator==(const GrayImage&) const = default;
};
std::ostream& operator<<(std::ostream& os, const GrayImage& img);

// Internal helpers exposed only so TestPixels.cpp can exercise them
// directly. Not part of the public API.
namespace detail {
Result<std::vector<uint8_t>> interleave(ColorMode mode, std::span<const std::span<const uint8_t>> color, std::optional<std::span<const uint8_t>> alpha,
                                         std::span<const uint8_t> palette, size_t n);
}

// A borrowed view of a layer with the file context needed to decode it.
// Must not outlive the PsdFile it came from (same discipline as Rust's
// Layer<'a> lifetime).
class LayerView {
public:
    LayerView(size_t index, const LayerRecord& record, const Header& header) : m_index(index), m_record(&record), m_header(&header) {}

    size_t index() const { return m_index; }
    const LayerRecord& record() const { return *m_record; }

    // Name, preferring `luni`.
    std::string name() const { return m_record->resolvedName(); }
    // PSD/PSB version of the containing file.
    Version version() const { return m_header->version; }
    // Decodes a channel (planar, big-endian samples).
    Result<std::vector<uint8_t>> channelBytes(int16_t id) const;
    // Layer pixels as RGBA8 (transparency channel as alpha; the user mask is
    // *not* applied, see userMask()).
    Result<RgbaImage> rgba8() const;
    // The user mask (channel -3 if present, else -2) as 8-bit gray, with its
    // rectangle and default color. nullopt if the layer has no mask channel.
    std::optional<Result<GrayImage>> userMask() const;

private:
    size_t m_index;
    const LayerRecord* m_record;
    const Header* m_header;
};

} // namespace psd
