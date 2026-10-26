#include "Linked.hpp"

#include "File.hpp"
#include "Io.hpp"
#include "Tagged.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iterator>

namespace psd {
namespace {

bool keyIs(std::array<uint8_t, 4> key, const char* s)
{
    return std::memcmp(key.data(), s, 4) == 0;
}

std::string pascalToString(std::span<const uint8_t> bytes)
{
    return std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size());
}

// A descriptor number, whatever OSType it arrived as: the same geometric
// field is written as `doub`, `UntF` or `long` depending on the version that
// saved the file.
std::optional<double> number(const Value* v)
{
    if (!v)
        return std::nullopt;
    switch (v->kind()) {
    case Value::Kind::Double: return v->asDouble();
    case Value::Kind::UnitFloat: return v->asUnitFloat().value;
    case Value::Kind::Integer: return static_cast<double>(v->asInteger());
    case Value::Kind::LargeInteger: return static_cast<double>(v->asLargeInteger());
    default: return std::nullopt;
    }
}

std::optional<double> number(const Descriptor& d, const std::string& key)
{
    return number(d.get(key));
}

// An eight-double quad (`Trnf`, `nonAffineTransform`), stored as a `VlLs` of
// `doub`.
std::optional<PlacedQuad> quadFromValue(const Value* v)
{
    if (!v || v->kind() != Value::Kind::List)
        return std::nullopt;
    const auto& items = v->asList();
    if (items.size() != 8)
        return std::nullopt;
    PlacedQuad quad;
    for (size_t i = 0; i < 8; ++i) {
        const auto n = number(&items[i]);
        if (!n)
            return std::nullopt;
        quad.corners[i] = *n;
    }
    return quad;
}

// The doubles of a `UnFl` (or a `VlLs` of `doub`) mesh axis.
std::vector<double> axisValues(const Value* v)
{
    std::vector<double> out;
    if (!v)
        return out;
    if (v->kind() == Value::Kind::UnitFloats)
        return v->asUnitFloats().values;
    if (v->kind() == Value::Kind::List) {
        for (const auto& item : v->asList()) {
            const auto n = number(&item);
            if (!n)
                return {};
            out.push_back(*n);
        }
    }
    return out;
}

// Does this warp descriptor leave the source untouched? A warp descriptor is
// written on EVERY placed layer, so the key's presence says nothing:
// an unwarped Smart Object carries either style `warpNone` or a custom
// envelope whose mesh is still the regular lattice spanning the source
// bounds (which is what entering and leaving Warp without dragging leaves
// behind). Anything this function cannot prove to be the identity is
// reported as a real warp — a false "warped" only costs the importer its
// fast path, while a false "unwarped" would silently drop a deformation.
bool isIdentityWarp(const Descriptor& warp)
{
    if (const auto* style = warp.get("warpStyle")) {
        if (style->kind() == Value::Kind::Enumerated && style->asEnumerated().value.is("warpNone"))
            return true;
    }

    const auto* envelope = warp.get("customEnvelopeWarp");
    if (!envelope || envelope->kind() != Value::Kind::DescriptorKind)
        return false; // a preset warp (arc, flag, …) with no mesh of its own
    const Descriptor& env = envelope->asDescriptor();
    // Quilt slices subdivide the mesh unevenly; proving such a mesh is the
    // identity would mean reproducing the writer's slice layout, so don't try.
    if (env.get("quiltSliceX") || env.get("quiltSliceY"))
        return false;

    const auto* points = env.get("meshPoints");
    if (!points || points->kind() != Value::Kind::ObjectArray)
        return false;
    const Descriptor& mesh = points->asObjectArray().body;
    const std::vector<double> xs = axisValues(mesh.get("Hrzn"));
    const std::vector<double> ys = axisValues(mesh.get("Vrtc"));
    if (xs.empty() || xs.size() != ys.size())
        return false;

    const auto* boundsValue = warp.get("bounds");
    if (!boundsValue || boundsValue->kind() != Value::Kind::DescriptorKind)
        return false;
    const Descriptor& bounds = boundsValue->asDescriptor();
    const auto left = number(bounds, "Left");
    const auto top = number(bounds, "Top ");
    const auto right = number(bounds, "Rght");
    const auto bottom = number(bounds, "Btom");
    if (!left || !top || !right || !bottom)
        return false;

    const auto intOr = [&](const std::string& key, int fallback) {
        const auto n = number(warp, key);
        return n ? static_cast<int>(*n) : fallback;
    };
    const int cols = intOr("deformNumCols", intOr("uOrder", 4));
    const int rows = intOr("deformNumRows", intOr("vOrder", 4));
    if (cols < 2 || rows < 2 || static_cast<size_t>(cols) * static_cast<size_t>(rows) != xs.size())
        return false;

    constexpr double kTolerancePx = 0.5;
    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            const size_t i = static_cast<size_t>(row) * cols + col;
            const double wantX = *left + (*right - *left) * col / (cols - 1);
            const double wantY = *top + (*bottom - *top) * row / (rows - 1);
            if (std::abs(xs[i] - wantX) > kTolerancePx || std::abs(ys[i] - wantY) > kTolerancePx)
                return false;
        }
    }
    return true;
}

bool anyWarp(const Descriptor& d)
{
    for (const char* key : {"warp", "quiltWarp"}) {
        const auto* v = d.get(key);
        if (v && v->kind() == Value::Kind::DescriptorKind && !isIdentityWarp(v->asDescriptor()))
            return true;
    }
    return false;
}

Result<LinkedFile> readLinkedItem(std::span<const uint8_t> item)
{
    Reader r(item);
    PSD_TRY_ASSIGN(const auto type, r.array<4>());
    LinkedFile file;
    if (keyIs(type, "liFD"))
        file.kind = LinkedFile::Kind::Embedded;
    else if (keyIs(type, "liFE"))
        file.kind = LinkedFile::Kind::External;
    else if (keyIs(type, "liFA"))
        file.kind = LinkedFile::Kind::Alias;
    else
        return PsdError::invalid("linked item of unknown type");

    PSD_TRY_ASSIGN(file.version, r.u32());
    PSD_TRY_ASSIGN(const auto uniqueId, readPascal(r, 1));
    file.uniqueId = pascalToString(uniqueId);
    PSD_TRY_ASSIGN(const auto name, readUnicodeUnits(r));
    file.fileName = utf16ToUtf8Lossy(name);
    PSD_TRY_ASSIGN(file.fileType, r.array<4>());
    PSD_TRY_ASSIGN(file.fileCreator, r.array<4>());
    PSD_TRY_ASSIGN(const auto dataSize, r.u64());
    PSD_TRY_ASSIGN(const auto hasOpenDescriptor, r.u8());
    if (hasOpenDescriptor != 0)
        PSD_TRY(VersionedDescriptor::read(r));

    // Only the embedded kind continues into a payload here. The external and
    // alias kinds carry further descriptors/timestamps whose layout varies by
    // version and which say nothing this importer can act on — their bytes are
    // not in this document at all.
    if (file.kind == LinkedFile::Kind::Embedded) {
        PSD_TRY_ASSIGN(const auto data, r.bytesU64(dataSize));
        file.data.assign(data.begin(), data.end());
    }
    return file;
}

} // namespace

bool PlacedQuad::isAffine(double tolerance) const
{
    return std::abs((x(0) + x(2)) - (x(1) + x(3))) <= tolerance
        && std::abs((y(0) + y(2)) - (y(1) + y(3))) <= tolerance;
}

bool PlacedQuad::equals(const PlacedQuad& other, double tolerance) const
{
    for (size_t i = 0; i < corners.size(); ++i) {
        if (std::abs(corners[i] - other.corners[i]) > tolerance)
            return false;
    }
    return true;
}

bool PlacedLayer::isPlainAffine() const
{
    if (warped || !transform || !transform->isAffine())
        return false;
    return !nonAffineTransform || nonAffineTransform->equals(*transform);
}

std::vector<LinkedFile> readLinkedFiles(const TaggedBlock& block)
{
    std::vector<LinkedFile> out;
    Reader r(block.data);
    // Each item is length-prefixed and padded to four bytes, so an item that
    // cannot be read never costs the walk the ones after it.
    while (r.remaining() >= 8) {
        auto len = r.u64();
        if (!len)
            break;
        auto item = r.bytesU64(len.value());
        if (!item)
            break;
        auto parsed = readLinkedItem(item.value());
        if (parsed)
            out.push_back(std::move(parsed).value());
        const uint64_t pad = std::min<uint64_t>((4 - len.value() % 4) % 4, static_cast<uint64_t>(r.remaining()));
        if (!r.skip(static_cast<size_t>(pad)))
            break;
    }
    return out;
}

std::vector<LinkedFile> readLinkedFiles(const PsdFile& file)
{
    std::vector<LinkedFile> out;
    for (const TaggedBlock& block : file.globalBlocks) {
        if (!keyIs(block.key, "lnk2") && !keyIs(block.key, "lnk3") && !keyIs(block.key, "lnkD"))
            continue;
        auto items = readLinkedFiles(block);
        out.insert(out.end(), std::make_move_iterator(items.begin()), std::make_move_iterator(items.end()));
    }
    return out;
}

const LinkedFile* findLinkedFile(const std::vector<LinkedFile>& files, const std::string& uniqueId)
{
    if (uniqueId.empty())
        return nullptr;
    for (const LinkedFile& file : files) {
        if (file.uniqueId == uniqueId)
            return &file;
    }
    return nullptr;
}

Result<PlacedLayer> readPlacedLayer(const TaggedBlock& block)
{
    Reader r(block.data);
    PlacedLayer placed;

    if (keyIs(block.key, "PlLd")) {
        // `plcL` keeps its fields as fixed-width records rather than in a
        // descriptor, and its transform is the EFFECTIVE placement (the
        // perspective quad when there is one) — the affine/non-affine split
        // only exists in `SoLd`'s descriptor.
        PSD_TRY_ASSIGN(const auto kind, r.array<4>());
        if (!keyIs(kind, "plcL"))
            return PsdError::invalid("PlLd: not a plcL record");
        PSD_TRY(r.u32()); // version
        PSD_TRY_ASSIGN(const auto uniqueId, readPascal(r, 1));
        placed.uniqueId = pascalToString(uniqueId);
        PSD_TRY(r.skip(4 * 4)); // page number, total pages, anti-alias policy, type
        PlacedQuad quad;
        for (size_t i = 0; i < 8; ++i) {
            PSD_TRY_ASSIGN(const double corner, r.f64());
            quad.corners[i] = corner;
        }
        placed.transform = quad;
        PSD_TRY(r.u32()); // warp version
        PSD_TRY_ASSIGN(const auto warp, VersionedDescriptor::read(r));
        placed.warped = !isIdentityWarp(warp.descriptor);
        return placed;
    }

    if (!keyIs(block.key, "SoLd") && !keyIs(block.key, "SoLE"))
        return PsdError::invalid("not a placed-layer block");

    PSD_TRY_ASSIGN(const auto kind, r.array<4>());
    if (!keyIs(kind, "soLD"))
        return PsdError::invalid("SoLd: not a soLD record");
    PSD_TRY(r.u32()); // version
    PSD_TRY_ASSIGN(const auto versioned, VersionedDescriptor::read(r));
    const Descriptor& d = versioned.descriptor;

    if (const auto* idnt = d.get("Idnt"); idnt && idnt->kind() == Value::Kind::Text)
        placed.uniqueId = idnt->asText().toStringLossy();
    placed.transform = quadFromValue(d.get("Trnf"));
    placed.nonAffineTransform = quadFromValue(d.get("nonAffineTransform"));
    placed.warped = anyWarp(d);
    if (const auto* size = d.get("Sz  "); size && size->kind() == Value::Kind::DescriptorKind) {
        placed.sourceWidth = number(size->asDescriptor(), "Wdth").value_or(0.0);
        placed.sourceHeight = number(size->asDescriptor(), "Hght").value_or(0.0);
    }
    placed.sourceResolution = number(d, "Rslt").value_or(0.0);
    return placed;
}

Result<PlacedLayer> readPlacedLayer(const LayerRecord& rec)
{
    // `SoLd`/`SoLE` first: `PlLd` is the older, smaller record, and a file
    // that has both (current writers emit both) keeps the
    // source size, resolution and the affine/non-affine split only there.
    for (const char* key : {"SoLd", "SoLE", "PlLd"}) {
        const std::array<uint8_t, 4> k{static_cast<uint8_t>(key[0]), static_cast<uint8_t>(key[1]),
                                       static_cast<uint8_t>(key[2]), static_cast<uint8_t>(key[3])};
        if (const TaggedBlock* block = rec.block(k))
            return readPlacedLayer(*block);
    }
    return PsdError::invalid("layer has no placed-layer block");
}

} // namespace psd
