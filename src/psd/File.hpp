#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <variant>
#include <vector>

#include "Error.hpp"
#include "Header.hpp"
#include "ImageData.hpp"
#include "Io.hpp"
#include "Layer.hpp"
#include "Resources.hpp"
#include "Tagged.hpp"

// The top-level PsdFile model, parser and writer. See PROVENANCE.md for
// origin details.

namespace psd {

class LayerView;    // Pixels.hpp; PsdFile::layer()/iterLayers() are defined in Pixels.cpp.
struct RgbaImage;   // Pixels.hpp; PsdFile::compositeRgba8() is defined in Pixels.cpp.
class LayerNode;    // Tree.hpp; PsdFile::layerTree()/layerTreeMaxDepth() are defined in Tree.cpp.

// Where the layer info was stored in the file.
class LayerInfoPlacement {
public:
    enum class Kind { Section, GlobalBlock };
    struct GlobalBlockInfo {
        size_t index = 0;
        std::array<uint8_t, 4> signature{};
        std::array<uint8_t, 4> key{};
        std::optional<std::vector<uint8_t>> padding;
        bool operator==(const GlobalBlockInfo&) const = default;
    };

    LayerInfoPlacement() : m_kind(Kind::Section) {}
    static LayerInfoPlacement section() { return LayerInfoPlacement(); }
    static LayerInfoPlacement globalBlock(GlobalBlockInfo info) {
        LayerInfoPlacement p;
        p.m_kind = Kind::GlobalBlock;
        p.m_info = std::move(info);
        return p;
    }

    Kind kind() const { return m_kind; }
    const GlobalBlockInfo& globalBlockInfo() const { return *m_info; }

    bool operator==(const LayerInfoPlacement& other) const {
        if (m_kind != other.m_kind) return false;
        if (m_kind == Kind::Section) return true;
        return m_info == other.m_info;
    }
    bool operator!=(const LayerInfoPlacement& other) const { return !(*this == other); }

private:
    Kind m_kind;
    std::optional<GlobalBlockInfo> m_info;
};
std::ostream& operator<<(std::ostream& os, LayerInfoPlacement::Kind kind);
std::ostream& operator<<(std::ostream& os, const LayerInfoPlacement& p);

// Global layer mask info, stored raw with typed accessors.
struct GlobalLayerMask {
    std::vector<uint8_t> data; // without the length field.

    std::optional<uint16_t> overlayColorSpace() const;
    std::optional<std::array<uint16_t, 4>> colorComponents() const;
    std::optional<uint16_t> opacity() const; // 0..=100
    std::optional<uint8_t> kind() const;     // 0 color selected, 1 color protected, 128 per layer

    bool operator==(const GlobalLayerMask&) const = default;
};
std::ostream& operator<<(std::ostream& os, const GlobalLayerMask& m);

// A complete PSD/PSB file.
//
// PsdFile::fromBytes(b)?.toBytes()? == b for unmodified well-formed files,
// and fromBytes(toBytes(f)) == f for any model f.
struct PsdFile {
    Header header;
    std::vector<uint8_t> colorModeData; // palette for indexed, duotone spec, ...
    std::vector<ImageResource> resources;
    std::optional<LayerInfo> layerInfo; // nullopt when there is no layer info at all.
    LayerInfoPlacement layerInfoPlacement;
    std::optional<GlobalLayerMask> globalLayerMask; // nullopt when the field is absent entirely.
    std::vector<TaggedBlock> globalBlocks;
    std::vector<uint8_t> layerMaskTrailing;
    ImageData imageData;

    // Parses a PSD or PSB file.
    static Result<PsdFile> fromBytes(std::span<const uint8_t> data);

    // Parses like fromBytes and, when `want` asks for it, decodes the merged
    // image in the same pass.
    static Result<std::pair<PsdFile, std::optional<Result<std::vector<uint8_t>>>>> fromBytesAndMerged(std::span<const uint8_t> data,
                                                                                                        std::function<bool(const PsdFile&)> want);

    // Parses like fromBytesAndMerged, but lets `recover` accept damage
    // confined to the merged image after all layer records and channel data
    // have been read successfully.
    static Result<std::pair<PsdFile, std::optional<Result<std::vector<uint8_t>>>>>
    fromBytesAndMergedRecovering(std::span<const uint8_t> data, std::function<bool(const PsdFile&)> want, std::function<bool(const PsdFile&)> recover);

    // Serializes the file.
    Result<std::vector<uint8_t>> toBytes() const;

    // Layer records in file order (bottom-most first); empty if none.
    const std::vector<LayerRecord>& layers() const;
    // Mutable layer records (creates an empty layer info if absent).
    std::vector<LayerRecord>& layersMut();

    // Borrowed view of layer `index` (Pixels.cpp).
    std::optional<LayerView> layer(size_t index) const;
    // Views of every layer in file order (bottom-most first) (Pixels.cpp).
    std::vector<LayerView> iterLayers() const;
    // The merged composite as RGBA8 (Pixels.cpp).
    Result<RgbaImage> compositeRgba8() const;

    // Builds the nested layer tree (Tree.cpp). Root nodes and children are
    // ordered bottom-to-top (file order). Malformed nesting is tolerated: a
    // folder without a matching divider becomes an empty group; unclosed
    // dividers at the end are dropped and their children are hoisted to the
    // enclosing level.
    std::vector<LayerNode> layerTree() const;
    // layerTree() with at most `maxDepth` levels of groups: a group nested
    // maxDepth or more groups deep (a root node is at depth 0) keeps its
    // node but not its children. Built iteratively (bounded recursion when
    // walking the result).
    std::vector<LayerNode> layerTreeMaxDepth(size_t maxDepth) const;

    // First resource with the given id.
    const ImageResource* resource(uint16_t id) const;
    // The global Lr16/Lr32/Layr block that should have held the layers but
    // could not be parsed, with the parse error.
    std::optional<std::pair<const TaggedBlock*, PsdError>> unreadableLayerBlock() const;
    // First global block with the given key.
    const TaggedBlock* globalBlock(std::array<uint8_t, 4> key) const;
    // ICC profile bytes (resource 1039).
    std::optional<std::span<const uint8_t>> iccProfile() const;
    // Resolution info (resource 1005).
    std::optional<ResolutionInfo> resolution() const;
    // The "has real merged data" flag of resource 1057, if present.
    std::optional<bool> hasRealMergedData() const;
    // Decodes the merged image into planar big-endian samples.
    Result<std::vector<uint8_t>> decodeMerged() const;
    // true if the merged image carries a transparency channel.
    bool mergedHasAlpha() const;
    // Checks structural invariants that the writer relies on.
    Result<void> validate() const;

    // Native file I/O.
    static Result<PsdFile> open(const std::string& path);
    Result<void> save(const std::string& path) const;

    bool operator==(const PsdFile&) const = default;

private:
    bool namesMergedTransparency(uint16_t extra) const;
    // Outer Result: parse error (fatal). Inner Result: the merged image's own decode outcome.
    Result<Result<std::vector<uint8_t>>> readMergedDecoded(Reader& r);
};
std::ostream& operator<<(std::ostream& os, const PsdFile& f);

} // namespace psd
