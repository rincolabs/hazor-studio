#pragma once

#include <array>
#include <cstdint>
#include <ostream>
#include <span>
#include <vector>

#include "Error.hpp"
#include "Header.hpp"

// Channel image data codecs (Raw, RLE/PackBits, ZIP, ZIP with prediction).
// See PROVENANCE.md for origin details.
//
// All decoded data is *planar* bytes in the file's native big-endian sample
// order: planes x height x rowBytes, where rowBytes depends on depth (depth
// 1 packs 8 pixels per byte, MSB first).

namespace psd {

// Maximum number of decoded bytes a single decode call may produce: 8 GiB on
// 64-bit targets, 2 GiB elsewhere. See compression.rs's MAX_DECODED_BYTES doc
// comment for the rationale (real PSBs need this much).
inline constexpr uint64_t kMaxDecodedBytes = (sizeof(void*) >= 8) ? (uint64_t{8} << 30) : (uint64_t{2} << 30);

// Compression method stored before channel / image data.
class Compression {
public:
    enum class Kind {
        Raw,           // 0: uncompressed.
        Rle,           // 1: PackBits RLE with a per-row byte-count table.
        Zip,           // 2: zlib without prediction.
        ZipPrediction, // 3: zlib with horizontal delta prediction.
        Unknown,       // unknown method; data is preserved but cannot be decoded.
    };

    static const Compression Raw, Rle, Zip, ZipPrediction;
    // All four decodable methods.
    static const std::array<Compression, 4>& all();

    Compression() : Compression(Kind::Raw) {}
    static Compression fromU16(uint16_t v);
    uint16_t asU16() const;
    Kind kind() const { return m_kind; }

    bool operator==(const Compression& other) const { return m_kind == other.m_kind && (m_kind != Kind::Unknown || m_raw == other.m_raw); }
    bool operator!=(const Compression& other) const { return !(*this == other); }

private:
    explicit Compression(Kind kind, uint16_t raw = 0) : m_kind(kind), m_raw(raw) {}
    Kind m_kind;
    uint16_t m_raw;
};

// PackBits (Apple / TIFF) run-length coding as used by Photoshop RLE.
namespace packbits {

// Encodes one row, appending to `out`. Never emits the -128 no-op.
void encode(std::span<const uint8_t> src, std::vector<uint8_t>& out);
// Returns the encoded form of `src`.
std::vector<uint8_t> encodeVec(std::span<const uint8_t> src);
// Decodes `src` into exactly `expected` bytes appended to `out`.
Result<void> decodeInto(std::span<const uint8_t> src, size_t expected, std::vector<uint8_t>& out);
// Decodes into a new vector of exactly `expected` bytes.
Result<std::vector<uint8_t>> decode(std::span<const uint8_t> src, size_t expected);

} // namespace packbits

// Geometry of a set of planes to encode/decode.
struct PlaneLayout {
    size_t planes = 0; // Number of planes (1 for a layer channel, channel count for the merged image).
    size_t width = 0;  // Pixels per row.
    size_t height = 0; // Rows per plane.
    uint16_t depth = 0; // Bits per sample (1, 8, 16, 32).
    Version version = Version::Psd; // PSD vs PSB (affects RLE count width).

    size_t rowBytes() const { return psd::rowBytes(width, depth); }
    Result<size_t> rows() const;
    Result<uint64_t> totalBytes() const;
    Result<size_t> decodedLen() const;
    // Byte width of the per-row RLE count field: 2 (PSD) or 4 (PSB).
    size_t countSize() const { return isPsb(version) ? 4 : 2; }
};

// Decodes planar data (excluding the 2-byte compression field).
Result<std::vector<uint8_t>> decodePlanes(Compression compression, std::span<const uint8_t> data, const PlaneLayout& layout);
// Encodes planar data (excluding the 2-byte compression field).
Result<std::vector<uint8_t>> encodePlanes(Compression compression, std::span<const uint8_t> decoded, const PlaneLayout& layout);
// Validates encoded sizes and, for ZIP, the complete stream and checksum.
Result<void> validatePlanes(Compression compression, std::span<const uint8_t> data, const PlaneLayout& layout);

// zlib-compresses `data` at the default level.
std::vector<uint8_t> zipCompress(std::span<const uint8_t> data);
// Decompresses a zlib stream, requiring at least `expected` output bytes and
// returning exactly `expected`.
Result<std::vector<uint8_t>> zipDecompress(std::span<const uint8_t> data, size_t expected);

// Applies horizontal delta prediction in place (encoder side).
Result<void> predict(std::span<uint8_t> buf, const PlaneLayout& layout);
// Reverses predict() in place (decoder side).
Result<void> unpredict(std::span<uint8_t> buf, const PlaneLayout& layout);

// Debug-style printers (see Error.hpp).
std::ostream& operator<<(std::ostream& os, Compression::Kind kind);
std::ostream& operator<<(std::ostream& os, const Compression& c);

} // namespace psd
