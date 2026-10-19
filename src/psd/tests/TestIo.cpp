// Unit tests for Io.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdIoTest
#include <boost/test/included/unit_test.hpp>

#include "psd/Io.hpp"

using namespace psd;

BOOST_AUTO_TEST_SUITE(psd_io)

BOOST_AUTO_TEST_CASE(reader_reads_big_endian) {
    std::vector<uint8_t> data = {0x00, 0x01, 0xff, 0xfe, 0x00, 0x00, 0x00, 0x02};
    Reader r(data);
    BOOST_TEST(r.u16().value() == 1);
    BOOST_TEST(r.i16().value() == -2);
    BOOST_TEST(r.u32().value() == 2u);
    BOOST_TEST(r.isEmpty());
    auto failed = r.u8();
    BOOST_TEST(!failed);
    BOOST_TEST(failed.error().kind() == PsdError::Kind::UnexpectedEof);
}

BOOST_AUTO_TEST_CASE(reader_eof_reports_needed) {
    std::vector<uint8_t> data = {1, 2};
    Reader r(data);
    auto result = r.u32();
    BOOST_REQUIRE(!result);
    BOOST_TEST(result.error().offset() == 0u);
    BOOST_TEST(result.error().needed() == 2u);
}

BOOST_AUTO_TEST_CASE(check_count_rejects_huge) {
    std::vector<uint8_t> data(8, 0);
    Reader r(data);
    BOOST_TEST(!r.checkCount(UINT64_MAX, 4));
    BOOST_TEST(bool(r.checkCount(2, 4)));
    BOOST_TEST(!r.checkCount(3, 4));
}

BOOST_AUTO_TEST_CASE(len_patch_roundtrip) {
    std::vector<uint8_t> v;
    size_t at = beginLen(v, false);
    put(v, std::vector<uint8_t>{1, 2, 3});
    BOOST_REQUIRE(bool(endLen(v, at, false)));
    BOOST_TEST(v == (std::vector<uint8_t>{0, 0, 0, 3, 1, 2, 3}), boost::test_tools::per_element());

    std::vector<uint8_t> v2;
    size_t at2 = beginLen(v2, true);
    put(v2, std::vector<uint8_t>{9});
    BOOST_REQUIRE(bool(endLen(v2, at2, true)));
    BOOST_TEST(v2 == (std::vector<uint8_t>{0, 0, 0, 0, 0, 0, 0, 1, 9}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(pascal_padding) {
    struct Case {
        std::vector<uint8_t> name;
        size_t align;
        size_t total;
    };
    std::vector<Case> cases = {
        {{}, 2, 2}, {{'a'}, 2, 2}, {{'a', 'b'}, 2, 4}, {{}, 4, 4}, {{'a', 'b', 'c'}, 4, 4}, {{'a', 'b', 'c', 'd'}, 4, 8},
    };
    for (auto& c : cases) {
        std::vector<uint8_t> v;
        writePascal(v, c.name, c.align);
        BOOST_TEST(v.size() == c.total);
        Reader r(v);
        auto got = readPascal(r, c.align);
        BOOST_REQUIRE(bool(got));
        BOOST_TEST(got.value() == c.name, boost::test_tools::per_element());
        BOOST_TEST(r.isEmpty());
    }
}

BOOST_AUTO_TEST_CASE(legacy_names) {
    BOOST_TEST(decodeLegacyName(std::vector<uint8_t>{'L', 'a', 'y', 'e', 'r', ' ', '1'}) == "Layer 1");
    BOOST_TEST(decodeLegacyName(std::vector<uint8_t>{0x41, 0xe9}) == "A\xc3\xa9"); // "A\u{e9}" in UTF-8
    auto enc = encodeLegacyName("h\xc3\xa9llo");                                  // "h\u{e9}llo" in UTF-8
    BOOST_TEST(enc == (std::vector<uint8_t>{'h', '?', 'l', 'l', 'o'}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(unicode_units_roundtrip) {
    std::vector<uint8_t> v;
    std::vector<uint16_t> units = {0x48, 0x69, 0};
    writeUnicodeUnits(v, units);
    Reader r(v);
    auto got = readUnicodeUnits(r);
    BOOST_REQUIRE(bool(got));
    BOOST_TEST(got.value() == units, boost::test_tools::per_element());

    std::vector<uint8_t> bad = {0xff, 0xff, 0xff, 0xff};
    Reader r2(bad);
    BOOST_TEST(!readUnicodeUnits(r2));
}

BOOST_AUTO_TEST_SUITE_END()
