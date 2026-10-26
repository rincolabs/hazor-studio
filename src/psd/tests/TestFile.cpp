// Unit tests for File.hpp's write path. See PROVENANCE.md for origin details.
#define BOOST_TEST_MODULE PsdFileTest
#include <boost/test/included/unit_test.hpp>

#include <algorithm>
#include <array>
#include <optional>

#include "Testgen.hpp"
#include "psd/File.hpp"

using namespace psd;

BOOST_AUTO_TEST_SUITE(psd_file_write_tests)

BOOST_AUTO_TEST_CASE(global_blocks_default_to_four_byte_padding) {
    for (Version version : {Version::Psd, Version::Psb}) {
        for (size_t len = 1; len < 8; ++len) {
            auto file = testgen::small(version, Compression::Raw);
            file.globalBlocks = {TaggedBlock::make({'T', 'E', 'S', 'T'}, std::vector<uint8_t>(len, 7)), TaggedBlock::make({'N', 'E', 'X', 'T'}, std::vector<uint8_t>(4, 8))};
            auto bytes = file.toBytes();
            BOOST_REQUIRE(bool(bytes));
            auto& b = bytes.value();
            auto findSeq = [&](const std::vector<uint8_t>& needle, size_t from) -> std::optional<size_t> {
                if (needle.size() > b.size()) return std::nullopt;
                for (size_t i = from; i + needle.size() <= b.size(); ++i) {
                    if (std::equal(needle.begin(), needle.end(), b.begin() + static_cast<ptrdiff_t>(i))) return i;
                }
                return std::nullopt;
            };
            auto startPos = findSeq({'8', 'B', 'I', 'M', 'T', 'E', 'S', 'T'}, 0);
            BOOST_REQUIRE(startPos.has_value());
            size_t start = *startPos + 12;
            auto nextPos = findSeq({'8', 'B', 'I', 'M', 'N', 'E', 'X', 'T'}, 0);
            BOOST_REQUIRE(nextPos.has_value());
            size_t next = *nextPos;
            size_t lenPadded = ((len + 3) / 4) * 4;
            BOOST_TEST(next - start == lenPadded);
            std::vector<uint8_t> expected(len, 7);
            expected.resize(lenPadded, 0);
            std::vector<uint8_t> got(b.begin() + static_cast<ptrdiff_t>(start), b.begin() + static_cast<ptrdiff_t>(start + expected.size()));
            BOOST_TEST(got == expected, boost::test_tools::per_element());
            auto parsed = PsdFile::fromBytes(b);
            BOOST_REQUIRE(bool(parsed));
            BOOST_TEST(parsed.value() == file);
        }
    }
}

BOOST_AUTO_TEST_CASE(streamed_layer_info_equals_a_materialized_tagged_block) {
    for (Version version : {Version::Psd, Version::Psb}) {
        for (auto key : {std::array<uint8_t, 4>{'L', 'r', '1', '6'}, std::array<uint8_t, 4>{'L', 'r', '3', '2'}, std::array<uint8_t, 4>{'L', 'a', 'y', 'r'}}) {
            for (size_t index : {size_t{0}, size_t{1}, size_t{2}, SIZE_MAX}) {
                for (std::optional<std::vector<uint8_t>> padding :
                     {std::optional<std::vector<uint8_t>>(std::nullopt), std::optional<std::vector<uint8_t>>(std::vector<uint8_t>{}),
                      std::optional<std::vector<uint8_t>>(std::vector<uint8_t>{42}), std::optional<std::vector<uint8_t>>(std::vector<uint8_t>{1, 2, 3})}) {
                    auto file = testgen::small(version, Compression::Raw);
                    file.globalBlocks = {TaggedBlock::make({'z', 'z', '0', '1'}, {11}), TaggedBlock::make({'z', 'z', '0', '2'}, {12, 13})};
                    LayerInfoPlacement::GlobalBlockInfo gbi;
                    gbi.index = index;
                    gbi.signature = {'8', 'B', '6', '4'};
                    gbi.key = key;
                    gbi.padding = padding;
                    file.layerInfoPlacement = LayerInfoPlacement::globalBlock(gbi);
                    // An odd body catches padding calculated against the final file offset.
                    file.layerInfo->padding = std::vector<uint8_t>{77};
                    auto actual = file.toBytes();
                    BOOST_REQUIRE(bool(actual));

                    PsdFile materialized = file;
                    std::vector<uint8_t> data;
                    BOOST_REQUIRE(bool(materialized.layerInfo->writeBody(data, version)));
                    materialized.layerInfo = std::nullopt;
                    TaggedBlock tb;
                    tb.signature = {'8', 'B', '6', '4'};
                    tb.key = key;
                    tb.data = data;
                    tb.padding = padding;
                    size_t insertAt = std::min(index, materialized.globalBlocks.size());
                    materialized.globalBlocks.insert(materialized.globalBlocks.begin() + static_cast<ptrdiff_t>(insertAt), tb);
                    materialized.layerInfoPlacement = LayerInfoPlacement::section();

                    auto materializedBytes = materialized.toBytes();
                    BOOST_REQUIRE(bool(materializedBytes));
                    BOOST_TEST(actual.value() == materializedBytes.value(), boost::test_tools::per_element());
                }
            }
        }
    }
}

// #2926: a PSB whose ZIP merged image declares more than MAX_DECODED_BYTES opens without
// inflating that stream (its layers are still usable); decoding the composite reports the limit.
BOOST_AUTO_TEST_CASE(over_budget_zip_merged_image_opens_without_inflating) {
    auto small = testgen::small(Version::Psb, Compression::Zip);
    auto smallBytes = small.toBytes();
    BOOST_REQUIRE(bool(smallBytes));
    auto bytes = smallBytes.value();
    // Header: height at 14, width at 18 (big-endian u32).
    uint32_t big = 300000;
    for (int i = 0; i < 4; ++i) bytes[14 + i] = static_cast<uint8_t>(big >> (24 - 8 * i));
    for (int i = 0; i < 4; ++i) bytes[18 + i] = static_cast<uint8_t>(big >> (24 - 8 * i));
    auto file = PsdFile::fromBytes(bytes);
    BOOST_REQUIRE(bool(file));
    BOOST_TEST(!file.value().layers().empty());
    auto decoded = file.value().decodeMerged();
    BOOST_REQUIRE(!decoded);
    BOOST_TEST(decoded.error().kind() == PsdError::Kind::LimitExceeded);

    auto result = PsdFile::fromBytesAndMerged(bytes, [](const PsdFile&) { return true; });
    BOOST_REQUIRE(bool(result));
    BOOST_TEST(result.value().first == file.value());
    BOOST_REQUIRE(result.value().second.has_value());
    auto& merged = *result.value().second;
    BOOST_REQUIRE(!merged);
    BOOST_TEST(merged.error().kind() == PsdError::Kind::LimitExceeded);
}

// Parsing and decoding a ZIP merged image together inflates it once (the parse's stream check
// is the decode's), with the same model, pixels and errors as the two steps apart.
BOOST_AUTO_TEST_CASE(merged_zip_image_is_inflated_once_when_decoded_with_the_parse) {
    for (Compression compression : {Compression::Zip, Compression::ZipPrediction}) {
        auto smallFile = testgen::small(Version::Psd, compression);
        auto bytesResult = smallFile.toBytes();
        BOOST_REQUIRE(bool(bytesResult));
        auto& bytes = bytesResult.value();
        auto file = PsdFile::fromBytes(bytes);
        BOOST_REQUIRE(bool(file));
        auto pixels = file.value().decodeMerged();
        BOOST_REQUIRE(bool(pixels));

        auto together = PsdFile::fromBytesAndMerged(bytes, [](const PsdFile& f) { return f.imageData.data.empty(); });
        BOOST_REQUIRE(bool(together));
        BOOST_TEST(together.value().first == file.value());
        BOOST_REQUIRE(together.value().second.has_value());
        BOOST_REQUIRE(bool(*together.value().second));
        BOOST_TEST(together.value().second->value() == pixels.value(), boost::test_tools::per_element());

        auto apart = PsdFile::fromBytesAndMerged(bytes, [](const PsdFile&) { return false; });
        BOOST_REQUIRE(bool(apart));
        BOOST_TEST(apart.value().first == file.value());
        BOOST_TEST(!apart.value().second.has_value());

        // A broken stream (its checksum) is still the parse's error.
        auto broken = bytes;
        broken.back() ^= 0xff;
        auto alone = PsdFile::fromBytes(broken);
        BOOST_REQUIRE(!alone);
        auto brokenTogether = PsdFile::fromBytesAndMerged(broken, [](const PsdFile&) { return true; });
        BOOST_REQUIRE(!brokenTogether);
        BOOST_TEST(brokenTogether.error().message() == alone.error().message());
    }
}

BOOST_AUTO_TEST_CASE(global_layer_info_serialization_propagates_layer_limit_errors) {
    auto file = testgen::small(Version::Psd, Compression::Raw);
    LayerInfo info;
    info.layers = std::vector<LayerRecord>(32768);
    file.layerInfo = std::move(info);
    LayerInfoPlacement::GlobalBlockInfo gbi;
    gbi.index = SIZE_MAX;
    gbi.signature = {'8', 'B', 'I', 'M'};
    gbi.key = {'L', 'r', '1', '6'};
    gbi.padding = std::nullopt;
    file.layerInfoPlacement = LayerInfoPlacement::globalBlock(gbi);
    auto result = file.toBytes();
    BOOST_REQUIRE(!result);
    BOOST_TEST(result.error().kind() == PsdError::Kind::LimitExceeded);
}

BOOST_AUTO_TEST_CASE(global_layers_preserve_order_padding_and_first_candidate) {
    for (Version version : {Version::Psd, Version::Psb}) {
        for (uint16_t depth : {uint16_t{16}, uint16_t{32}}) {
            auto file = testgen::layered(version, ColorMode::Rgb, depth, Compression::Raw);
            std::array<uint8_t, 4> key = depth == 16 ? std::array<uint8_t, 4>{'L', 'r', '1', '6'} : std::array<uint8_t, 4>{'L', 'r', '3', '2'};
            file.globalBlocks = {TaggedBlock::make({'B', 'E', 'F', 'R'}, {1}), TaggedBlock::make({'A', 'F', 'T', 'R'}, {2, 3})};
            LayerInfoPlacement::GlobalBlockInfo gbi;
            gbi.index = 1;
            gbi.signature = {'8', 'B', '6', '4'};
            gbi.key = key;
            gbi.padding = std::vector<uint8_t>{7, 8};
            file.layerInfoPlacement = LayerInfoPlacement::globalBlock(gbi);
            file.layerMaskTrailing = std::vector<uint8_t>(4, 9);
            auto bytesResult = file.toBytes();
            BOOST_REQUIRE(bool(bytesResult));
            auto& bytes = bytesResult.value();
            auto parsed = PsdFile::fromBytes(bytes);
            BOOST_REQUIRE(bool(parsed));
            BOOST_TEST(parsed.value() == file);
            auto parsedBytes = parsed.value().toBytes();
            BOOST_REQUIRE(bool(parsedBytes));
            BOOST_TEST(parsedBytes.value() == bytes, boost::test_tools::per_element());

            std::vector<uint8_t> duplicate;
            BOOST_REQUIRE(bool(file.layerInfo->writeBody(duplicate, version)));
            file.globalBlocks.push_back(TaggedBlock::make(key, duplicate));
            auto bytes2 = file.toBytes();
            BOOST_REQUIRE(bool(bytes2));
            auto parsed2 = PsdFile::fromBytes(bytes2.value());
            BOOST_REQUIRE(bool(parsed2));
            BOOST_TEST(parsed2.value() == file);

            file.globalBlocks.insert(file.globalBlocks.begin(), TaggedBlock::make(key, {255}));
            auto bytes3 = file.toBytes();
            BOOST_REQUIRE(bool(bytes3));
            auto parsed3 = PsdFile::fromBytes(bytes3.value());
            BOOST_REQUIRE(bool(parsed3));
            BOOST_TEST(!parsed3.value().layerInfo.has_value());
            BOOST_TEST(parsed3.value().globalBlocks.size() == 5u);
            auto parsed3Bytes = parsed3.value().toBytes();
            BOOST_REQUIRE(bool(parsed3Bytes));
            BOOST_TEST(parsed3Bytes.value() == bytes3.value(), boost::test_tools::per_element());
        }
    }
}

BOOST_AUTO_TEST_CASE(global_layers_prioritized_over_compatibility_section) {
    for (Version version : {Version::Psd, Version::Psb}) {
        for (uint16_t depth : {uint16_t{16}, uint16_t{32}}) {
            auto file = testgen::layered(version, ColorMode::Rgb, depth, Compression::Raw);
            std::array<uint8_t, 4> key = depth == 16 ? std::array<uint8_t, 4>{'L', 'r', '1', '6'} : std::array<uint8_t, 4>{'L', 'r', '3', '2'};
            std::vector<uint8_t> lrData;
            BOOST_REQUIRE(bool(file.layerInfo->writeBody(lrData, version)));

            PsdFile compatFile = file;
            LayerInfo compatInfo;
            LayerRecord compatRec;
            compatRec.name = {'C', 'o', 'm', 'p', 'a', 't', 'i', 'b', 'i', 'l', 'i', 't', 'y', 'L', 'a', 'y', 'e', 'r'};
            compatInfo.layers = {compatRec};
            compatFile.layerInfo = std::move(compatInfo);
            compatFile.layerInfoPlacement = LayerInfoPlacement::section();
            compatFile.globalBlocks.insert(compatFile.globalBlocks.begin(), TaggedBlock::make(key, lrData));

            auto bytesResult = compatFile.toBytes();
            BOOST_REQUIRE(bool(bytesResult));
            auto parsed = PsdFile::fromBytes(bytesResult.value());
            BOOST_REQUIRE(bool(parsed));

            BOOST_TEST(parsed.value().layers().size() == file.layers().size());
            BOOST_TEST(parsed.value().layers()[0].resolvedName() != "CompatibilityLayer");
            BOOST_TEST(parsed.value().layers()[0].resolvedName() == file.layers()[0].resolvedName());
            BOOST_TEST(parsed.value().layerInfoPlacement.kind() == LayerInfoPlacement::Kind::GlobalBlock);
            bool keyEq = (parsed.value().layerInfoPlacement.globalBlockInfo().key == key);
            BOOST_TEST(keyEq);
        }
    }
}

BOOST_AUTO_TEST_SUITE_END()
