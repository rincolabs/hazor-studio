#include "Tagged.hpp"

#include <algorithm>
#include <cstring>

namespace psd {

bool usesLongLength(Version version, std::array<uint8_t, 4> key) {
    if (!isPsb(version)) return false;
    return std::any_of(kPsbLongKeys.begin(), kPsbLongKeys.end(), [&](const auto& k) { return k == key; });
}

namespace {
size_t defaultPadding(size_t len, size_t alignment) { return (alignment - len % alignment) % alignment; }

std::array<uint8_t, 4> tag4(const char* s) { return {uint8_t(s[0]), uint8_t(s[1]), uint8_t(s[2]), uint8_t(s[3])}; }
bool tagIs(const std::array<uint8_t, 4>& k, const char* s) { return k == tag4(s); }
} // namespace

TaggedBlock TaggedBlock::make(std::array<uint8_t, 4> key, std::vector<uint8_t> data) {
    TaggedBlock b;
    b.signature = tag4("8BIM");
    b.key = key;
    b.data = std::move(data);
    b.padding = std::nullopt;
    return b;
}

bool TaggedBlock::isForeignOrderSignature(std::span<const uint8_t> b) {
    auto starts = [&](const char* s) { return b.size() >= 4 && std::memcmp(b.data(), s, 4) == 0; };
    return starts("MIB8") || starts("46B8");
}

std::string TaggedBlock::keyStr() const { return utf8BytesToStringLossy(key); }

namespace {
Result<SectionDivider> parseSection(std::span<const uint8_t> d);
}

std::optional<Result<TaggedBlock::BlockData>> TaggedBlock::parsed() const {
    using R = Result<BlockData>;
    Reader r(data);
    if (tagIs(key, "luni")) {
        auto s = UnicodeString::read(r);
        if (!s) return R(s.error());
        return R(BlockData::unicodeName(s.value().toStringLossy()));
    }
    if (tagIs(key, "lsct") || tagIs(key, "lsdk")) {
        auto s = parseSection(data);
        if (!s) return R(s.error());
        return R(BlockData::sectionDivider(s.value()));
    }
    if (tagIs(key, "lyid")) {
        auto v = r.u32();
        if (!v) return R(v.error());
        return R(BlockData::layerId(v.value()));
    }
    if (tagIs(key, "lnsr")) {
        auto v = r.array<4>();
        if (!v) return R(v.error());
        return R(BlockData::nameSource(v.value()));
    }
    if (tagIs(key, "clbl")) {
        auto v = r.u8();
        if (!v) return R(v.error());
        return R(BlockData::blendClippedAsGroup(v.value() != 0));
    }
    if (tagIs(key, "infx")) {
        auto v = r.u8();
        if (!v) return R(v.error());
        return R(BlockData::blendInteriorElements(v.value() != 0));
    }
    if (tagIs(key, "knko")) {
        auto v = r.u8();
        if (!v) return R(v.error());
        return R(BlockData::knockout(v.value()));
    }
    if (tagIs(key, "tsly")) {
        auto v = r.u8();
        if (!v) return R(v.error());
        return R(BlockData::transparencyShapes(v.value() != 0));
    }
    if (tagIs(key, "lmgm")) {
        auto v = r.u8();
        if (!v) return R(v.error());
        return R(BlockData::layerMaskHidesEffects(v.value() != 0));
    }
    if (tagIs(key, "vmgm")) {
        auto v = r.u8();
        if (!v) return R(v.error());
        return R(BlockData::vectorMaskHidesEffects(v.value() != 0));
    }
    if (tagIs(key, "lspf")) {
        auto v = r.u32();
        if (!v) return R(v.error());
        return R(BlockData::protection(v.value()));
    }
    if (tagIs(key, "lclr")) {
        auto v = r.u16();
        if (!v) return R(v.error());
        return R(BlockData::sheetColor(v.value()));
    }
    if (tagIs(key, "iOpa")) {
        auto v = r.u8();
        if (!v) return R(v.error());
        return R(BlockData::fillOpacity(v.value()));
    }
    if (tagIs(key, "shmd")) {
        return R(BlockData::metadataSetting(data));
    }
    return std::nullopt;
}

TaggedBlock TaggedBlock::unicodeName(const std::string& name) {
    std::vector<uint8_t> d;
    UnicodeString::fromUtf8(name).write(d);
    while (d.size() % 4 != 0) d.push_back(0);
    return make(tag4("luni"), std::move(d));
}

TaggedBlock TaggedBlock::sectionDivider(SectionType kind, std::optional<BlendMode> blendMode, std::optional<uint32_t> subType) {
    std::vector<uint8_t> d;
    putU32(d, kind.asU32());
    if (blendMode.has_value()) {
        put(d, tag4("8BIM"));
        put(d, blendMode->key());
        if (subType.has_value()) putU32(d, *subType);
    }
    return make(tag4("lsct"), std::move(d));
}

TaggedBlock TaggedBlock::layerId(uint32_t id) {
    std::vector<uint8_t> d;
    putU32(d, id);
    return make(tag4("lyid"), std::move(d));
}

TaggedBlock TaggedBlock::nameSource(std::array<uint8_t, 4> src) { return make(tag4("lnsr"), std::vector<uint8_t>(src.begin(), src.end())); }

namespace {
TaggedBlock flagBlock(std::array<uint8_t, 4> key, uint8_t v) { return TaggedBlock::make(key, std::vector<uint8_t>{v, 0, 0, 0}); }
} // namespace

TaggedBlock TaggedBlock::blendClippedAsGroup(bool v) { return flagBlock(tag4("clbl"), v ? 1 : 0); }
TaggedBlock TaggedBlock::blendInteriorElements(bool v) { return flagBlock(tag4("infx"), v ? 1 : 0); }
TaggedBlock TaggedBlock::knockout(uint8_t v) { return flagBlock(tag4("knko"), v); }
TaggedBlock TaggedBlock::transparencyShapes(bool v) { return flagBlock(tag4("tsly"), v ? 1 : 0); }
TaggedBlock TaggedBlock::layerMaskHidesEffects(bool v) { return flagBlock(tag4("lmgm"), v ? 1 : 0); }
TaggedBlock TaggedBlock::vectorMaskHidesEffects(bool v) { return flagBlock(tag4("vmgm"), v ? 1 : 0); }

TaggedBlock TaggedBlock::protection(uint32_t flags) {
    std::vector<uint8_t> d;
    putU32(d, flags);
    return make(tag4("lspf"), std::move(d));
}

TaggedBlock TaggedBlock::sheetColor(uint16_t color) {
    std::vector<uint8_t> d;
    putU16(d, color);
    d.insert(d.end(), 6, 0);
    return make(tag4("lclr"), std::move(d));
}

TaggedBlock TaggedBlock::fillOpacity(uint8_t v) { return flagBlock(tag4("iOpa"), v); }

namespace {

Result<void> expectKind(Reader& r, const char* kind) {
    PSD_TRY_ASSIGN(auto found, r.array<4>());
    if (found != tag4(kind)) {
        return PsdError::invalid("expected " + std::string(kind) + " structure, found " + utf8BytesToStringLossy(found));
    }
    return {};
}

// Strict check of a `soLD` descriptor: the unique id and transform Photoshop needs to place the
// layer, and a well-formed smart filter stack.
Result<void> checkPlacedDescriptor(const Descriptor& d) {
    auto* idnt = d.get("Idnt");
    if (!idnt || idnt->kind() != Value::Kind::Text) return PsdError::invalid("no Idnt (unique id)");
    auto* trnf = d.get("Trnf");
    bool trnfOk = trnf && trnf->kind() == Value::Kind::List && trnf->asList().size() == 8 &&
                  std::all_of(trnf->asList().begin(), trnf->asList().end(), [](const Value& v) { return v.kind() == Value::Kind::Double; });
    if (!trnfOk) return PsdError::invalid("Trnf is not eight doubles");
    auto* fx = d.get("filterFX");
    if (!fx) return {};
    if (fx->kind() != Value::Kind::DescriptorKind) return PsdError::invalid("filterFX is not an object");
    const Descriptor& fxd = fx->asDescriptor();
    auto* list = fxd.get("filterFXList");
    if (!list || list->kind() != Value::Kind::List) return PsdError::invalid("filterFX has no filterFXList");
    const auto& items = list->asList();
    for (size_t i = 0; i < items.size(); ++i) {
        if (items[i].kind() != Value::Kind::DescriptorKind) return PsdError::invalid("filter " + std::to_string(i) + " is not an object");
        const Descriptor& f = items[i].asDescriptor();
        auto* nm = f.get("Nm  ");
        auto* blendOptions = f.get("blendOptions");
        auto* enab = f.get("enab");
        auto* filterId = f.get("filterID");
        auto* fltr = f.get("Fltr");
        bool ok = nm && nm->kind() == Value::Kind::Text && blendOptions && blendOptions->kind() == Value::Kind::DescriptorKind && enab &&
                  enab->kind() == Value::Kind::Boolean && filterId && filterId->kind() == Value::Kind::Integer &&
                  (!fltr || fltr->kind() == Value::Kind::DescriptorKind);
        if (!ok) return PsdError::invalid("filter " + std::to_string(i) + " lacks Nm/blendOptions/enab/filterID");
    }
    return {};
}

// Strict check of one embedded (`liFD`) linked-layer item.
Result<void> checkEmbeddedItem(std::span<const uint8_t> item) {
    Reader r(item);
    PSD_TRY(r.skip(4));
    PSD_TRY_ASSIGN(auto version, r.u32());
    if (version < 1 || version > 16) return PsdError::invalid("version " + std::to_string(version));
    PSD_TRY(readPascal(r, 1));
    PSD_TRY(readUnicodeUnits(r));
    PSD_TRY(r.skip(8)); // file type, creator
    PSD_TRY_ASSIGN(auto len, r.u64());
    PSD_TRY_ASSIGN(auto hasDescriptor, r.u8());
    if (hasDescriptor != 0) {
        PSD_TRY(VersionedDescriptor::read(r));
    }
    PSD_TRY(r.bytesU64(len));
    return {};
}

} // namespace

Result<void> TaggedBlock::checkStructure() const {
    Reader r(data);
    if (tagIs(key, "PlLd")) {
        PSD_TRY(expectKind(r, "plcL"));
        PSD_TRY(r.u32());
        PSD_TRY(readPascal(r, 1));
        PSD_TRY(r.skip(4 * 4 + 8 * 8));
        PSD_TRY(r.u32());
        PSD_TRY(VersionedDescriptor::read(r));
    } else if (tagIs(key, "SoLd") || tagIs(key, "SoLE")) {
        PSD_TRY(expectKind(r, "soLD"));
        PSD_TRY(r.u32());
        PSD_TRY_ASSIGN(auto d, VersionedDescriptor::read(r));
        auto c = checkPlacedDescriptor(d.descriptor);
        if (!c) return PsdError::invalid(keyStr() + ": " + c.error().message());
    } else if (tagIs(key, "lfx2")) {
        PSD_TRY(r.u32());
        PSD_TRY(VersionedDescriptor::read(r));
    } else if (tagIs(key, "lnk2") || tagIs(key, "lnk3") || tagIs(key, "lnkD")) {
        while (r.remaining() >= 8) {
            PSD_TRY_ASSIGN(auto len, r.u64());
            PSD_TRY_ASSIGN(auto item, r.bytesU64(len));
            bool known = item.size() >= 4 &&
                         (std::memcmp(item.data(), "liFD", 4) == 0 || std::memcmp(item.data(), "liFE", 4) == 0 || std::memcmp(item.data(), "liFA", 4) == 0);
            if (!known) return PsdError::invalid(keyStr() + ": linked item of unknown type");
            if (item.size() >= 4 && std::memcmp(item.data(), "liFD", 4) == 0) {
                auto c = checkEmbeddedItem(item);
                if (!c) return PsdError::invalid(keyStr() + ": embedded file: " + c.error().message());
            }
            uint64_t pad = std::min<uint64_t>((4 - len % 4) % 4, static_cast<uint64_t>(r.remaining()));
            PSD_TRY(r.skip(static_cast<size_t>(pad)));
        }
    } else if (tagIs(key, "FEid") || tagIs(key, "FXid")) {
        auto fx = FilterEffects::parse(data);
        if (!fx) return PsdError::invalid(keyStr() + ": " + fx.error().message());
        return {};
    } else {
        return {};
    }
    auto rest = r.peekRest();
    bool allZero = std::all_of(rest.begin(), rest.end(), [](uint8_t b) { return b == 0; });
    if (rest.size() > 3 || !allZero) {
        return PsdError::invalid(keyStr() + ": " + std::to_string(rest.size()) + " unexpected bytes after the block structure");
    }
    return {};
}

Result<void> TaggedBlock::write(std::vector<uint8_t>& out, Version version, size_t alignment) const {
    return writeParts(out, signature, key, data, padding ? std::optional<std::span<const uint8_t>>(*padding) : std::nullopt, version, alignment);
}

Result<void> TaggedBlock::writeParts(std::vector<uint8_t>& out, std::array<uint8_t, 4> signature, std::array<uint8_t, 4> key,
                                      std::span<const uint8_t> data, std::optional<std::span<const uint8_t>> padding, Version version, size_t alignment) {
    put(out, signature);
    put(out, key);
    PSD_TRY(putLen(out, static_cast<uint64_t>(data.size()), usesLongLength(version, key)));
    put(out, data);
    if (padding.has_value()) {
        put(out, *padding);
    } else {
        out.insert(out.end(), defaultPadding(data.size(), alignment), 0u);
    }
    return {};
}

size_t TaggedBlock::encodedLen(Version version) const {
    size_t l = usesLongLength(version, key) ? 8 : 4;
    size_t p = padding.has_value() ? padding->size() : defaultPadding(data.size(), 2);
    return 8 + l + data.size() + p;
}

// --- SectionType / SectionDivider -------------------------------------------

const SectionType SectionType::Other(SectionType::Kind::Other, 0);
const SectionType SectionType::OpenFolder(SectionType::Kind::OpenFolder, 1);
const SectionType SectionType::ClosedFolder(SectionType::Kind::ClosedFolder, 2);
const SectionType SectionType::BoundingDivider(SectionType::Kind::BoundingDivider, 3);

SectionType SectionType::fromU32(uint32_t v) {
    switch (v) {
    case 0: return Other;
    case 1: return OpenFolder;
    case 2: return ClosedFolder;
    case 3: return BoundingDivider;
    default: return SectionType(Kind::Unknown, v);
    }
}
uint32_t SectionType::asU32() const {
    switch (m_kind) {
    case Kind::Other: return 0;
    case Kind::OpenFolder: return 1;
    case Kind::ClosedFolder: return 2;
    case Kind::BoundingDivider: return 3;
    case Kind::Unknown: return m_raw;
    }
    return m_raw;
}

namespace {
Result<SectionDivider> parseSection(std::span<const uint8_t> d) {
    Reader r(d);
    PSD_TRY_ASSIGN(auto kindRaw, r.u32());
    SectionType kind = SectionType::fromU32(kindRaw);
    std::optional<BlendMode> blendMode;
    std::optional<uint32_t> subType;
    if (r.remaining() >= 8) {
        PSD_TRY_ASSIGN(auto sig, r.array<4>());
        if (sig != tag4("8BIM")) return PsdError::invalidSignature("8BIM", sig);
        PSD_TRY_ASSIGN(auto key, r.array<4>());
        blendMode = BlendMode::fromKey(key);
        if (r.remaining() >= 4) {
            PSD_TRY_ASSIGN(auto st, r.u32());
            subType = st;
        }
    }
    return SectionDivider{kind, blendMode, subType};
}
} // namespace

// --- read/write blocks --------------------------------------------------------

namespace {
// `8BIM` / `8B64`, or the markers `MIB8` / `46B8` kept verbatim from a
// little-endian TIFF when its layout is unknown.
bool isSig(std::span<const uint8_t> b) {
    auto starts = [&](const char* s) { return b.size() >= 4 && std::memcmp(b.data(), s, 4) == 0; };
    return starts("8BIM") || starts("8B64") || TaggedBlock::isForeignOrderSignature(b);
}
} // namespace

Result<std::pair<std::vector<TaggedBlock>, std::vector<uint8_t>>> readBlocks(Reader& r, Version version) {
    return readBlocksWith(r, version, 2, [](size_t, std::array<uint8_t, 4>, std::array<uint8_t, 4>, std::span<const uint8_t>, const std::optional<std::vector<uint8_t>>&) {
        return false;
    });
}

Result<std::pair<std::vector<TaggedBlock>, std::vector<uint8_t>>> readBlocksWith(Reader& r, Version version, size_t alignment, ConsumeBlockFn consume) {
    std::vector<TaggedBlock> blocks;
    for (;;) {
        auto rest = r.peekRest();
        if (rest.size() < 12 || !isSig(rest)) {
            std::vector<uint8_t> trailing(rest.begin(), rest.end());
            PSD_TRY(r.skip(rest.size()));
            return std::make_pair(std::move(blocks), std::move(trailing));
        }
        PSD_TRY_ASSIGN(auto signature, r.array<4>());
        PSD_TRY_ASSIGN(auto key, r.array<4>());
        PSD_TRY_ASSIGN(auto len, r.lenField(usesLongLength(version, key)));
        PSD_TRY_ASSIGN(auto data, r.bytesU64(len));
        // Detect padding: the smallest k in 0..=3 such that the next block
        // signature (or the end of the region) follows k bytes later.
        auto after = r.peekRest();
        std::optional<size_t> pad;
        for (size_t k = 0; k <= 3; ++k) {
            if (k == after.size() || (k < after.size() && isSig(after.subspan(k)))) {
                pad = k;
                break;
            }
        }
        // Opaque trailing bytes can follow canonical zero padding without another signature.
        size_t defaultPad = defaultPadding(data.size(), alignment);
        size_t padLen;
        if (pad.has_value()) {
            padLen = *pad;
        } else {
            bool fitsDefault = after.size() >= defaultPad && std::all_of(after.begin(), after.begin() + static_cast<ptrdiff_t>(defaultPad), [](uint8_t b) { return b == 0; });
            padLen = fitsDefault ? defaultPad : 0;
        }
        PSD_TRY_ASSIGN(auto padBytesSpan, r.bytes(padLen));
        std::vector<uint8_t> padBytes(padBytesSpan.begin(), padBytesSpan.end());
        std::optional<std::vector<uint8_t>> padding;
        bool isDefaultPadding = padBytes.size() == defaultPadding(data.size(), alignment) && std::all_of(padBytes.begin(), padBytes.end(), [](uint8_t b) { return b == 0; });
        if (!isDefaultPadding) padding = padBytes;
        if (!consume(blocks.size(), signature, key, data, padding)) {
            TaggedBlock b;
            b.signature = signature;
            b.key = key;
            b.data = std::vector<uint8_t>(data.begin(), data.end());
            b.padding = padding;
            blocks.push_back(std::move(b));
        }
    }
}

Result<void> writeBlocks(std::vector<uint8_t>& out, const std::vector<TaggedBlock>& blocks, Version version, size_t alignment) {
    for (const auto& b : blocks) {
        PSD_TRY(b.write(out, version, alignment));
    }
    return {};
}

// --- BlockData factories -----------------------------------------------------

TaggedBlock::BlockData TaggedBlock::BlockData::unicodeName(std::string s) { return BlockData(Kind::UnicodeName, std::move(s)); }
TaggedBlock::BlockData TaggedBlock::BlockData::sectionDivider(psd::SectionDivider s) { return BlockData(Kind::SectionDivider, std::move(s)); }
TaggedBlock::BlockData TaggedBlock::BlockData::layerId(uint32_t v) { return BlockData(Kind::LayerId, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::nameSource(std::array<uint8_t, 4> v) { return BlockData(Kind::NameSource, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::blendClippedAsGroup(bool v) { return BlockData(Kind::BlendClippedAsGroup, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::blendInteriorElements(bool v) { return BlockData(Kind::BlendInteriorElements, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::knockout(uint8_t v) { return BlockData(Kind::Knockout, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::transparencyShapes(bool v) { return BlockData(Kind::TransparencyShapes, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::layerMaskHidesEffects(bool v) { return BlockData(Kind::LayerMaskHidesEffects, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::vectorMaskHidesEffects(bool v) { return BlockData(Kind::VectorMaskHidesEffects, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::protection(uint32_t v) { return BlockData(Kind::Protection, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::sheetColor(uint16_t v) { return BlockData(Kind::SheetColor, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::fillOpacity(uint8_t v) { return BlockData(Kind::FillOpacity, v); }
TaggedBlock::BlockData TaggedBlock::BlockData::metadataSetting(std::vector<uint8_t> v) { return BlockData(Kind::MetadataSetting, std::move(v)); }

// --- printers -----------------------------------------------------------------

std::ostream& operator<<(std::ostream& os, const TaggedBlock& b) {
    return os << "TaggedBlock{key=\"" << utf8BytesToStringLossy(b.key) << "\", n=" << b.data.size() << "}";
}

std::ostream& operator<<(std::ostream& os, const SectionType& t) {
    switch (t.kind()) {
    case SectionType::Kind::Other: return os << "Other";
    case SectionType::Kind::OpenFolder: return os << "OpenFolder";
    case SectionType::Kind::ClosedFolder: return os << "ClosedFolder";
    case SectionType::Kind::BoundingDivider: return os << "BoundingDivider";
    case SectionType::Kind::Unknown: return os << "Unknown(" << t.asU32() << ")";
    }
    return os << "?";
}

std::ostream& operator<<(std::ostream& os, const SectionDivider& s) {
    os << "SectionDivider{kind=" << s.kind << ", blendMode=";
    if (s.blendMode) os << *s.blendMode;
    else os << "None";
    os << ", subType=";
    if (s.subType) os << *s.subType;
    else os << "None";
    return os << "}";
}

std::ostream& operator<<(std::ostream& os, const TaggedBlock::BlockData& b) {
    using K = TaggedBlock::BlockData::Kind;
    switch (b.kind()) {
    case K::UnicodeName: return os << "UnicodeName(\"" << b.asUnicodeName() << "\")";
    case K::SectionDivider: return os << "SectionDivider(" << b.asSectionDivider() << ")";
    case K::LayerId: return os << "LayerId(" << b.asLayerId() << ")";
    case K::NameSource: return os << "NameSource";
    case K::BlendClippedAsGroup: return os << "BlendClippedAsGroup(" << b.asBlendClippedAsGroup() << ")";
    case K::BlendInteriorElements: return os << "BlendInteriorElements(" << b.asBlendInteriorElements() << ")";
    case K::Knockout: return os << "Knockout(" << int(b.asKnockout()) << ")";
    case K::TransparencyShapes: return os << "TransparencyShapes(" << b.asTransparencyShapes() << ")";
    case K::LayerMaskHidesEffects: return os << "LayerMaskHidesEffects(" << b.asLayerMaskHidesEffects() << ")";
    case K::VectorMaskHidesEffects: return os << "VectorMaskHidesEffects(" << b.asVectorMaskHidesEffects() << ")";
    case K::Protection: return os << "Protection(" << b.asProtection() << ")";
    case K::SheetColor: return os << "SheetColor(" << b.asSheetColor() << ")";
    case K::FillOpacity: return os << "FillOpacity(" << int(b.asFillOpacity()) << ")";
    case K::MetadataSetting: return os << "MetadataSetting(n=" << b.asMetadataSetting().size() << ")";
    }
    return os << "?";
}

} // namespace psd
