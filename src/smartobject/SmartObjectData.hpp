#pragma once

#include <QSize>
#include <QString>
#include <cstdint>
#include <limits>

// How the document a Smart Object Layer displays is stored.
enum class SmartObjectStorageMode {
    Embedded,   // the document lives in this file's own DocumentContainer
    Linked,     // the document is the root of another .hzs file
};

// Stable identity of a document inside a DocumentContainer (a QUuid without
// braces). Survives save/load and is shared by every instance that displays
// that document, which is what makes "three instances of the same document"
// a real concept rather than three copies.
using SmartObjectDocId = QString;

// A generation no real document can hold, marking an instance as "never
// rendered yet" so the staleness test schedules its first render. A plain 0
// will not do: a brand-new document's compositionGeneration is also 0, so a
// freshly created instance would read as up to date and sit on its transparent
// placeholder forever if its first render were ever dropped.
inline constexpr std::uint64_t kSmartObjectNeverRendered =
    (std::numeric_limits<std::uint64_t>::max)();

// The small, versioned half of the payload: which render produced the layer's
// current cpuImage. Copied by value on every undo step (cheap — no pixels).
struct SmartObjectState {
    QSize sourceSize;                    // canvas of the referenced document at render time
    double sourceDpi = 300.0;
    // Referenced document's compositionGeneration at render time, or
    // kSmartObjectNeverRendered before the first one lands.
    std::uint64_t sourceGeneration = kSmartObjectNeverRendered;
    std::uint64_t revision = 0;          // ++ per re-render; breaks ties between async results
};

// Non-destructive Smart Object metadata carried on a Layer, alongside
// textData / shapeData / distortData / rawData. The layer's cpuImage holds the
// rendered composite of the referenced document — exactly as rawData + cpuImage
// hold a RAW file's developed result.
//
// ─── Why there is no Document* / shared_ptr<Document> in here ───
// This is the load-bearing decision of the whole feature, so it is written
// down where someone about to "optimize" it will read it first.
//
// The CPU projection is composited on a WORKER THREAD over a
// LayerTreeNode::shallowClone() snapshot (ProjectionCache::makeSnapshot). That
// snapshot is only thread-safe because every payload a layer shares is either
// immutable or copy-on-write. A Document pointer in here would hand the
// compositor a live, UI-thread-mutated object graph to walk — a data race that
// shows up as a crash or a torn frame far from this file. So the composite
// NEVER resolves a reference: an instance composites as an ordinary raster
// layer, and resolution happens only in SmartObjectRenderService, which owns
// its own snapshot. That is also why mask, blend, opacity, effects and the
// instance transform work with zero lines of compositor code.
//
// Keeping only a docId (not an owning pointer) is what also makes an ownership
// cycle unrepresentable: containers never reference each other, and a Linked
// reference is a path resolved through ContainerRegistry.
//
// Stored as std::shared_ptr<const SmartObjectData>: immutability enforced by
// the compiler, not by convention. No clone, snapshot, paste or undo path can
// mutate through the pointer; every change builds a new value and swaps the
// pointer. Same contract as shared_ptr<const RawSource> (see RawLayerData.hpp).
//
// And the consequence that organizes everything downstream: the cpuImage here
// is a CACHE, not document state. Its content depends only on (referenced
// document, destination profile) — the instance's transform, mask and effects
// are applied by the compositor, downstream. That is what lets one render be
// shared by every instance of a key, and what keeps it out of Undo. See
// docs/RENDER-PIPELINE.md.
struct SmartObjectData {
    SmartObjectStorageMode mode = SmartObjectStorageMode::Embedded;
    SmartObjectDocId docId;     // key in the owning DocumentContainer
    // Linked only: the path as persisted (relative to the container's .hzs when
    // possible, so moving a project folder keeps the links intact).
    QString linkedPath;
    SmartObjectState state;
    bool linkBroken = false;    // Linked: the last resolve attempt failed
};
