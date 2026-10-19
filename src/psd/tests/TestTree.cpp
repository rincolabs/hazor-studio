// Layer tree reconstruction tests for Tree.hpp. See PROVENANCE.md for origin
// details.
#define BOOST_TEST_MODULE PsdTreeTest
#include <boost/test/included/unit_test.hpp>

#include <optional>
#include <sstream>

#include "Testgen.hpp"
#include "psd/Builder.hpp"
#include "psd/Tree.hpp"

using namespace psd;

namespace {
LayerRecord rec(const std::string& name, std::optional<SectionType> kind) {
    LayerRecord r;
    r.name = std::vector<uint8_t>(name.begin(), name.end());
    if (kind.has_value()) {
        r.blocks.push_back(TaggedBlock::sectionDivider(*kind, BlendMode::PassThrough, std::nullopt));
    }
    return r;
}

PsdFile fileWith(std::vector<LayerRecord> records) {
    auto f = testgen::mergedOnly(Version::Psd, ColorMode::Rgb, 8, Compression::Raw, 1, 1);
    LayerInfo info;
    info.layers = std::move(records);
    f.layerInfo = std::move(info);
    return f;
}

std::string names(const PsdFile& f, std::span<const LayerNode> nodes) {
    std::ostringstream out;
    bool first = true;
    for (auto& n : nodes) {
        if (!first) out << ",";
        first = false;
        if (n.kind() == LayerNode::Kind::Layer) {
            out << f.layers()[n.index()].resolvedName();
        } else {
            out << f.layers()[n.index()].resolvedName() << "[" << names(f, n.children()) << "]";
        }
    }
    return out.str();
}

// Plain `const SectionType kEnd = SectionType::BoundingDivider;` globals
// would be a static-initialization-order fiasco: SectionType::BoundingDivider
// is itself a static defined in another translation unit (Tagged.cpp), and
// init order across TUs is unspecified. Macros are pure text substitution,
// so each use evaluates at the point of the surrounding test body (always
// after all static init has completed), not at this file's own static-init
// time.
#define kEnd SectionType::BoundingDivider
#define kOpen SectionType::OpenFolder
#define kClosed SectionType::ClosedFolder

// Iterative so a deep tree cannot overflow the test's stack.
size_t treeDepth(std::span<const LayerNode> nodes) {
    size_t maxD = 0;
    std::vector<std::pair<const LayerNode*, size_t>> todo;
    for (auto& n : nodes) todo.emplace_back(&n, 1);
    while (!todo.empty()) {
        auto [n, d] = todo.back();
        todo.pop_back();
        maxD = std::max(maxD, d);
        for (auto& c : n->children()) todo.emplace_back(&c, d + 1);
    }
    return maxD;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_tree)

BOOST_AUTO_TEST_CASE(flat_layers) {
    auto f = fileWith({rec("a", std::nullopt), rec("b", std::nullopt), rec("c", std::nullopt)});
    auto t = f.layerTree();
    BOOST_TEST(names(f, t) == "a,b,c");
    bool noneAreGroups = true;
    for (auto& n : t)
        if (n.isGroup()) noneAreGroups = false;
    BOOST_TEST(noneAreGroups);
}

BOOST_AUTO_TEST_CASE(empty_file_has_empty_tree) {
    auto f = testgen::mergedOnly(Version::Psd, ColorMode::Rgb, 8, Compression::Raw, 1, 1);
    BOOST_TEST(f.layerTree().empty());
}

BOOST_AUTO_TEST_CASE(single_group) {
    auto f = fileWith({rec("bg", std::nullopt), rec("</Layer group>", kEnd), rec("x", std::nullopt), rec("y", std::nullopt), rec("G", kOpen)});
    auto t = f.layerTree();
    BOOST_TEST(names(f, t) == "bg,G[x,y]");
    BOOST_REQUIRE(t.size() == 2);
    BOOST_TEST(t[1].index() == 4u);
    bool dividerEq = (t[1].divider() == std::optional<size_t>(1));
    BOOST_TEST(dividerEq);
    BOOST_TEST(t[1].children().size() == 2u);
}

BOOST_AUTO_TEST_CASE(closed_group) {
    auto f = fileWith({rec("</Layer group>", kEnd), rec("x", std::nullopt), rec("G", kClosed)});
    BOOST_TEST(names(f, f.layerTree()) == "G[x]");
}

BOOST_AUTO_TEST_CASE(nested_groups) {
    auto f = fileWith({
        rec("</Layer group>", kEnd),
        rec("a", std::nullopt),
        rec("</Layer group>", kEnd),
        rec("b", std::nullopt),
        rec("Inner", kClosed),
        rec("c", std::nullopt),
        rec("Outer", kOpen),
        rec("top", std::nullopt),
    });
    auto t = f.layerTree();
    BOOST_TEST(names(f, t) == "Outer[a,Inner[b],c],top");
    BOOST_TEST(t[0].count() == 5u);
}

BOOST_AUTO_TEST_CASE(empty_group) {
    auto f = fileWith({rec("</Layer group>", kEnd), rec("G", kOpen)});
    auto t = f.layerTree();
    BOOST_TEST(names(f, t) == "G[]");
    BOOST_TEST(t[0].children().empty());
}

BOOST_AUTO_TEST_CASE(sibling_groups) {
    auto f = fileWith({rec("</Layer group>", kEnd), rec("a", std::nullopt), rec("G1", kOpen), rec("</Layer group>", kEnd), rec("b", std::nullopt),
                       rec("G2", kOpen)});
    BOOST_TEST(names(f, f.layerTree()) == "G1[a],G2[b]");
}

BOOST_AUTO_TEST_CASE(deeply_nested) {
    std::vector<LayerRecord> v;
    for (int i = 0; i < 50; ++i) v.push_back(rec("</Layer group>", kEnd));
    v.push_back(rec("leaf", std::nullopt));
    for (int i = 0; i < 50; ++i) v.push_back(rec("g" + std::to_string(i), kOpen));
    auto f = fileWith(std::move(v));
    auto t = f.layerTree();
    BOOST_REQUIRE(t.size() == 1u);
    BOOST_TEST(t[0].count() == 51u);
    const LayerNode* n = &t[0];
    size_t depthCount = 1;
    while (!n->children().empty()) {
        n = &n->children()[0];
        ++depthCount;
    }
    BOOST_TEST(depthCount == 51u);
}

// #2940: the capped tree keeps a group at the cap but not its children, so its depth is bounded
// whatever the file nests.
BOOST_AUTO_TEST_CASE(layer_tree_max_depth_drops_deeper_children) {
    std::vector<LayerRecord> v = {rec("</Layer group>", kEnd), rec("</Layer group>", kEnd), rec("</Layer group>", kEnd), rec("x", std::nullopt)};
    for (auto& r : {rec("g2", kOpen), rec("g1", kOpen), rec("g0", kOpen), rec("top", std::nullopt)}) v.push_back(r);
    auto f = fileWith(std::move(v));
    BOOST_TEST(names(f, f.layerTree()) == "g0[g1[g2[x]]],top");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(SIZE_MAX)) == "g0[g1[g2[x]]],top");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(2)) == "g0[g1[g2[]]],top");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(1)) == "g0[g1[]],top");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(0)) == "g0[],top");
}

// #5758: layers under a divider that is never closed are hoisted to the enclosing level, as
// layerTree does, also when the cap is shallower than the unclosed divider.
BOOST_AUTO_TEST_CASE(layer_tree_max_depth_hoists_unclosed_divider_children) {
    auto f = fileWith({rec("a", std::nullopt), rec("</Layer group>", kEnd), rec("b", std::nullopt), rec("c", std::nullopt)});
    BOOST_TEST(names(f, f.layerTree()) == "a,b,c");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(1)) == "a,b,c");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(0)) == "a,b,c");
}

// #5758: a closed group inside unclosed dividers sits at the depth it ends up at after hoisting,
// so the cap keeps its node and drops only its deeper children.
BOOST_AUTO_TEST_CASE(layer_tree_max_depth_keeps_closed_group_inside_unclosed_dividers) {
    std::vector<LayerRecord> v = {rec("</Layer group>", kEnd), rec("</Layer group>", kEnd)};
    for (auto& r :
         {rec("</Layer group>", kEnd), rec("</Layer group>", kEnd), rec("x", std::nullopt), rec("g1", kOpen), rec("g0", kOpen)})
        v.push_back(r);
    auto f = fileWith(std::move(v));
    BOOST_TEST(names(f, f.layerTree()) == "g0[g1[x]]");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(1)) == "g0[g1[]]");
    BOOST_TEST(names(f, f.layerTreeMaxDepth(0)) == "g0[]");
}

// #5758: thousands of unclosed dividers around a closed chain stay bounded by the cap and keep
// every layer the full tree has.
BOOST_AUTO_TEST_CASE(layer_tree_max_depth_bounds_deep_unclosed_chain) {
    std::vector<LayerRecord> v;
    for (int i = 0; i < 16000; ++i) {
        v.push_back(rec("</Layer group>", kEnd));
        v.push_back(rec("l" + std::to_string(i), std::nullopt));
    }
    for (auto& r : {rec("</Layer group>", kEnd), rec("</Layer group>", kEnd), rec("</Layer group>", kEnd), rec("x", std::nullopt)}) v.push_back(r);
    for (auto& r : {rec("g2", kOpen), rec("g1", kOpen), rec("g0", kOpen)}) v.push_back(r);
    auto f = fileWith(std::move(v));
    auto full = f.layerTree();
    BOOST_REQUIRE(full.size() == 16001u);
    BOOST_TEST(treeDepth(full) == 4u);
    auto capped = f.layerTreeMaxDepth(1);
    BOOST_TEST(treeDepth(capped) <= 2u);
    BOOST_REQUIRE(capped.size() == 16001u);
    std::span<const LayerNode> tail(capped.data() + 16000, capped.size() - 16000);
    BOOST_TEST(names(f, tail) == "g0[g1[]]");
    std::span<const LayerNode> head(capped.data(), 2);
    BOOST_TEST(names(f, head) == "l0,l1");
}

BOOST_AUTO_TEST_CASE(folder_without_divider_is_empty_group) {
    auto f = fileWith({rec("a", std::nullopt), rec("G", kOpen), rec("b", std::nullopt)});
    BOOST_TEST(names(f, f.layerTree()) == "a,G[],b");
}

BOOST_AUTO_TEST_CASE(unclosed_divider_hoists_children) {
    auto f = fileWith({rec("a", std::nullopt), rec("</Layer group>", kEnd), rec("b", std::nullopt), rec("c", std::nullopt)});
    BOOST_TEST(names(f, f.layerTree()) == "a,b,c");
}

BOOST_AUTO_TEST_CASE(lsdk_is_honored) {
    auto end = rec("</Layer group>", std::nullopt);
    auto b = TaggedBlock::sectionDivider(SectionType::BoundingDivider, std::nullopt, std::nullopt);
    b.key = {'l', 's', 'd', 'k'};
    end.blocks.push_back(b);
    auto f = fileWith({end, rec("x", std::nullopt), rec("G", kOpen)});
    BOOST_TEST(names(f, f.layerTree()) == "G[x]");
}

BOOST_AUTO_TEST_CASE(other_section_type_is_layer) {
    auto f = fileWith({rec("a", SectionType::Other), rec("b", SectionType::fromU32(9))});
    BOOST_TEST(names(f, f.layerTree()) == "a,b");
}

BOOST_AUTO_TEST_CASE(generated_layered_tree) {
    auto f = testgen::layered(Version::Psd, ColorMode::Rgb, 8, Compression::Rle);
    auto t = f.layerTree();
    const LayerNode* outer = nullptr;
    for (auto& n : t) {
        if (n.isGroup()) {
            outer = &n;
            break;
        }
    }
    BOOST_REQUIRE(outer != nullptr);
    BOOST_TEST(f.layers()[outer->index()].resolvedName() == "Outer");
    BOOST_REQUIRE(outer->children().size() == 2u);
    const LayerNode& inner = outer->children()[1];
    BOOST_TEST(inner.isGroup());
    BOOST_TEST(f.layers()[inner.index()].resolvedName() == "Inner");
    bool stEq = (f.layers()[inner.index()].sectionType() == SectionType::ClosedFolder);
    BOOST_TEST(stEq);
    BOOST_REQUIRE(!inner.children().empty());
    BOOST_TEST(f.layers()[inner.children()[0].index()].resolvedName() == "Gr\xc3\xbc\xc3\x9f" "e");
}

BOOST_AUTO_TEST_CASE(builder_groups_tree) {
    auto px = [](size_t n) { return PixelData::rgba8(std::vector<uint8_t>(n * 4, 255)); };
    PsdBuilder b(2, 2);
    b.pushLayer(LayerSpec("bg", 0, 0, 2, 2, px(4)));
    b.beginGroup(GroupSpec("A"));
    b.pushLayer(LayerSpec("a1", 0, 0, 1, 1, px(1)));
    GroupSpec gB("B");
    gB.open = false;
    b.beginGroup(gB);
    b.pushLayer(LayerSpec("b1", 0, 0, 1, 1, px(1)));
    BOOST_REQUIRE(bool(b.endGroup()));
    BOOST_REQUIRE(bool(b.endGroup()));
    auto bytes = b.toBytes();
    BOOST_REQUIRE(bool(bytes));
    auto f = PsdFile::fromBytes(bytes.value());
    BOOST_REQUIRE(bool(f));
    BOOST_TEST(names(f.value(), f.value().layerTree()) == "bg,A[a1,B[b1]]");
}

BOOST_AUTO_TEST_SUITE_END()
