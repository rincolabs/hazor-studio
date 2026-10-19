// Unit tests for Header.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdHeaderTest
#include <boost/test/included/unit_test.hpp>

#include "psd/Header.hpp"

using namespace psd;

namespace {
std::vector<uint8_t> bytesOf(const Header& h) {
    std::vector<uint8_t> v;
    h.write(v);
    return v;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_header)

BOOST_AUTO_TEST_CASE(header_roundtrip) {
    Header h(Version::Psb, 1000, 20, 4, 16, ColorMode::Cmyk);
    auto b = bytesOf(h);
    BOOST_TEST(b.size() == 26u);
    Reader r(b);
    auto parsed = Header::read(r);
    BOOST_REQUIRE(bool(parsed));
    BOOST_TEST(parsed.value() == h);
}

BOOST_AUTO_TEST_CASE(bad_signature) {
    auto b = bytesOf(Header(Version::Psd, 1, 1, 1, 8, ColorMode::Grayscale));
    b[0] = 'X';
    Reader r(b);
    auto parsed = Header::read(r);
    BOOST_REQUIRE(!parsed);
    BOOST_TEST(parsed.error().kind() == PsdError::Kind::InvalidSignature);
}

BOOST_AUTO_TEST_CASE(bad_version) {
    auto b = bytesOf(Header(Version::Psd, 1, 1, 1, 8, ColorMode::Grayscale));
    b[5] = 3;
    Reader r(b);
    auto parsed = Header::read(r);
    BOOST_REQUIRE(!parsed);
    BOOST_TEST(parsed.error() == PsdError::unsupportedVersion(3));
}

BOOST_AUTO_TEST_CASE(limits) {
    std::vector<Header> bad = {
        Header(Version::Psd, 300001, 1, 1, 8, ColorMode::Rgb),
        Header(Version::Psd, 0, 1, 1, 8, ColorMode::Rgb),
        Header(Version::Psd, 1, 0, 1, 8, ColorMode::Rgb),
        Header(Version::Psb, 0, 0, 1, 8, ColorMode::Rgb),
        Header(Version::Psd, 1, 1, 0, 8, ColorMode::Rgb),
        Header(Version::Psd, 1, 1, 57, 8, ColorMode::Rgb),
        Header(Version::Psd, 1, 1, 3, 12, ColorMode::Rgb),
    };
    for (auto& h : bad) {
        auto b = bytesOf(h);
        Reader r(b);
        BOOST_TEST(!Header::read(r));
    }
}

BOOST_AUTO_TEST_CASE(color_modes_roundtrip) {
    for (uint16_t v = 0; v < 20; ++v) {
        BOOST_TEST(ColorMode::fromU16(v).asU16() == v);
    }
    BOOST_TEST(ColorMode::fromU16(5).kind() == ColorMode::Kind::Unknown);
    BOOST_TEST(ColorMode::Rgb.colorChannels().value() == 3);
}

BOOST_AUTO_TEST_CASE(row_bytes_values) {
    BOOST_TEST(rowBytes(9, 1) == 2u);
    BOOST_TEST(rowBytes(8, 1) == 1u);
    BOOST_TEST(rowBytes(0, 1) == 0u);
    BOOST_TEST(rowBytes(3, 8) == 3u);
    BOOST_TEST(rowBytes(3, 16) == 6u);
    BOOST_TEST(rowBytes(3, 32) == 12u);
}

BOOST_AUTO_TEST_SUITE_END()
