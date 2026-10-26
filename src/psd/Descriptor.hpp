#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "Error.hpp"
#include "Io.hpp"

// Photoshop ActionDescriptor structures (OSType-tagged key/value trees).
// See PROVENANCE.md for origin details.
//
// These appear inside many tagged blocks (lfx2, SoCo, GdFl, TySh, PlLd, ...),
// usually wrapped in a VersionedDescriptor (version 16). Parsing and writing
// are exact: write(parse(b)) == b for well-formed input.

namespace psd {

// Maximum nesting depth accepted when parsing (fuzz safety).
inline constexpr size_t kDescriptorMaxDepth = 64;

// A Photoshop Unicode string: UTF-16 code units, preserved exactly
// (including any trailing NUL and unpaired surrogates).
struct UnicodeString {
    std::vector<uint16_t> units;

    UnicodeString() = default;
    explicit UnicodeString(std::vector<uint16_t> u) : units(std::move(u)) {}

    // Encodes `s` as UTF-16 without a trailing NUL.
    static UnicodeString fromUtf8(const std::string& s) { return UnicodeString(utf8ToUtf16(s)); }
    // Encodes `s` as UTF-16 followed by a NUL terminator (as Photoshop commonly writes in descriptors).
    static UnicodeString fromUtf8Nul(const std::string& s) {
        auto v = utf8ToUtf16(s);
        v.push_back(0);
        return UnicodeString(std::move(v));
    }
    // Lossy conversion, stripping trailing NULs.
    std::string toStringLossy() const {
        size_t end = units.size();
        while (end > 0 && units[end - 1] == 0) end -= 1;
        return utf16ToUtf8Lossy(std::span<const uint16_t>(units.data(), end));
    }

    static Result<UnicodeString> read(Reader& r);
    void write(std::vector<uint8_t>& out) const;

    bool operator==(const UnicodeString&) const = default;
};
std::ostream& operator<<(std::ostream& os, const UnicodeString& s);

// Four-character class and key ids Photoshop writes as length-4 strings,
// never as codes ("00000004 'warp'", not "00000000 'warp'").
inline constexpr std::array<const char*, 3> kDescriptorStringIds = {"warp", "list", "time"};

// A class / key / type identifier: a length-prefixed string, where a stored
// length of 0 means a 4-character code follows.
class Id {
public:
    // Defaults to Code([0,0,0,0]) via the in-class member initializers below.
    Id() = default;
    static Id code(std::array<uint8_t, 4> c);
    static Id str(std::vector<uint8_t> s);
    // Convenience constructor: 4-byte strings become a code, others a
    // string. The four-character names Photoshop stores as length-4
    // strings (kDescriptorStringIds) are strings too: Photoshop treats the
    // string and the code as different keys and doesn't recognise the code form.
    static Id fromString(const std::string& s);

    bool isCode() const { return m_isCode; }
    // The identifier's bytes.
    std::span<const uint8_t> asBytes() const { return m_isCode ? std::span<const uint8_t>(m_code) : std::span<const uint8_t>(m_str); }
    // `true` if the identifier's bytes equal `s`.
    bool is(const std::string& s) const;

    static Result<Id> read(Reader& r);
    void write(std::vector<uint8_t>& out) const;

    bool operator==(const Id& other) const { return asBytes().size() == other.asBytes().size() && std::ranges::equal(asBytes(), other.asBytes()); }
    bool operator!=(const Id& other) const { return !(*this == other); }

private:
    bool m_isCode = true;
    std::array<uint8_t, 4> m_code{};
    std::vector<uint8_t> m_str;
};
std::ostream& operator<<(std::ostream& os, const Id& id);

// A class reference: display name + class id.
struct Class {
    UnicodeString name;
    Id classId;

    static Result<Class> read(Reader& r);
    void write(std::vector<uint8_t>& out) const;
    bool operator==(const Class&) const = default;
};
std::ostream& operator<<(std::ostream& os, const Class& c);

// One element of an 'obj ' reference.
struct RefProperty {
    Class classRef;
    Id key;
    bool operator==(const RefProperty&) const = default;
};
struct RefClass {
    Class classRef;
    bool operator==(const RefClass&) const = default;
};
struct RefEnumerated {
    Class classRef;
    Id typeId;
    Id value;
    bool operator==(const RefEnumerated&) const = default;
};
struct RefOffset {
    Class classRef;
    int32_t offset = 0;
    bool operator==(const RefOffset&) const = default;
};
struct RefIdentifier {
    Class classRef;
    uint32_t id = 0;
    bool operator==(const RefIdentifier&) const = default;
};
struct RefIndex {
    Class classRef;
    uint32_t index = 0;
    bool operator==(const RefIndex&) const = default;
};
struct RefName {
    Class classRef;
    UnicodeString name;
    bool operator==(const RefName&) const = default;
};
// `prop` / `Clss` / `Enmr` / `rele` / `Idnt` / `indx` / `name`.
using ReferenceItem = std::variant<RefProperty, RefClass, RefEnumerated, RefOffset, RefIdentifier, RefIndex, RefName>;

Result<ReferenceItem> readReferenceItem(Reader& r);
void writeReferenceItem(const ReferenceItem& item, std::vector<uint8_t>& out);
std::ostream& operator<<(std::ostream& os, const ReferenceItem& item);

class Descriptor; // Value holds one by (heap) value; see Boxed<T> below.

// A small owning, deep-copying indirection: this project targets C++20 (no
// polymorphic_value), and breaks the Value <-> Descriptor mutual-recursion
// cycle the same way Rust's Box<Descriptor> would (Rust's recursive enum
// needs no such wrapper only because Vec<T>/Box<T> already imply it).
template <typename T>
class Boxed {
public:
    Boxed(T value) : m_ptr(std::make_unique<T>(std::move(value))) {}
    Boxed(const Boxed& other) : m_ptr(std::make_unique<T>(*other.m_ptr)) {}
    Boxed(Boxed&&) noexcept = default;
    Boxed& operator=(const Boxed& other) {
        if (this != &other) m_ptr = std::make_unique<T>(*other.m_ptr);
        return *this;
    }
    Boxed& operator=(Boxed&&) noexcept = default;
    T& operator*() { return *m_ptr; }
    const T& operator*() const { return *m_ptr; }
    T* operator->() { return m_ptr.get(); }
    const T* operator->() const { return m_ptr.get(); }
    bool operator==(const Boxed& other) const { return *m_ptr == *other.m_ptr; }

private:
    std::unique_ptr<T> m_ptr;
};

struct ObjectArray; // defined after Descriptor; Value boxes it too.

// A descriptor value, tagged by its OSType.
class Value {
public:
    enum class Kind {
        Reference,     // `obj `
        DescriptorKind, // `Objc`
        GlobalObject,  // `GlbO`
        List,          // `VlLs`
        Double,        // `doub`
        UnitFloat,     // `UntF`: unit (#Ang, #Rsl, #Rlt, #Nne, #Prc, #Pxl, ...) + value.
        UnitFloats,    // `UnFl`: unit + array of doubles (used in ObAr).
        Text,          // `TEXT`
        Enumerated,    // `enum`
        Integer,       // `long`
        LargeInteger,  // `comp`
        Boolean,       // `bool`
        Class,         // `type`
        GlobalClass,   // `GlbC`
        Alias,         // `alis`: raw alias data.
        Path,          // `Pth `: raw file path data (length-prefixed).
        RawData,       // `tdta`: raw data.
        ObjectArray,   // `ObAr`: object array (best effort).
    };

    struct UnitFloatPayload {
        std::array<uint8_t, 4> unit{};
        double value = 0;
        bool operator==(const UnitFloatPayload&) const = default;
    };
    struct UnitFloatsPayload {
        std::array<uint8_t, 4> unit{};
        std::vector<double> values;
        bool operator==(const UnitFloatsPayload&) const = default;
    };
    struct EnumeratedPayload {
        Id typeId;
        Id value;
        bool operator==(const EnumeratedPayload&) const = default;
    };

    static Value reference(std::vector<ReferenceItem> items);
    static Value descriptor(Descriptor d);
    static Value globalObject(Descriptor d);
    static Value list(std::vector<Value> items);
    static Value doubleValue(double d);
    static Value unitFloat(std::array<uint8_t, 4> unit, double value);
    static Value unitFloats(std::array<uint8_t, 4> unit, std::vector<double> values);
    static Value text(UnicodeString s);
    static Value enumerated(Id typeId, Id value);
    static Value integer(int32_t v);
    static Value largeInteger(int64_t v);
    static Value boolean(bool v);
    static Value classValue(Class c);
    static Value globalClass(Class c);
    static Value alias(std::vector<uint8_t> b);
    static Value path(std::vector<uint8_t> b);
    static Value rawData(std::vector<uint8_t> b);
    static Value objectArray(ObjectArray o);

    Kind kind() const { return m_kind; }

    const std::vector<ReferenceItem>& asReference() const { return std::get<std::vector<ReferenceItem>>(m_payload); }
    const Descriptor& asDescriptor() const { return *std::get<Boxed<Descriptor>>(m_payload); }
    const std::vector<Value>& asList() const { return std::get<std::vector<Value>>(m_payload); }
    double asDouble() const { return std::get<double>(m_payload); }
    const UnitFloatPayload& asUnitFloat() const { return std::get<UnitFloatPayload>(m_payload); }
    const UnitFloatsPayload& asUnitFloats() const { return std::get<UnitFloatsPayload>(m_payload); }
    const UnicodeString& asText() const { return std::get<UnicodeString>(m_payload); }
    const EnumeratedPayload& asEnumerated() const { return std::get<EnumeratedPayload>(m_payload); }
    int32_t asInteger() const { return std::get<int32_t>(m_payload); }
    int64_t asLargeInteger() const { return std::get<int64_t>(m_payload); }
    bool asBoolean() const { return std::get<bool>(m_payload); }
    const Class& asClass() const { return std::get<Class>(m_payload); }
    const std::vector<uint8_t>& asBytes() const { return std::get<std::vector<uint8_t>>(m_payload); }
    const ObjectArray& asObjectArray() const { return *std::get<Boxed<ObjectArray>>(m_payload); }

    std::array<uint8_t, 4> typeCode() const;
    void write(std::vector<uint8_t>& out) const;

    bool operator==(const Value& other) const { return m_kind == other.m_kind && m_payload == other.m_payload; }
    bool operator!=(const Value& other) const { return !(*this == other); }

private:
    using Payload = std::variant<std::vector<ReferenceItem>, Boxed<Descriptor>, std::vector<Value>, double, UnitFloatPayload, UnitFloatsPayload,
                                  UnicodeString, EnumeratedPayload, int32_t, int64_t, bool, Class, std::vector<uint8_t>, Boxed<ObjectArray>>;
    Value(Kind kind, Payload payload) : m_kind(kind), m_payload(std::move(payload)) {}
    Kind m_kind;
    Payload m_payload;
};
std::ostream& operator<<(std::ostream& os, const Value& v);

Result<Value> readValue(Reader& r, size_t depth);

// An ActionDescriptor: class plus ordered key/value items.
class Descriptor {
public:
    UnicodeString name;
    Id classId;
    std::vector<std::pair<Id, Value>> items;

    Descriptor() : Descriptor("null") {}
    explicit Descriptor(const std::string& classId_) : classId(Id::fromString(classId_)) {}

    // Builder-style item append.
    Descriptor& with(const std::string& key, Value value) {
        items.emplace_back(Id::fromString(key), std::move(value));
        return *this;
    }
    // Looks up the first item with key `key`.
    const Value* get(const std::string& key) const {
        for (auto& [k, v] : items)
            if (k.is(key)) return &v;
        return nullptr;
    }

    // Parses a descriptor from `data`, which must be consumed entirely.
    static Result<Descriptor> fromBytes(std::span<const uint8_t> data);
    // Serializes the descriptor.
    std::vector<uint8_t> toBytes() const;

    static Result<Descriptor> read(Reader& r);
    void write(std::vector<uint8_t>& out) const;

    bool operator==(const Descriptor&) const = default;
};
std::ostream& operator<<(std::ostream& os, const Descriptor& d);

// `ObAr` object array. Undocumented in the Adobe spec; the layout follows
// ag-psd (MIT): a u32 (observed as the element count / version 16) followed
// by a descriptor-shaped body whose values are usually `UnFl`.
struct ObjectArray {
    uint32_t prefix = 0;
    Descriptor body;
    bool operator==(const ObjectArray&) const = default;
};
std::ostream& operator<<(std::ostream& os, const ObjectArray& o);

// A descriptor prefixed by a u32 version (16 in all current Photoshop files).
struct VersionedDescriptor {
    uint32_t version = 16;
    Descriptor descriptor;

    // Wraps a descriptor with version 16.
    static VersionedDescriptor wrap(Descriptor d) { return VersionedDescriptor{16, std::move(d)}; }
    // Parses from bytes (must be fully consumed).
    static Result<VersionedDescriptor> fromBytes(std::span<const uint8_t> data);
    // Parses a versioned descriptor at the start of `data`, returning it and
    // the number of bytes consumed (trailing data is allowed, e.g. the warp
    // descriptor after the text descriptor in TySh).
    static Result<std::pair<VersionedDescriptor, size_t>> parsePrefix(std::span<const uint8_t> data);
    std::vector<uint8_t> toBytes() const;

    static Result<VersionedDescriptor> read(Reader& r);
    void write(std::vector<uint8_t>& out) const;

    bool operator==(const VersionedDescriptor&) const = default;
};
std::ostream& operator<<(std::ostream& os, const VersionedDescriptor& v);

} // namespace psd
