// Unit tests for Pixels.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdPixelsTest
#include <boost/test/included/unit_test.hpp>

#include <algorithm>
#include <array>
#include <limits>

#include "Testgen.hpp"
#include "psd/Builder.hpp"
#include "psd/Pixels.hpp"

using namespace psd;

BOOST_AUTO_TEST_SUITE(psd_pixels)

BOOST_AUTO_TEST_CASE(sample_conversions) {
    std::vector<uint8_t> b = {0x12, 0x34, 0xff, 0xff};
    BOOST_TEST(samplesU16(b) == (std::vector<uint16_t>{0x1234, 0xffff}), boost::test_tools::per_element());
    std::vector<uint16_t> s = {0x1234};
    BOOST_TEST(u16ToBytes(s) == (std::vector<uint8_t>{0x12, 0x34}), boost::test_tools::per_element());
    std::vector<float> f = {0.5f, -1.0f};
    auto bytes = f32ToBytes(f);
    BOOST_TEST(samplesF32(bytes) == f, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(unpack_bits_rows) {
    std::vector<uint8_t> b = {0b10100000, 0b10000000};
    auto r = unpackBits(b, 3, 2);
    BOOST_REQUIRE(bool(r));
    BOOST_TEST(r.value() == (std::vector<uint8_t>{1, 0, 1, 1, 0, 0}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(plane_to_u8_depths) {
    std::vector<uint8_t> b16 = {0xff, 0xff, 0, 0, 0x80, 0x00};
    auto r16 = planeToU8(b16, 16, 3, 1);
    BOOST_REQUIRE(bool(r16));
    BOOST_TEST(r16.value() == (std::vector<uint8_t>{255, 0, 128}), boost::test_tools::per_element());

    std::vector<float> fv = {0.0f, 1.0f, 2.0f, -1.0f, std::numeric_limits<float>::quiet_NaN()};
    auto f32bytes = f32ToBytes(fv);
    auto r32 = planeToU8(f32bytes, 32, 5, 1);
    BOOST_REQUIRE(bool(r32));
    BOOST_TEST(r32.value() == (std::vector<uint8_t>{0, 255, 255, 0, 0}), boost::test_tools::per_element());

    std::vector<uint8_t> b1 = {0b10000000};
    auto r1 = planeToU8(b1, 1, 2, 1);
    BOOST_REQUIRE(bool(r1));
    BOOST_TEST(r1.value() == (std::vector<uint8_t>{0, 255}), boost::test_tools::per_element());

    BOOST_TEST(!planeToU8({}, 12, 0, 0));
}

// A plane size that overflows or cannot be allocated is an error, not a panic or abort.
BOOST_AUTO_TEST_CASE(plane_conversion_reports_absurd_sizes) {
    std::vector<uint8_t> one = {0};
    auto r1 = unpackBits(one, SIZE_MAX / 2, 4);
    BOOST_REQUIRE(!r1);
    BOOST_TEST(r1.error().kind() == PsdError::Kind::LimitExceeded);
    auto r2 = planeToU8(one, 1, SIZE_MAX / 2, 4);
    BOOST_REQUIRE(!r2);
    BOOST_TEST(r2.error().kind() == PsdError::Kind::LimitExceeded);
    auto r3 = unpackBits(one, size_t{1} << 40, size_t{1} << 20);
    BOOST_REQUIRE(!r3);
    BOOST_TEST(r3.error().kind() == PsdError::Kind::LimitExceeded);
}

BOOST_AUTO_TEST_CASE(rgb_interleave_checks_planes_and_preserves_alpha) {
    std::vector<uint8_t> rCh = {1, 2}, gCh = {3, 4}, bCh = {5, 6}, aCh = {7, 8}, aShort = {7};
    std::vector<std::span<const uint8_t>> color = {rCh, gCh, bCh};
    auto withAlpha = detail::interleave(ColorMode::Rgb, color, std::span<const uint8_t>(aCh), {}, 2);
    BOOST_REQUIRE(bool(withAlpha));
    BOOST_TEST(withAlpha.value() == (std::vector<uint8_t>{1, 3, 5, 7, 2, 4, 6, 8}), boost::test_tools::per_element());

    auto noAlpha = detail::interleave(ColorMode::Rgb, color, std::nullopt, {}, 2);
    BOOST_REQUIRE(bool(noAlpha));
    BOOST_TEST(noAlpha.value() == (std::vector<uint8_t>{1, 3, 5, 255, 2, 4, 6, 255}), boost::test_tools::per_element());

    std::vector<std::span<const uint8_t>> colorShort = {rCh, gCh};
    BOOST_TEST(!detail::interleave(ColorMode::Rgb, colorShort, std::nullopt, {}, 2));
    BOOST_TEST(!detail::interleave(ColorMode::Rgb, color, std::span<const uint8_t>(aShort), {}, 2));
}

BOOST_AUTO_TEST_CASE(float_composite_removes_white_matte_without_changing_native_data) {
    auto file = testgen::mergedOnly(Version::Psd, ColorMode::Rgb, 32, Compression::Raw, 1, 1);
    file.header.channels = 4;
    std::vector<float> nativeSamples = {0.75f, 0.875f, 1.0f, 0.25f};
    auto native = f32ToBytes(nativeSamples);
    auto encoded = ImageData::encode(Compression::Raw, native, file.header);
    BOOST_REQUIRE(bool(encoded));
    file.imageData = encoded.value();
    auto c = file.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    BOOST_TEST(c.value().data == (std::vector<uint8_t>{0, 128, 255, 64}), boost::test_tools::per_element());
    auto decoded = file.decodeMerged();
    BOOST_REQUIRE(bool(decoded));
    BOOST_TEST(decoded.value() == native, boost::test_tools::per_element());
}

// #5694: the 8-bit RGB composite unmattes through a table; every (sample, alpha) pair comes out
// exactly as the per-pixel division it replaced (kept here as the reference).
BOOST_AUTO_TEST_CASE(rgba8_composite_unmattes_every_sample_and_alpha_as_the_division_did) {
    auto file = testgen::mergedOnly(Version::Psd, ColorMode::Rgb, 8, Compression::Raw, 256, 256);
    file.header.channels = 4;
    auto xOf = [](size_t i) { return static_cast<uint8_t>(i % 256); };
    auto yOf = [](size_t i) { return static_cast<uint8_t>(i / 256); };
    std::array<std::vector<uint8_t>, 4> planes;
    for (auto& p : planes) p.resize(256 * 256);
    for (size_t i = 0; i < 256 * 256; ++i) {
        planes[0][i] = xOf(i);
        planes[1][i] = static_cast<uint8_t>(255 - xOf(i));
        planes[2][i] = static_cast<uint8_t>(xOf(i) ^ yOf(i));
        planes[3][i] = yOf(i);
    }
    std::vector<uint8_t> concatenated;
    for (auto& p : planes) concatenated.insert(concatenated.end(), p.begin(), p.end());
    auto encoded = ImageData::encode(Compression::Raw, concatenated, file.header);
    BOOST_REQUIRE(bool(encoded));
    file.imageData = encoded.value();

    std::vector<uint8_t> expected(256 * 256 * 4);
    for (size_t i = 0; i < 256 * 256; ++i) {
        for (size_t p = 0; p < 4; ++p) expected[i * 4 + p] = planes[p][i];
    }
    for (size_t i = 0; i < expected.size(); i += 4) {
        uint32_t a = expected[i + 3];
        for (size_t c = 0; c < 3; ++c) {
            uint32_t cv = expected[i + c];
            if (a != 0) {
                uint32_t base = cv + a;
                uint32_t sub = base > 255 ? base - 255 : 0;
                uint32_t straight = (sub * 255 + a / 2) / a;
                expected[i + c] = static_cast<uint8_t>(std::min<uint32_t>(straight, 255));
            }
        }
    }
    auto c = file.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    BOOST_TEST(c.value().data == expected, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(oversized_layer_preview_rejects_missing_channels_before_allocating) {
    auto file = testgen::small(Version::Psb, Compression::Raw);
    LayerRecord rec;
    rec.rect = Rect::fromXywh(0, 0, 300000, 300000);
    file.layersMut().push_back(rec);
    auto views = file.iterLayers();
    BOOST_REQUIRE(!views.empty());
    auto img = views.back().rgba8();
    BOOST_REQUIRE(!img);
    BOOST_TEST(img.error().kind() == PsdError::Kind::LimitExceeded);
}

BOOST_AUTO_TEST_CASE(composite_rgba8_checks_the_output_budget_first) {
    auto file = testgen::small(Version::Psb, Compression::Zip);
    file.header.colorMode = ColorMode::Bitmap;
    file.header.depth = 1;
    file.header.channels = 1;
    file.header.width = 60000;
    file.header.height = 60000;
    auto c = file.compositeRgba8();
    BOOST_REQUIRE(!c);
    BOOST_TEST(c.error().kind() == PsdError::Kind::LimitExceeded);
    BOOST_TEST(c.error().text() == "RGBA output exceeds MAX_DECODED_BYTES");
}

BOOST_AUTO_TEST_SUITE_END()
