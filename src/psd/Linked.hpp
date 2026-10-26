#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "Descriptor.hpp"
#include "Error.hpp"
#include "Layer.hpp"

// Placed (Smart Object) layers: the `SoLd`/`SoLE`/`PlLd` per-layer blocks
// that describe WHERE a placed layer sits, and the document-global
// `lnk2`/`lnk3`/`lnkD` blocks that carry WHAT was placed — the original
// file's own bytes for an embedded Smart Object.
//
// Not a port: upstream (see PROVENANCE.md) validates the structure of these
// blocks but never exposes their contents. Everything here is written
// against the published PSD/PSB file-format specification (Placed Layer Data
// / Linked Layer) plus the field order the MIT-licensed psd-tools and ag-psd
// document for the parts the specification leaves out, and matches
// TaggedBlock::checkStructure()'s own reading of the same bytes.
//
// Kept separate from Tagged.cpp on purpose: a tagged block stays RAW there
// (that is what makes unknown blocks round-trip byte for byte), so this is a
// read-only view over bytes that module already owns.

namespace psd {

struct PsdFile;

// One item of a `lnk2`/`lnk3`/`lnkD` block: the file behind a placed layer.
struct LinkedFile {
    enum class Kind {
        Embedded, // `liFD`: the original file's bytes travel inside the PSD
        External, // `liFE`: a reference to a file on disk
        Alias,    // `liFA`: a reference by (legacy) alias record
    };

    Kind kind = Kind::Embedded;
    uint32_t version = 0;
    // The item's identity, which a placed layer's `Idnt` points at. Several
    // layers can share one id: that is "two instances of the same Smart
    // Object", and it is why the lookup is by id and not by position.
    std::string uniqueId;
    std::string fileName; // as shown in the authoring app's layer list ("art.psb")
    std::array<uint8_t, 4> fileType{};    // e.g. `8BPB`, `png `, `JPEG`
    std::array<uint8_t, 4> fileCreator{};
    // Embedded only: the complete original file, exactly as it was stored
    // here (a nested PSD/PSB, a PNG, a JPEG, …). Empty for the other kinds —
    // their bytes live outside this document.
    std::vector<uint8_t> data;

    bool isEmbedded() const { return kind == Kind::Embedded && !data.empty(); }
};

// Every linked item of `file`, in file order, across all of its
// `lnk2`/`lnk3`/`lnkD` blocks. Items that cannot be read are skipped rather
// than failing the whole list: one unreadable entry must not cost the
// document every other Smart Object (the block is self-delimiting, so the
// walk can always continue at the next item).
std::vector<LinkedFile> readLinkedFiles(const PsdFile& file);

// The items of one `lnk2`/`lnk3`/`lnkD` block.
std::vector<LinkedFile> readLinkedFiles(const TaggedBlock& block);

// The item whose uniqueId is `uniqueId`, or nullptr.
const LinkedFile* findLinkedFile(const std::vector<LinkedFile>& files, const std::string& uniqueId);

// Four corners (x, y pairs: top-left, top-right, bottom-right, bottom-left)
// in the destination document's pixel coordinates, Y down — where the
// placed file's full extent lands.
struct PlacedQuad {
    std::array<double, 8> corners{};

    double x(size_t corner) const { return corners[corner * 2]; }
    double y(size_t corner) const { return corners[corner * 2 + 1]; }
    // True when the quad is a parallelogram, i.e. it can be expressed as an
    // affine map of the source rectangle (p0 + p2 == p1 + p3). A perspective
    // placement is not: the format keeps the affine part in `transform` and
    // the full one in `nonAffineTransform`.
    bool isAffine(double tolerance = 0.01) const;
    bool equals(const PlacedQuad& other, double tolerance = 0.01) const;
};

// A placed layer's `SoLd`/`SoLE`/`PlLd` block, read for the fields that say
// what was placed and where.
struct PlacedLayer {
    std::string uniqueId; // `Idnt` — the LinkedFile this layer displays
    // `Trnf` (SoLd) or the block's own transform (PlLd): the placement's
    // affine part.
    std::optional<PlacedQuad> transform;
    // `nonAffineTransform`: the real placement when it is a perspective one.
    // Equal to `transform` whenever the placement is affine.
    std::optional<PlacedQuad> nonAffineTransform;
    // A `warp`/`quiltWarp` that actually deforms the source. A warp
    // descriptor is written even when nothing is warped (style
    // `warpNone`, or a custom envelope whose mesh is still the identity
    // lattice), so this is a test of the mesh, not of the key's presence.
    bool warped = false;
    // `Sz  `: the placed file's own size in pixels. 0 when absent (PlLd).
    double sourceWidth = 0.0;
    double sourceHeight = 0.0;
    // `Rslt`: the placed file's own resolution in DPI. 0 when absent.
    double sourceResolution = 0.0;

    // The placement that was actually rendered: the non-affine quad when
    // there is one, else the affine one.
    const std::optional<PlacedQuad>& effectiveTransform() const {
        return nonAffineTransform ? nonAffineTransform : transform;
    }
    // True when the layer's pixels are the source placed by an affine map
    // alone — the only case another renderer can reproduce from the source
    // without reimplementing the format's warp mesh.
    bool isPlainAffine() const;
};

// Reads the first `SoLd`/`SoLE`/`PlLd` block of `rec`. Returns an error when
// the record has no placed-layer block at all, or when the one it has cannot
// be read.
Result<PlacedLayer> readPlacedLayer(const LayerRecord& rec);

// Reads a placed-layer block directly (`SoLd`, `SoLE` or `PlLd`).
Result<PlacedLayer> readPlacedLayer(const TaggedBlock& block);

} // namespace psd
