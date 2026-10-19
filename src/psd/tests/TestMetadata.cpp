// Unit tests for Metadata.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdMetadataTest
#include <boost/test/included/unit_test.hpp>

#include "psd/Metadata.hpp"

using namespace psd;

BOOST_AUTO_TEST_SUITE(psd_metadata)

BOOST_AUTO_TEST_CASE(roundtrip_exact) {
    auto cust = MetadataItem::make({'c', 'u', 's', 't'}, std::vector<uint8_t>(4, 9));
    cust.copy = true;
    std::vector<MetadataItem> items = {MetadataItem::make({'c', 'm', 'l', 's'}, {1, 2, 3}), cust};
    BOOST_TEST(items[0].data.size() == 4u);
    auto bytes = writeShmd(items);
    auto parsed = parseShmd(bytes);
    BOOST_REQUIRE(bool(parsed));
    BOOST_TEST(parsed.value() == items, boost::test_tools::per_element());
    BOOST_TEST(writeShmd(parsed.value()) == bytes, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(malformed_is_an_error) {
    BOOST_TEST(!parseShmd({}));
    std::vector<uint8_t> bad = {0, 0, 0, 9};
    BOOST_TEST(!parseShmd(bad));
    auto b = writeShmd({MetadataItem::make({'c', 'm', 'l', 's'}, std::vector<uint8_t>(2, 0))});
    b.push_back(0);
    BOOST_TEST(!parseShmd(b));
    b.resize(b.size() - 3);
    BOOST_TEST(!parseShmd(b));
}

BOOST_AUTO_TEST_SUITE_END()
