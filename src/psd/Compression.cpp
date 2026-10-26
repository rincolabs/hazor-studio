#include "Compression.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <optional>
#include <thread>

#include <zlib.h>

namespace psd {

const Compression Compression::Raw(Compression::Kind::Raw);
const Compression Compression::Rle(Compression::Kind::Rle);
const Compression Compression::Zip(Compression::Kind::Zip);
const Compression Compression::ZipPrediction(Compression::Kind::ZipPrediction);

const std::array<Compression, 4>& Compression::all() {
    static const std::array<Compression, 4> kAll = {Raw, Rle, Zip, ZipPrediction};
    return kAll;
}

Compression Compression::fromU16(uint16_t v) {
    switch (v) {
    case 0: return Raw;
    case 1: return Rle;
    case 2: return Zip;
    case 3: return ZipPrediction;
    default: return Compression(Kind::Unknown, v);
    }
}

std::ostream& operator<<(std::ostream& os, Compression::Kind kind) {
    switch (kind) {
    case Compression::Kind::Raw: return os << "Raw";
    case Compression::Kind::Rle: return os << "Rle";
    case Compression::Kind::Zip: return os << "Zip";
    case Compression::Kind::ZipPrediction: return os << "ZipPrediction";
    case Compression::Kind::Unknown: return os << "Unknown";
    }
    return os << "?";
}

std::ostream& operator<<(std::ostream& os, const Compression& c) {
    if (c.kind() == Compression::Kind::Unknown) return os << "Unknown(" << c.asU16() << ")";
    return os << c.kind();
}

uint16_t Compression::asU16() const {
    switch (m_kind) {
    case Kind::Raw: return 0;
    case Kind::Rle: return 1;
    case Kind::Zip: return 2;
    case Kind::ZipPrediction: return 3;
    case Kind::Unknown: return m_raw;
    }
    return m_raw;
}

namespace {

uint64_t saturatingMulU64(uint64_t a, uint64_t b) {
    if (a == 0 || b == 0) return 0;
    if (a > UINT64_MAX / b) return UINT64_MAX;
    return a * b;
}

uint64_t saturatingAddU64(uint64_t a, uint64_t b) {
    uint64_t r = a + b;
    return r < a ? UINT64_MAX : r;
}

PsdError oomError() { return PsdError::limitExceeded("not enough memory for the decoded channel data"); }

Result<void> tryReserveExact(std::vector<uint8_t>& v, size_t n) {
    try {
        v.reserve(n);
    } catch (const std::bad_alloc&) {
        return oomError();
    }
    return {};
}

Result<void> tryReserveMore(std::vector<uint8_t>& v, size_t n) {
    try {
        v.reserve(v.size() + n);
    } catch (const std::bad_alloc&) {
        return oomError();
    }
    return {};
}

Result<size_t> readCount(Reader& r, Version v) {
    if (isPsb(v)) {
        PSD_TRY_ASSIGN(auto c, r.u32());
        return static_cast<size_t>(c);
    }
    PSD_TRY_ASSIGN(auto c, r.u16());
    return static_cast<size_t>(c);
}

} // namespace

// --- PackBits --------------------------------------------------------------

void packbits::encode(std::span<const uint8_t> src, std::vector<uint8_t>& out) {
    size_t n = src.size();
    size_t i = 0;
    while (i < n) {
        size_t run = 1;
        while (i + run < n && run < 128 && src[i + run] == src[i]) run += 1;
        if (run >= 2) {
            int16_t header = static_cast<int16_t>(1 - static_cast<int16_t>(run));
            out.push_back(static_cast<uint8_t>(static_cast<int8_t>(header)));
            out.push_back(src[i]);
            i += run;
            continue;
        }
        size_t start = i;
        size_t len = 0;
        while (i < n && len < 128) {
            if (i + 2 < n && src[i] == src[i + 1] && src[i] == src[i + 2]) break;
            i += 1;
            len += 1;
        }
        out.push_back(static_cast<uint8_t>(len - 1));
        out.insert(out.end(), src.begin() + static_cast<ptrdiff_t>(start), src.begin() + static_cast<ptrdiff_t>(start + len));
    }
}

std::vector<uint8_t> packbits::encodeVec(std::span<const uint8_t> src) {
    std::vector<uint8_t> v;
    v.reserve(src.size() + src.size() / 128 + 1);
    encode(src, v);
    return v;
}

Result<void> packbits::decodeInto(std::span<const uint8_t> src, size_t expected, std::vector<uint8_t>& out) {
    size_t target = out.size() + expected;
    size_t i = 0;
    while (out.size() < target) {
        if (i >= src.size()) return PsdError::decompress("PackBits row ended early");
        int8_t h = static_cast<int8_t>(src[i]);
        i += 1;
        if (h >= 0) {
            size_t len = static_cast<size_t>(h) + 1;
            if (i + len > src.size()) return PsdError::decompress("PackBits literal truncated");
            if (out.size() + len > target) return PsdError::decompress("PackBits literal overflows row");
            out.insert(out.end(), src.begin() + static_cast<ptrdiff_t>(i), src.begin() + static_cast<ptrdiff_t>(i + len));
            i += len;
        } else if (h != -128) {
            size_t len = static_cast<size_t>(1 - static_cast<int32_t>(h));
            if (i >= src.size()) return PsdError::decompress("PackBits run truncated");
            uint8_t b = src[i];
            i += 1;
            if (out.size() + len > target) return PsdError::decompress("PackBits run overflows row");
            out.insert(out.end(), len, b);
        }
    }
    return {};
}

Result<std::vector<uint8_t>> packbits::decode(std::span<const uint8_t> src, size_t expected) {
    std::vector<uint8_t> v;
    v.reserve(std::min<size_t>(expected, src.size() > SIZE_MAX / 64 ? SIZE_MAX : src.size() * 64));
    PSD_TRY(decodeInto(src, expected, v));
    return v;
}

// --- PlaneLayout -------------------------------------------------------------

Result<size_t> PlaneLayout::rows() const {
    if (planes != 0 && height > SIZE_MAX / planes) return PsdError::limitExceeded("row count overflow");
    return planes * height;
}

Result<uint64_t> PlaneLayout::totalBytes() const {
    uint64_t p = static_cast<uint64_t>(planes), h = static_cast<uint64_t>(height), rb = static_cast<uint64_t>(rowBytes());
    if (p != 0 && h > UINT64_MAX / p) return PsdError::limitExceeded("decoded size overflow");
    uint64_t v = p * h;
    if (rb != 0 && v > UINT64_MAX / rb) return PsdError::limitExceeded("decoded size overflow");
    return v * rb;
}

Result<size_t> PlaneLayout::decodedLen() const {
    PSD_TRY_ASSIGN(auto total, totalBytes());
    if (total > kMaxDecodedBytes) return PsdError::limitExceeded("decoded channel data exceeds MAX_DECODED_BYTES");
    if (total > static_cast<uint64_t>(SIZE_MAX)) return PsdError::limitExceeded("decoded size exceeds address space");
    return static_cast<size_t>(total);
}

// --- zlib ---------------------------------------------------------------

std::vector<uint8_t> zipCompress(std::span<const uint8_t> data) {
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY) != Z_OK) return {};
    std::vector<uint8_t> out;
    std::vector<uint8_t> buf(1 << 16);
    size_t offset = 0;
    for (;;) {
        size_t chunkLen = std::min<size_t>(data.size() - offset, static_cast<size_t>(UINT32_MAX));
        bool isLastInputChunk = offset + chunkLen >= data.size();
        zs.next_in = const_cast<Bytef*>(data.data() + offset);
        zs.avail_in = static_cast<uInt>(chunkLen);
        offset += chunkLen;
        int flush = isLastInputChunk ? Z_FINISH : Z_NO_FLUSH;
        bool streamEnd = false;
        for (;;) {
            zs.next_out = buf.data();
            zs.avail_out = static_cast<uInt>(buf.size());
            int ret = deflate(&zs, flush);
            size_t produced = buf.size() - zs.avail_out;
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<ptrdiff_t>(produced));
            if (ret == Z_STREAM_END) {
                streamEnd = true;
                break;
            }
            if (ret != Z_OK && ret != Z_BUF_ERROR) {
                deflateEnd(&zs);
                return {};
            }
            if (zs.avail_out != 0) break; // drained this input chunk's available output
        }
        if (streamEnd) break;
        if (isLastInputChunk) break; // Z_FINISH should reach Z_STREAM_END before this
    }
    deflateEnd(&zs);
    return out;
}

Result<std::vector<uint8_t>> zipDecompress(std::span<const uint8_t> data, size_t expected) {
    std::vector<uint8_t> out;
    {
        uint64_t cap = std::min<uint64_t>(static_cast<uint64_t>(expected), saturatingMulU64(static_cast<uint64_t>(data.size()), 1032));
        PSD_TRY(tryReserveExact(out, static_cast<size_t>(cap)));
    }
    if (expected == 0) return out;

    z_stream zs{};
    if (inflateInit2(&zs, 15) != Z_OK) return PsdError::decompress("zlib inflateInit2 failed");
    std::vector<uint8_t> buf(std::max<size_t>(std::min<size_t>(static_cast<size_t>(1) << 20, expected), 1));
    uint64_t totalIn = 0, totalOut = 0;
    while (out.size() < expected) {
        zs.next_in = const_cast<Bytef*>(data.data() + totalIn);
        zs.avail_in = static_cast<uInt>(std::min<uint64_t>(static_cast<uint64_t>(data.size()) - totalIn, UINT32_MAX));
        zs.next_out = buf.data();
        size_t want = std::min(buf.size(), expected - out.size());
        zs.avail_out = static_cast<uInt>(want);
        int ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            std::string msg = zs.msg ? zs.msg : "zlib error";
            inflateEnd(&zs);
            return PsdError::decompress(msg);
        }
        uint64_t newIn = zs.total_in, newOut = zs.total_out;
        size_t produced = static_cast<size_t>(newOut - totalOut);
        auto reserved = tryReserveMore(out, produced);
        if (!reserved) {
            inflateEnd(&zs);
            return std::move(reserved).error();
        }
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<ptrdiff_t>(produced));
        bool noProgress = newIn == totalIn && newOut == totalOut;
        totalIn = newIn;
        totalOut = newOut;
        if (ret == Z_STREAM_END) break;
        if (noProgress) {
            inflateEnd(&zs);
            return PsdError::decompress("incomplete zlib stream");
        }
    }
    inflateEnd(&zs);
    if (out.size() < expected) {
        return PsdError::decompress("zlib stream produced " + std::to_string(out.size()) + " of " + std::to_string(expected) + " bytes");
    }
    return out;
}

namespace {

// The careful decoder used by decode_planes/validate_planes: bounded,
// requires at least `expected` bytes, and (when `keep`) materializes them.
Result<std::vector<uint8_t>> inflateChecked(std::span<const uint8_t> data, uint64_t expected, bool keep) {
    std::vector<uint8_t> out;
    if (keep) {
        uint64_t cap = std::min<uint64_t>(expected, saturatingMulU64(static_cast<uint64_t>(data.size()), 1032));
        PSD_TRY(tryReserveExact(out, static_cast<size_t>(std::min<uint64_t>(cap, SIZE_MAX))));
    }
    size_t bufSize = static_cast<size_t>(std::clamp<uint64_t>(saturatingAddU64(expected, 1), 8192, uint64_t{1} << 20));
    std::vector<uint8_t> buf(bufSize);

    z_stream zs{};
    if (inflateInit2(&zs, 15) != Z_OK) return PsdError::decompress("zlib inflateInit2 failed");
    uint64_t totalIn = 0, totalOut = 0;
    while (true) {
        if (totalIn > static_cast<uint64_t>(data.size())) {
            inflateEnd(&zs);
            return PsdError::decompress("zlib input overflow");
        }
        zs.next_in = const_cast<Bytef*>(data.data() + totalIn);
        zs.avail_in = static_cast<uInt>(std::min<uint64_t>(static_cast<uint64_t>(data.size()) - totalIn, UINT32_MAX));
        zs.next_out = buf.data();
        zs.avail_out = static_cast<uInt>(buf.size());
        int ret = inflate(&zs, Z_NO_FLUSH);
        if (ret != Z_OK && ret != Z_STREAM_END && ret != Z_BUF_ERROR) {
            std::string msg = zs.msg ? zs.msg : "zlib error";
            inflateEnd(&zs);
            return PsdError::decompress(msg);
        }
        uint64_t newOut = zs.total_out;
        uint64_t got = newOut;
        if (got > saturatingAddU64(expected, uint64_t{1} << 20)) {
            inflateEnd(&zs);
            return PsdError::limitExceeded("zlib output exceeds the declared size and padding allowance");
        }
        if (keep) {
            uint64_t avail = newOut - totalOut;
            uint64_t need = expected > static_cast<uint64_t>(out.size()) ? expected - static_cast<uint64_t>(out.size()) : 0;
            uint64_t n64 = std::min<uint64_t>(avail, need);
            size_t n = static_cast<size_t>(n64);
            if (n > buf.size()) {
                inflateEnd(&zs);
                return PsdError::decompress("zlib output overflow");
            }
            auto reserved = tryReserveMore(out, n);
            if (!reserved) {
                inflateEnd(&zs);
                return std::move(reserved).error();
            }
            out.insert(out.end(), buf.begin(), buf.begin() + static_cast<ptrdiff_t>(n));
        }
        uint64_t newIn = zs.total_in;
        if (ret == Z_STREAM_END) {
            inflateEnd(&zs);
            if (got < expected) {
                return PsdError::decompress("zlib stream produced " + std::to_string(got) + " of " + std::to_string(expected) + " bytes");
            }
            return out;
        }
        if (newIn == totalIn && newOut == totalOut) {
            inflateEnd(&zs);
            return PsdError::decompress("incomplete zlib stream");
        }
        totalIn = newIn;
        totalOut = newOut;
    }
}

} // namespace

// --- Prediction --------------------------------------------------------------

Result<void> predict(std::span<uint8_t> buf, const PlaneLayout& layout) {
    size_t rb = layout.rowBytes();
    if (rb == 0) return {};
    size_t w = layout.width;
    std::vector<uint8_t> scratch;
    size_t rowCount = buf.size() / rb;
    for (size_t ri = 0; ri < rowCount; ++ri) {
        auto row = buf.subspan(ri * rb, rb);
        switch (layout.depth) {
        case 1:
        case 8:
            for (size_t i = row.size(); i-- > 1;) {
                row[i] = static_cast<uint8_t>(row[i] - row[i - 1]);
            }
            break;
        case 16:
            for (size_t i = w; i-- > 1;) {
                uint16_t a = static_cast<uint16_t>((static_cast<uint16_t>(row[2 * i]) << 8) | row[2 * i + 1]);
                uint16_t b = static_cast<uint16_t>((static_cast<uint16_t>(row[2 * i - 2]) << 8) | row[2 * i - 1]);
                uint16_t d = static_cast<uint16_t>(a - b);
                row[2 * i] = static_cast<uint8_t>(d >> 8);
                row[2 * i + 1] = static_cast<uint8_t>(d);
            }
            break;
        case 32: {
            scratch.assign(rb, 0);
            for (size_t i = 0; i < w; ++i) {
                for (size_t k = 0; k < 4; ++k) {
                    scratch[k * w + i] = row[4 * i + k];
                }
            }
            for (size_t i = rb; i-- > 1;) {
                scratch[i] = static_cast<uint8_t>(scratch[i] - scratch[i - 1]);
            }
            std::copy(scratch.begin(), scratch.end(), row.begin());
            break;
        }
        default:
            return PsdError::unsupported("prediction for depth " + std::to_string(layout.depth));
        }
    }
    return {};
}

Result<void> unpredict(std::span<uint8_t> buf, const PlaneLayout& layout) {
    size_t rb = layout.rowBytes();
    if (rb == 0) return {};
    size_t w = layout.width;
    std::vector<uint8_t> scratch;
    size_t rowCount = buf.size() / rb;
    for (size_t ri = 0; ri < rowCount; ++ri) {
        auto row = buf.subspan(ri * rb, rb);
        switch (layout.depth) {
        case 1:
        case 8:
            for (size_t i = 1; i < row.size(); ++i) {
                row[i] = static_cast<uint8_t>(row[i] + row[i - 1]);
            }
            break;
        case 16:
            for (size_t i = 1; i < w; ++i) {
                uint16_t a = static_cast<uint16_t>((static_cast<uint16_t>(row[2 * i]) << 8) | row[2 * i + 1]);
                uint16_t b = static_cast<uint16_t>((static_cast<uint16_t>(row[2 * i - 2]) << 8) | row[2 * i - 1]);
                uint16_t d = static_cast<uint16_t>(a + b);
                row[2 * i] = static_cast<uint8_t>(d >> 8);
                row[2 * i + 1] = static_cast<uint8_t>(d);
            }
            break;
        case 32: {
            for (size_t i = 1; i < rb; ++i) {
                row[i] = static_cast<uint8_t>(row[i] + row[i - 1]);
            }
            scratch.assign(row.begin(), row.end());
            for (size_t i = 0; i < w; ++i) {
                for (size_t k = 0; k < 4; ++k) {
                    row[4 * i + k] = scratch[k * w + i];
                }
            }
            break;
        }
        default:
            return PsdError::unsupported("prediction for depth " + std::to_string(layout.depth));
        }
    }
    return {};
}

// --- RLE (PackBits over rows, with length tables) ----------------------------

namespace {

Result<std::vector<uint8_t>> decodeRle(std::span<const uint8_t> data, const PlaneLayout& layout, size_t total) {
    PSD_TRY_ASSIGN(auto rows, layout.rows());
    size_t rb = layout.rowBytes();
    Reader r(data);
    PSD_TRY(r.checkCount(rows, layout.countSize()));
    std::vector<size_t> counts;
    counts.reserve(rows);
    for (size_t i = 0; i < rows; ++i) {
        PSD_TRY_ASSIGN(auto c, readCount(r, layout.version));
        counts.push_back(c);
    }
    if (static_cast<uint64_t>(total) > saturatingMulU64(static_cast<uint64_t>(r.remaining()), 64)) {
        return PsdError::decompress("RLE data too short for declared size");
    }
    std::vector<uint8_t> out;
    PSD_TRY(tryReserveExact(out, total));
    for (size_t c : counts) {
        PSD_TRY_ASSIGN(auto row, r.bytes(c));
        PSD_TRY(packbits::decodeInto(row, rb, out));
    }
    return out;
}

struct RleChunk {
    std::vector<uint8_t> counts;
    std::vector<uint8_t> packed;
};

Result<RleChunk> packRleChunk(std::span<const uint8_t> decoded, size_t rb, size_t cs, size_t first, size_t count) {
    RleChunk chunk;
    chunk.counts.assign(count * cs, 0);
    for (size_t row = first; row < first + count; ++row) {
        size_t start = chunk.packed.size();
        packbits::encode(decoded.subspan(row * rb, rb), chunk.packed);
        size_t n = chunk.packed.size() - start;
        size_t idx = (row - first) * cs;
        if (cs == 2) {
            if (n > UINT16_MAX) return PsdError::limitExceeded("RLE row exceeds 65535 bytes (use PSB)");
            chunk.counts[idx] = static_cast<uint8_t>(n >> 8);
            chunk.counts[idx + 1] = static_cast<uint8_t>(n);
        } else {
            chunk.counts[idx] = static_cast<uint8_t>(n >> 24);
            chunk.counts[idx + 1] = static_cast<uint8_t>(n >> 16);
            chunk.counts[idx + 2] = static_cast<uint8_t>(n >> 8);
            chunk.counts[idx + 3] = static_cast<uint8_t>(n);
        }
    }
    return chunk;
}

Result<std::vector<uint8_t>> encodeRle(std::span<const uint8_t> decoded, const PlaneLayout& layout) {
    PSD_TRY_ASSIGN(auto rows, layout.rows());
    size_t rb = layout.rowBytes();
    size_t cs = layout.countSize();
    if (rows == 0) return std::vector<uint8_t>{};

    // Rows are independent, so they pack in parallel chunks and concatenate in order.
    unsigned hw = std::thread::hardware_concurrency();
    size_t threads = std::clamp<size_t>(hw == 0 ? 1 : static_cast<size_t>(hw), size_t{1}, size_t{32});
    size_t chunkRows = std::max<size_t>((rows + threads - 1) / threads, size_t{1});

    std::vector<RleChunk> chunkList(threads);
    std::vector<std::optional<PsdError>> errors(threads);
    if (threads == 1) {
        auto r = packRleChunk(decoded, rb, cs, 0, rows);
        if (!r) return std::move(r).error();
        chunkList[0] = std::move(r).value();
    } else {
        std::vector<std::thread> workers;
        workers.reserve(threads);
        for (size_t t = 0; t < threads; ++t) {
            size_t first = t * chunkRows;
            size_t count = first >= rows ? 0 : std::min(chunkRows, rows - first);
            workers.emplace_back([&decoded, rb, cs, first, count, &chunkList, &errors, t]() {
                auto r = packRleChunk(decoded, rb, cs, first, count);
                if (!r) errors[t] = std::move(r).error();
                else chunkList[t] = std::move(r).value();
            });
        }
        for (auto& w : workers) w.join();
        for (auto& e : errors) {
            if (e) return *e;
        }
    }
    size_t packedTotal = 0;
    for (auto& c : chunkList) packedTotal += c.packed.size();
    std::vector<uint8_t> out(rows * cs + packedTotal, 0);
    size_t countsAt = 0, packedAt = rows * cs;
    for (auto& c : chunkList) {
        std::copy(c.counts.begin(), c.counts.end(), out.begin() + static_cast<ptrdiff_t>(countsAt));
        countsAt += c.counts.size();
        std::copy(c.packed.begin(), c.packed.end(), out.begin() + static_cast<ptrdiff_t>(packedAt));
        packedAt += c.packed.size();
    }
    return out;
}

} // namespace

// --- decode/encode/validate planes -------------------------------------------

Result<std::vector<uint8_t>> decodePlanes(Compression compression, std::span<const uint8_t> data, const PlaneLayout& layout) {
    PSD_TRY_ASSIGN(auto total, layout.decodedLen());
    switch (compression.kind()) {
    case Compression::Kind::Raw: {
        if (data.size() < total) return PsdError::unexpectedEof(data.size(), total - data.size());
        std::vector<uint8_t> out;
        PSD_TRY(tryReserveExact(out, total));
        out.insert(out.end(), data.begin(), data.begin() + static_cast<ptrdiff_t>(total));
        return out;
    }
    case Compression::Kind::Rle:
        return decodeRle(data, layout, total);
    case Compression::Kind::Zip:
        return inflateChecked(data, static_cast<uint64_t>(total), true);
    case Compression::Kind::ZipPrediction: {
        PSD_TRY_ASSIGN(auto v, inflateChecked(data, static_cast<uint64_t>(total), true));
        PSD_TRY(unpredict(v, layout));
        return v;
    }
    case Compression::Kind::Unknown:
        return PsdError::unsupported("compression method " + std::to_string(compression.asU16()));
    }
    return PsdError::unsupported("compression method");
}

Result<std::vector<uint8_t>> encodePlanes(Compression compression, std::span<const uint8_t> decoded, const PlaneLayout& layout) {
    PSD_TRY_ASSIGN(auto total, layout.decodedLen());
    if (decoded.size() != total) {
        return PsdError::invalid("decoded buffer has " + std::to_string(decoded.size()) + " bytes, layout requires " + std::to_string(total));
    }
    switch (compression.kind()) {
    case Compression::Kind::Raw:
        return std::vector<uint8_t>(decoded.begin(), decoded.end());
    case Compression::Kind::Rle:
        return encodeRle(decoded, layout);
    case Compression::Kind::Zip:
        return zipCompress(decoded);
    case Compression::Kind::ZipPrediction: {
        std::vector<uint8_t> v(decoded.begin(), decoded.end());
        PSD_TRY(predict(v, layout));
        return zipCompress(v);
    }
    case Compression::Kind::Unknown:
        return PsdError::unsupported("compression method " + std::to_string(compression.asU16()));
    }
    return PsdError::unsupported("compression method");
}

Result<void> validatePlanes(Compression compression, std::span<const uint8_t> data, const PlaneLayout& layout) {
    PSD_TRY_ASSIGN(auto total, layout.totalBytes());
    switch (compression.kind()) {
    case Compression::Kind::Raw:
        if (static_cast<uint64_t>(data.size()) < total) {
            return PsdError::unexpectedEof(data.size(), static_cast<size_t>(std::min<uint64_t>(total - static_cast<uint64_t>(data.size()), SIZE_MAX)));
        }
        return {};
    case Compression::Kind::Rle: {
        PSD_TRY_ASSIGN(auto rows, layout.rows());
        Reader r(data);
        PSD_TRY(r.checkCount(rows, layout.countSize()));
        uint64_t sum = 0;
        for (size_t i = 0; i < rows; ++i) {
            PSD_TRY_ASSIGN(auto c, readCount(r, layout.version));
            sum += static_cast<uint64_t>(c);
        }
        if (sum > static_cast<uint64_t>(r.remaining())) {
            return PsdError::unexpectedEof(data.size(), static_cast<size_t>(std::min<uint64_t>(sum - static_cast<uint64_t>(r.remaining()), SIZE_MAX)));
        }
        return {};
    }
    case Compression::Kind::Zip:
    case Compression::Kind::ZipPrediction: {
        if (total > kMaxDecodedBytes) return {};
        auto r = inflateChecked(data, total, false);
        if (!r) return std::move(r).error();
        return {};
    }
    case Compression::Kind::Unknown:
        return {};
    }
    return {};
}

} // namespace psd
