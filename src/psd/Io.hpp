#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <span>
#include <string>
#include <vector>

#include "Error.hpp"

// A bounds-checked big-endian reader/writer, plus the Pascal-string,
// legacy-name and Unicode helpers shared by many modules. See PROVENANCE.md
// for origin details.

namespace psd {

// A cursor over a byte span. Every read is bounds checked. Integers are
// big-endian (the PSD byte order) unless constructed little-endian, which
// only the TIFF byte-order transcoder (Tiff.cpp) uses.
class Reader {
public:
    explicit Reader(std::span<const uint8_t> data, bool little = false) : m_data(data), m_little(little) {}

    size_t pos() const { return m_pos; }
    size_t remaining() const { return m_data.size() - m_pos; }
    bool isEmpty() const { return remaining() == 0; }
    bool isLittle() const { return m_little; }

    // Bytes from the current position without consuming.
    std::span<const uint8_t> peekRest() const { return m_data.subspan(m_pos); }

    Result<std::span<const uint8_t>> bytes(size_t n);
    // Reads `n` bytes where `n` comes from a (possibly 64-bit) length field.
    Result<std::span<const uint8_t>> bytesU64(uint64_t n);
    Result<Reader> sub(uint64_t n);
    Result<void> skip(size_t n);

    template <size_t N>
    Result<std::array<uint8_t, N>> array() {
        auto s = bytes(N);
        if (!s) return std::move(s).error();
        std::array<uint8_t, N> out{};
        std::memcpy(out.data(), s.value().data(), N);
        return out;
    }

    Result<uint8_t> u8();
    Result<uint16_t> u16();
    Result<int16_t> i16();
    Result<uint32_t> u32();
    Result<int32_t> i32();
    Result<uint64_t> u64();
    Result<int64_t> i64();
    Result<double> f64();

    // Reads a length field that is 4 bytes, or 8 bytes when `isLong`.
    Result<uint64_t> lenField(bool isLong);

    // Returns an error unless `count * minItemSize` bytes remain. Used to
    // validate element counts before allocating.
    Result<void> checkCount(uint64_t count, uint64_t minItemSize) const;

private:
    std::span<const uint8_t> m_data;
    size_t m_pos = 0;
    bool m_little = false;
};

// --- Big-endian writing helpers on std::vector<uint8_t> (WriteExt) --------

void putU8(std::vector<uint8_t>& out, uint8_t v);
void putU16(std::vector<uint8_t>& out, uint16_t v);
void putI16(std::vector<uint8_t>& out, int16_t v);
void putU32(std::vector<uint8_t>& out, uint32_t v);
void putI32(std::vector<uint8_t>& out, int32_t v);
void putU64(std::vector<uint8_t>& out, uint64_t v);
void putI64(std::vector<uint8_t>& out, int64_t v);
void putF64(std::vector<uint8_t>& out, double v);
void put(std::vector<uint8_t>& out, std::span<const uint8_t> b);

// Writes a length field (4 or 8 bytes).
Result<void> putLen(std::vector<uint8_t>& out, uint64_t v, bool isLong);
// Reserves a length placeholder, returning its position.
size_t beginLen(std::vector<uint8_t>& out, bool isLong);
// Back-patches a placeholder with the number of bytes written since.
Result<void> endLen(std::vector<uint8_t>& out, size_t at, bool isLong);

// --- Pascal / legacy-name / Unicode helpers -------------------------------

// Reads a Pascal string (length byte + bytes) whose total size is padded to
// a multiple of `align`. Returns the raw name bytes.
Result<std::vector<uint8_t>> readPascal(Reader& r, size_t align);
void writePascal(std::vector<uint8_t>& out, std::span<const uint8_t> name, size_t align);

// Decodes legacy (Pascal) names: UTF-8 when valid, otherwise Latin-1.
std::string decodeLegacyName(std::span<const uint8_t> b);
// Encodes a name for a legacy Pascal field: non-ASCII chars become '?'.
std::vector<uint8_t> encodeLegacyName(const std::string& s);

// Reads a Photoshop Unicode string: u32 code-unit count + UTF-16BE units.
Result<std::vector<uint16_t>> readUnicodeUnits(Reader& r);
void writeUnicodeUnits(std::vector<uint8_t>& out, std::span<const uint16_t> units);

// A name as UTF-16 code units, capped at `max` without ever splitting a
// surrogate pair.
std::vector<uint16_t> utf16Units(const std::string& name, size_t max);

// UTF-8 (std::string) <-> UTF-16 (std::vector<uint16_t>) conversions used by
// Descriptor's UnicodeString. `utf8ToUtf16` assumes well-formed UTF-8 input
// (true for every std::string this library itself produces);
// `utf16ToUtf8Lossy` mirrors Rust's String::from_utf16_lossy (invalid/lone
// surrogates become U+FFFD).
std::vector<uint16_t> utf8ToUtf16(const std::string& s);
std::string utf16ToUtf8Lossy(std::span<const uint16_t> units);

// Rust's String::from_utf8_lossy: invalid byte sequences become U+FFFD.
std::string utf8BytesToStringLossy(std::span<const uint8_t> bytes);

// Rust's str::from_utf8(...).is_ok(): strict UTF-8 validity (no lossy
// replacement, no overlong encodings, no surrogates).
bool isValidUtf8(std::span<const uint8_t> bytes);

// Generic std::optional<T> printer (Rust's Option<T> Debug equivalent),
// needed for Boost.Test's assertion diagnostics whenever a ported struct
// field is optional. Found via ADL for psd:: element types, and via `using
// namespace psd` (present in every test file) otherwise.
template <typename T>
std::ostream& operator<<(std::ostream& os, const std::optional<T>& v) {
    if (v.has_value()) return os << *v;
    return os << "None";
}

} // namespace psd
