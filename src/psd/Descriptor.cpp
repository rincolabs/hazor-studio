#include "Descriptor.hpp"

#include <algorithm>

namespace psd {

namespace {
constexpr size_t kMaxPrealloc = 4096;

template <typename T>
std::vector<T> vecForCount(uint32_t n) {
    std::vector<T> v;
    v.reserve(std::min<size_t>(n, kMaxPrealloc));
    return v;
}

bool tagIs(const std::array<uint8_t, 4>& tag, const char* s) { return tag[0] == uint8_t(s[0]) && tag[1] == uint8_t(s[1]) && tag[2] == uint8_t(s[2]) && tag[3] == uint8_t(s[3]); }

std::array<uint8_t, 4> tagOf(const char* s) { return {uint8_t(s[0]), uint8_t(s[1]), uint8_t(s[2]), uint8_t(s[3])}; }

std::string tagToString(const std::array<uint8_t, 4>& tag) { return std::string(reinterpret_cast<const char*>(tag.data()), 4); }

Result<Descriptor> readDescriptorImpl(Reader& r, size_t depth);
} // namespace

// --- UnicodeString -----------------------------------------------------------

Result<UnicodeString> UnicodeString::read(Reader& r) {
    PSD_TRY_ASSIGN(auto units, readUnicodeUnits(r));
    return UnicodeString(std::move(units));
}
void UnicodeString::write(std::vector<uint8_t>& out) const { writeUnicodeUnits(out, units); }

std::ostream& operator<<(std::ostream& os, const UnicodeString& s) { return os << "\"" << s.toStringLossy() << "\""; }

// --- Id -----------------------------------------------------------------------

Id Id::code(std::array<uint8_t, 4> c) {
    Id id;
    id.m_isCode = true;
    id.m_code = c;
    return id;
}
Id Id::str(std::vector<uint8_t> s) {
    Id id;
    id.m_isCode = false;
    id.m_str = std::move(s);
    return id;
}
Id Id::fromString(const std::string& s) {
    bool isStringId = std::any_of(kDescriptorStringIds.begin(), kDescriptorStringIds.end(), [&](const char* x) { return s == x; });
    if (s.size() == 4 && !isStringId) {
        return code({uint8_t(s[0]), uint8_t(s[1]), uint8_t(s[2]), uint8_t(s[3])});
    }
    return str(std::vector<uint8_t>(s.begin(), s.end()));
}

bool Id::is(const std::string& s) const {
    auto bytes = asBytes();
    return bytes.size() == s.size() && std::equal(bytes.begin(), bytes.end(), s.begin(), [](uint8_t a, char b) { return a == static_cast<uint8_t>(b); });
}

Result<Id> Id::read(Reader& r) {
    PSD_TRY_ASSIGN(auto n, r.u32());
    if (n == 0) {
        PSD_TRY_ASSIGN(auto c, r.array<4>());
        return Id::code(c);
    }
    PSD_TRY_ASSIGN(auto bytes, r.bytesU64(n));
    return Id::str(std::vector<uint8_t>(bytes.begin(), bytes.end()));
}
void Id::write(std::vector<uint8_t>& out) const {
    if (m_isCode) {
        putU32(out, 0);
        put(out, m_code);
    } else {
        putU32(out, static_cast<uint32_t>(m_str.size()));
        put(out, m_str);
    }
}

std::ostream& operator<<(std::ostream& os, const Id& id) {
    auto b = id.asBytes();
    os << "Id(\"";
    for (uint8_t c : b) os << (c >= 0x20 && c < 0x7f ? static_cast<char>(c) : '.');
    return os << "\")";
}

// --- Class --------------------------------------------------------------------

Result<Class> Class::read(Reader& r) {
    PSD_TRY_ASSIGN(auto name, UnicodeString::read(r));
    PSD_TRY_ASSIGN(auto classId, Id::read(r));
    return Class{std::move(name), std::move(classId)};
}
void Class::write(std::vector<uint8_t>& out) const {
    name.write(out);
    classId.write(out);
}

std::ostream& operator<<(std::ostream& os, const Class& c) { return os << "Class{name=" << c.name << ", classId=" << c.classId << "}"; }

// --- ReferenceItem --------------------------------------------------------------

Result<ReferenceItem> readReferenceItem(Reader& r) {
    PSD_TRY_ASSIGN(auto ty, r.array<4>());
    if (tagIs(ty, "prop")) {
        PSD_TRY_ASSIGN(auto cls, Class::read(r));
        PSD_TRY_ASSIGN(auto key, Id::read(r));
        return ReferenceItem{RefProperty{std::move(cls), std::move(key)}};
    }
    if (tagIs(ty, "Clss")) {
        PSD_TRY_ASSIGN(auto cls, Class::read(r));
        return ReferenceItem{RefClass{std::move(cls)}};
    }
    if (tagIs(ty, "Enmr")) {
        PSD_TRY_ASSIGN(auto cls, Class::read(r));
        PSD_TRY_ASSIGN(auto typeId, Id::read(r));
        PSD_TRY_ASSIGN(auto value, Id::read(r));
        return ReferenceItem{RefEnumerated{std::move(cls), std::move(typeId), std::move(value)}};
    }
    if (tagIs(ty, "rele")) {
        PSD_TRY_ASSIGN(auto cls, Class::read(r));
        PSD_TRY_ASSIGN(auto offset, r.i32());
        return ReferenceItem{RefOffset{std::move(cls), offset}};
    }
    if (tagIs(ty, "Idnt")) {
        PSD_TRY_ASSIGN(auto cls, Class::read(r));
        PSD_TRY_ASSIGN(auto id, r.u32());
        return ReferenceItem{RefIdentifier{std::move(cls), id}};
    }
    if (tagIs(ty, "indx")) {
        PSD_TRY_ASSIGN(auto cls, Class::read(r));
        PSD_TRY_ASSIGN(auto index, r.u32());
        return ReferenceItem{RefIndex{std::move(cls), index}};
    }
    if (tagIs(ty, "name")) {
        PSD_TRY_ASSIGN(auto cls, Class::read(r));
        PSD_TRY_ASSIGN(auto name, UnicodeString::read(r));
        return ReferenceItem{RefName{std::move(cls), std::move(name)}};
    }
    return PsdError::unsupported("reference item type \"" + tagToString(ty) + "\"");
}

void writeReferenceItem(const ReferenceItem& item, std::vector<uint8_t>& out) {
    std::visit(
        [&out](const auto& it) {
            using T = std::decay_t<decltype(it)>;
            if constexpr (std::is_same_v<T, RefProperty>) {
                put(out, tagOf("prop"));
                it.classRef.write(out);
                it.key.write(out);
            } else if constexpr (std::is_same_v<T, RefClass>) {
                put(out, tagOf("Clss"));
                it.classRef.write(out);
            } else if constexpr (std::is_same_v<T, RefEnumerated>) {
                put(out, tagOf("Enmr"));
                it.classRef.write(out);
                it.typeId.write(out);
                it.value.write(out);
            } else if constexpr (std::is_same_v<T, RefOffset>) {
                put(out, tagOf("rele"));
                it.classRef.write(out);
                putI32(out, it.offset);
            } else if constexpr (std::is_same_v<T, RefIdentifier>) {
                put(out, tagOf("Idnt"));
                it.classRef.write(out);
                putU32(out, it.id);
            } else if constexpr (std::is_same_v<T, RefIndex>) {
                put(out, tagOf("indx"));
                it.classRef.write(out);
                putU32(out, it.index);
            } else if constexpr (std::is_same_v<T, RefName>) {
                put(out, tagOf("name"));
                it.classRef.write(out);
                it.name.write(out);
            }
        },
        item);
}

std::ostream& operator<<(std::ostream& os, const ReferenceItem& item) {
    std::visit(
        [&os](const auto& it) {
            using T = std::decay_t<decltype(it)>;
            if constexpr (std::is_same_v<T, RefProperty>) os << "Property{class=" << it.classRef << ", key=" << it.key << "}";
            else if constexpr (std::is_same_v<T, RefClass>) os << "Class(" << it.classRef << ")";
            else if constexpr (std::is_same_v<T, RefEnumerated>) os << "Enumerated{class=" << it.classRef << ", typeId=" << it.typeId << ", value=" << it.value << "}";
            else if constexpr (std::is_same_v<T, RefOffset>) os << "Offset{class=" << it.classRef << ", offset=" << it.offset << "}";
            else if constexpr (std::is_same_v<T, RefIdentifier>) os << "Identifier{class=" << it.classRef << ", id=" << it.id << "}";
            else if constexpr (std::is_same_v<T, RefIndex>) os << "Index{class=" << it.classRef << ", index=" << it.index << "}";
            else if constexpr (std::is_same_v<T, RefName>) os << "Name{class=" << it.classRef << ", name=" << it.name << "}";
        },
        item);
    return os;
}

// --- Value --------------------------------------------------------------------

Value Value::reference(std::vector<ReferenceItem> items) { return Value(Kind::Reference, std::move(items)); }
Value Value::descriptor(Descriptor d) { return Value(Kind::DescriptorKind, Boxed<Descriptor>(std::move(d))); }
Value Value::globalObject(Descriptor d) { return Value(Kind::GlobalObject, Boxed<Descriptor>(std::move(d))); }
Value Value::list(std::vector<Value> items) { return Value(Kind::List, std::move(items)); }
Value Value::doubleValue(double d) { return Value(Kind::Double, d); }
Value Value::unitFloat(std::array<uint8_t, 4> unit, double value) { return Value(Kind::UnitFloat, UnitFloatPayload{unit, value}); }
Value Value::unitFloats(std::array<uint8_t, 4> unit, std::vector<double> values) { return Value(Kind::UnitFloats, UnitFloatsPayload{unit, std::move(values)}); }
Value Value::text(UnicodeString s) { return Value(Kind::Text, std::move(s)); }
Value Value::enumerated(Id typeId, Id value) { return Value(Kind::Enumerated, EnumeratedPayload{std::move(typeId), std::move(value)}); }
Value Value::integer(int32_t v) { return Value(Kind::Integer, v); }
Value Value::largeInteger(int64_t v) { return Value(Kind::LargeInteger, v); }
Value Value::boolean(bool v) { return Value(Kind::Boolean, v); }
Value Value::classValue(Class c) { return Value(Kind::Class, std::move(c)); }
Value Value::globalClass(Class c) { return Value(Kind::GlobalClass, std::move(c)); }
Value Value::alias(std::vector<uint8_t> b) { return Value(Kind::Alias, std::move(b)); }
Value Value::path(std::vector<uint8_t> b) { return Value(Kind::Path, std::move(b)); }
Value Value::rawData(std::vector<uint8_t> b) { return Value(Kind::RawData, std::move(b)); }
Value Value::objectArray(ObjectArray o) { return Value(Kind::ObjectArray, Boxed<ObjectArray>(std::move(o))); }

std::array<uint8_t, 4> Value::typeCode() const {
    switch (m_kind) {
    case Kind::Reference: return tagOf("obj ");
    case Kind::DescriptorKind: return tagOf("Objc");
    case Kind::GlobalObject: return tagOf("GlbO");
    case Kind::List: return tagOf("VlLs");
    case Kind::Double: return tagOf("doub");
    case Kind::UnitFloat: return tagOf("UntF");
    case Kind::UnitFloats: return tagOf("UnFl");
    case Kind::Text: return tagOf("TEXT");
    case Kind::Enumerated: return tagOf("enum");
    case Kind::Integer: return tagOf("long");
    case Kind::LargeInteger: return tagOf("comp");
    case Kind::Boolean: return tagOf("bool");
    case Kind::Class: return tagOf("type");
    case Kind::GlobalClass: return tagOf("GlbC");
    case Kind::Alias: return tagOf("alis");
    case Kind::Path: return tagOf("Pth ");
    case Kind::RawData: return tagOf("tdta");
    case Kind::ObjectArray: return tagOf("ObAr");
    }
    return {0, 0, 0, 0};
}

void Value::write(std::vector<uint8_t>& out) const {
    put(out, typeCode());
    switch (m_kind) {
    case Kind::Reference: {
        auto& items = asReference();
        putU32(out, static_cast<uint32_t>(items.size()));
        for (auto& it : items) writeReferenceItem(it, out);
        break;
    }
    case Kind::DescriptorKind:
    case Kind::GlobalObject:
        asDescriptor().write(out);
        break;
    case Kind::List: {
        auto& items = asList();
        putU32(out, static_cast<uint32_t>(items.size()));
        for (auto& it : items) it.write(out);
        break;
    }
    case Kind::Double: putF64(out, asDouble()); break;
    case Kind::UnitFloat: {
        auto& p = asUnitFloat();
        put(out, p.unit);
        putF64(out, p.value);
        break;
    }
    case Kind::UnitFloats: {
        auto& p = asUnitFloats();
        put(out, p.unit);
        putU32(out, static_cast<uint32_t>(p.values.size()));
        for (double v : p.values) putF64(out, v);
        break;
    }
    case Kind::Text: asText().write(out); break;
    case Kind::Enumerated: {
        auto& p = asEnumerated();
        p.typeId.write(out);
        p.value.write(out);
        break;
    }
    case Kind::Integer: putI32(out, asInteger()); break;
    case Kind::LargeInteger: putI64(out, asLargeInteger()); break;
    case Kind::Boolean: putU8(out, asBoolean() ? 1 : 0); break;
    case Kind::Class:
    case Kind::GlobalClass:
        asClass().write(out);
        break;
    case Kind::Alias:
    case Kind::Path:
    case Kind::RawData: {
        auto& b = asBytes();
        putU32(out, static_cast<uint32_t>(b.size()));
        put(out, b);
        break;
    }
    case Kind::ObjectArray: {
        auto& o = asObjectArray();
        putU32(out, o.prefix);
        o.body.write(out);
        break;
    }
    }
}

namespace {
Result<Value> readTypedValue(Reader& r, const std::array<uint8_t, 4>& ty, size_t depth) {
    if (tagIs(ty, "obj ")) {
        PSD_TRY_ASSIGN(auto n, r.u32());
        PSD_TRY(r.checkCount(n, 8));
        auto items = vecForCount<ReferenceItem>(n);
        for (uint32_t i = 0; i < n; ++i) {
            PSD_TRY_ASSIGN(auto it, readReferenceItem(r));
            items.push_back(std::move(it));
        }
        return Value::reference(std::move(items));
    }
    if (tagIs(ty, "Objc")) {
        PSD_TRY_ASSIGN(auto d, readDescriptorImpl(r, depth + 1));
        return Value::descriptor(std::move(d));
    }
    if (tagIs(ty, "GlbO")) {
        PSD_TRY_ASSIGN(auto d, readDescriptorImpl(r, depth + 1));
        return Value::globalObject(std::move(d));
    }
    if (tagIs(ty, "VlLs")) {
        PSD_TRY_ASSIGN(auto n, r.u32());
        PSD_TRY(r.checkCount(n, 4));
        auto items = vecForCount<Value>(n);
        for (uint32_t i = 0; i < n; ++i) {
            PSD_TRY_ASSIGN(auto v, readValue(r, depth + 1));
            items.push_back(std::move(v));
        }
        return Value::list(std::move(items));
    }
    if (tagIs(ty, "doub")) {
        PSD_TRY_ASSIGN(auto d, r.f64());
        return Value::doubleValue(d);
    }
    if (tagIs(ty, "UntF")) {
        PSD_TRY_ASSIGN(auto unit, r.array<4>());
        PSD_TRY_ASSIGN(auto value, r.f64());
        return Value::unitFloat(unit, value);
    }
    if (tagIs(ty, "UnFl")) {
        PSD_TRY_ASSIGN(auto unit, r.array<4>());
        PSD_TRY_ASSIGN(auto n, r.u32());
        PSD_TRY(r.checkCount(n, 8));
        auto values = vecForCount<double>(n);
        for (uint32_t i = 0; i < n; ++i) {
            PSD_TRY_ASSIGN(auto v, r.f64());
            values.push_back(v);
        }
        return Value::unitFloats(unit, std::move(values));
    }
    if (tagIs(ty, "TEXT")) {
        PSD_TRY_ASSIGN(auto s, UnicodeString::read(r));
        return Value::text(std::move(s));
    }
    if (tagIs(ty, "enum")) {
        PSD_TRY_ASSIGN(auto typeId, Id::read(r));
        PSD_TRY_ASSIGN(auto value, Id::read(r));
        return Value::enumerated(std::move(typeId), std::move(value));
    }
    if (tagIs(ty, "long")) {
        PSD_TRY_ASSIGN(auto v, r.i32());
        return Value::integer(v);
    }
    if (tagIs(ty, "comp")) {
        PSD_TRY_ASSIGN(auto v, r.i64());
        return Value::largeInteger(v);
    }
    if (tagIs(ty, "bool")) {
        PSD_TRY_ASSIGN(auto v, r.u8());
        return Value::boolean(v != 0);
    }
    if (tagIs(ty, "type")) {
        PSD_TRY_ASSIGN(auto c, Class::read(r));
        return Value::classValue(std::move(c));
    }
    if (tagIs(ty, "GlbC")) {
        PSD_TRY_ASSIGN(auto c, Class::read(r));
        return Value::globalClass(std::move(c));
    }
    if (tagIs(ty, "alis")) {
        PSD_TRY_ASSIGN(auto n, r.u32());
        PSD_TRY_ASSIGN(auto b, r.bytesU64(n));
        return Value::alias(std::vector<uint8_t>(b.begin(), b.end()));
    }
    if (tagIs(ty, "Pth ")) {
        PSD_TRY_ASSIGN(auto n, r.u32());
        PSD_TRY_ASSIGN(auto b, r.bytesU64(n));
        return Value::path(std::vector<uint8_t>(b.begin(), b.end()));
    }
    if (tagIs(ty, "tdta")) {
        PSD_TRY_ASSIGN(auto n, r.u32());
        PSD_TRY_ASSIGN(auto b, r.bytesU64(n));
        return Value::rawData(std::vector<uint8_t>(b.begin(), b.end()));
    }
    if (tagIs(ty, "ObAr")) {
        PSD_TRY_ASSIGN(auto prefix, r.u32());
        PSD_TRY_ASSIGN(auto body, readDescriptorImpl(r, depth + 1));
        return Value::objectArray(ObjectArray{prefix, std::move(body)});
    }
    return PsdError::unsupported("descriptor OSType \"" + tagToString(ty) + "\"");
}
} // namespace

Result<Value> readValue(Reader& r, size_t depth) {
    if (depth > kDescriptorMaxDepth) return PsdError::limitExceeded("descriptor nesting too deep");
    PSD_TRY_ASSIGN(auto ty, r.array<4>());
    return readTypedValue(r, ty, depth);
}

std::ostream& operator<<(std::ostream& os, const Value& v) {
    switch (v.kind()) {
    case Value::Kind::Reference: {
        os << "Reference[";
        bool first = true;
        for (auto& it : v.asReference()) {
            if (!first) os << ", ";
            first = false;
            os << it;
        }
        return os << "]";
    }
    case Value::Kind::DescriptorKind: return os << "Descriptor(" << v.asDescriptor() << ")";
    case Value::Kind::GlobalObject: return os << "GlobalObject(" << v.asDescriptor() << ")";
    case Value::Kind::List: {
        os << "List[";
        bool first = true;
        for (auto& it : v.asList()) {
            if (!first) os << ", ";
            first = false;
            os << it;
        }
        return os << "]";
    }
    case Value::Kind::Double: return os << "Double(" << v.asDouble() << ")";
    case Value::Kind::UnitFloat: return os << "UnitFloat(" << v.asUnitFloat().value << ")";
    case Value::Kind::UnitFloats: return os << "UnitFloats(n=" << v.asUnitFloats().values.size() << ")";
    case Value::Kind::Text: return os << "Text(" << v.asText() << ")";
    case Value::Kind::Enumerated: return os << "Enumerated{typeId=" << v.asEnumerated().typeId << ", value=" << v.asEnumerated().value << "}";
    case Value::Kind::Integer: return os << "Integer(" << v.asInteger() << ")";
    case Value::Kind::LargeInteger: return os << "LargeInteger(" << v.asLargeInteger() << ")";
    case Value::Kind::Boolean: return os << "Boolean(" << (v.asBoolean() ? "true" : "false") << ")";
    case Value::Kind::Class: return os << "Class(" << v.asClass() << ")";
    case Value::Kind::GlobalClass: return os << "GlobalClass(" << v.asClass() << ")";
    case Value::Kind::Alias: return os << "Alias(n=" << v.asBytes().size() << ")";
    case Value::Kind::Path: return os << "Path(n=" << v.asBytes().size() << ")";
    case Value::Kind::RawData: return os << "RawData(n=" << v.asBytes().size() << ")";
    case Value::Kind::ObjectArray: return os << "ObjectArray(" << v.asObjectArray() << ")";
    }
    return os << "?";
}

// --- Descriptor -----------------------------------------------------------------

namespace {
Result<Descriptor> readDescriptorImpl(Reader& r, size_t depth) {
    if (depth > kDescriptorMaxDepth) return PsdError::limitExceeded("descriptor nesting too deep");
    PSD_TRY_ASSIGN(auto name, UnicodeString::read(r));
    PSD_TRY_ASSIGN(auto classId, Id::read(r));
    PSD_TRY_ASSIGN(auto n, r.u32());
    // Each item needs at least 5 (1-byte string key) + 4 (type) + 1 (bool).
    PSD_TRY(r.checkCount(n, 10));
    auto items = vecForCount<std::pair<Id, Value>>(n);
    for (uint32_t i = 0; i < n; ++i) {
        PSD_TRY_ASSIGN(auto key, Id::read(r));
        PSD_TRY_ASSIGN(auto v, readValue(r, depth + 1));
        items.emplace_back(std::move(key), std::move(v));
    }
    Descriptor d;
    d.name = std::move(name);
    d.classId = std::move(classId);
    d.items = std::move(items);
    return d;
}
} // namespace

Result<Descriptor> Descriptor::fromBytes(std::span<const uint8_t> data) {
    Reader r(data);
    PSD_TRY_ASSIGN(auto d, Descriptor::read(r));
    if (!r.isEmpty()) return PsdError::invalid("trailing bytes after descriptor");
    return d;
}

std::vector<uint8_t> Descriptor::toBytes() const {
    std::vector<uint8_t> v;
    write(v);
    return v;
}

Result<Descriptor> Descriptor::read(Reader& r) { return readDescriptorImpl(r, 0); }

void Descriptor::write(std::vector<uint8_t>& out) const {
    name.write(out);
    classId.write(out);
    putU32(out, static_cast<uint32_t>(items.size()));
    for (auto& [k, v] : items) {
        k.write(out);
        v.write(out);
    }
}

std::ostream& operator<<(std::ostream& os, const Descriptor& d) {
    os << "Descriptor{name=" << d.name << ", classId=" << d.classId << ", items=[";
    bool first = true;
    for (auto& [k, v] : d.items) {
        if (!first) os << ", ";
        first = false;
        os << k << "=" << v;
    }
    return os << "]}";
}

std::ostream& operator<<(std::ostream& os, const ObjectArray& o) { return os << "ObjectArray{prefix=" << o.prefix << ", body=" << o.body << "}"; }

// --- VersionedDescriptor ----------------------------------------------------------

Result<VersionedDescriptor> VersionedDescriptor::fromBytes(std::span<const uint8_t> data) {
    Reader r(data);
    PSD_TRY_ASSIGN(auto v, VersionedDescriptor::read(r));
    if (!r.isEmpty()) return PsdError::invalid("trailing bytes after versioned descriptor");
    return v;
}

Result<std::pair<VersionedDescriptor, size_t>> VersionedDescriptor::parsePrefix(std::span<const uint8_t> data) {
    Reader r(data);
    PSD_TRY_ASSIGN(auto v, VersionedDescriptor::read(r));
    return std::make_pair(std::move(v), r.pos());
}

std::vector<uint8_t> VersionedDescriptor::toBytes() const {
    std::vector<uint8_t> v;
    write(v);
    return v;
}

Result<VersionedDescriptor> VersionedDescriptor::read(Reader& r) {
    PSD_TRY_ASSIGN(auto version, r.u32());
    if (version != 16) return PsdError::invalid("descriptor version " + std::to_string(version) + ", expected 16");
    PSD_TRY_ASSIGN(auto d, Descriptor::read(r));
    return VersionedDescriptor{version, std::move(d)};
}

void VersionedDescriptor::write(std::vector<uint8_t>& out) const {
    putU32(out, version);
    descriptor.write(out);
}

std::ostream& operator<<(std::ostream& os, const VersionedDescriptor& v) { return os << "VersionedDescriptor{version=" << v.version << ", descriptor=" << v.descriptor << "}"; }

} // namespace psd
