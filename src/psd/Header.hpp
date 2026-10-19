#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>

#include "Error.hpp"
#include "Io.hpp"

// The 26-byte file header. See PROVENANCE.md for origin details.

namespace psd {

// Maximum width/height accepted by the parser (PSB limit).
inline constexpr uint32_t kMaxDimension = 300000;
// Maximum number of channels accepted (spec: 1..=56).
inline constexpr uint16_t kMaxChannels = 56;

enum class Version {
    Psd, // Version 1: Photoshop Document (32-bit section lengths).
    Psb, // Version 2: Large Document Format (64-bit lengths for some sections).
};

inline bool isPsb(Version v) { return v == Version::Psb; }
inline uint16_t versionAsU16(Version v) { return v == Version::Psb ? 2 : 1; }

// Document color mode.
class ColorMode {
public:
    enum class Kind {
        Bitmap,     // 0
        Grayscale,  // 1
        Indexed,    // 2
        Rgb,        // 3
        Cmyk,       // 4
        Multichannel, // 7
        Duotone,    // 8
        Lab,        // 9
        Unknown,    // any other value, preserved in rawValue()
    };

    static const ColorMode Bitmap, Grayscale, Indexed, Rgb, Cmyk, Multichannel, Duotone, Lab;

    static ColorMode fromU16(uint16_t v);
    uint16_t asU16() const;

    Kind kind() const { return m_kind; }
    // Number of color (non-alpha) channels for the mode, when fixed.
    std::optional<int> colorChannels() const;

    bool operator==(const ColorMode& other) const { return m_kind == other.m_kind && (m_kind != Kind::Unknown || m_raw == other.m_raw); }
    bool operator!=(const ColorMode& other) const { return !(*this == other); }

private:
    explicit ColorMode(Kind kind, uint16_t raw = 0) : m_kind(kind), m_raw(raw) {}
    Kind m_kind;
    uint16_t m_raw;
};

// The 26-byte file header.
struct Header {
    Version version = Version::Psd;
    // Reserved bytes (spec: must be zero). Preserved for exact round-trip.
    std::array<uint8_t, 6> reserved{};
    // Number of channels in the merged image, including alpha channels (1..=56).
    uint16_t channels = 0;
    uint32_t height = 0;
    uint32_t width = 0;
    // Bits per channel: 1, 8, 16 or 32.
    uint16_t depth = 0;
    ColorMode colorMode = ColorMode::Rgb;

    Header() = default;
    Header(Version version, uint32_t width, uint32_t height, uint16_t channels, uint16_t depth, ColorMode colorMode)
        : version(version), channels(channels), height(height), width(width), depth(depth), colorMode(colorMode) {}

    // Bytes per row of one channel plane.
    size_t rowBytes() const;

    static Result<Header> read(Reader& r);
    void write(std::vector<uint8_t>& out) const;

    // Checks the header against spec constraints and safety limits.
    Result<void> validate() const;

    bool operator==(const Header& other) const {
        return version == other.version && reserved == other.reserved && channels == other.channels && height == other.height &&
               width == other.width && depth == other.depth && colorMode == other.colorMode;
    }
    bool operator!=(const Header& other) const { return !(*this == other); }
};

// Bytes per row for `width` samples at `depth` bits.
size_t rowBytes(size_t width, uint16_t depth);

// Debug-style printers (see Error.hpp).
std::ostream& operator<<(std::ostream& os, Version v);
std::ostream& operator<<(std::ostream& os, ColorMode::Kind kind);
std::ostream& operator<<(std::ostream& os, const ColorMode& mode);
std::ostream& operator<<(std::ostream& os, const Header& h);

} // namespace psd
