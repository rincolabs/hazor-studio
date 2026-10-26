// Unit tests for Descriptor.hpp, plus a seeded-PRNG round-trip sweep over
// Value/Descriptor construction (see PROVENANCE.md: no new fuzzing
// dependency, invariants checked deterministically).
#define BOOST_TEST_MODULE PsdDescriptorTest
#include <boost/test/included/unit_test.hpp>

#include <random>

#include "psd/Descriptor.hpp"

using namespace psd;

namespace {
Class makeClass(const std::string& name, const std::string& id) { return Class{UnicodeString::fromUtf8(name), Id::fromString(id)}; }

Descriptor sample() {
    Descriptor d("null");
    d.with("Nm  ", Value::text(UnicodeString::fromUtf8Nul("Hello \xf0\x9f\x98\x80")))
        .with("Opct", Value::unitFloat({'#', 'P', 'r', 'c'}, 75.5))
        .with("enab", Value::boolean(true))
        .with("long", Value::integer(-5))
        .with("comp", Value::largeInteger(int64_t{1} << 40))
        .with("doub", Value::doubleValue(0.25))
        .with("Md  ", Value::enumerated(Id::fromString("BlnM"), Id::fromString("Mltp")))
        .with("artboardRect", Value::descriptor(Descriptor("classFloatRect").with("Top ", Value::doubleValue(1.0))))
        .with("list", Value::list({Value::integer(1), Value::text(UnicodeString::fromUtf8("x"))}))
        .with("null", Value::reference({
                          ReferenceItem{RefProperty{makeClass("", "Lyr "), Id::fromString("Nm  ")}},
                          ReferenceItem{RefClass{makeClass("", "Dcmn")}},
                          ReferenceItem{RefEnumerated{makeClass("", "Lyr "), Id::fromString("Ordn"), Id::fromString("Trgt")}},
                          ReferenceItem{RefOffset{makeClass("", "Lyr "), -1}},
                          ReferenceItem{RefIdentifier{makeClass("", "Lyr "), 7}},
                          ReferenceItem{RefIndex{makeClass(std::string("\0", 1), "Lyr "), 2}},
                          ReferenceItem{RefName{makeClass("", "Lyr "), UnicodeString::fromUtf8("Bg")}},
                      }))
        .with("type", Value::classValue(makeClass("c", "Clr ")))
        .with("glbc", Value::globalClass(makeClass("c", "RGBC")))
        .with("glbo", Value::globalObject(Descriptor()))
        .with("alis", Value::alias({1, 2, 3}))
        .with("tdta", Value::rawData({0xde, 0xad}))
        .with("pth ", Value::path({9, 9}))
        .with("ObAr", Value::objectArray(ObjectArray{16, Descriptor("rationalPoint").with("Hrzn", Value::unitFloats({'#', 'P', 'x', 'l'}, {1.0, 2.0}))}));
    return d;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_descriptor)

BOOST_AUTO_TEST_CASE(declared_counts_do_not_drive_preallocation) {
    // A 1M-item list of booleans parses (items grow the vector as needed).
    uint32_t n = 1000000;
    std::vector<uint8_t> data = {'V', 'l', 'L', 's'};
    putU32(data, n);
    for (uint32_t i = 0; i < n; ++i) {
        data.push_back('b');
        data.push_back('o');
        data.push_back('o');
        data.push_back('l');
        data.push_back(1);
    }
    Reader r(data);
    auto v = readValue(r, 0);
    BOOST_REQUIRE(bool(v));
    BOOST_REQUIRE(v.value().kind() == Value::Kind::List);
    BOOST_TEST(v.value().asList().size() == n);
}

BOOST_AUTO_TEST_CASE(roundtrip_all_types) {
    Descriptor d = sample();
    auto b = d.toBytes();
    auto p = Descriptor::fromBytes(b);
    BOOST_REQUIRE(bool(p));
    BOOST_TEST(p.value() == d);
    BOOST_TEST(p.value().toBytes() == b, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(photoshop_string_ids_are_written_as_strings) {
    auto strId = [](const std::string& s) {
        std::vector<uint8_t> v = {0, 0, 0, 4};
        v.insert(v.end(), s.begin(), s.end());
        return v;
    };
    auto codeId = [](const std::string& s) {
        std::vector<uint8_t> v = {0, 0, 0, 0};
        v.insert(v.end(), s.begin(), s.end());
        return v;
    };
    for (const char* name : kDescriptorStringIds) {
        BOOST_TEST(!Id::fromString(name).isCode());
        Descriptor d(name);
        d.with(name, Value::integer(1));
        auto b = d.toBytes();
        std::vector<uint8_t> classSlice(b.begin() + 4, b.begin() + 12);
        BOOST_TEST(classSlice == strId(name), boost::test_tools::per_element());
        std::vector<uint8_t> keySlice(b.begin() + 16, b.begin() + 24);
        BOOST_TEST(keySlice == strId(name), boost::test_tools::per_element());
        auto back = Descriptor::fromBytes(b);
        BOOST_REQUIRE(bool(back));
        BOOST_TEST(back.value() == d);
        BOOST_TEST(back.value().classId.is(name));
        BOOST_TEST(back.value().get(name) != nullptr);
        auto coded = b;
        auto replacement = codeId(name);
        coded.erase(coded.begin() + 16, coded.begin() + 24);
        coded.insert(coded.begin() + 16, replacement.begin(), replacement.end());
        auto back2 = Descriptor::fromBytes(coded);
        BOOST_REQUIRE(bool(back2));
        auto* gotVal = back2.value().get(name);
        BOOST_REQUIRE(gotVal != nullptr);
        BOOST_TEST(*gotVal == Value::integer(1));
        BOOST_TEST(back2.value().toBytes() == coded, boost::test_tools::per_element());
    }
    BOOST_TEST(Id::fromString("view").isCode());
    BOOST_TEST(Id::fromString("Txt ").isCode());
}

BOOST_AUTO_TEST_CASE(versioned_roundtrip) {
    auto v = VersionedDescriptor::wrap(sample());
    auto b = v.toBytes();
    BOOST_TEST(b[0] == 0);
    BOOST_TEST(b[1] == 0);
    BOOST_TEST(b[2] == 0);
    BOOST_TEST(b[3] == 16);
    auto back = VersionedDescriptor::fromBytes(b);
    BOOST_REQUIRE(bool(back));
    BOOST_TEST(back.value() == v);
}

BOOST_AUTO_TEST_CASE(versioned_parse_prefix) {
    auto v = VersionedDescriptor::wrap(sample());
    auto b = v.toBytes();
    size_t n = b.size();
    b.push_back(1);
    b.push_back(2);
    b.push_back(3);
    auto result = VersionedDescriptor::parsePrefix(b);
    BOOST_REQUIRE(bool(result));
    BOOST_TEST(result.value().first == v);
    BOOST_TEST(result.value().second == n);
}

BOOST_AUTO_TEST_CASE(versioned_rejects_other_version) {
    auto b = VersionedDescriptor::wrap(Descriptor("null")).toBytes();
    b[3] = 15;
    BOOST_TEST(!VersionedDescriptor::fromBytes(b));
}

BOOST_AUTO_TEST_CASE(id_encoding) {
    std::vector<uint8_t> v;
    Id::fromString("Nm  ").write(v);
    BOOST_TEST(v == (std::vector<uint8_t>{0, 0, 0, 0, 'N', 'm', ' ', ' '}), boost::test_tools::per_element());
    std::vector<uint8_t> v2;
    Id::fromString("abc").write(v2);
    BOOST_TEST(v2 == (std::vector<uint8_t>{0, 0, 0, 3, 'a', 'b', 'c'}), boost::test_tools::per_element());
    std::vector<uint8_t> data = {0, 0, 0, 4, 'a', 'b', 'c', 'd'};
    Reader r(data);
    auto id = Id::read(r);
    BOOST_REQUIRE(bool(id));
    BOOST_TEST(!id.value().isCode());
    std::vector<uint8_t> v3;
    id.value().write(v3);
    BOOST_TEST(v3 == data, boost::test_tools::per_element());
}

BOOST_AUTO_TEST_CASE(get_and_lookup) {
    Descriptor d = sample();
    auto* v = d.get("long");
    BOOST_REQUIRE(v != nullptr);
    BOOST_TEST(*v == Value::integer(-5));
    BOOST_TEST(d.get("nope") == nullptr);
}

BOOST_AUTO_TEST_CASE(unicode_lossy) {
    BOOST_TEST(UnicodeString::fromUtf8Nul("ab").toStringLossy() == "ab");
    UnicodeString s({0xd800, 0x41});
    BOOST_TEST(s.toStringLossy() == "\xef\xbf\xbd" "A"); // U+FFFD then 'A'
}

BOOST_AUTO_TEST_CASE(truncations_error) {
    auto b = sample().toBytes();
    for (size_t cut = 0; cut < b.size(); ++cut) {
        std::span<const uint8_t> partial(b.data(), cut);
        BOOST_TEST(!Descriptor::fromBytes(partial));
    }
}

BOOST_AUTO_TEST_CASE(trailing_bytes_error) {
    auto b = sample().toBytes();
    b.push_back(0);
    BOOST_TEST(!Descriptor::fromBytes(b));
}

BOOST_AUTO_TEST_CASE(unknown_ostype_errors) {
    auto b = Descriptor("null").toBytes();
    size_t n = b.size();
    b[n - 1] = 1; // one item
    std::vector<uint8_t> extra = {0, 0, 0, 0, 'k', 'e', 'y', ' ', '?', '?', '?', '?'};
    b.insert(b.end(), extra.begin(), extra.end());
    auto result = Descriptor::fromBytes(b);
    BOOST_REQUIRE(!result);
    BOOST_TEST(result.error().kind() == PsdError::Kind::Unsupported);
}

BOOST_AUTO_TEST_CASE(deep_nesting_is_limited) {
    Descriptor d("null");
    for (size_t i = 0; i < kDescriptorMaxDepth + 5; ++i) {
        Descriptor next("null");
        next.with("in  ", Value::descriptor(std::move(d)));
        d = std::move(next);
    }
    auto b = d.toBytes();
    auto result = Descriptor::fromBytes(b);
    BOOST_REQUIRE(!result);
    BOOST_TEST(result.error().kind() == PsdError::Kind::LimitExceeded);
}

BOOST_AUTO_TEST_CASE(huge_counts_rejected_without_alloc) {
    std::vector<uint8_t> b;
    putU32(b, 0);
    putU32(b, 0);
    put(b, std::vector<uint8_t>{'n', 'u', 'l', 'l'});
    putU32(b, UINT32_MAX);
    BOOST_TEST(!Descriptor::fromBytes(b));
}

BOOST_AUTO_TEST_SUITE_END()

// --- seeded replacement for proptests ---------------------------------------

namespace {
Id arbId(std::mt19937& rng) {
    std::uniform_int_distribution<int> coin(0, 1);
    if (coin(rng) == 0) {
        std::array<uint8_t, 4> c;
        std::uniform_int_distribution<int> byte(0, 255);
        for (auto& b : c) b = static_cast<uint8_t>(byte(rng));
        return Id::code(c);
    }
    std::uniform_int_distribution<size_t> lenDist(1, 11);
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<uint8_t> s(lenDist(rng));
    for (auto& b : s) b = static_cast<uint8_t>(byte(rng));
    return Id::str(s);
}
UnicodeString arbUstr(std::mt19937& rng) {
    std::uniform_int_distribution<size_t> lenDist(0, 7);
    std::uniform_int_distribution<int> unitDist(0, 65535);
    std::vector<uint16_t> units(lenDist(rng));
    for (auto& u : units) u = static_cast<uint16_t>(unitDist(rng));
    return UnicodeString(units);
}
Class arbClass(std::mt19937& rng) { return Class{arbUstr(rng), arbId(rng)}; }
double arbFinite(std::mt19937& rng) {
    std::uniform_real_distribution<double> d(-1e12, 1e12);
    return d(rng);
}
std::vector<uint8_t> arbBytes(std::mt19937& rng, size_t maxLen) {
    std::uniform_int_distribution<size_t> lenDist(0, maxLen);
    std::uniform_int_distribution<int> byte(0, 255);
    std::vector<uint8_t> v(lenDist(rng));
    for (auto& b : v) b = static_cast<uint8_t>(byte(rng));
    return v;
}
ReferenceItem arbRef(std::mt19937& rng) {
    std::uniform_int_distribution<int> which(0, 6);
    switch (which(rng)) {
    case 0: return ReferenceItem{RefProperty{arbClass(rng), arbId(rng)}};
    case 1: return ReferenceItem{RefClass{arbClass(rng)}};
    case 2: return ReferenceItem{RefEnumerated{arbClass(rng), arbId(rng), arbId(rng)}};
    case 3: {
        std::uniform_int_distribution<int32_t> off(INT32_MIN, INT32_MAX);
        return ReferenceItem{RefOffset{arbClass(rng), off(rng)}};
    }
    case 4: {
        std::uniform_int_distribution<uint32_t> idd;
        return ReferenceItem{RefIdentifier{arbClass(rng), idd(rng)}};
    }
    case 5: {
        std::uniform_int_distribution<uint32_t> idx;
        return ReferenceItem{RefIndex{arbClass(rng), idx(rng)}};
    }
    default: return ReferenceItem{RefName{arbClass(rng), arbUstr(rng)}};
    }
}
Value arbLeaf(std::mt19937& rng) {
    std::uniform_int_distribution<int> which(0, 12);
    switch (which(rng)) {
    case 0: return Value::doubleValue(arbFinite(rng));
    case 1: {
        std::array<uint8_t, 4> unit;
        std::uniform_int_distribution<int> byte(0, 255);
        for (auto& b : unit) b = static_cast<uint8_t>(byte(rng));
        return Value::unitFloat(unit, arbFinite(rng));
    }
    case 2: {
        std::array<uint8_t, 4> unit;
        std::uniform_int_distribution<int> byte(0, 255);
        for (auto& b : unit) b = static_cast<uint8_t>(byte(rng));
        std::uniform_int_distribution<size_t> n(0, 3);
        std::vector<double> values(n(rng));
        for (auto& v : values) v = arbFinite(rng);
        return Value::unitFloats(unit, values);
    }
    case 3: return Value::text(arbUstr(rng));
    case 4: return Value::enumerated(arbId(rng), arbId(rng));
    case 5: {
        std::uniform_int_distribution<int32_t> d(INT32_MIN, INT32_MAX);
        return Value::integer(d(rng));
    }
    case 6: {
        std::uniform_int_distribution<int64_t> d(INT64_MIN, INT64_MAX);
        return Value::largeInteger(d(rng));
    }
    case 7: {
        std::uniform_int_distribution<int> b(0, 1);
        return Value::boolean(b(rng) != 0);
    }
    case 8: return Value::classValue(arbClass(rng));
    case 9: return Value::globalClass(arbClass(rng));
    case 10: return Value::alias(arbBytes(rng, 8));
    case 11: return Value::rawData(arbBytes(rng, 8));
    default: return Value::path(arbBytes(rng, 8));
    }
}
Value arbValue(std::mt19937& rng, int depth);

// Value has no default constructor (neither does Rust's Value), so these
// build vectors by push_back/emplace_back rather than sized construction.
std::vector<std::pair<Id, Value>> arbItems(std::mt19937& rng, size_t maxCount, int depth) {
    std::uniform_int_distribution<size_t> n(0, maxCount);
    size_t count = n(rng);
    std::vector<std::pair<Id, Value>> items;
    items.reserve(count);
    for (size_t i = 0; i < count; ++i) {
        Id k = arbId(rng);
        Value v = arbValue(rng, depth);
        items.emplace_back(std::move(k), std::move(v));
    }
    return items;
}
Value arbValue(std::mt19937& rng, int depth) {
    if (depth <= 0) return arbLeaf(rng);
    std::uniform_int_distribution<int> which(0, 3);
    switch (which(rng)) {
    case 0: {
        std::uniform_int_distribution<size_t> n(0, 3);
        size_t count = n(rng);
        std::vector<Value> items;
        items.reserve(count);
        for (size_t i = 0; i < count; ++i) items.push_back(arbValue(rng, depth - 1));
        return Value::list(std::move(items));
    }
    case 1: {
        Descriptor d;
        d.name = arbUstr(rng);
        d.classId = arbId(rng);
        d.items = arbItems(rng, 3, depth - 1);
        return Value::descriptor(std::move(d));
    }
    case 2: {
        Descriptor d;
        d.name = arbUstr(rng);
        d.classId = arbId(rng);
        d.items = arbItems(rng, 2, depth - 1);
        return Value::globalObject(std::move(d));
    }
    default: {
        std::uniform_int_distribution<uint32_t> prefix;
        Descriptor body;
        body.items = arbItems(rng, 2, depth - 1);
        return Value::objectArray(ObjectArray{prefix(rng), std::move(body)});
    }
    }
}
Descriptor arbDescriptor(std::mt19937& rng) {
    Descriptor d;
    d.name = arbUstr(rng);
    d.classId = arbId(rng);
    d.items = arbItems(rng, 6, 3);
    return d;
}
} // namespace

BOOST_AUTO_TEST_SUITE(psd_descriptor_proptests)

BOOST_AUTO_TEST_CASE(descriptor_roundtrip) {
    std::mt19937 rng(0xC0FFEE);
    for (int i = 0; i < 500; ++i) {
        Descriptor d = arbDescriptor(rng);
        auto b = d.toBytes();
        auto p = Descriptor::fromBytes(b);
        BOOST_REQUIRE(bool(p));
        BOOST_TEST(p.value() == d);
        BOOST_TEST(p.value().toBytes() == b, boost::test_tools::per_element());
    }
}

BOOST_AUTO_TEST_CASE(descriptor_parse_never_panics) {
    std::mt19937 rng(0x5EED);
    std::uniform_int_distribution<size_t> lenDist(0, 255);
    std::uniform_int_distribution<int> byte(0, 255);
    for (int i = 0; i < 2000; ++i) {
        std::vector<uint8_t> data(lenDist(rng));
        for (auto& b : data) b = static_cast<uint8_t>(byte(rng));
        (void)Descriptor::fromBytes(data);
        (void)VersionedDescriptor::fromBytes(data);
    }
}

BOOST_AUTO_TEST_SUITE_END()
