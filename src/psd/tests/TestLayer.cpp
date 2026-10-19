// Unit tests for Layer.hpp. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdLayerTest
#include <boost/test/included/unit_test.hpp>

#include <cstring>

#include "Testgen.hpp"
#include "psd/Layer.hpp"

using namespace psd;

namespace {
LayerRecord sampleRecord(Version version) {
    Rect rect = Rect::fromXywh(-3, 2, 5, 4);
    std::vector<uint8_t> px(20);
    for (size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>(i);
    LayerRecord rec;
    rec.rect = rect;
    struct ChSpec {
        int16_t id;
        Compression comp;
    };
    for (auto& spec : std::vector<ChSpec>{{-1, Compression::Rle}, {0, Compression::Raw}, {1, Compression::Zip}, {2, Compression::ZipPrediction}}) {
        auto cd = ChannelData::encode(spec.id, spec.comp, px, 5, 4, 8, version);
        BOOST_REQUIRE(bool(cd));
        rec.channels.push_back(cd.value());
    }
    auto mask = LayerMask::make(Rect::fromXywh(0, 0, 2, 2), 255, LayerMask::kFlagRelative);
    auto maskCh = ChannelData::encode(-2, Compression::Rle, std::vector<uint8_t>{1, 2, 3, 4}, 2, 2, 8, version);
    BOOST_REQUIRE(bool(maskCh));
    rec.channels.push_back(maskCh.value());
    rec.blendMode = BlendMode::Screen;
    rec.opacity = 200;
    rec.clipping = 1;
    rec.flags = LayerFlags(LayerFlags::kHidden);
    rec.mask = MaskData::mask(mask);
    rec.blendingRanges = BlendingRanges::full(3);
    rec.name = {'a', 'b', 'c'};
    rec.blocks.push_back(TaggedBlock::unicodeName("\xc3\xa4" "bc")); // "\u{e4}bc"
    rec.blocks.push_back(TaggedBlock::layerId(5));
    return rec;
}

std::vector<uint8_t> rt(const LayerInfo& info, Version version) {
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(info.writeBody(out, version)));
    auto back = LayerInfo::readBody(out, version);
    BOOST_REQUIRE(bool(back));
    BOOST_TEST(back.value() == info);
    std::vector<uint8_t> again;
    BOOST_REQUIRE(bool(back.value().writeBody(again, version)));
    BOOST_TEST(again == out, boost::test_tools::per_element());
    return out;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_layer)

BOOST_AUTO_TEST_CASE(empty_layer_info) {
    auto out = rt(LayerInfo{}, Version::Psd);
    BOOST_TEST(out == (std::vector<uint8_t>{0, 0}), boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(record_roundtrip_psd_psb) {
    for (Version v : {Version::Psd, Version::Psb}) {
        LayerInfo info;
        info.mergedAlpha = true;
        info.layers = {sampleRecord(v), LayerRecord{}};
        info.padding = std::nullopt;
        auto out = rt(info, v);
        BOOST_TEST(out.size() % 2 == 0);
        BOOST_TEST(out[0] == 0xFF); // (-2i16).to_be_bytes()
        BOOST_TEST(out[1] == 0xFE);
    }
}

BOOST_AUTO_TEST_CASE(psb_channel_lengths_are_8_bytes) {
    LayerRecord rec;
    ChannelData cd;
    cd.id = 0;
    cd.compression = Compression::Raw;
    rec.channels.push_back(cd);
    LayerInfo info;
    info.layers = {rec};
    auto psd = rt(info, Version::Psd);
    auto psb = rt(info, Version::Psb);
    BOOST_TEST(psb.size() == psd.size() + 4);
}

BOOST_AUTO_TEST_CASE(channel_decode_via_record) {
    auto rec = sampleRecord(Version::Psd);
    std::vector<uint8_t> px(20);
    for (size_t i = 0; i < px.size(); ++i) px[i] = static_cast<uint8_t>(i);
    for (int16_t id : {int16_t(-1), int16_t(0), int16_t(1), int16_t(2)}) {
        auto d = rec.decodeChannel(id, 8, Version::Psd);
        BOOST_REQUIRE(bool(d));
        BOOST_TEST(d.value() == px, boost::test_tools::per_element());
    }
    auto maskDecoded = rec.decodeChannel(-2, 8, Version::Psd);
    BOOST_REQUIRE(bool(maskDecoded));
    BOOST_TEST(maskDecoded.value() == (std::vector<uint8_t>{1, 2, 3, 4}), boost::test_tools::per_element());
    BOOST_TEST(!rec.decodeChannel(7, 8, Version::Psd));
}

BOOST_AUTO_TEST_CASE(record_accessors) {
    auto rec = sampleRecord(Version::Psd);
    BOOST_TEST(rec.resolvedName() == "\xc3\xa4" "bc");
    BOOST_TEST(!rec.isVisible());
    bool idEq = (rec.layerId() == std::optional<uint32_t>(5));
    BOOST_TEST(idEq);
    BOOST_TEST(rec.fillOpacity() == 255);
    BOOST_TEST(rec.sectionType() == SectionType::Other);
    BOOST_TEST(rec.layerMask() != nullptr);
    BOOST_TEST(rec.channelRect(-2) == Rect::fromXywh(0, 0, 2, 2));
    bool rectEq = (rec.channelRect(0) == rec.rect);
    BOOST_TEST(rectEq);

    LayerRecord plain;
    plain.name = {'l', 'e', 'g', 'a', 'c', 'y'};
    BOOST_TEST(plain.resolvedName() == "legacy");
    plain.blocks.push_back(TaggedBlock::fillOpacity(10));
    BOOST_TEST(plain.fillOpacity() == 10);
    auto* b = plain.blockMut({'i', 'O', 'p', 'a'});
    BOOST_REQUIRE(b != nullptr);
    b->data[0] = 11;
    BOOST_TEST(plain.fillOpacity() == 11);
}

BOOST_AUTO_TEST_CASE(mask_variants_roundtrip) {
    auto base = LayerMask::make(Rect::fromXywh(1, 2, 3, 4), 0, 0);
    auto withReal = base;
    withReal.real = RealMask{1, 255, Rect::fromXywh(0, 0, 9, 9)};
    withReal.trailing = {};
    MaskParameters params;
    params.flags = 0b1111;
    params.userDensity = 128;
    params.userFeather = 2.5;
    params.vectorDensity = 64;
    params.vectorFeather = 0.5;
    auto withParams = base;
    withParams.flags = LayerMask::kFlagParameters;
    withParams.parameters = params;
    withParams.real = RealMask{};
    withParams.trailing = {0, 0};

    MaskParameters partial;
    partial.flags = 0b0010;
    partial.userFeather = 1.0;
    auto partialParams = base;
    partialParams.flags = LayerMask::kFlagParameters;
    partialParams.parameters = partial;
    partialParams.trailing = {};

    for (auto& m : {base, withReal, withParams, partialParams}) {
        std::vector<uint8_t> out;
        m.write(out);
        auto parsed = LayerMask::parse(out);
        BOOST_REQUIRE(bool(parsed));
        BOOST_TEST(parsed.value() == m);
    }
}

BOOST_AUTO_TEST_CASE(real_mask_before_parameters_photoshop_layout) {
    std::vector<uint8_t> d;
    Rect::fromXywh(15, -1, 18, 10).write(d);
    d.push_back(0);
    d.push_back(LayerMask::kFlagParameters | 8);
    d.push_back(0);
    d.push_back(255);
    Rect::fromXywh(0, 0, 32, 8).write(d);
    d.push_back(0b0101);
    d.push_back(64);
    d.push_back(64);
    d.push_back(0);
    auto parsed = LayerMask::parse(d);
    BOOST_REQUIRE(bool(parsed));
    auto& m = parsed.value();
    BOOST_REQUIRE(m.real.has_value());
    BOOST_TEST(m.real->rect == Rect::fromXywh(0, 0, 32, 8));
    BOOST_TEST(m.real->background == 255);
    BOOST_REQUIRE(m.parameters.has_value());
    bool densEq = (m.parameters->userDensity == std::optional<uint8_t>(64)) && (m.parameters->vectorDensity == std::optional<uint8_t>(64));
    BOOST_TEST(densEq);
    BOOST_TEST(m.trailing == (std::vector<uint8_t>{0}), boost::test_tools::per_element());
    BOOST_TEST(m.realFirst);
    std::vector<uint8_t> out;
    m.write(out);
    BOOST_TEST(out == d, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(density_only_spec_layout_is_not_read_as_photoshop_order) {
    struct Case {
        uint8_t density;
        uint32_t right;
    };
    for (auto c : std::vector<Case>{{64, 1}, {200, 1}, {64, 300}, {1, 7}}) {
        std::vector<uint8_t> d;
        Rect::fromXywh(0, 0, 1, 1).write(d);
        d.push_back(255);
        d.push_back(LayerMask::kFlagParameters);
        d.push_back(0b0001);
        d.push_back(c.density);
        d.push_back(0);
        d.push_back(0);
        Rect::fromXywh(0, 0, c.right, 1).write(d);
        auto parsed = LayerMask::parse(d);
        BOOST_REQUIRE(bool(parsed));
        auto& m = parsed.value();
        BOOST_TEST(!m.realFirst);
        BOOST_REQUIRE(m.parameters.has_value());
        bool densEq = (m.parameters->userDensity == std::optional<uint8_t>(c.density));
        BOOST_TEST(densEq);
        BOOST_TEST(m.real == std::optional<RealMask>(RealMask{0, 0, Rect::fromXywh(0, 0, c.right, 1)}));
        BOOST_TEST(m.trailing.empty());
        std::vector<uint8_t> out;
        m.write(out);
        BOOST_TEST(out == d, boost::test_tools::per_element());
    }
    std::vector<uint8_t> d;
    Rect::fromXywh(0, 0, 1, 1).write(d);
    d.push_back(255);
    d.push_back(LayerMask::kFlagParameters);
    d.push_back(0);
    d.push_back(255);
    Rect::fromXywh(0, 0, 1, 1).write(d);
    d.push_back(0b0001);
    d.push_back(64);
    auto parsed = LayerMask::parse(d);
    BOOST_REQUIRE(bool(parsed));
    auto& m = parsed.value();
    BOOST_TEST(m.realFirst);
    BOOST_REQUIRE(m.parameters.has_value());
    bool densEq = (m.parameters->userDensity == std::optional<uint8_t>(64));
    BOOST_TEST(densEq);
    BOOST_REQUIRE(m.real.has_value());
    BOOST_TEST(m.real->background == 255);
    std::vector<uint8_t> out;
    m.write(out);
    BOOST_TEST(out == d, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(real_mask_after_parameters_spec_layout) {
    std::vector<uint8_t> d;
    Rect::fromXywh(1, 2, 3, 4).write(d);
    d.push_back(255);
    d.push_back(LayerMask::kFlagParameters);
    d.push_back(0b0011);
    d.push_back(200);
    double feather = 1.5;
    uint64_t bits;
    std::memcpy(&bits, &feather, 8);
    for (int i = 56; i >= 0; i -= 8) d.push_back(static_cast<uint8_t>(bits >> i));
    d.push_back(0);
    d.push_back(255);
    Rect::fromXywh(0, 0, 3, 3).write(d);
    auto parsed = LayerMask::parse(d);
    BOOST_REQUIRE(bool(parsed));
    auto& m = parsed.value();
    BOOST_TEST(!m.realFirst);
    BOOST_REQUIRE(m.real.has_value());
    BOOST_TEST(m.real->rect == Rect::fromXywh(0, 0, 3, 3));
    BOOST_REQUIRE(m.parameters.has_value());
    bool featherEq = (m.parameters->userFeather == std::optional<double>(1.5));
    BOOST_TEST(featherEq);
    std::vector<uint8_t> out;
    m.write(out);
    BOOST_TEST(out == d, boost::test_tools::per_element());

    // Parameters alone (no real mask) keep reading in the spec's order.
    std::vector<uint8_t> d2;
    Rect::fromXywh(1, 2, 3, 4).write(d2);
    d2.push_back(255);
    d2.push_back(LayerMask::kFlagParameters);
    d2.push_back(0b1010);
    double f1 = 1.0, f2 = 2.0;
    uint64_t b1, b2;
    std::memcpy(&b1, &f1, 8);
    std::memcpy(&b2, &f2, 8);
    for (int i = 56; i >= 0; i -= 8) d2.push_back(static_cast<uint8_t>(b1 >> i));
    for (int i = 56; i >= 0; i -= 8) d2.push_back(static_cast<uint8_t>(b2 >> i));
    d2.push_back(0);
    d2.push_back(0);
    auto parsed2 = LayerMask::parse(d2);
    BOOST_REQUIRE(bool(parsed2));
    auto& m2 = parsed2.value();
    BOOST_TEST(!m2.real.has_value());
    BOOST_REQUIRE(m2.parameters.has_value());
    bool feathersEq = (m2.parameters->userFeather == std::optional<double>(1.0)) && (m2.parameters->vectorFeather == std::optional<double>(2.0));
    BOOST_TEST(feathersEq);
}

BOOST_AUTO_TEST_CASE(mask_sizes) {
    std::vector<uint8_t> out;
    LayerMask::make(Rect{}, 0, 0).write(out);
    BOOST_TEST(out.size() == 20u);
    auto m = LayerMask::make(Rect{}, 0, 0);
    m.real = RealMask{};
    m.trailing = {};
    std::vector<uint8_t> out2;
    m.write(out2);
    BOOST_TEST(out2.size() == 36u);
}

BOOST_AUTO_TEST_CASE(short_mask_kept_raw) {
    LayerRecord rec;
    rec.mask = MaskData::raw({1, 2, 3});
    rec.channels.clear();
    LayerInfo info;
    info.layers = {rec};
    rt(info, Version::Psd);
}

BOOST_AUTO_TEST_CASE(flags_helpers) {
    LayerFlags f;
    f.setHidden(true);
    f.setTransparencyProtected(true);
    BOOST_TEST(f.hidden());
    BOOST_TEST(f.transparencyProtected());
    f.setPixelDataIrrelevant(true);
    BOOST_TEST(f.bit4Useful());
    BOOST_TEST(f.pixelDataIrrelevant());
    f.setHidden(false);
    BOOST_TEST(!f.hidden());
}

BOOST_AUTO_TEST_CASE(blending_ranges_parse) {
    auto br = BlendingRanges::full(3);
    BOOST_TEST(br.data.size() == 32u);
    auto r = br.ranges();
    BOOST_TEST(r.size() == 4u);
    BOOST_TEST(r[0].source == (std::array<uint8_t, 4>{0, 0, 255, 255}));
}

BOOST_AUTO_TEST_CASE(rect_helpers) {
    auto r = Rect::fromXywh(-5, -6, 10, 3);
    BOOST_TEST(r.width() == 10);
    BOOST_TEST(r.height() == 3);
    auto sz = r.size();
    BOOST_REQUIRE(bool(sz));
    BOOST_TEST(sz.value().first == 10u);
    BOOST_TEST(sz.value().second == 3u);
    BOOST_TEST(!r.isEmpty());
    Rect bad{5, 0, 0, 1};
    BOOST_TEST(!bad.size());
    BOOST_TEST(bad.isEmpty());
    Rect huge{0, INT32_MIN, 1, INT32_MAX};
    BOOST_TEST(!huge.size());
}

BOOST_AUTO_TEST_CASE(truncation_errors) {
    LayerInfo info;
    info.layers = {sampleRecord(Version::Psd)};
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(info.writeBody(out, Version::Psd)));
    // The final byte may be the (optional) even padding; stop before it.
    for (size_t cut = 0; cut < out.size() - 1; ++cut) {
        std::span<const uint8_t> partial(out.data(), cut);
        BOOST_TEST(!LayerInfo::readBody(partial, Version::Psd));
    }
}

BOOST_AUTO_TEST_CASE(bad_blend_signature) {
    LayerInfo info;
    info.layers = {LayerRecord{}};
    std::vector<uint8_t> out;
    BOOST_REQUIRE(bool(info.writeBody(out, Version::Psd)));
    // count(2) + rect(16) + nch(2) = 20 -> signature
    out[20] = 'X';
    auto result = LayerInfo::readBody(out, Version::Psd);
    BOOST_REQUIRE(!result);
    BOOST_TEST(result.error().kind() == PsdError::Kind::InvalidSignature);
}

BOOST_AUTO_TEST_CASE(too_many_layers_rejected) {
    std::vector<uint8_t> out = {0x03, 0xE8}; // 1000 as big-endian i16
    BOOST_TEST(!LayerInfo::readBody(out, Version::Psd));
}

BOOST_AUTO_TEST_CASE(zero_length_channel) {
    LayerRecord rec;
    ChannelData cd;
    cd.id = 0;
    cd.compression = std::nullopt;
    rec.channels.push_back(cd);
    LayerInfo info;
    info.layers = {rec};
    rt(info, Version::Psd);
    auto decoded = info.layers[0].decodeChannel(0, 8, Version::Psd);
    BOOST_REQUIRE(bool(decoded));
    BOOST_TEST(decoded.value().empty());
}

BOOST_AUTO_TEST_CASE(unpadded_len_matches_the_written_body) {
    for (Version version : {Version::Psd, Version::Psb}) {
        std::vector<LayerRecord> layers = {sampleRecord(version), LayerRecord{}};
        struct Combo {
            bool params, real, realFirst;
        };
        for (auto combo : std::vector<Combo>{{true, true, true}, {true, true, false}, {true, false, true}, {false, true, true}}) {
            auto mask = LayerMask::make(Rect::fromXywh(1, 2, 3, 4), 0, LayerMask::kFlagParameters);
            if (combo.params) {
                MaskParameters p;
                p.flags = 0x0f;
                p.userDensity = 1;
                p.userFeather = 2.5;
                p.vectorDensity = 3;
                p.vectorFeather = std::nullopt;
                mask.parameters = p;
            }
            if (combo.real) mask.real = RealMask{0, 255, Rect::fromXywh(0, 0, 1, 1)};
            mask.realFirst = combo.realFirst;
            mask.trailing = std::vector<uint8_t>(3, 0);
            auto rec = sampleRecord(version);
            rec.mask = MaskData::mask(mask);
            rec.name = std::vector<uint8_t>(300, 'n');
            TaggedBlock lsct;
            lsct.signature = {'8', 'B', 'I', 'M'};
            lsct.key = {'l', 's', 'c', 't'};
            lsct.data = std::vector<uint8_t>(5, 1);
            lsct.padding = std::vector<uint8_t>(3, 0);
            rec.blocks.push_back(lsct);
            TaggedBlock lr16;
            lr16.signature = {'8', 'B', 'I', 'M'};
            lr16.key = {'L', 'r', '1', '6'};
            lr16.data = std::vector<uint8_t>(7, 2);
            lr16.padding = std::nullopt;
            rec.blocks.push_back(lr16);
            rec.extraTrailing = std::vector<uint8_t>(2, 9);
            layers.push_back(rec);
        }
        LayerRecord rawMaskRec;
        rawMaskRec.mask = MaskData::raw(std::vector<uint8_t>(11, 5));
        rawMaskRec.name = {'a', 'b'};
        layers.push_back(rawMaskRec);
        LayerInfo info;
        info.layers = layers;
        std::vector<uint8_t> out;
        BOOST_REQUIRE(bool(info.writeUnpadded(out, version)));
        auto len = info.unpaddedLen(version);
        BOOST_REQUIRE(bool(len));
        BOOST_TEST(len.value() == out.size());
    }
}

BOOST_AUTO_TEST_CASE(nondefault_padding_kept) {
    LayerInfo info;
    info.padding = std::vector<uint8_t>{0, 0};
    auto out = rt(info, Version::Psd);
    BOOST_TEST(out.size() == 4u);
}

BOOST_AUTO_TEST_CASE(unpadded_write_matches_pad_to_on_a_clone) {
    for (Version version : {Version::Psd, Version::Psb}) {
        for (uint16_t depth : {uint16_t{8}, uint16_t{16}, uint16_t{32}}) {
            for (Compression compression : {Compression::Raw, Compression::Rle}) {
                PsdFile file = testgen::layered(version, ColorMode::Rgb, depth, compression);
                BOOST_REQUIRE(file.layerInfo.has_value());
                LayerInfo info = *file.layerInfo;
                for (uint64_t align : {uint64_t{1}, uint64_t{2}, uint64_t{4}, uint64_t{8}}) {
                    LayerInfo cloned = info;
                    BOOST_REQUIRE(bool(cloned.padTo(version, align)));
                    std::vector<uint8_t> want;
                    BOOST_REQUIRE(bool(cloned.writeBody(want, version)));
                    auto padResult = info.padLen(version, align);
                    BOOST_REQUIRE(bool(padResult));
                    size_t pad = padResult.value();
                    std::vector<uint8_t> got;
                    BOOST_REQUIRE(bool(info.writeUnpadded(got, version)));
                    BOOST_TEST(got.size() >= info.channelDataLen());
                    got.resize(got.size() + pad, 0);
                    BOOST_TEST(got == want, boost::test_tools::per_element());
                }
                info.padding = std::vector<uint8_t>{7, 7};
                std::vector<uint8_t> withPadding;
                BOOST_REQUIRE(bool(info.writeBody(withPadding, version)));
                BOOST_REQUIRE(withPadding.size() >= 2);
                BOOST_TEST(withPadding[withPadding.size() - 2] == 7);
                BOOST_TEST(withPadding[withPadding.size() - 1] == 7);
            }
        }
    }
}

BOOST_AUTO_TEST_CASE(unknown_blend_and_blocks_passthrough) {
    LayerRecord rec;
    rec.blendMode = BlendMode::fromKey({'w', 'x', 'y', 'z'});
    rec.blocks.push_back(TaggedBlock::make({'Z', 'z', 'z', 'z'}, {1, 2, 3, 4, 5}));
    LayerInfo info;
    info.layers = {rec};
    rt(info, Version::Psd);
}

BOOST_AUTO_TEST_SUITE_END()
