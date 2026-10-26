// Unit tests for FilterEffects.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdFilterEffectsTest
#include <boost/test/included/unit_test.hpp>

#include "psd/FilterEffects.hpp"

using namespace psd;

namespace {
EffectsPlane plane(uint8_t v) {
    std::vector<uint8_t> src(16, v);
    auto p = EffectsPlane::encode(Compression::Rle, src, 4, 4, 8);
    BOOST_REQUIRE(bool(p));
    return p.value();
}

FilterEffectsItem item(const std::string& id, bool mask) {
    Rect rect{1, 2, 5, 6};
    std::vector<std::optional<EffectsPlane>> slots(26, std::nullopt);
    slots[0] = plane(10);
    slots[25] = plane(255);
    FilterEffectsItem it;
    it.id = id;
    it.version = 1;
    it.rect = rect;
    it.depth = 8;
    it.maxChannels = 24;
    it.slots = std::move(slots);
    if (mask) it.mask = std::make_pair(rect, plane(128));
    return it;
}

// The unpadded body of `it`'s section.
std::vector<uint8_t> section(const FilterEffectsItem& it) {
    FilterEffects fx{3, {it}};
    auto b = fx.toBytes();
    uint64_t len = 0;
    for (int i = 0; i < 8; ++i) len = (len << 8) | b[4 + i];
    return std::vector<uint8_t>(b.begin() + 12, b.begin() + 12 + static_cast<ptrdiff_t>(len));
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_filter_effects)

BOOST_AUTO_TEST_CASE(round_trips) {
    FilterEffects fx{3, {item("a-1", true), item("b-2", false)}};
    auto bytes = fx.toBytes();
    auto back = FilterEffects::parse(bytes);
    BOOST_REQUIRE(bool(back));
    BOOST_TEST(back.value() == fx);
    auto decodedMask = back.value().items[0].decodedMask();
    BOOST_REQUIRE(decodedMask.has_value());
    BOOST_TEST(decodedMask->second == (std::vector<uint8_t>(16, 128)), boost::test_tools::per_element());
    BOOST_REQUIRE(back.value().items[0].slots[0].has_value());
    auto decodedSlot = back.value().items[0].slots[0]->decode(4, 4, 8);
    BOOST_REQUIRE(bool(decodedSlot));
    BOOST_TEST(decodedSlot.value() == (std::vector<uint8_t>(16, 10)), boost::test_tools::per_element());

    FilterEffects empty{3, {}};
    auto parsedEmpty = FilterEffects::parse(empty.toBytes());
    BOOST_REQUIRE(bool(parsedEmpty));
    BOOST_TEST(parsedEmpty.value() == empty);

    auto padded = bytes;
    padded.push_back(0);
    padded.push_back(0);
    BOOST_TEST(bool(FilterEffects::parse(padded)));
    padded.push_back(7);
    BOOST_TEST(!FilterEffects::parse(padded));
}

BOOST_AUTO_TEST_CASE(reads_one_section_per_item) {
    auto a = item("a-1", true);
    auto b = item("b-2", false);
    auto sa = section(a);
    auto sb = section(b);
    BOOST_TEST(sa.size() % 4 != 0);
    std::vector<uint8_t> bytes;
    putU32(bytes, 3);
    for (auto* s : {&sa, &sb}) {
        putU64(bytes, static_cast<uint64_t>(s->size()));
        bytes.insert(bytes.end(), s->begin(), s->end());
        bytes.resize((bytes.size() + 3) / 4 * 4, 0);
    }
    auto fx = FilterEffects::parse(bytes);
    BOOST_REQUIRE(bool(fx));
    BOOST_TEST(fx.value().items == (std::vector<FilterEffectsItem>{a, b}), boost::test_tools::per_element());
    BOOST_TEST(fx.value().toBytes() == bytes, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(reads_several_items_in_one_section) {
    std::vector<FilterEffectsItem> items = {item("a-1", true), item("b-2", false)};
    std::vector<uint8_t> body;
    for (auto& it : items) {
        auto s = section(it);
        body.insert(body.end(), s.begin(), s.end());
    }
    std::vector<uint8_t> bytes;
    putU32(bytes, 3);
    putU64(bytes, static_cast<uint64_t>(body.size()));
    bytes.insert(bytes.end(), body.begin(), body.end());
    auto fx = FilterEffects::parse(bytes);
    BOOST_REQUIRE(bool(fx));
    BOOST_TEST(fx.value().items == items, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(mutations_never_panic) {
    FilterEffects fx{3, {item("x", true), item("y", false)}};
    auto bytes = fx.toBytes();
    uint64_t state = 0x9E3779B97F4A7C15ull;
    auto next = [&state]() {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        return state;
    };
    for (int i = 0; i < 2000; ++i) {
        auto b = bytes;
        uint64_t mutations = 1 + next() % 4;
        for (uint64_t m = 0; m < mutations; ++m) {
            size_t idx = static_cast<size_t>(next()) % b.size();
            b[idx] = static_cast<uint8_t>(next());
        }
        auto parsed = FilterEffects::parse(b);
        if (parsed) {
            for (auto& it : parsed.value().items) {
                (void)it.decodedMask();
                for (auto& p : it.slots) {
                    if (p.has_value()) (void)p->decode(4, 4, 8);
                }
            }
            (void)parsed.value().toBytes();
        }
    }
}

BOOST_AUTO_TEST_CASE(malformed_data_is_an_error) {
    FilterEffects fx{3, {item("x", true)}};
    auto bytes = fx.toBytes();
    size_t sectionLen = 12 + section(item("x", true)).size();
    for (size_t n = 0; n < sectionLen; ++n) {
        std::span<const uint8_t> partial(bytes.data(), n);
        BOOST_TEST(!FilterEffects::parse(partial));
    }
    auto bad = bytes;
    bad[3] = 9; // version
    BOOST_TEST(!FilterEffects::parse(bad));

    auto huge = bytes;
    size_t at = 4 + 8 + 2 + 4 + 8 + 16 + 4; // max channels
    huge[at] = 0xff;
    huge[at + 1] = 0xff;
    huge[at + 2] = 0xff;
    huge[at + 3] = 0xff;
    BOOST_TEST(!FilterEffects::parse(huge));
}

BOOST_AUTO_TEST_SUITE_END()
