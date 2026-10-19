// Unit tests for Compression.hpp, including the parallel/serial RLE
// row-packing equivalence check. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdCompressionTest
#include <boost/test/included/unit_test.hpp>

#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <utility>

#include "psd/Compression.hpp"

using namespace psd;

namespace {
PlaneLayout layout(size_t planes, size_t width, size_t height, uint16_t depth, Version version) {
    PlaneLayout l;
    l.planes = planes;
    l.width = width;
    l.height = height;
    l.depth = depth;
    l.version = version;
    return l;
}

std::vector<uint8_t> pbRoundtrip(std::span<const uint8_t> src) {
    auto enc = packbits::encodeVec(src);
    auto dec = packbits::decode(enc, src.size());
    BOOST_REQUIRE(bool(dec));
    BOOST_TEST(dec.value() == std::vector<uint8_t>(src.begin(), src.end()), boost::test_tools::per_element());
    return enc;
}

std::vector<uint8_t> sampleData(const PlaneLayout& l) {
    auto n = l.decodedLen();
    BOOST_REQUIRE(bool(n));
    std::vector<uint8_t> v(n.value());
    for (size_t i = 0; i < v.size(); ++i) v[i] = static_cast<uint8_t>((i * 7 + i / 5) % 251);
    return v;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_compression)

BOOST_AUTO_TEST_CASE(checked_zip_validates_trailer_size_and_padding_budget) {
    PlaneLayout l = layout(1, 3, 1, 8, Version::Psd);
    std::vector<uint8_t> src = {1, 2, 3};
    for (Compression compression : {Compression::Zip, Compression::ZipPrediction}) {
        auto data = encodePlanes(compression, src, l);
        BOOST_REQUIRE(bool(data));
        for (size_t cut = 0; cut < data.value().size(); ++cut) {
            std::span<const uint8_t> partial(data.value().data(), cut);
            BOOST_TEST(!decodePlanes(compression, partial, l));
            BOOST_TEST(!validatePlanes(compression, partial, l));
        }
        auto corrupt = data.value();
        corrupt.back() ^= 1;
        BOOST_TEST(!decodePlanes(compression, corrupt, l));
        BOOST_TEST(!validatePlanes(compression, corrupt, l));
    }
    std::vector<uint8_t> padded = zipCompress(std::vector<uint8_t>{1, 2, 3, 0});
    auto decPadded = decodePlanes(Compression::Zip, padded, l);
    BOOST_REQUIRE(bool(decPadded));
    BOOST_TEST(decPadded.value() == (std::vector<uint8_t>{1, 2, 3}), boost::test_tools::per_element());
    std::vector<uint8_t> bomb(((size_t{1} << 20) + 4), 0);
    auto bombZip = zipCompress(bomb);
    BOOST_TEST(!decodePlanes(Compression::Zip, bombZip, l));
    BOOST_TEST(!validatePlanes(Compression::Zip, bombZip, l));
}

BOOST_AUTO_TEST_CASE(zip_absurd_declared_size_is_an_error) {
    std::vector<uint8_t> z = zipCompress(std::vector<uint8_t>(64, 7));
    for (size_t expected : {size_t{1} << 40, SIZE_MAX / 2, SIZE_MAX}) {
        BOOST_TEST(!zipDecompress(z, expected));
    }
    PlaneLayout l1 = layout(1, size_t{1} << 16, size_t{1} << 15, 8, Version::Psb);
    BOOST_TEST(!decodePlanes(Compression::Zip, z, l1));
    BOOST_TEST(!decodePlanes(Compression::ZipPrediction, z, l1));
    PlaneLayout l2 = layout(4, size_t{1} << 20, size_t{1} << 20, 32, Version::Psb);
    BOOST_TEST(!decodePlanes(Compression::Zip, z, l2));
    std::vector<uint8_t> garbage(16, 0xff);
    BOOST_TEST(!zipDecompress(garbage, size_t{1} << 30));
}

BOOST_AUTO_TEST_CASE(zip_decompress_exact_and_truncated) {
    std::vector<uint8_t> src(3000000);
    for (size_t i = 0; i < src.size(); ++i) src[i] = static_cast<uint8_t>(i % 251);
    auto z = zipCompress(src);
    auto full = zipDecompress(z, src.size());
    BOOST_REQUIRE(bool(full));
    BOOST_TEST(full.value() == src, boost::test_tools::per_element());
    auto partial = zipDecompress(z, 1000);
    BOOST_REQUIRE(bool(partial));
    BOOST_TEST(partial.value() == std::vector<uint8_t>(src.begin(), src.begin() + 1000), boost::test_tools::per_element());
    BOOST_TEST(!zipDecompress(z, src.size() + 1));
    auto empty = zipDecompress(z, 0);
    BOOST_REQUIRE(bool(empty));
    BOOST_TEST(empty.value().empty());
}

BOOST_AUTO_TEST_CASE(packbits_empty) { BOOST_TEST(pbRoundtrip(std::vector<uint8_t>{}).empty()); }

BOOST_AUTO_TEST_CASE(packbits_single) {
    std::vector<uint8_t> src = {7};
    BOOST_TEST(pbRoundtrip(src) == (std::vector<uint8_t>{0, 7}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(packbits_run_of_two) {
    std::vector<uint8_t> src = {5, 5};
    BOOST_TEST(pbRoundtrip(src) == (std::vector<uint8_t>{0xff, 5}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(packbits_run_128) {
    std::vector<uint8_t> src(128, 9);
    BOOST_TEST(pbRoundtrip(src) == (std::vector<uint8_t>{0x81, 9}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(packbits_run_129) {
    std::vector<uint8_t> src(129, 9);
    BOOST_TEST(pbRoundtrip(src) == (std::vector<uint8_t>{0x81, 9, 0, 9}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(packbits_run_130_and_256) {
    pbRoundtrip(std::vector<uint8_t>(130, 1));
    auto enc = pbRoundtrip(std::vector<uint8_t>(256, 1));
    BOOST_TEST(enc == (std::vector<uint8_t>{0x81, 1, 0x81, 1}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(packbits_literal_128_and_129) {
    std::vector<uint8_t> src128(128);
    for (size_t i = 0; i < 128; ++i) src128[i] = static_cast<uint8_t>(i);
    auto enc128 = pbRoundtrip(src128);
    BOOST_TEST(enc128[0] == 127);
    BOOST_TEST(enc128.size() == 129u);
    std::vector<uint8_t> src129(129);
    for (size_t i = 0; i < 129; ++i) src129[i] = static_cast<uint8_t>(i);
    auto enc129 = pbRoundtrip(src129);
    BOOST_TEST(enc129[0] == 127);
    BOOST_TEST(enc129[129] == 0);
    BOOST_TEST(enc129.size() == 131u);
}

BOOST_AUTO_TEST_CASE(packbits_literal_run_boundary) {
    pbRoundtrip(std::vector<uint8_t>{1, 2, 3, 3, 3, 3, 4, 5});
    pbRoundtrip(std::vector<uint8_t>{1, 1, 2, 2, 3, 3});
    pbRoundtrip(std::vector<uint8_t>{1, 2, 2, 3});
    pbRoundtrip(std::vector<uint8_t>{0, 0, 0, 1, 0, 0, 0});
    auto enc = pbRoundtrip(std::vector<uint8_t>{1, 2, 3, 3, 3});
    BOOST_TEST(enc == (std::vector<uint8_t>{1, 1, 2, 0xfe, 3}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(packbits_never_emits_noop) {
    for (size_t n = 0; n < 300; ++n) {
        std::vector<uint8_t> src(n);
        for (size_t i = 0; i < n; ++i) src[i] = static_cast<uint8_t>((i / 3) % 5);
        auto enc = pbRoundtrip(src);
        size_t i = 0;
        while (i < enc.size()) {
            int8_t h = static_cast<int8_t>(enc[i]);
            BOOST_TEST(h != -128);
            i += h >= 0 ? static_cast<size_t>(h) + 2 : 2;
        }
    }
}

BOOST_AUTO_TEST_CASE(packbits_decode_noop_skipped) {
    auto dec = packbits::decode(std::vector<uint8_t>{0x80, 0, 42}, 1);
    BOOST_REQUIRE(bool(dec));
    BOOST_TEST(dec.value() == (std::vector<uint8_t>{42}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(packbits_decode_errors) {
    BOOST_TEST(!packbits::decode(std::vector<uint8_t>{}, 1));
    BOOST_TEST(!packbits::decode(std::vector<uint8_t>{2, 1, 2}, 3));       // literal truncated
    BOOST_TEST(!packbits::decode(std::vector<uint8_t>{0xfe}, 3));         // run truncated
    BOOST_TEST(!packbits::decode(std::vector<uint8_t>{0xfd, 1}, 3));      // run overflows (4 > 3)
    BOOST_TEST(!packbits::decode(std::vector<uint8_t>{3, 1, 2, 3, 4}, 3)); // literal overflows
}

BOOST_AUTO_TEST_CASE(packbits_zero_expected_ignores_input) {
    auto dec = packbits::decode(std::vector<uint8_t>{0xff, 1}, 0);
    BOOST_REQUIRE(bool(dec));
    BOOST_TEST(dec.value().empty());
}

BOOST_AUTO_TEST_CASE(compression_values) {
    for (uint16_t v = 0; v < 6; ++v) {
        BOOST_TEST(Compression::fromU16(v).asU16() == v);
    }
    BOOST_TEST(Compression::fromU16(9).kind() == Compression::Kind::Unknown);
}

BOOST_AUTO_TEST_CASE(all_codecs_roundtrip_all_depths) {
    for (Version version : {Version::Psd, Version::Psb}) {
        for (uint16_t depth : {uint16_t{1}, uint16_t{8}, uint16_t{16}, uint16_t{32}}) {
            for (auto [w, h] : std::initializer_list<std::pair<size_t, size_t>>{{0, 0}, {1, 1}, {3, 2}, {17, 5}, {0, 3}, {4, 0}}) {
                for (size_t planes : {size_t{1}, size_t{3}}) {
                    PlaneLayout l = layout(planes, w, h, depth, version);
                    auto data = sampleData(l);
                    for (Compression c : Compression::all()) {
                        auto enc = encodePlanes(c, data, l);
                        BOOST_REQUIRE(bool(enc));
                        BOOST_REQUIRE(bool(validatePlanes(c, enc.value(), l)));
                        auto dec = decodePlanes(c, enc.value(), l);
                        BOOST_REQUIRE(bool(dec));
                        BOOST_TEST(dec.value() == data, boost::test_tools::per_element());
                    }
                }
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(rle_count_width_psd_vs_psb) {
    std::vector<uint8_t> data(4, 0);
    auto psd = encodePlanes(Compression::Rle, data, layout(1, 2, 2, 8, Version::Psd));
    auto psb = encodePlanes(Compression::Rle, data, layout(1, 2, 2, 8, Version::Psb));
    BOOST_REQUIRE(bool(psd));
    BOOST_REQUIRE(bool(psb));
    BOOST_TEST(psd.value() == (std::vector<uint8_t>{0, 2, 0, 2, 0xff, 0, 0xff, 0}), boost::test_tools::per_element());
    BOOST_TEST(psb.value() == (std::vector<uint8_t>{0, 0, 0, 2, 0, 0, 0, 2, 0xff, 0, 0xff, 0}), boost::test_tools::per_element());
    BOOST_TEST(!decodePlanes(Compression::Rle, psd.value(), layout(1, 2, 2, 8, Version::Psb)));
}

BOOST_AUTO_TEST_CASE(rle_odd_width) {
    PlaneLayout l = layout(1, 7, 3, 16, Version::Psd);
    auto data = sampleData(l);
    auto enc = encodePlanes(Compression::Rle, data, l);
    BOOST_REQUIRE(bool(enc));
    auto dec = decodePlanes(Compression::Rle, enc.value(), l);
    BOOST_REQUIRE(bool(dec));
    BOOST_TEST(dec.value() == data, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(rle_truncated_fails) {
    PlaneLayout l = layout(1, 10, 10, 8, Version::Psd);
    auto data = sampleData(l);
    auto enc = encodePlanes(Compression::Rle, data, l);
    BOOST_REQUIRE(bool(enc));
    for (size_t cut = 0; cut < enc.value().size(); ++cut) {
        std::span<const uint8_t> partial(enc.value().data(), cut);
        BOOST_TEST(!decodePlanes(Compression::Rle, partial, l));
        BOOST_TEST(!validatePlanes(Compression::Rle, partial, l));
    }
}

BOOST_AUTO_TEST_CASE(zip_truncated_fails) {
    PlaneLayout l = layout(1, 10, 10, 8, Version::Psd);
    auto data = sampleData(l);
    auto enc = encodePlanes(Compression::Zip, data, l);
    BOOST_REQUIRE(bool(enc));
    for (size_t cut = 0; cut + 4 < enc.value().size(); ++cut) {
        std::span<const uint8_t> partial(enc.value().data(), cut);
        BOOST_TEST(!decodePlanes(Compression::Zip, partial, l));
        BOOST_TEST(!validatePlanes(Compression::Zip, partial, l));
    }
}

BOOST_AUTO_TEST_CASE(zip_over_budget_is_not_decompressed_at_parse) {
    PlaneLayout l = layout(4, 300000, 300000, 8, Version::Psb);
    auto total = l.totalBytes();
    BOOST_REQUIRE(bool(total));
    BOOST_TEST(total.value() > kMaxDecodedBytes);
    std::vector<uint8_t> garbage = {0xde, 0xad, 0xbe, 0xef, 0x01, 0x02, 0x03};
    for (Compression c : {Compression::Zip, Compression::ZipPrediction}) {
        BOOST_REQUIRE(bool(validatePlanes(c, garbage, l)));
        auto dec = decodePlanes(c, garbage, l);
        BOOST_REQUIRE(!dec);
        BOOST_TEST(dec.error().kind() == PsdError::Kind::LimitExceeded);
    }
    PlaneLayout small = layout(1, 4, 4, 8, Version::Psd);
    BOOST_TEST(!validatePlanes(Compression::Zip, garbage, small));
}

BOOST_AUTO_TEST_CASE(raw_short_fails) {
    PlaneLayout l = layout(2, 3, 3, 8, Version::Psd);
    std::vector<uint8_t> b17(17, 0), b18(18, 0);
    BOOST_TEST(!decodePlanes(Compression::Raw, b17, l));
    BOOST_TEST(bool(decodePlanes(Compression::Raw, b18, l)));
}

BOOST_AUTO_TEST_CASE(unknown_compression_errors) {
    PlaneLayout l = layout(1, 1, 1, 8, Version::Psd);
    Compression unknown = Compression::fromU16(7);
    std::vector<uint8_t> b = {0};
    BOOST_TEST(!decodePlanes(unknown, b, l));
    BOOST_TEST(!encodePlanes(unknown, b, l));
}

BOOST_AUTO_TEST_CASE(encode_rejects_wrong_size) {
    PlaneLayout l = layout(1, 2, 2, 8, Version::Psd);
    std::vector<uint8_t> b(3, 0);
    BOOST_TEST(!encodePlanes(Compression::Raw, b, l));
}

BOOST_AUTO_TEST_CASE(decoded_len_limits) {
    PlaneLayout l = layout(56, 300000, 300000, 32, Version::Psb);
    BOOST_TEST(!l.decodedLen());
    std::vector<uint8_t> b(16, 0);
    BOOST_TEST(!decodePlanes(Compression::Rle, b, l));
}

#if INTPTR_MAX == INT64_MAX
BOOST_AUTO_TEST_CASE(decoded_len_allows_large_psb_planes) {
    BOOST_TEST(layout(3, 30000, 30000, 8, Version::Psb).decodedLen().value() == 2700000000u);
    BOOST_TEST(layout(1, 33000, 33000, 16, Version::Psb).decodedLen().value() == 2178000000u);
    BOOST_TEST(!layout(4, 40000, 40000, 16, Version::Psb).decodedLen());
}
#endif

BOOST_AUTO_TEST_CASE(prediction_8bit_known_vector) {
    PlaneLayout l = layout(1, 4, 1, 8, Version::Psd);
    std::vector<uint8_t> v = {10, 12, 11, 255};
    BOOST_REQUIRE(bool(predict(v, l)));
    BOOST_TEST(v == (std::vector<uint8_t>{10, 2, 255, 244}), boost::test_tools::per_element());
    BOOST_REQUIRE(bool(unpredict(v, l)));
    BOOST_TEST(v == (std::vector<uint8_t>{10, 12, 11, 255}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(prediction_16bit_known_vector) {
    PlaneLayout l = layout(1, 3, 1, 16, Version::Psd);
    std::vector<uint8_t> v = {0x01, 0x00, 0x01, 0x05, 0x00, 0x00};
    BOOST_REQUIRE(bool(predict(v, l)));
    BOOST_TEST(v == (std::vector<uint8_t>{0x01, 0x00, 0x00, 0x05, 0xfe, 0xfb}), boost::test_tools::per_element());
    BOOST_REQUIRE(bool(unpredict(v, l)));
    BOOST_TEST(v == (std::vector<uint8_t>{0x01, 0x00, 0x01, 0x05, 0x00, 0x00}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(prediction_32bit_plane_shuffle) {
    PlaneLayout l = layout(1, 2, 1, 32, Version::Psd);
    std::vector<uint8_t> orig = {0xAA, 0xBB, 0xCC, 0xDD, 0x11, 0x22, 0x33, 0x44};
    std::vector<uint8_t> v = orig;
    BOOST_REQUIRE(bool(predict(v, l)));
    std::vector<uint8_t> expect = {0xAA, 0x11, 0xBB, 0x22, 0xCC, 0x33, 0xDD, 0x44};
    for (size_t i = 8; i-- > 1;) {
        expect[i] = static_cast<uint8_t>(expect[i] - expect[i - 1]);
    }
    BOOST_TEST(v == expect, boost::test_tools::per_element());
    BOOST_REQUIRE(bool(unpredict(v, l)));
    BOOST_TEST(v == orig, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(prediction_32bit_floats_roundtrip) {
    PlaneLayout l = layout(1, 5, 2, 32, Version::Psd);
    std::vector<uint8_t> v;
    for (int i = 0; i < 10; ++i) {
        float f = static_cast<float>(i) * 0.37f - 1.0f;
        uint32_t bits;
        std::memcpy(&bits, &f, 4);
        v.push_back(static_cast<uint8_t>(bits >> 24));
        v.push_back(static_cast<uint8_t>(bits >> 16));
        v.push_back(static_cast<uint8_t>(bits >> 8));
        v.push_back(static_cast<uint8_t>(bits));
    }
    auto enc = encodePlanes(Compression::ZipPrediction, v, l);
    BOOST_REQUIRE(bool(enc));
    auto dec = decodePlanes(Compression::ZipPrediction, enc.value(), l);
    BOOST_REQUIRE(bool(dec));
    BOOST_TEST(dec.value() == v, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(zip_bomb_is_bounded) {
    auto big = zipCompress(std::vector<uint8_t>(size_t{1} << 20, 0));
    auto out = zipDecompress(big, 10);
    BOOST_REQUIRE(bool(out));
    BOOST_TEST(out.value().size() == 10u);
}

BOOST_AUTO_TEST_CASE(zip_garbage_errors) { BOOST_TEST(!zipDecompress(std::vector<uint8_t>{1, 2, 3, 4}, 4)); }

BOOST_AUTO_TEST_SUITE_END()

// --- mod parallel_rle_equivalence -------------------------------------------

namespace {
// The pre-parallel encoder, kept as the oracle (mirrors compression.rs's own
// test-local encode_rle_sequential).
Result<std::vector<uint8_t>> encodeRleSequential(std::span<const uint8_t> decoded, const PlaneLayout& l) {
    PSD_TRY_ASSIGN(auto rows, l.rows());
    size_t rb = l.rowBytes();
    size_t cs = l.countSize();
    std::vector<uint8_t> out(rows * cs, 0);
    std::vector<uint8_t> enc;
    for (size_t row = 0; row < rows; ++row) {
        size_t start = enc.size();
        packbits::encode(decoded.subspan(row * rb, rb), enc);
        size_t n = enc.size() - start;
        if (cs == 2) {
            out[row * 2] = static_cast<uint8_t>(n >> 8);
            out[row * 2 + 1] = static_cast<uint8_t>(n);
        } else {
            out[row * 4] = static_cast<uint8_t>(n >> 24);
            out[row * 4 + 1] = static_cast<uint8_t>(n >> 16);
            out[row * 4 + 2] = static_cast<uint8_t>(n >> 8);
            out[row * 4 + 3] = static_cast<uint8_t>(n);
        }
    }
    out.insert(out.end(), enc.begin(), enc.end());
    return out;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_parallel_rle_equivalence)

BOOST_AUTO_TEST_CASE(parallel_rle_matches_sequential) {
    uint64_t state = 0x9e3779b97f4a7c15ull;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    struct Case {
        size_t rows;
        size_t rb;
        Version version;
    };
    std::vector<Case> cases = {
        {0, 4, Version::Psd},   {1, 4, Version::Psd},  {7, 4, Version::Psb},    {64, 4, Version::Psd},
        {65, 4, Version::Psb}, // chunk boundary
        {513, 4, Version::Psd}, // more rows than threads on most runners
        {2049, 4, Version::Psb}, {2, 4, Version::Psd}, // fewer rows than threads
        {33, 3000, Version::Psd}, {31, 20000, Version::Psb},
    };
    for (auto& c : cases) {
        std::vector<uint8_t> pattern;
        switch (c.rows % 3) {
        case 0: pattern.assign(c.rows * c.rb, 7); break; // long runs
        case 1:
            pattern.resize(c.rows * c.rb);
            for (size_t i = 0; i < pattern.size(); ++i) pattern[i] = static_cast<uint8_t>(i * 31 % 251);
            break;
        default: pattern.assign(c.rows * c.rb, 0); break;
        }
        std::vector<uint8_t> data = pattern;
        if (c.rows % 4 == 0) {
            for (size_t start = 0; start < data.size(); start += 97) data[start] = 200;
        }
        (void)next();
        PlaneLayout l = layout(1, c.rb, c.rows, 8, c.version);
        auto parallel = encodePlanes(Compression::Rle, data, l);
        auto sequential = encodeRleSequential(data, l);
        BOOST_REQUIRE(bool(parallel));
        BOOST_REQUIRE(bool(sequential));
        BOOST_TEST(parallel.value() == sequential.value(), boost::test_tools::per_element());
    }
}

BOOST_AUTO_TEST_SUITE_END()
