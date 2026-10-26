// Unit tests for Tagged.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdTaggedTest
#include <boost/test/included/unit_test.hpp>

#include "psd/Tagged.hpp"

using namespace psd;

namespace {
std::vector<uint8_t> rt(const std::vector<TaggedBlock>& blocks, Version version) {
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(writeBlocks(out, blocks, version, 2)));
    Reader r(out);
    auto result = readBlocks(r, version);
    BOOST_REQUIRE(bool(result));
    BOOST_TEST(result.value().first == blocks, boost::test_tools::per_element());
    BOOST_TEST(result.value().second.empty());
    return out;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_tagged)

BOOST_AUTO_TEST_CASE(unicode_name_typed) {
    auto b = TaggedBlock::unicodeName("Ebene \xc3\xa4\xf0\x9f\x98\x80"); // "Ebene \u{e4}\u{1F600}"
    BOOST_TEST(b.data.size() % 4 == 0);
    auto parsed = b.parsed();
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE(bool(*parsed));
    BOOST_TEST(parsed->value().asUnicodeName() == "Ebene \xc3\xa4\xf0\x9f\x98\x80");
    rt({b}, Version::Psd);
}

BOOST_AUTO_TEST_CASE(section_divider_variants) {
    struct Case {
        SectionType kind;
        std::optional<BlendMode> bm;
        std::optional<uint32_t> st;
        size_t len;
    };
    std::vector<Case> cases = {
        {SectionType::OpenFolder, std::nullopt, std::nullopt, 4},
        {SectionType::ClosedFolder, BlendMode::PassThrough, std::nullopt, 12},
        {SectionType::BoundingDivider, BlendMode::Multiply, uint32_t{1}, 16},
    };
    for (auto& c : cases) {
        auto b = TaggedBlock::sectionDivider(c.kind, c.bm, c.st);
        BOOST_TEST(b.data.size() == c.len);
        auto parsed = b.parsed();
        BOOST_REQUIRE(parsed.has_value());
        BOOST_REQUIRE(bool(*parsed));
        auto& s = parsed->value().asSectionDivider();
        BOOST_TEST(s.kind == c.kind);
        BOOST_TEST(s.blendMode == c.bm);
        bool subTypeEq = (s.subType == c.st);
        BOOST_TEST(subTypeEq);
    }
    auto b = TaggedBlock::sectionDivider(SectionType::OpenFolder, BlendMode::Normal, std::nullopt);
    b.data[4] = 'X';
    auto badParsed = b.parsed();
    BOOST_REQUIRE(badParsed.has_value());
    BOOST_TEST(!*badParsed);
    auto lsdk = TaggedBlock::sectionDivider(SectionType::OpenFolder, std::nullopt, std::nullopt);
    TaggedBlock lsdk2 = lsdk;
    lsdk2.key = {'l', 's', 'd', 'k'};
    auto lsdkParsed = lsdk2.parsed();
    BOOST_REQUIRE(lsdkParsed.has_value());
    BOOST_TEST(bool(*lsdkParsed));
}

BOOST_AUTO_TEST_CASE(section_type_values) {
    for (uint32_t v = 0; v < 6; ++v) {
        BOOST_TEST(SectionType::fromU32(v).asU32() == v);
    }
    BOOST_TEST(SectionType::OpenFolder.isFolder());
    BOOST_TEST(!SectionType::BoundingDivider.isFolder());
}

BOOST_AUTO_TEST_CASE(simple_typed_blocks) {
    auto getOk = [](const TaggedBlock& b) {
        auto p = b.parsed();
        BOOST_REQUIRE(p.has_value());
        BOOST_REQUIRE(bool(*p));
        return p->value();
    };
    BOOST_TEST(getOk(TaggedBlock::layerId(7)).asLayerId() == 7u);
    std::array<uint8_t, 4> cont = {'c', 'o', 'n', 't'};
    BOOST_TEST(getOk(TaggedBlock::nameSource(cont)).asNameSource() == cont);
    BOOST_TEST(getOk(TaggedBlock::blendClippedAsGroup(true)).asBlendClippedAsGroup());
    BOOST_TEST(!getOk(TaggedBlock::blendInteriorElements(false)).asBlendInteriorElements());
    BOOST_TEST(getOk(TaggedBlock::knockout(2)).asKnockout() == 2);
    BOOST_TEST(!getOk(TaggedBlock::transparencyShapes(false)).asTransparencyShapes());
    BOOST_TEST(getOk(TaggedBlock::layerMaskHidesEffects(true)).asLayerMaskHidesEffects());
    BOOST_TEST(getOk(TaggedBlock::vectorMaskHidesEffects(true)).asVectorMaskHidesEffects());
    BOOST_TEST(TaggedBlock::vectorMaskHidesEffects(true).data == (std::vector<uint8_t>{1, 0, 0, 0}), boost::test_tools::per_element());
    auto truncated = TaggedBlock::make({'t', 's', 'l', 'y'}, {});
    auto truncatedParsed = truncated.parsed();
    BOOST_REQUIRE(truncatedParsed.has_value());
    BOOST_TEST(!*truncatedParsed, "truncated flag is an error, not a panic");
    BOOST_TEST(getOk(TaggedBlock::protection(0x80000000)).asProtection() == 0x80000000u);
    BOOST_TEST(getOk(TaggedBlock::sheetColor(3)).asSheetColor() == 3u);
    BOOST_TEST(getOk(TaggedBlock::fillOpacity(128)).asFillOpacity() == 128);
    auto shmd = TaggedBlock::make({'s', 'h', 'm', 'd'}, {0, 0, 0, 0});
    BOOST_TEST(getOk(shmd).asMetadataSetting() == (std::vector<uint8_t>{0, 0, 0, 0}), boost::test_tools::per_element());
    BOOST_TEST(!TaggedBlock::make({'l', 'f', 'x', '2'}, {}).parsed().has_value());
    auto lyidBad = TaggedBlock::make({'l', 'y', 'i', 'd'}, {1});
    auto lyidBadParsed = lyidBad.parsed();
    BOOST_REQUIRE(lyidBadParsed.has_value());
    BOOST_TEST(!*lyidBadParsed);
}

BOOST_AUTO_TEST_CASE(default_padding_odd_data) {
    auto b = TaggedBlock::make({'a', 'b', 'c', 'd'}, {1, 2, 3});
    auto out = rt({b, b}, Version::Psd);
    BOOST_TEST(out.size() == 2 * (12 + 4));
}

BOOST_AUTO_TEST_CASE(explicit_padding_preserved) {
    auto a = TaggedBlock::make({'a', 'b', 'c', 'd'}, {1, 2, 3});
    a.padding = std::vector<uint8_t>{0, 0, 0}; // pad-to-4 in a file that aligned to 4
    auto b = TaggedBlock::make({'e', 'f', 'g', 'h'}, {1, 2});
    b.padding = std::vector<uint8_t>{0, 0};
    auto c = TaggedBlock::make({'i', 'j', 'k', 'l'}, {1});
    c.padding = std::vector<uint8_t>{}; // no padding at all
    rt({a, b, c}, Version::Psd);
}

BOOST_AUTO_TEST_CASE(signature_8b64_preserved) {
    auto b = TaggedBlock::make({'L', 'r', '1', '6'}, std::vector<uint8_t>(6, 0));
    b.signature = {'8', 'B', '6', '4'};
    rt({b}, Version::Psb);
}

BOOST_AUTO_TEST_CASE(psb_long_keys) {
    for (auto key : kPsbLongKeys) {
        BOOST_TEST(usesLongLength(Version::Psb, key));
        BOOST_TEST(!usesLongLength(Version::Psd, key));
        auto b = TaggedBlock::make(key, std::vector<uint8_t>(4, 7));
        auto out = rt({b}, Version::Psb);
        BOOST_TEST(out.size() == 8u + 8u + 4u);
        BOOST_TEST(b.encodedLen(Version::Psb) == out.size());
        auto out2 = rt({b}, Version::Psd);
        BOOST_TEST(out2.size() == 8u + 4u + 4u);
    }
    std::array<uint8_t, 4> luni = {'l', 'u', 'n', 'i'};
    BOOST_TEST(!usesLongLength(Version::Psb, luni));
}

BOOST_AUTO_TEST_CASE(trailing_bytes_kept) {
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(writeBlocks(out, {TaggedBlock::layerId(1)}, Version::Psd, 2)));
    out.insert(out.end(), 8, 0);
    Reader r(out);
    auto result = readBlocks(r, Version::Psd);
    BOOST_REQUIRE(bool(result));
    BOOST_TEST(result.value().first.size() == 1u);
    BOOST_TEST(result.value().second == (std::vector<uint8_t>(8, 0)), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(short_trailing_pad_attributed_to_block) {
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(writeBlocks(out, {TaggedBlock::layerId(1)}, Version::Psd, 2)));
    out.insert(out.end(), 2, 0);
    Reader r(out);
    auto result = readBlocks(r, Version::Psd);
    BOOST_REQUIRE(bool(result));
    BOOST_REQUIRE(result.value().first[0].padding.has_value());
    BOOST_TEST(*result.value().first[0].padding == (std::vector<uint8_t>{0, 0}), boost::test_tools::per_element());
    BOOST_TEST(result.value().second.empty());
}

BOOST_AUTO_TEST_CASE(overlong_length_errors) {
    std::vector<uint8_t> out;
    put(out, std::vector<uint8_t>{'8', 'B', 'I', 'M', 'a', 'b', 'c', 'd'});
    putU32(out, 100);
    out.insert(out.end(), 4, 0);
    Reader r(out);
    BOOST_TEST(!readBlocks(r, Version::Psd));
}

BOOST_AUTO_TEST_SUITE_END()
