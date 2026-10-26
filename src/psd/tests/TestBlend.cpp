// Unit tests for Blend.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdBlendTest
#include <boost/test/included/unit_test.hpp>

#include <set>

#include "psd/Blend.hpp"

using namespace psd;

BOOST_AUTO_TEST_SUITE(psd_blend)

BOOST_AUTO_TEST_CASE(all_keys_roundtrip_and_unique) {
    std::set<std::array<uint8_t, 4>> keys;
    for (const BlendMode& m : BlendMode::all()) {
        BOOST_TEST(BlendMode::fromKey(m.key()) == m);
        keys.insert(m.key());
    }
    BOOST_TEST(keys.size() == 28u);
}

BOOST_AUTO_TEST_CASE(unknown_preserved) {
    std::array<uint8_t, 4> zzzz = {'z', 'z', 'z', 'z'};
    BlendMode m = BlendMode::fromKey(zzzz);
    BOOST_TEST(m.kind() == BlendMode::Kind::Unknown);
    BOOST_TEST(m.key() == zzzz);
}

BOOST_AUTO_TEST_CASE(specific_keys) {
    std::array<uint8_t, 4> mul = {'m', 'u', 'l', ' '};
    BOOST_TEST(BlendMode::Multiply.key() == mul);
    std::array<uint8_t, 4> smud = {'s', 'm', 'u', 'd'};
    BOOST_TEST(BlendMode::fromKey(smud) == BlendMode::Exclusion);
    std::array<uint8_t, 4> idiv = {'i', 'd', 'i', 'v'};
    BOOST_TEST(BlendMode::fromKey(idiv) == BlendMode::ColorBurn);
    BOOST_TEST(BlendMode() == BlendMode::Normal);
}

BOOST_AUTO_TEST_SUITE_END()
