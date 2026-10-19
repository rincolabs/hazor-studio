#pragma once

#include <array>
#include <cstdint>
#include <ostream>

// Layer blend modes (the 4-character keys stored after the `8BIM`
// signature). See PROVENANCE.md for origin details.

namespace psd {

// Photoshop blend mode keys.
class BlendMode {
public:
    enum class Kind {
        PassThrough,
        Normal,
        Dissolve,
        Darken,
        Multiply,
        ColorBurn,
        LinearBurn,
        DarkerColor,
        Lighten,
        Screen,
        ColorDodge,
        LinearDodge,
        LighterColor,
        Overlay,
        SoftLight,
        HardLight,
        VividLight,
        LinearLight,
        PinLight,
        HardMix,
        Difference,
        Exclusion,
        Subtract,
        Divide,
        Hue,
        Saturation,
        Color,
        Luminosity,
        Unknown, // key not known to this library, preserved verbatim in key()
    };

    static const BlendMode PassThrough, Normal, Dissolve, Darken, Multiply, ColorBurn, LinearBurn, DarkerColor, Lighten, Screen, ColorDodge,
        LinearDodge, LighterColor, Overlay, SoftLight, HardLight, VividLight, LinearLight, PinLight, HardMix, Difference, Exclusion, Subtract,
        Divide, Hue, Saturation, Color, Luminosity;

    // All known modes (27 layer modes plus pass-through for groups).
    static const std::array<BlendMode, 28>& all();

    BlendMode() : BlendMode(Normal) {}

    // The stored 4-byte key.
    std::array<uint8_t, 4> key() const;
    // Parses a stored key; unknown keys map to an Unknown-kind BlendMode.
    static BlendMode fromKey(std::array<uint8_t, 4> key);

    Kind kind() const { return m_kind; }

    bool operator==(const BlendMode& other) const { return m_kind == other.m_kind && (m_kind != Kind::Unknown || m_unknownKey == other.m_unknownKey); }
    bool operator!=(const BlendMode& other) const { return !(*this == other); }

private:
    explicit BlendMode(Kind kind, std::array<uint8_t, 4> unknownKey = {}) : m_kind(kind), m_unknownKey(unknownKey) {}
    Kind m_kind;
    std::array<uint8_t, 4> m_unknownKey{};
};

// Debug-style printers (see Error.hpp).
std::ostream& operator<<(std::ostream& os, BlendMode::Kind kind);
std::ostream& operator<<(std::ostream& os, const BlendMode& mode);

} // namespace psd
