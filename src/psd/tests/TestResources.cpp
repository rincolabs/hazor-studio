// Unit tests for Resources.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdResourcesTest
#include <boost/test/included/unit_test.hpp>

#include "psd/Resources.hpp"

using namespace psd;

namespace {
std::vector<uint8_t> roundtrip(const std::vector<ImageResource>& res) {
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(writeResourcesSection(out, res)));
    Reader r(out);
    auto back = readResourcesSection(r);
    BOOST_REQUIRE(bool(back));
    BOOST_TEST(back.value() == res, boost::test_tools::per_element());
    return out;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_resources)

BOOST_AUTO_TEST_CASE(empty_section) { BOOST_TEST(roundtrip({}) == (std::vector<uint8_t>{0, 0, 0, 0}), boost::test_tools::per_element()); }

BOOST_AUTO_TEST_CASE(odd_data_is_padded) {
    auto out = roundtrip({ImageResource::make(1000, {1, 2, 3})});
    // len(4) + sig(4) + id(2) + name(2) + size(4) + data(3) + pad(1)
    BOOST_TEST(out.size() == 4u + 4u + 2u + 2u + 4u + 4u);
    BOOST_TEST(out.back() == 0);
}

BOOST_AUTO_TEST_CASE(named_resources) {
    auto r = ImageResource::make(2000, std::vector<uint8_t>(4, 9));
    r.name = {'p', 'a', 't', 'h'};
    auto r2 = ImageResource::make(2001, {});
    r2.name = {'a', 'b', 'c'};
    r2.signature = {'M', 'e', 'S', 'a'};
    roundtrip({r, r2});
}

BOOST_AUTO_TEST_CASE(bad_signature) {
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(writeResourcesSection(out, {ImageResource::make(1, {})})));
    out[4] = 'X';
    Reader r(out);
    BOOST_TEST(!readResourcesSection(r));
}

BOOST_AUTO_TEST_CASE(truncation_errors) {
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(writeResourcesSection(out, {ImageResource::make(ids::kResolutionInfo, ResolutionInfo::fromDpi(72.0).toBytes())})));
    for (size_t cut = 0; cut < out.size(); ++cut) {
        std::span<const uint8_t> partial(out.data(), cut);
        Reader r(partial);
        BOOST_TEST(!readResourcesSection(r));
    }
}

BOOST_AUTO_TEST_CASE(typed_resolution) {
    auto ri = ResolutionInfo::fromDpi(300.0);
    auto r = ImageResource::make(ids::kResolutionInfo, ri.toBytes());
    auto parsed = r.parsed();
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE(bool(*parsed));
    BOOST_REQUIRE(parsed->value().kind() == ResourceData::Kind::ResolutionInfo);
    auto& p = parsed->value().asResolutionInfo();
    BOOST_TEST(p == ri);
    BOOST_TEST(std::abs(p.hRes() - 300.0) < 1e-9);
    BOOST_TEST(std::abs(p.vRes() - 300.0) < 1e-9);
}

BOOST_AUTO_TEST_CASE(typed_misc) {
    std::vector<uint8_t> angleBytes = {0, 0, 0, 120};
    auto r1 = ImageResource::make(ids::kGlobalAngle, angleBytes);
    auto p1 = r1.parsed();
    BOOST_REQUIRE(p1.has_value() && bool(*p1));
    BOOST_TEST(p1->value() == ResourceData::globalAngle(120));

    std::vector<uint8_t> altBytes = {0, 0, 0, 30};
    auto r2 = ImageResource::make(ids::kGlobalAltitude, altBytes);
    auto p2 = r2.parsed();
    BOOST_REQUIRE(p2.has_value() && bool(*p2));
    BOOST_TEST(p2->value() == ResourceData::globalAltitude(30));

    auto r3 = ImageResource::make(ids::kLayerState, std::vector<uint8_t>{0, 2});
    auto p3 = r3.parsed();
    BOOST_REQUIRE(p3.has_value() && bool(*p3));
    BOOST_TEST(p3->value() == ResourceData::layerState(2));

    auto r4 = ImageResource::make(ids::kLayerGroupInfo, std::vector<uint8_t>{0, 1, 0, 2});
    auto p4 = r4.parsed();
    BOOST_REQUIRE(p4.has_value() && bool(*p4));
    BOOST_TEST(p4->value() == ResourceData::layerGroupInfo({1, 2}));

    auto r5 = ImageResource::make(ids::kLayerGroupInfo, std::vector<uint8_t>{0});
    auto p5 = r5.parsed();
    BOOST_REQUIRE(p5.has_value());
    BOOST_TEST(!*p5);

    std::vector<uint8_t> one = {1};
    auto r6 = ImageResource::make(ids::kIccProfile, one);
    auto p6 = r6.parsed();
    BOOST_REQUIRE(p6.has_value() && bool(*p6));
    BOOST_TEST(p6->value() == ResourceData::iccProfile(one));

    std::vector<uint8_t> two = {2};
    auto r7 = ImageResource::make(ids::kExif, two);
    auto p7 = r7.parsed();
    BOOST_REQUIRE(p7.has_value() && bool(*p7));
    BOOST_TEST(p7->value() == ResourceData::exif(two));

    std::vector<uint8_t> three = {3};
    auto r8 = ImageResource::make(ids::kThumbnail, three);
    auto p8 = r8.parsed();
    BOOST_REQUIRE(p8.has_value() && bool(*p8));
    BOOST_TEST(p8->value() == ResourceData::thumbnail(three));

    auto r9 = ImageResource::make(ids::kXmp, std::vector<uint8_t>{'<', 'x', '/', '>'});
    bool xmpEq = (r9.xmpStr() == std::optional<std::string>("<x/>"));
    BOOST_TEST(xmpEq);

    BOOST_TEST(!ImageResource::make(4242, {}).parsed().has_value());
}

BOOST_AUTO_TEST_CASE(version_info_roundtrip) {
    auto r = versionInfoResource(false);
    auto parsed = r.parsed();
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE(bool(*parsed));
    BOOST_REQUIRE(parsed->value().kind() == ResourceData::Kind::VersionInfo);
    auto& v = parsed->value().asVersionInfo();
    BOOST_TEST(!v.hasRealMergedData);
    BOOST_TEST(v.toBytes() == r.data, boost::test_tools::per_element());
}

// Byte for byte what Photoshop 2026 writes: u32 count + UTF-16BE, no NUL terminator.
BOOST_AUTO_TEST_CASE(version_info_matches_photoshop_bytes) {
    std::vector<uint8_t> want = {0, 0, 0, 1, 1};
    for (const std::string& s : {std::string("Adobe Photoshop"), std::string("Adobe Photoshop 2026")}) {
        putU32(want, static_cast<uint32_t>(s.size()));
        for (char c : s) {
            want.push_back(0);
            want.push_back(static_cast<uint8_t>(c));
        }
    }
    want.insert(want.end(), {0, 0, 0, 1});
    BOOST_TEST(versionInfoResource(true).data == want, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_SUITE_END()
