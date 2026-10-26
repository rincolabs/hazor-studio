#include "Header.hpp"

namespace psd {

const ColorMode ColorMode::Bitmap(ColorMode::Kind::Bitmap);
const ColorMode ColorMode::Grayscale(ColorMode::Kind::Grayscale);
const ColorMode ColorMode::Indexed(ColorMode::Kind::Indexed);
const ColorMode ColorMode::Rgb(ColorMode::Kind::Rgb);
const ColorMode ColorMode::Cmyk(ColorMode::Kind::Cmyk);
const ColorMode ColorMode::Multichannel(ColorMode::Kind::Multichannel);
const ColorMode ColorMode::Duotone(ColorMode::Kind::Duotone);
const ColorMode ColorMode::Lab(ColorMode::Kind::Lab);

ColorMode ColorMode::fromU16(uint16_t v) {
    switch (v) {
    case 0: return Bitmap;
    case 1: return Grayscale;
    case 2: return Indexed;
    case 3: return Rgb;
    case 4: return Cmyk;
    case 7: return Multichannel;
    case 8: return Duotone;
    case 9: return Lab;
    default: return ColorMode(Kind::Unknown, v);
    }
}

uint16_t ColorMode::asU16() const {
    switch (m_kind) {
    case Kind::Bitmap: return 0;
    case Kind::Grayscale: return 1;
    case Kind::Indexed: return 2;
    case Kind::Rgb: return 3;
    case Kind::Cmyk: return 4;
    case Kind::Multichannel: return 7;
    case Kind::Duotone: return 8;
    case Kind::Lab: return 9;
    case Kind::Unknown: return m_raw;
    }
    return m_raw;
}

std::optional<int> ColorMode::colorChannels() const {
    switch (m_kind) {
    case Kind::Bitmap:
    case Kind::Grayscale:
    case Kind::Indexed:
    case Kind::Duotone:
        return 1;
    case Kind::Rgb:
    case Kind::Lab:
        return 3;
    case Kind::Cmyk:
        return 4;
    case Kind::Multichannel:
    case Kind::Unknown:
        return std::nullopt;
    }
    return std::nullopt;
}

size_t rowBytes(size_t width, uint16_t depth) {
    if (depth == 1) return (width + 7) / 8;
    size_t bytesPerSample = static_cast<size_t>(depth / 8);
    if (bytesPerSample < 1) bytesPerSample = 1;
    return width * bytesPerSample;
}

size_t Header::rowBytes() const { return psd::rowBytes(width, depth); }

Result<Header> Header::read(Reader& r) {
    PSD_TRY_ASSIGN(auto sig, r.array<4>());
    if (sig != std::array<uint8_t, 4>{'8', 'B', 'P', 'S'}) {
        return PsdError::invalidSignature("8BPS", sig);
    }
    PSD_TRY_ASSIGN(auto versionNum, r.u16());
    Version version = Version::Psd;
    switch (versionNum) {
    case 1: version = Version::Psd; break;
    case 2: version = Version::Psb; break;
    default: return PsdError::unsupportedVersion(versionNum);
    }
    PSD_TRY_ASSIGN(auto reserved, r.array<6>());
    PSD_TRY_ASSIGN(auto channels, r.u16());
    PSD_TRY_ASSIGN(auto height, r.u32());
    PSD_TRY_ASSIGN(auto width, r.u32());
    PSD_TRY_ASSIGN(auto depth, r.u16());
    PSD_TRY_ASSIGN(auto colorModeRaw, r.u16());
    Header h;
    h.version = version;
    h.reserved = reserved;
    h.channels = channels;
    h.height = height;
    h.width = width;
    h.depth = depth;
    h.colorMode = ColorMode::fromU16(colorModeRaw);
    PSD_TRY(h.validate());
    return h;
}

Result<void> Header::validate() const {
    if (channels == 0 || channels > kMaxChannels) {
        return PsdError::invalid("channel count " + std::to_string(channels) + " out of range 1..=56");
    }
    if (width == 0 || height == 0) {
        return PsdError::invalid("image dimensions " + std::to_string(width) + "x" + std::to_string(height) + " must be at least 1x1");
    }
    if (width > kMaxDimension || height > kMaxDimension) {
        return PsdError::limitExceeded("image dimensions exceed 300000");
    }
    if (depth != 1 && depth != 8 && depth != 16 && depth != 32) {
        return PsdError::invalid("unsupported bit depth " + std::to_string(depth));
    }
    return {};
}

std::ostream& operator<<(std::ostream& os, Version v) { return os << (v == Version::Psb ? "Psb" : "Psd"); }

std::ostream& operator<<(std::ostream& os, ColorMode::Kind kind) {
    switch (kind) {
    case ColorMode::Kind::Bitmap: return os << "Bitmap";
    case ColorMode::Kind::Grayscale: return os << "Grayscale";
    case ColorMode::Kind::Indexed: return os << "Indexed";
    case ColorMode::Kind::Rgb: return os << "Rgb";
    case ColorMode::Kind::Cmyk: return os << "Cmyk";
    case ColorMode::Kind::Multichannel: return os << "Multichannel";
    case ColorMode::Kind::Duotone: return os << "Duotone";
    case ColorMode::Kind::Lab: return os << "Lab";
    case ColorMode::Kind::Unknown: return os << "Unknown";
    }
    return os << "?";
}

std::ostream& operator<<(std::ostream& os, const ColorMode& mode) {
    if (mode.kind() == ColorMode::Kind::Unknown) return os << "Unknown(" << mode.asU16() << ")";
    return os << mode.kind();
}

std::ostream& operator<<(std::ostream& os, const Header& h) {
    return os << "Header{version=" << h.version << ", channels=" << h.channels << ", width=" << h.width << ", height=" << h.height
              << ", depth=" << h.depth << ", colorMode=" << h.colorMode << "}";
}

void Header::write(std::vector<uint8_t>& out) const {
    put(out, std::array<uint8_t, 4>{'8', 'B', 'P', 'S'});
    putU16(out, versionAsU16(version));
    put(out, reserved);
    putU16(out, channels);
    putU32(out, height);
    putU32(out, width);
    putU16(out, depth);
    putU16(out, colorMode.asU16());
}

} // namespace psd
