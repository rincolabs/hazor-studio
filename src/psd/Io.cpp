#include "Io.hpp"

#include <bit>
#include <climits>

namespace psd {

namespace {

uint64_t saturatingMulU64(uint64_t a, uint64_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
}

// Decodes one UTF-8 codepoint starting at byte index `i` of `s`, advancing
// `i` past it. Malformed sequences decode to U+FFFD and advance by one byte
// (lossy; used only for strings this library already produced as valid
// UTF-8, where this path is never actually exercised).
uint32_t decodeUtf8Next(const std::string& s, size_t& i) {
    uint8_t c = static_cast<uint8_t>(s[i]);
    if (c < 0x80) {
        i += 1;
        return c;
    }
    size_t len;
    uint32_t first;
    uint32_t minCp;
    if ((c & 0xE0) == 0xC0) {
        len = 2;
        first = c & 0x1F;
        minCp = 0x80;
    } else if ((c & 0xF0) == 0xE0) {
        len = 3;
        first = c & 0x0F;
        minCp = 0x800;
    } else if ((c & 0xF8) == 0xF0) {
        len = 4;
        first = c & 0x07;
        minCp = 0x10000;
    } else {
        i += 1;
        return 0xFFFD;
    }
    if (i + len > s.size()) {
        i += 1;
        return 0xFFFD;
    }
    uint32_t cp = first;
    for (size_t k = 1; k < len; ++k) {
        uint8_t cc = static_cast<uint8_t>(s[i + k]);
        if ((cc & 0xC0) != 0x80) {
            i += 1;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
        i += 1;
        return 0xFFFD;
    }
    i += len;
    return cp;
}

void appendUtf8(std::string& out, uint32_t cp) {
    if (cp < 0x80) {
        out.push_back(static_cast<char>(cp));
    } else if (cp < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else if (cp < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
}

void appendUtf16(std::vector<uint16_t>& out, uint32_t cp) {
    if (cp < 0x10000) {
        out.push_back(static_cast<uint16_t>(cp));
    } else {
        cp -= 0x10000;
        out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
    }
}

} // namespace

bool isValidUtf8(std::span<const uint8_t> b) {
    size_t i = 0;
    while (i < b.size()) {
        uint8_t c = b[i];
        if (c < 0x80) {
            i += 1;
            continue;
        }
        size_t len;
        uint32_t first;
        uint32_t minCp;
        if ((c & 0xE0) == 0xC0) {
            len = 2;
            first = c & 0x1F;
            minCp = 0x80;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            first = c & 0x0F;
            minCp = 0x800;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            first = c & 0x07;
            minCp = 0x10000;
        } else {
            return false;
        }
        if (i + len > b.size()) return false;
        uint32_t cp = first;
        for (size_t k = 1; k < len; ++k) {
            uint8_t cc = b[i + k];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += len;
    }
    return true;
}

Result<std::span<const uint8_t>> Reader::bytes(size_t n) {
    if (n > remaining()) {
        return PsdError::unexpectedEof(m_pos, n - remaining());
    }
    auto s = m_data.subspan(m_pos, n);
    m_pos += n;
    return s;
}

Result<std::span<const uint8_t>> Reader::bytesU64(uint64_t n) {
    if (n > static_cast<uint64_t>(SIZE_MAX)) {
        return PsdError::limitExceeded("length exceeds address space");
    }
    return bytes(static_cast<size_t>(n));
}

Result<Reader> Reader::sub(uint64_t n) {
    PSD_TRY_ASSIGN(auto s, bytesU64(n));
    return Reader(s, m_little);
}

Result<void> Reader::skip(size_t n) {
    PSD_TRY(bytes(n));
    return {};
}

Result<uint8_t> Reader::u8() {
    PSD_TRY_ASSIGN(auto a, array<1>());
    return a[0];
}

Result<uint16_t> Reader::u16() {
    PSD_TRY_ASSIGN(auto b, array<2>());
    return m_little ? static_cast<uint16_t>(b[0] | (b[1] << 8)) : static_cast<uint16_t>((b[0] << 8) | b[1]);
}

Result<int16_t> Reader::i16() {
    PSD_TRY_ASSIGN(auto v, u16());
    return static_cast<int16_t>(v);
}

Result<uint32_t> Reader::u32() {
    PSD_TRY_ASSIGN(auto b, array<4>());
    if (m_little) {
        return static_cast<uint32_t>(b[0]) | (static_cast<uint32_t>(b[1]) << 8) | (static_cast<uint32_t>(b[2]) << 16) | (static_cast<uint32_t>(b[3]) << 24);
    }
    return (static_cast<uint32_t>(b[0]) << 24) | (static_cast<uint32_t>(b[1]) << 16) | (static_cast<uint32_t>(b[2]) << 8) | static_cast<uint32_t>(b[3]);
}

Result<int32_t> Reader::i32() {
    PSD_TRY_ASSIGN(auto v, u32());
    return static_cast<int32_t>(v);
}

Result<uint64_t> Reader::u64() {
    PSD_TRY_ASSIGN(auto b, array<8>());
    uint64_t v = 0;
    if (m_little) {
        for (int i = 7; i >= 0; --i) v = (v << 8) | b[static_cast<size_t>(i)];
    } else {
        for (int i = 0; i < 8; ++i) v = (v << 8) | b[static_cast<size_t>(i)];
    }
    return v;
}

Result<int64_t> Reader::i64() {
    PSD_TRY_ASSIGN(auto v, u64());
    return static_cast<int64_t>(v);
}

Result<double> Reader::f64() {
    PSD_TRY_ASSIGN(auto v, u64());
    return std::bit_cast<double>(v);
}

Result<uint64_t> Reader::lenField(bool isLong) {
    if (isLong) return u64();
    PSD_TRY_ASSIGN(auto v, u32());
    return static_cast<uint64_t>(v);
}

Result<void> Reader::checkCount(uint64_t count, uint64_t minItemSize) const {
    uint64_t need = saturatingMulU64(count, minItemSize);
    uint64_t rem = static_cast<uint64_t>(remaining());
    if (need > rem) {
        uint64_t diff = need - rem;
        size_t neededSz = diff > static_cast<uint64_t>(SIZE_MAX) ? SIZE_MAX : static_cast<size_t>(diff);
        return PsdError::unexpectedEof(m_pos, neededSz);
    }
    return {};
}

void putU8(std::vector<uint8_t>& out, uint8_t v) { out.push_back(v); }
void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}
void putI16(std::vector<uint8_t>& out, int16_t v) { putU16(out, static_cast<uint16_t>(v)); }
void putU32(std::vector<uint8_t>& out, uint32_t v) {
    out.push_back(static_cast<uint8_t>(v >> 24));
    out.push_back(static_cast<uint8_t>(v >> 16));
    out.push_back(static_cast<uint8_t>(v >> 8));
    out.push_back(static_cast<uint8_t>(v));
}
void putI32(std::vector<uint8_t>& out, int32_t v) { putU32(out, static_cast<uint32_t>(v)); }
void putU64(std::vector<uint8_t>& out, uint64_t v) {
    for (int shift = 56; shift >= 0; shift -= 8) out.push_back(static_cast<uint8_t>(v >> shift));
}
void putI64(std::vector<uint8_t>& out, int64_t v) { putU64(out, static_cast<uint64_t>(v)); }
void putF64(std::vector<uint8_t>& out, double v) { putU64(out, std::bit_cast<uint64_t>(v)); }
void put(std::vector<uint8_t>& out, std::span<const uint8_t> b) { out.insert(out.end(), b.begin(), b.end()); }

Result<void> putLen(std::vector<uint8_t>& out, uint64_t v, bool isLong) {
    if (isLong) {
        putU64(out, v);
        return {};
    }
    if (v > static_cast<uint64_t>(UINT32_MAX)) {
        return PsdError::limitExceeded("section too large for 32-bit length (use PSB)");
    }
    putU32(out, static_cast<uint32_t>(v));
    return {};
}

size_t beginLen(std::vector<uint8_t>& out, bool isLong) {
    size_t at = out.size();
    out.insert(out.end(), isLong ? 8 : 4, 0u);
    return at;
}

Result<void> endLen(std::vector<uint8_t>& out, size_t at, bool isLong) {
    size_t hdr = isLong ? 8 : 4;
    uint64_t n = static_cast<uint64_t>(out.size() - at - hdr);
    if (isLong) {
        for (int i = 0; i < 8; ++i) out[at + static_cast<size_t>(i)] = static_cast<uint8_t>(n >> (56 - 8 * i));
    } else {
        if (n > static_cast<uint64_t>(UINT32_MAX)) {
            return PsdError::limitExceeded("section too large for 32-bit length (use PSB)");
        }
        for (int i = 0; i < 4; ++i) out[at + static_cast<size_t>(i)] = static_cast<uint8_t>(n >> (24 - 8 * i));
    }
    return {};
}

Result<std::vector<uint8_t>> readPascal(Reader& r, size_t align) {
    PSD_TRY_ASSIGN(auto n, r.u8());
    PSD_TRY_ASSIGN(auto nameSpan, r.bytes(n));
    std::vector<uint8_t> name(nameSpan.begin(), nameSpan.end());
    size_t total = 1 + static_cast<size_t>(n);
    size_t pad = (align - total % align) % align;
    PSD_TRY(r.skip(pad));
    return name;
}

void writePascal(std::vector<uint8_t>& out, std::span<const uint8_t> name, size_t align) {
    size_t n = std::min(name.size(), static_cast<size_t>(255));
    putU8(out, static_cast<uint8_t>(n));
    put(out, name.subspan(0, n));
    size_t total = 1 + n;
    size_t pad = (align - total % align) % align;
    out.insert(out.end(), pad, 0u);
}

std::string decodeLegacyName(std::span<const uint8_t> b) {
    if (isValidUtf8(b)) {
        return std::string(reinterpret_cast<const char*>(b.data()), b.size());
    }
    std::string out;
    for (uint8_t c : b) appendUtf8(out, c);
    return out;
}

std::vector<uint8_t> encodeLegacyName(const std::string& s) {
    std::vector<uint8_t> out;
    size_t i = 0;
    size_t count = 0;
    while (i < s.size() && count < 255) {
        uint32_t cp = decodeUtf8Next(s, i);
        bool asciiPrintable = cp < 0x80 && !(cp < 0x20 || cp == 0x7F);
        out.push_back(asciiPrintable ? static_cast<uint8_t>(cp) : uint8_t{'?'});
        ++count;
    }
    return out;
}

Result<std::vector<uint16_t>> readUnicodeUnits(Reader& r) {
    PSD_TRY_ASSIGN(auto n, r.u32());
    PSD_TRY(r.checkCount(n, 2));
    std::vector<uint16_t> v;
    v.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        PSD_TRY_ASSIGN(auto u, r.u16());
        v.push_back(u);
    }
    return v;
}

void writeUnicodeUnits(std::vector<uint8_t>& out, std::span<const uint16_t> units) {
    putU32(out, static_cast<uint32_t>(units.size()));
    for (uint16_t u : units) putU16(out, u);
}

std::vector<uint16_t> utf16Units(const std::string& name, size_t max) {
    std::vector<uint16_t> units;
    size_t i = 0;
    while (i < name.size()) {
        size_t before = i;
        uint32_t cp = decodeUtf8Next(name, i);
        size_t need = cp >= 0x10000 ? 2 : 1;
        if (units.size() + need > max) {
            i = before;
            break;
        }
        appendUtf16(units, cp);
    }
    return units;
}

std::string utf8BytesToStringLossy(std::span<const uint8_t> bytes) {
    if (bytes.empty()) return {};
    std::string tmp(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::string out;
    size_t i = 0;
    while (i < tmp.size()) {
        uint32_t cp = decodeUtf8Next(tmp, i);
        appendUtf8(out, cp);
    }
    return out;
}

std::vector<uint16_t> utf8ToUtf16(const std::string& s) {
    std::vector<uint16_t> out;
    size_t i = 0;
    while (i < s.size()) {
        uint32_t cp = decodeUtf8Next(s, i);
        appendUtf16(out, cp);
    }
    return out;
}

std::string utf16ToUtf8Lossy(std::span<const uint16_t> units) {
    std::string out;
    size_t i = 0;
    while (i < units.size()) {
        uint16_t u = units[i];
        if (u >= 0xD800 && u <= 0xDBFF) {
            if (i + 1 < units.size() && units[i + 1] >= 0xDC00 && units[i + 1] <= 0xDFFF) {
                uint32_t cp = 0x10000 + ((static_cast<uint32_t>(u) - 0xD800) << 10) + (static_cast<uint32_t>(units[i + 1]) - 0xDC00);
                appendUtf8(out, cp);
                i += 2;
                continue;
            }
            appendUtf8(out, 0xFFFD);
            i += 1;
            continue;
        }
        if (u >= 0xDC00 && u <= 0xDFFF) {
            appendUtf8(out, 0xFFFD);
            i += 1;
            continue;
        }
        appendUtf8(out, u);
        i += 1;
    }
    return out;
}

} // namespace psd
