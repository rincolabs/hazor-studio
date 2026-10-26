// Builder and pixel extraction tests for Builder.hpp. See PROVENANCE.md for
// origin details.
#define BOOST_TEST_MODULE PsdBuilderTest
#include <boost/test/included/unit_test.hpp>

#include <cmath>
#include <sstream>

#include "psd/Builder.hpp"
#include "psd/Pixels.hpp"
#include "psd/Tree.hpp"

using namespace psd;

namespace {
std::vector<uint8_t> rgbaPattern(size_t w, size_t h, uint8_t seed) {
    std::vector<uint8_t> v;
    v.reserve(w * h * 4);
    for (size_t y = 0; y < h; ++y) {
        for (size_t x = 0; x < w; ++x) {
            v.push_back(static_cast<uint8_t>(x * 10) ^ seed);
            v.push_back(static_cast<uint8_t>(y * 20));
            v.push_back(static_cast<uint8_t>(x + y) ^ seed);
            v.push_back((x + y) % 3 == 0 ? 0 : static_cast<uint8_t>(200 + (x % 50)));
        }
    }
    return v;
}

PsdFile built(const PsdBuilder& b) {
    auto bytes = b.toBytes();
    BOOST_REQUIRE(bool(bytes));
    auto f = PsdFile::fromBytes(bytes.value());
    BOOST_REQUIRE(bool(f));
    auto back = f.value().toBytes();
    BOOST_REQUIRE(bool(back));
    BOOST_TEST(back.value() == bytes.value(), boost::test_tools::per_element());
    auto built2 = b.build();
    BOOST_REQUIRE(bool(built2));
    BOOST_TEST(f.value() == built2.value());
    return f.value();
}

void rgba8Case(Version ver, Compression comp) {
    auto px = rgbaPattern(7, 5, 3);
    PsdBuilder b(10, 8);
    b.version(ver).compression(comp);
    b.pushLayer(LayerSpec("L", -2, 1, 7, 5, PixelData::rgba8(px)));
    auto f = built(b);
    auto layer = f.layer(0);
    BOOST_REQUIRE(layer.has_value());
    auto img = layer->rgba8();
    BOOST_REQUIRE(bool(img));
    BOOST_TEST(img.value().left == -2);
    BOOST_TEST(img.value().top == 1);
    BOOST_TEST(img.value().width == 7u);
    BOOST_TEST(img.value().height == 5u);
    BOOST_TEST(img.value().data == px, boost::test_tools::per_element());
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_builder)

BOOST_AUTO_TEST_CASE(rgba8_psd_raw) { rgba8Case(Version::Psd, Compression::Raw); }
BOOST_AUTO_TEST_CASE(rgba8_psd_rle) { rgba8Case(Version::Psd, Compression::Rle); }
BOOST_AUTO_TEST_CASE(rgba8_psd_zip) { rgba8Case(Version::Psd, Compression::Zip); }
BOOST_AUTO_TEST_CASE(rgba8_psd_zipp) { rgba8Case(Version::Psd, Compression::ZipPrediction); }
BOOST_AUTO_TEST_CASE(rgba8_psb_raw) { rgba8Case(Version::Psb, Compression::Raw); }
BOOST_AUTO_TEST_CASE(rgba8_psb_rle) { rgba8Case(Version::Psb, Compression::Rle); }
BOOST_AUTO_TEST_CASE(rgba8_psb_zip) { rgba8Case(Version::Psb, Compression::Zip); }
BOOST_AUTO_TEST_CASE(rgba8_psb_zipp) { rgba8Case(Version::Psb, Compression::ZipPrediction); }

BOOST_AUTO_TEST_CASE(rgba16_layers_scale_to_8bit) {
    size_t n = 4 * 3;
    std::vector<uint16_t> px(n * 4);
    for (size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint16_t>(i * 5003);
    for (Compression comp : Compression::all()) {
        PsdBuilder b(4, 3);
        b.depth(16).compression(comp);
        b.pushLayer(LayerSpec("x", 0, 0, 4, 3, PixelData::rgba16(px)));
        auto f = built(b);
        BOOST_TEST(f.layerInfoPlacement.kind() == LayerInfoPlacement::Kind::GlobalBlock);
        BOOST_TEST(f.layerInfoPlacement.globalBlockInfo().key == (std::array<uint8_t, 4>{'L', 'r', '1', '6'}));
        auto l = f.layer(0);
        BOOST_REQUIRE(l.has_value());
        auto img = l->rgba8();
        BOOST_REQUIRE(bool(img));
        std::vector<uint8_t> expect(px.size());
        for (size_t i = 0; i < px.size(); ++i) expect[i] = static_cast<uint8_t>((uint32_t(px[i]) * 255 + 32767) / 65535);
        BOOST_TEST(img.value().data == expect, boost::test_tools::per_element());
        auto redBytes = l->channelBytes(0);
        BOOST_REQUIRE(bool(redBytes));
        auto red = samplesU16(redBytes.value());
        std::vector<uint16_t> expectedRed;
        for (size_t i = 0; i < px.size(); i += 4) expectedRed.push_back(px[i]);
        BOOST_TEST(red == expectedRed, boost::test_tools::per_element());
    }
}

BOOST_AUTO_TEST_CASE(grayscale_builder) {
    std::vector<uint8_t> px;
    for (uint8_t i = 0; i < 6; ++i) {
        px.push_back(static_cast<uint8_t>(i * 40));
        px.push_back(static_cast<uint8_t>(255 - i));
    }
    PsdBuilder b(3, 2);
    b.colorMode(ColorMode::Grayscale);
    b.pushLayer(LayerSpec("g", 0, 0, 3, 2, PixelData::grayA8(px)));
    b.composite(PixelData::grayA8(px));
    auto f = built(b);
    bool modeEq = (f.header.colorMode == ColorMode::Grayscale);
    BOOST_TEST(modeEq);
    auto layer = f.layer(0);
    BOOST_REQUIRE(layer.has_value());
    auto img = layer->rgba8();
    BOOST_REQUIRE(bool(img));
    for (size_t i = 0; i < 6; ++i) {
        std::vector<uint8_t> got(img.value().data.begin() + static_cast<ptrdiff_t>(i * 4), img.value().data.begin() + static_cast<ptrdiff_t>(i * 4 + 4));
        std::vector<uint8_t> want = {px[i * 2], px[i * 2], px[i * 2], px[i * 2 + 1]};
        BOOST_TEST(got == want, boost::test_tools::per_element());
    }
    auto c = f.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    BOOST_TEST(c.value().data == img.value().data, boost::test_tools::per_element());
    BOOST_TEST(f.header.channels == 2);
}

BOOST_AUTO_TEST_CASE(grayscale16_builder) {
    std::vector<uint16_t> px = {0, 65535, 32768, 65535};
    PsdBuilder b(2, 1);
    b.colorMode(ColorMode::Grayscale).depth(16);
    b.pushLayer(LayerSpec("g", 0, 0, 2, 1, PixelData::grayA16(px)));
    auto f = built(b);
    auto layer = f.layer(0);
    BOOST_REQUIRE(layer.has_value());
    auto img = layer->rgba8();
    BOOST_REQUIRE(bool(img));
    BOOST_TEST(img.value().data == (std::vector<uint8_t>{0, 0, 0, 255, 128, 128, 128, 255}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(cmyk_builder_inverts_ink) {
    std::vector<uint8_t> px = {0, 0, 0, 0, 255, 0, 0, 0, 255, 255};
    PsdBuilder b(2, 1);
    b.colorMode(ColorMode::Cmyk);
    b.pushLayer(LayerSpec("c", 0, 0, 2, 1, PixelData::cmyka8(px)));
    b.composite(PixelData::cmyka8(px));
    auto f = built(b);
    BOOST_TEST(f.header.channels == 4);
    auto l = f.layer(0);
    BOOST_REQUIRE(l.has_value());
    auto ch0 = l->channelBytes(0);
    BOOST_REQUIRE(bool(ch0));
    BOOST_TEST(ch0.value() == (std::vector<uint8_t>{255, 255}), boost::test_tools::per_element());
    auto ch3 = l->channelBytes(3);
    BOOST_REQUIRE(bool(ch3));
    BOOST_TEST(ch3.value() == (std::vector<uint8_t>{255, 0}), boost::test_tools::per_element());
    auto img = l->rgba8();
    BOOST_REQUIRE(bool(img));
    BOOST_TEST(img.value().data == (std::vector<uint8_t>{255, 255, 255, 255, 0, 0, 0, 255}), boost::test_tools::per_element());
    auto c = f.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    BOOST_TEST(c.value().data == (std::vector<uint8_t>{255, 255, 255, 255, 0, 0, 0, 255}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(cmyk16_builder) {
    std::vector<uint16_t> px = {0, 0, 0, 0, 65535};
    PsdBuilder b(1, 1);
    b.colorMode(ColorMode::Cmyk).depth(16);
    b.pushLayer(LayerSpec("c", 0, 0, 1, 1, PixelData::cmyka16(px)));
    auto f = built(b);
    auto l = f.layer(0);
    BOOST_REQUIRE(l.has_value());
    auto img = l->rgba8();
    BOOST_REQUIRE(bool(img));
    BOOST_TEST(img.value().data == (std::vector<uint8_t>{255, 255, 255, 255}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(layer_properties_roundtrip) {
    PsdBuilder b(4, 4);
    LayerSpec spec("Props \xf0\x9f\x8e\xa8", 1, 1, 2, 2, PixelData::rgba8(std::vector<uint8_t>(16, 9)));
    spec.blendMode = BlendMode::SoftLight;
    spec.opacity = 128;
    spec.fillOpacity = 64;
    spec.visible = false;
    spec.clipping = true;
    spec.extraBlocks.push_back(TaggedBlock::sheetColor(4));
    b.pushLayer(spec);
    auto f = built(b);
    auto& r = f.layers()[0];
    BOOST_TEST(r.resolvedName() == "Props \xf0\x9f\x8e\xa8");
    BOOST_TEST(r.name == (std::vector<uint8_t>{'P', 'r', 'o', 'p', 's', ' ', '?'}), boost::test_tools::per_element());
    bool blendEq = (r.blendMode == BlendMode::SoftLight);
    BOOST_TEST(blendEq);
    BOOST_TEST(r.opacity == 128);
    BOOST_TEST(r.fillOpacity() == 64);
    BOOST_TEST(!r.isVisible());
    BOOST_TEST(r.clipping == 1);
    bool idEq = (r.layerId() == std::optional<uint32_t>(1));
    BOOST_TEST(idEq);
    const TaggedBlock* blk = r.block({'l', 'c', 'l', 'r'});
    BOOST_REQUIRE(blk != nullptr);
    auto parsed = blk->parsed();
    BOOST_REQUIRE(parsed.has_value());
    BOOST_REQUIRE(bool(*parsed));
    BOOST_TEST(parsed->value().asSheetColor() == 4u);
}

BOOST_AUTO_TEST_CASE(all_blend_modes_via_builder) {
    PsdBuilder b(1, 1);
    for (const BlendMode& m : BlendMode::all()) {
        std::ostringstream name;
        name << m;
        LayerSpec s(name.str(), 0, 0, 1, 1, PixelData::rgba8(std::vector<uint8_t>{1, 2, 3, 4}));
        s.blendMode = m;
        b.pushLayer(s);
    }
    auto f = built(b);
    BOOST_TEST(f.layers().size() == BlendMode::all().size());
    for (size_t i = 0; i < f.layers().size(); ++i) {
        bool eq = (f.layers()[i].blendMode == BlendMode::all()[i]);
        BOOST_TEST(eq);
    }
}

BOOST_AUTO_TEST_CASE(masks_roundtrip) {
    std::vector<uint8_t> maskPx(12);
    for (uint8_t i = 0; i < 12; ++i) maskPx[i] = static_cast<uint8_t>(i * 20);
    LayerSpec s0("m", 0, 0, 3, 3, PixelData::rgba8(std::vector<uint8_t>(36, 100)));
    MaskSpec ms;
    ms.rect = Rect::fromXywh(-1, 0, 4, 3);
    ms.data = maskPx;
    ms.defaultColor = 255;
    ms.disabled = true;
    s0.mask = ms;
    for (uint16_t depth : {uint16_t{8}, uint16_t{16}}) {
        PsdBuilder b(3, 3);
        b.depth(depth);
        LayerSpec s = s0;
        if (depth == 16) s.pixels = PixelData::rgba16(std::vector<uint16_t>(36, 25700));
        b.pushLayer(s);
        auto f = built(b);
        auto l = f.layer(0);
        BOOST_REQUIRE(l.has_value());
        auto maskResult = l->userMask();
        BOOST_REQUIRE(maskResult.has_value());
        BOOST_REQUIRE(bool(*maskResult));
        auto& m = maskResult->value();
        BOOST_TEST(m.left == -1);
        BOOST_TEST(m.top == 0);
        BOOST_TEST(m.width == 4u);
        BOOST_TEST(m.height == 3u);
        BOOST_TEST(m.data == maskPx, boost::test_tools::per_element());
        BOOST_TEST(m.defaultValue == 255);
        const LayerMask* lm = l->record().layerMask();
        BOOST_REQUIRE(lm != nullptr);
        BOOST_TEST(lm->disabled());
        auto img = l->rgba8();
        BOOST_REQUIRE(bool(img));
        bool allMatch = true;
        for (size_t i = 0; i + 3 < img.value().data.size(); i += 4) {
            if (img.value().data[i] != 100 || img.value().data[i + 1] != 100 || img.value().data[i + 2] != 100 || img.value().data[i + 3] != 100)
                allMatch = false;
        }
        BOOST_TEST(allMatch);
    }
}

BOOST_AUTO_TEST_CASE(no_mask_returns_none) {
    PsdBuilder b(1, 1);
    b.pushLayer(LayerSpec("x", 0, 0, 1, 1, PixelData::rgba8(std::vector<uint8_t>(4, 0))));
    auto f = built(b);
    auto l = f.layer(0);
    BOOST_REQUIRE(l.has_value());
    BOOST_TEST(!l->userMask().has_value());
}

BOOST_AUTO_TEST_CASE(empty_layer) {
    PsdBuilder b(2, 2);
    b.pushLayer(LayerSpec("empty", 0, 0, 0, 0, PixelData::rgba8({})));
    auto f = built(b);
    auto l = f.layer(0);
    BOOST_REQUIRE(l.has_value());
    auto img = l->rgba8();
    BOOST_REQUIRE(bool(img));
    BOOST_TEST(img.value().width == 0u);
    BOOST_TEST(img.value().height == 0u);
    BOOST_TEST(img.value().data.empty());
}

BOOST_AUTO_TEST_CASE(composite_supplied_with_alpha) {
    std::vector<uint8_t> px = {255, 0, 0, 128, 17, 89, 203, 0};
    PsdBuilder b(2, 1);
    b.pushLayer(LayerSpec("x", 0, 0, 2, 1, PixelData::rgba8(px)));
    b.composite(PixelData::rgba8(px));
    auto f = built(b);
    BOOST_TEST(f.header.channels == 4);
    BOOST_REQUIRE(f.layerInfo.has_value());
    BOOST_TEST(f.layerInfo->mergedAlpha);
    BOOST_TEST(f.mergedHasAlpha());
    bool realEq = (f.hasRealMergedData() == std::optional<bool>(true));
    BOOST_TEST(realEq);
    auto decoded = f.decodeMerged();
    BOOST_REQUIRE(bool(decoded));
    BOOST_TEST(decoded.value() == (std::vector<uint8_t>{255, 17, 127, 89, 127, 203, 128, 0}), boost::test_tools::per_element());
    auto c = f.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    BOOST_TEST(c.value().data == px, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(composite_opaque_drops_alpha) {
    std::vector<uint8_t> px;
    for (uint8_t i = 0; i < 6; ++i) {
        px.push_back(i);
        px.push_back(i);
        px.push_back(i);
        px.push_back(255);
    }
    PsdBuilder b(3, 2);
    b.composite(PixelData::rgba8(px));
    auto f = built(b);
    BOOST_TEST(f.header.channels == 3);
    BOOST_TEST(!f.layerInfo.has_value());
    auto c = f.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    BOOST_TEST(c.value().data == px, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(composite_16bit) {
    std::vector<uint16_t> px = {65535, 0, 0, 65535, 0, 65535, 0, 32768};
    PsdBuilder b(2, 1);
    b.depth(16);
    b.composite(PixelData::rgba16(px));
    auto f = built(b);
    auto c = f.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    BOOST_TEST(c.value().data == (std::vector<uint8_t>{255, 0, 0, 255, 0, 255, 0, 128}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(placeholder_composite_is_flagged) {
    PsdBuilder b(2, 2);
    b.pushLayer(LayerSpec("x", 0, 0, 1, 1, PixelData::rgba8(std::vector<uint8_t>(4, 0))));
    auto f = built(b);
    bool realEq = (f.hasRealMergedData() == std::optional<bool>(false));
    BOOST_TEST(realEq);
    auto c = f.compositeRgba8();
    BOOST_REQUIRE(bool(c));
    bool allWhite = true;
    for (uint8_t v : c.value().data)
        if (v != 255) allWhite = false;
    BOOST_TEST(allWhite);
}

BOOST_AUTO_TEST_CASE(builder_resources) {
    PsdBuilder b(1, 1);
    b.resolution(300.0).iccProfile({1, 2, 3});
    auto f = built(b);
    auto res = f.resolution();
    BOOST_REQUIRE(res.has_value());
    BOOST_TEST(std::abs(res->vRes() - 300.0) < 1e-9);
    auto icc = f.iccProfile();
    BOOST_REQUIRE(icc.has_value());
    BOOST_TEST(std::vector<uint8_t>(icc->begin(), icc->end()) == (std::vector<uint8_t>{1, 2, 3}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(builder_errors) {
    {
        PsdBuilder b(2, 2);
        b.pushLayer(LayerSpec("bad", 0, 0, 2, 2, PixelData::rgba8(std::vector<uint8_t>(3, 0))));
        BOOST_TEST(!b.build());
    }
    {
        PsdBuilder b(2, 2);
        b.pushLayer(LayerSpec("wrong fmt", 0, 0, 1, 1, PixelData::grayA8(std::vector<uint8_t>(2, 0))));
        BOOST_TEST(!b.build());
    }
    {
        PsdBuilder b(2, 2);
        b.beginGroup(GroupSpec("open"));
        BOOST_TEST(!b.build());
    }
    {
        PsdBuilder b(2, 2);
        BOOST_TEST(!b.endGroup());
    }
    {
        PsdBuilder b(1, 1);
        b.depth(32);
        BOOST_TEST(!b.build());
    }
    {
        PsdBuilder b(1, 1);
        b.colorMode(ColorMode::Lab);
        BOOST_TEST(!b.build());
    }
    {
        PsdBuilder b(2, 2);
        b.composite(PixelData::rgba8(std::vector<uint8_t>(4, 0)));
        BOOST_TEST(!b.build());
    }
    {
        PsdBuilder b(2, 2);
        LayerSpec s("m", 0, 0, 1, 1, PixelData::rgba8(std::vector<uint8_t>(4, 0)));
        MaskSpec ms;
        ms.rect = Rect::fromXywh(0, 0, 2, 2);
        ms.data = std::vector<uint8_t>(3, 0);
        ms.defaultColor = 0;
        ms.disabled = false;
        s.mask = ms;
        b.pushLayer(s);
        BOOST_TEST(!b.build());
    }
}

BOOST_AUTO_TEST_CASE(group_properties) {
    PsdBuilder b(1, 1);
    GroupSpec g("G");
    g.blendMode = BlendMode::Multiply;
    g.opacity = 77;
    g.visible = false;
    b.beginGroup(g);
    b.pushLayer(LayerSpec("x", 0, 0, 1, 1, PixelData::rgba8(std::vector<uint8_t>(4, 0))));
    BOOST_REQUIRE(bool(b.endGroup()));
    auto f = built(b);
    auto& grp = f.layers()[2];
    BOOST_TEST(grp.resolvedName() == "G");
    BOOST_TEST(grp.opacity == 77);
    BOOST_TEST(!grp.isVisible());
    auto sd = grp.sectionDivider();
    BOOST_REQUIRE(sd.has_value());
    bool kindEq = (sd->kind == SectionType::OpenFolder);
    BOOST_TEST(kindEq);
    bool bmEq = (sd->blendMode == std::optional<BlendMode>(BlendMode::Multiply));
    BOOST_TEST(bmEq);
    bool stEq = (f.layers()[0].sectionType() == SectionType::BoundingDivider);
    BOOST_TEST(stEq);
}

BOOST_AUTO_TEST_CASE(psb_builder_large_canvas_header) {
    PsdBuilder b(40000, 1);
    b.version(Version::Psb).compression(Compression::Rle);
    auto f = built(b);
    BOOST_TEST(f.header.width == 40000u);
    BOOST_TEST(bool(f.validate()));
}

BOOST_AUTO_TEST_CASE(iter_layers_indices) {
    PsdBuilder b(1, 1);
    for (int i = 0; i < 3; ++i) {
        b.pushLayer(LayerSpec("l" + std::to_string(i), 0, 0, 1, 1, PixelData::rgba8(std::vector<uint8_t>(4, 0))));
    }
    auto f = built(b);
    auto views = f.iterLayers();
    BOOST_REQUIRE(views.size() == 3u);
    for (size_t i = 0; i < 3; ++i) {
        BOOST_TEST(views[i].index() == i);
        BOOST_TEST(views[i].name() == "l" + std::to_string(i));
    }
    BOOST_TEST(!f.layer(3).has_value());
    auto l0 = f.layer(0);
    BOOST_REQUIRE(l0.has_value());
    bool verEq = (l0->version() == Version::Psd);
    BOOST_TEST(verEq);
}

BOOST_AUTO_TEST_SUITE_END()
