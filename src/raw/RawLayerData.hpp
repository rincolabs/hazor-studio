#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <cstdint>
#include <memory>

#include "raw/RawGeometry.hpp"
#include "raw/RawMetadata.hpp"

// How the original RAW file's bytes are kept alive for this layer.
enum class RawStorageMode {
    Linked,     // `RawSource::path` on disk; re-reveal re-reads the file.
    Embedded,   // `RawSource::bytes` holds the file; survives the source moving/disappearing.
};

// The immutable half of a RAW Layer's payload: the original file (by path or
// by value) plus the metadata read from it. Never mutated after construction
// and never copied — every edit step in the undo history and every clone of
// the layer shares the same instance via shared_ptr<const RawSource>, so the
// (potentially 20-60 MB) embedded bytes exist exactly once no matter how many
// re-reveals pile up in Undo/Redo. Which of path/bytes is populated is told
// by the owning RawLayerData::mode, not duplicated here.
struct RawSource {
    // Original file's path/name. Always populated (display, extension
    // sniffing for the Embedded temp-file materialization in
    // RawDevelopService) — in Embedded mode the file at this path need not
    // exist any more; decoding uses `bytes`, never re-reads it.
    QString path;
    QByteArray bytes;        // Embedded only: the original RAW file's bytes.
    RawMetadata meta;
};

// The small, versioned half: which develop profile produced the layer's
// current cpuImage and the geometry that profile rendered at. Copied by
// value on every edit step (cheap — no pixels, no file bytes).
struct RawDevelopState {
    QString pp3;                // Full PP3 text (RawDevelopParams::toPP3String()).
    QSize developedSize;        // Dimensions of the render this state came from.
    RawGeometry geometry;       // Develop-space geometry of that same render.
    std::uint64_t revision = 0; // Bumped on every re-reveal; breaks ties when two
                                 // async results could otherwise land out of order.
};

// Non-destructive RAW metadata carried on a Layer (alongside textData/
// shapeData/distortData). `source` is the immutable original file; `state`
// is the develop profile + geometry that produced the layer's current
// cpuImage. The cpuImage itself holds the rendered (already 8-bit, see
// RawDevelopService) result and composites through the normal pipeline.
struct RawLayerData {
    std::shared_ptr<const RawSource> source;
    RawDevelopState state;
    RawStorageMode mode = RawStorageMode::Linked;
};
