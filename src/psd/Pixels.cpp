#include "Pixels.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace psd {

std::vector<uint16_t> samplesU16(std::span<const uint8_t> bytes) {
    std::vector<uint16_t> out;
    out.reserve(bytes.size() / 2);
    for (size_t i = 0; i + 2 <= bytes.size(); i += 2) {
        out.push_back(static_cast<uint16_t>((bytes[i] << 8) | bytes[i + 1]));
    }
    return out;
}

std::vector<float> samplesF32(std::span<const uint8_t> bytes) {
    std::vector<float> out;
    out.reserve(bytes.size() / 4);
    for (size_t i = 0; i + 4 <= bytes.size(); i += 4) {
        uint32_t bits = (uint32_t(bytes[i]) << 24) | (uint32_t(bytes[i + 1]) << 16) | (uint32_t(bytes[i + 2]) << 8) | uint32_t(bytes[i + 3]);
        float f;
        std::memcpy(&f, &bits, 4);
        out.push_back(f);
    }
    return out;
}

std::vector<uint8_t> u16ToBytes(std::span<const uint16_t> samples) {
    std::vector<uint8_t> out;
    out.reserve(samples.size() * 2);
    for (uint16_t s : samples) {
        out.push_back(static_cast<uint8_t>(s >> 8));
        out.push_back(static_cast<uint8_t>(s));
    }
    return out;
}

std::vector<uint8_t> f32ToBytes(std::span<const float> samples) {
    std::vector<uint8_t> out;
    out.reserve(samples.size() * 4);
    for (float s : samples) {
        uint32_t bits;
        std::memcpy(&bits, &s, 4);
        out.push_back(static_cast<uint8_t>(bits >> 24));
        out.push_back(static_cast<uint8_t>(bits >> 16));
        out.push_back(static_cast<uint8_t>(bits >> 8));
        out.push_back(static_cast<uint8_t>(bits));
    }
    return out;
}

namespace {
PsdError oomError(const char* what) { return PsdError::limitExceeded(what); }

Result<std::vector<uint8_t>> tryBuffer(size_t len) {
    std::vector<uint8_t> out;
    try {
        out.reserve(len);
    } catch (const std::bad_alloc&) {
        return oomError("not enough memory for the converted plane");
    }
    return out;
}

template <typename F>
Result<std::vector<uint8_t>> unpackBitsWith(std::span<const uint8_t> bytes, size_t width, size_t height, F map) {
    if (width != 0 && height > SIZE_MAX / width) return PsdError::limitExceeded("plane size overflow");
    size_t len = width * height;
    size_t rb = (width + 7) / 8;
    PSD_TRY_ASSIGN(auto out, tryBuffer(len));
    // rb <= width (for width > 0), and width*height == len already fit in
    // size_t above, so y*rb (y < height) cannot overflow either.
    for (size_t y = 0; y < height; ++y) {
        size_t row = y * rb;
        for (size_t x = 0; x < width; ++x) {
            size_t idx = row + x / 8;
            uint8_t b = idx < bytes.size() ? bytes[idx] : 0;
            out.push_back(map(static_cast<uint8_t>((b >> (7 - (x % 8))) & 1)));
        }
    }
    return out;
}
} // namespace

Result<std::vector<uint8_t>> unpackBits(std::span<const uint8_t> bytes, size_t width, size_t height) {
    return unpackBitsWith(bytes, width, height, [](uint8_t bit) { return bit; });
}

Result<std::vector<uint8_t>> planeToU8(std::span<const uint8_t> bytes, uint16_t depth, size_t width, size_t height) {
    switch (depth) {
    case 1:
        return unpackBitsWith(bytes, width, height, [](uint8_t b) -> uint8_t { return b == 1 ? 0 : 255; });
    case 8: {
        PSD_TRY_ASSIGN(auto out, tryBuffer(bytes.size()));
        out.insert(out.end(), bytes.begin(), bytes.end());
        return out;
    }
    case 16: {
        size_t n = bytes.size() / 2;
        PSD_TRY_ASSIGN(auto out, tryBuffer(n));
        for (size_t i = 0; i < n; ++i) {
            uint16_t v = static_cast<uint16_t>((bytes[2 * i] << 8) | bytes[2 * i + 1]);
            out.push_back(static_cast<uint8_t>((uint32_t(v) * 255 + 32767) / 65535));
        }
        return out;
    }
    case 32: {
        size_t n = bytes.size() / 4;
        PSD_TRY_ASSIGN(auto out, tryBuffer(n));
        for (size_t i = 0; i < n; ++i) {
            uint32_t bits =
                (uint32_t(bytes[4 * i]) << 24) | (uint32_t(bytes[4 * i + 1]) << 16) | (uint32_t(bytes[4 * i + 2]) << 8) | uint32_t(bytes[4 * i + 3]);
            float v;
            std::memcpy(&v, &bits, 4);
            if (std::isnan(v)) {
                out.push_back(0);
            } else {
                float c = std::clamp(v, 0.0f, 1.0f);
                out.push_back(static_cast<uint8_t>(std::lround(c * 255.0f)));
            }
        }
        return out;
    }
    default:
        return PsdError::unsupported("depth " + std::to_string(depth));
    }
}

std::ostream& operator<<(std::ostream& os, const RgbaImage& img) {
    return os << "RgbaImage{left=" << img.left << ", top=" << img.top << ", width=" << img.width << ", height=" << img.height << "}";
}
std::ostream& operator<<(std::ostream& os, const GrayImage& img) {
    return os << "GrayImage{left=" << img.left << ", top=" << img.top << ", width=" << img.width << ", height=" << img.height << "}";
}

namespace {

Result<size_t> colorChannelCount(ColorMode mode) {
    switch (mode.kind()) {
    case ColorMode::Kind::Rgb: return size_t{3};
    case ColorMode::Kind::Cmyk: return size_t{4};
    case ColorMode::Kind::Grayscale:
    case ColorMode::Kind::Indexed:
    case ColorMode::Kind::Bitmap:
    case ColorMode::Kind::Duotone:
        return size_t{1};
    default:
        return PsdError::unsupported("rgba8 for color mode");
    }
}

} // namespace

namespace detail {
Result<std::vector<uint8_t>> interleave(ColorMode mode, std::span<const std::span<const uint8_t>> color, std::optional<std::span<const uint8_t>> alpha,
                                         std::span<const uint8_t> palette, size_t n) {
    PSD_TRY_ASSIGN(auto cc, colorChannelCount(mode));
    if (color.size() != cc) return PsdError::invalid("channel size mismatch");
    for (auto& p : color) {
        if (p.size() != n) return PsdError::invalid("channel size mismatch");
    }
    if (alpha.has_value() && alpha->size() != n) return PsdError::invalid("channel size mismatch");

    if (n != 0 && n > SIZE_MAX / 4) return PsdError::limitExceeded("RGBA output exceeds MAX_DECODED_BYTES");
    size_t len = n * 4;
    if (static_cast<uint64_t>(len) > kMaxDecodedBytes) return PsdError::limitExceeded("RGBA output exceeds MAX_DECODED_BYTES");
    std::vector<uint8_t> out;
    try {
        out.reserve(len);
    } catch (const std::bad_alloc&) {
        return PsdError::limitExceeded("not enough memory for RGBA output");
    }
    out.resize(len, 255);

    if (mode == ColorMode::Rgb) {
        for (size_t i = 0; i < n; ++i) {
            out[i * 4] = color[0][i];
            out[i * 4 + 1] = color[1][i];
            out[i * 4 + 2] = color[2][i];
            out[i * 4 + 3] = alpha.has_value() ? (*alpha)[i] : 255;
        }
        return out;
    }
    for (size_t i = 0; i < n; ++i) {
        uint8_t r, g, b;
        switch (mode.kind()) {
        case ColorMode::Kind::Grayscale:
        case ColorMode::Kind::Bitmap:
        case ColorMode::Kind::Duotone:
            r = g = b = color[0][i];
            break;
        case ColorMode::Kind::Indexed: {
            size_t idx = color[0][i];
            auto p = [&](size_t k) -> uint8_t {
                size_t at = k * 256 + idx;
                return at < palette.size() ? palette[at] : 0;
            };
            r = p(0);
            g = p(1);
            b = p(2);
            break;
        }
        case ColorMode::Kind::Cmyk: {
            uint32_t k = color[3][i];
            auto f = [&](uint8_t c) -> uint8_t { return static_cast<uint8_t>((uint32_t(c) * k + 127) / 255); };
            r = f(color[0][i]);
            g = f(color[1][i]);
            b = f(color[2][i]);
            break;
        }
        default:
            return PsdError::unsupported("rgba8 for color mode");
        }
        out[i * 4] = r;
        out[i * 4 + 1] = g;
        out[i * 4 + 2] = b;
        out[i * 4 + 3] = alpha.has_value() ? (*alpha)[i] : 255;
    }
    return out;
}
} // namespace detail

namespace {

// One 8-bit sample matted against white at alpha `a`, back to straight
// colour (rounded, at most 255). At a == 0 there is no colour to recover
// and the sample is kept.
uint8_t unmatteU8(uint8_t c, uint8_t a) {
    uint32_t aa = a;
    if (aa == 0) return c;
    uint32_t base = uint32_t(c) + aa;
    uint32_t sub = base > 255 ? base - 255 : 0;
    uint32_t straight = (sub * 255 + aa / 2) / aa;
    return static_cast<uint8_t>(std::min<uint32_t>(straight, 255));
}

// unmatteU8 of every sample, one row per alpha: table[a][c].
std::vector<std::array<uint8_t, 256>> unmatteU8Table() {
    std::vector<std::array<uint8_t, 256>> table(256);
    for (int a = 0; a <= 255; ++a) {
        for (int c = 0; c <= 255; ++c) table[static_cast<size_t>(a)][static_cast<size_t>(c)] = unmatteU8(static_cast<uint8_t>(c), static_cast<uint8_t>(a));
    }
    return table;
}

Result<std::vector<uint8_t>> unmatteToU8(std::span<const uint8_t> color, std::span<const uint8_t> alpha, uint16_t depth) {
    auto convert = [](float c, float a) -> uint8_t {
        float straight = a > 0.0f ? (c + a - 1.0f) / a : c;
        float clamped = std::clamp(straight, 0.0f, 1.0f);
        return static_cast<uint8_t>(std::lround(clamped * 255.0f));
    };
    std::vector<uint8_t> out;
    switch (depth) {
    case 16: {
        size_t n = color.size() / 2;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            uint16_t cv = static_cast<uint16_t>((color[2 * i] << 8) | color[2 * i + 1]);
            uint16_t av = static_cast<uint16_t>((alpha[2 * i] << 8) | alpha[2 * i + 1]);
            out.push_back(convert(float(cv) / 65535.0f, float(av) / 65535.0f));
        }
        return out;
    }
    case 32: {
        size_t n = color.size() / 4;
        out.reserve(n);
        for (size_t i = 0; i < n; ++i) {
            uint32_t cb = (uint32_t(color[4 * i]) << 24) | (uint32_t(color[4 * i + 1]) << 16) | (uint32_t(color[4 * i + 2]) << 8) | uint32_t(color[4 * i + 3]);
            uint32_t ab = (uint32_t(alpha[4 * i]) << 24) | (uint32_t(alpha[4 * i + 1]) << 16) | (uint32_t(alpha[4 * i + 2]) << 8) | uint32_t(alpha[4 * i + 3]);
            float cf, af;
            std::memcpy(&cf, &cb, 4);
            std::memcpy(&af, &ab, 4);
            out.push_back(convert(cf, af));
        }
        return out;
    }
    default:
        return PsdError::unsupported("RGB composite depth " + std::to_string(depth));
    }
}

} // namespace

Result<std::vector<uint8_t>> LayerView::channelBytes(int16_t id) const { return m_record->decodeChannel(id, m_header->depth, m_header->version); }

Result<RgbaImage> LayerView::rgba8() const {
    Rect rect = m_record->rect;
    PSD_TRY_ASSIGN(auto wh, rect.size());
    size_t w = wh.first, h = wh.second;
    if (w != 0 && h > SIZE_MAX / w) return PsdError::limitExceeded("layer size overflow");
    size_t n = w * h;
    if (n != 0 && n > SIZE_MAX / 4) return PsdError::limitExceeded("RGBA output exceeds MAX_DECODED_BYTES");
    if (static_cast<uint64_t>(n) * 4 > kMaxDecodedBytes) return PsdError::limitExceeded("RGBA output exceeds MAX_DECODED_BYTES");

    uint16_t depth = m_header->depth;
    ColorMode mode = m_header->colorMode;
    PSD_TRY_ASSIGN(auto cc, colorChannelCount(mode));
    std::vector<std::vector<uint8_t>> colorOwned;
    colorOwned.reserve(cc);
    for (int16_t id = 0; id < static_cast<int16_t>(cc); ++id) {
        std::vector<uint8_t> plane;
        if (m_record->channel(id) != nullptr) {
            PSD_TRY_ASSIGN(auto bytes, channelBytes(id));
            if (depth == 8) plane = std::move(bytes);
            else {
                PSD_TRY_ASSIGN(plane, planeToU8(bytes, depth, w, h));
            }
        } else {
            plane.assign(n, mode == ColorMode::Cmyk ? 255 : 0);
        }
        if (plane.size() != n) return PsdError::invalid("channel size mismatch");
        colorOwned.push_back(std::move(plane));
    }
    std::optional<std::vector<uint8_t>> alphaOwned;
    if (m_record->channel(kChannelTransparency) != nullptr) {
        PSD_TRY_ASSIGN(auto bytes, channelBytes(kChannelTransparency));
        if (depth == 8) alphaOwned = std::move(bytes);
        else {
            PSD_TRY_ASSIGN(auto converted, planeToU8(bytes, depth, w, h));
            alphaOwned = std::move(converted);
        }
    }
    std::vector<std::span<const uint8_t>> colorSpans;
    colorSpans.reserve(colorOwned.size());
    for (auto& p : colorOwned) colorSpans.emplace_back(p);
    std::optional<std::span<const uint8_t>> alphaSpan;
    if (alphaOwned.has_value()) alphaSpan = std::span<const uint8_t>(*alphaOwned);
    PSD_TRY_ASSIGN(auto data, detail::interleave(mode, colorSpans, alphaSpan, std::span<const uint8_t>{}, n));
    RgbaImage img;
    img.left = rect.left;
    img.top = rect.top;
    img.width = static_cast<uint32_t>(w);
    img.height = static_cast<uint32_t>(h);
    img.data = std::move(data);
    return img;
}

std::optional<Result<GrayImage>> LayerView::userMask() const {
    int16_t id;
    if (m_record->channel(kChannelRealUserMask) != nullptr) id = kChannelRealUserMask;
    else if (m_record->channel(kChannelUserMask) != nullptr) id = kChannelUserMask;
    else return std::nullopt;

    Rect rect = m_record->channelRect(id);
    auto wh = rect.size();
    if (!wh) return Result<GrayImage>(wh.error());
    auto bytes = channelBytes(id);
    if (!bytes) return Result<GrayImage>(bytes.error());
    auto data = planeToU8(bytes.value(), m_header->depth, wh.value().first, wh.value().second);
    if (!data) return Result<GrayImage>(data.error());
    uint8_t defaultValue = 0;
    const LayerMask* m = m_record->layerMask();
    if (m) {
        if (id == kChannelRealUserMask) defaultValue = m->real.has_value() ? m->real->background : m->defaultColor;
        else defaultValue = m->defaultColor;
    }
    GrayImage img;
    img.left = rect.left;
    img.top = rect.top;
    img.width = static_cast<uint32_t>(wh.value().first);
    img.height = static_cast<uint32_t>(wh.value().second);
    img.data = std::move(data.value());
    img.defaultValue = defaultValue;
    return Result<GrayImage>(img);
}

std::optional<LayerView> PsdFile::layer(size_t index) const {
    auto& ls = layers();
    if (index >= ls.size()) return std::nullopt;
    return LayerView(index, ls[index], header);
}

std::vector<LayerView> PsdFile::iterLayers() const {
    std::vector<LayerView> out;
    auto& ls = layers();
    out.reserve(ls.size());
    for (size_t i = 0; i < ls.size(); ++i) out.emplace_back(i, ls[i], header);
    return out;
}

Result<RgbaImage> PsdFile::compositeRgba8() const {
    const Header& h = header;
    PSD_TRY(h.validate());
    size_t w = h.width, hh = h.height;
    if (w != 0 && hh > SIZE_MAX / w) return PsdError::limitExceeded("composite size overflow");
    size_t n = w * hh;
    if (n != 0 && n > SIZE_MAX / 4) return PsdError::limitExceeded("RGBA output exceeds MAX_DECODED_BYTES");
    if (static_cast<uint64_t>(n) * 4 > kMaxDecodedBytes) return PsdError::limitExceeded("RGBA output exceeds MAX_DECODED_BYTES");

    PSD_TRY_ASSIGN(auto cc, colorChannelCount(h.colorMode));
    if (static_cast<size_t>(h.channels) < cc) return PsdError::invalid("fewer channels than the color mode requires");

    PSD_TRY_ASSIGN(auto all, decodeMerged());
    size_t rb = h.rowBytes();
    if (hh != 0 && rb > SIZE_MAX / hh) return PsdError::limitExceeded("plane size overflow");
    size_t plane = rb * hh;
    bool hasAlpha = mergedHasAlpha();
    std::optional<std::span<const uint8_t>> alphaBytes;
    if (hasAlpha) {
        size_t start = cc * plane, end = (cc + 1) * plane;
        if (end > all.size()) return PsdError::invalid("alpha channel size mismatch");
        alphaBytes = std::span<const uint8_t>(all.data() + start, plane);
    }

    auto getOwned = [&](size_t i) -> Result<std::vector<uint8_t>> {
        size_t start = i * plane, end = start + plane;
        if (end > all.size()) return PsdError::invalid("channel size mismatch");
        std::span<const uint8_t> bytes(all.data() + start, plane);
        if (h.depth == 8) return std::vector<uint8_t>(bytes.begin(), bytes.end());
        return planeToU8(bytes, h.depth, w, hh);
    };

    std::vector<std::vector<uint8_t>> colorOwned;
    colorOwned.reserve(cc);
    for (size_t i = 0; i < cc; ++i) {
        if (h.colorMode == ColorMode::Rgb && h.depth != 8 && alphaBytes.has_value()) {
            size_t start = i * plane, end = start + plane;
            if (end > all.size()) return PsdError::invalid("channel size mismatch");
            std::span<const uint8_t> bytes(all.data() + start, plane);
            PSD_TRY_ASSIGN(auto unmatted, unmatteToU8(bytes, *alphaBytes, h.depth));
            colorOwned.push_back(std::move(unmatted));
        } else {
            PSD_TRY_ASSIGN(auto owned, getOwned(i));
            colorOwned.push_back(std::move(owned));
        }
    }
    std::optional<std::vector<uint8_t>> alphaOwned;
    if (hasAlpha) {
        PSD_TRY_ASSIGN(alphaOwned, getOwned(cc));
    }
    std::vector<std::span<const uint8_t>> colorSpans;
    colorSpans.reserve(colorOwned.size());
    for (auto& p : colorOwned) colorSpans.emplace_back(p);
    std::optional<std::span<const uint8_t>> alphaSpan;
    if (alphaOwned.has_value()) alphaSpan = std::span<const uint8_t>(*alphaOwned);
    PSD_TRY_ASSIGN(auto data, detail::interleave(h.colorMode, colorSpans, alphaSpan, colorModeData, n));

    if (h.colorMode == ColorMode::Rgb && h.depth == 8 && alphaOwned.has_value()) {
        auto table = unmatteU8Table();
        for (size_t i = 0; i < n; ++i) {
            uint8_t a = data[i * 4 + 3];
            auto& row = table[a];
            for (size_t c = 0; c < 3; ++c) data[i * 4 + c] = row[data[i * 4 + c]];
        }
    }
    RgbaImage img;
    img.left = 0;
    img.top = 0;
    img.width = h.width;
    img.height = h.height;
    img.data = std::move(data);
    return img;
}

} // namespace psd
