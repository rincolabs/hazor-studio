#include "Blend.hpp"

namespace psd {

const BlendMode BlendMode::PassThrough(BlendMode::Kind::PassThrough);
const BlendMode BlendMode::Normal(BlendMode::Kind::Normal);
const BlendMode BlendMode::Dissolve(BlendMode::Kind::Dissolve);
const BlendMode BlendMode::Darken(BlendMode::Kind::Darken);
const BlendMode BlendMode::Multiply(BlendMode::Kind::Multiply);
const BlendMode BlendMode::ColorBurn(BlendMode::Kind::ColorBurn);
const BlendMode BlendMode::LinearBurn(BlendMode::Kind::LinearBurn);
const BlendMode BlendMode::DarkerColor(BlendMode::Kind::DarkerColor);
const BlendMode BlendMode::Lighten(BlendMode::Kind::Lighten);
const BlendMode BlendMode::Screen(BlendMode::Kind::Screen);
const BlendMode BlendMode::ColorDodge(BlendMode::Kind::ColorDodge);
const BlendMode BlendMode::LinearDodge(BlendMode::Kind::LinearDodge);
const BlendMode BlendMode::LighterColor(BlendMode::Kind::LighterColor);
const BlendMode BlendMode::Overlay(BlendMode::Kind::Overlay);
const BlendMode BlendMode::SoftLight(BlendMode::Kind::SoftLight);
const BlendMode BlendMode::HardLight(BlendMode::Kind::HardLight);
const BlendMode BlendMode::VividLight(BlendMode::Kind::VividLight);
const BlendMode BlendMode::LinearLight(BlendMode::Kind::LinearLight);
const BlendMode BlendMode::PinLight(BlendMode::Kind::PinLight);
const BlendMode BlendMode::HardMix(BlendMode::Kind::HardMix);
const BlendMode BlendMode::Difference(BlendMode::Kind::Difference);
const BlendMode BlendMode::Exclusion(BlendMode::Kind::Exclusion);
const BlendMode BlendMode::Subtract(BlendMode::Kind::Subtract);
const BlendMode BlendMode::Divide(BlendMode::Kind::Divide);
const BlendMode BlendMode::Hue(BlendMode::Kind::Hue);
const BlendMode BlendMode::Saturation(BlendMode::Kind::Saturation);
const BlendMode BlendMode::Color(BlendMode::Kind::Color);
const BlendMode BlendMode::Luminosity(BlendMode::Kind::Luminosity);

const std::array<BlendMode, 28>& BlendMode::all() {
    static const std::array<BlendMode, 28> kAll = {
        PassThrough, Normal,     Dissolve,  Darken,       Multiply,  ColorBurn,  LinearBurn, DarkerColor, Lighten,    Screen,
        ColorDodge,  LinearDodge, LighterColor, Overlay,  SoftLight, HardLight,  VividLight, LinearLight, PinLight,   HardMix,
        Difference,  Exclusion,  Subtract,  Divide,       Hue,       Saturation, Color,      Luminosity,
    };
    return kAll;
}

std::array<uint8_t, 4> BlendMode::key() const {
    switch (m_kind) {
    case Kind::PassThrough: return {'p', 'a', 's', 's'};
    case Kind::Normal: return {'n', 'o', 'r', 'm'};
    case Kind::Dissolve: return {'d', 'i', 's', 's'};
    case Kind::Darken: return {'d', 'a', 'r', 'k'};
    case Kind::Multiply: return {'m', 'u', 'l', ' '};
    case Kind::ColorBurn: return {'i', 'd', 'i', 'v'};
    case Kind::LinearBurn: return {'l', 'b', 'r', 'n'};
    case Kind::DarkerColor: return {'d', 'k', 'C', 'l'};
    case Kind::Lighten: return {'l', 'i', 't', 'e'};
    case Kind::Screen: return {'s', 'c', 'r', 'n'};
    case Kind::ColorDodge: return {'d', 'i', 'v', ' '};
    case Kind::LinearDodge: return {'l', 'd', 'd', 'g'};
    case Kind::LighterColor: return {'l', 'g', 'C', 'l'};
    case Kind::Overlay: return {'o', 'v', 'e', 'r'};
    case Kind::SoftLight: return {'s', 'L', 'i', 't'};
    case Kind::HardLight: return {'h', 'L', 'i', 't'};
    case Kind::VividLight: return {'v', 'L', 'i', 't'};
    case Kind::LinearLight: return {'l', 'L', 'i', 't'};
    case Kind::PinLight: return {'p', 'L', 'i', 't'};
    case Kind::HardMix: return {'h', 'M', 'i', 'x'};
    case Kind::Difference: return {'d', 'i', 'f', 'f'};
    case Kind::Exclusion: return {'s', 'm', 'u', 'd'};
    case Kind::Subtract: return {'f', 's', 'u', 'b'};
    case Kind::Divide: return {'f', 'd', 'i', 'v'};
    case Kind::Hue: return {'h', 'u', 'e', ' '};
    case Kind::Saturation: return {'s', 'a', 't', ' '};
    case Kind::Color: return {'c', 'o', 'l', 'r'};
    case Kind::Luminosity: return {'l', 'u', 'm', ' '};
    case Kind::Unknown: return m_unknownKey;
    }
    return m_unknownKey;
}

BlendMode BlendMode::fromKey(std::array<uint8_t, 4> key) {
    for (const BlendMode& m : all()) {
        if (m.key() == key) return m;
    }
    return BlendMode(Kind::Unknown, key);
}

std::ostream& operator<<(std::ostream& os, BlendMode::Kind kind) {
    static const char* const kNames[] = {"PassThrough", "Normal",     "Dissolve",  "Darken",     "Multiply",  "ColorBurn",  "LinearBurn",
                                          "DarkerColor", "Lighten",    "Screen",    "ColorDodge", "LinearDodge", "LighterColor", "Overlay",
                                          "SoftLight",   "HardLight",  "VividLight", "LinearLight", "PinLight",  "HardMix",    "Difference",
                                          "Exclusion",   "Subtract",   "Divide",    "Hue",        "Saturation", "Color",      "Luminosity"};
    if (kind == BlendMode::Kind::Unknown) return os << "Unknown";
    return os << kNames[static_cast<size_t>(kind)];
}

std::ostream& operator<<(std::ostream& os, const BlendMode& mode) {
    if (mode.kind() == BlendMode::Kind::Unknown) {
        auto k = mode.key();
        return os << "Unknown([" << int(k[0]) << ", " << int(k[1]) << ", " << int(k[2]) << ", " << int(k[3]) << "])";
    }
    return os << mode.kind();
}

} // namespace psd
