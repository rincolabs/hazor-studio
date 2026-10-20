#pragma once

// Shared helpers for the PsdImportService_*.cpp translation units. Not a
// public header — included only by the service's own sources.
//
// The psd/*.hpp includes MUST come first, before any Qt header (directly or
// transitively, e.g. via core/Layer.hpp): psd::FilterEffectsItem has a field
// named `slots`, and Qt's global `slots`/`signals`/`emit` keyword macros
// (defined once a header pulls in QObject, e.g. QSettings in
// core/GuideTypes.hpp) corrupt that declaration if they are already active
// when FilterEffects.hpp is parsed.
#include "psd/Descriptor.hpp"
#include "psd/File.hpp"
#include "psd/Layer.hpp"
#include "psd/Linked.hpp"

#include <QColor>
#include <QImage>
#include <QRect>
#include <QRectF>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QTransform>
#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <vector>

#include "core/DocumentChannel.hpp"
#include "core/Layer.hpp"
#include "core/LayerEffect.hpp"

class LayerTreeNode;
class Document;

namespace psdimport {

// A 4-byte tagged-block/channel key from a string literal, e.g. psdKey("TySh").
inline std::array<uint8_t, 4> psdKey(const char* s)
{
    return {static_cast<uint8_t>(s[0]), static_cast<uint8_t>(s[1]),
            static_cast<uint8_t>(s[2]), static_cast<uint8_t>(s[3])};
}

// Shared state threaded through every conversion function: the source file,
// the destination canvas size (for the pixel-rect -> NDC transform) and the
// accumulated compatibility-report lines.
struct Context {
    const psd::PsdFile& file;
    QSize docSize;
    double dpi = 72.0;
    QStringList warnings;
    // Content documents created for Smart Object layers (see buildLeafNode in
    // PsdImportService.cpp), copied into PsdImportResult::embeddedSmartObjects
    // at the end of import(). Document is only forward-declared here —
    // constructing/destroying the pointee happens in PsdImportService.cpp,
    // which has the full definition; storing/moving a shared_ptr elsewhere
    // needs no more than the forward declaration.
    std::vector<std::pair<QString, std::shared_ptr<Document>>> embeddedDocs;
    // How many Smart Object sources deep this conversion already is. A PSD
    // whose Smart Object embeds a PSD whose Smart Object embeds a PSD… is
    // legal (and a hand-built file could even be circular), so the recursion
    // through importEmbeddedSmartObjectDocument() is bounded.
    int smartObjectDepth = 0;
    // Lazily filled by linkedFiles(); not read directly.
    std::optional<std::vector<psd::LinkedFile>> linkedFileCache;

    void warn(const QString& line) { warnings.push_back(line); }

    // The document's `lnk2`/`lnk3`/`lnkD` table: the original files behind its
    // placed/Smart Object layers. Read at most once per import — the embedded
    // entries carry whole files (tens of megabytes is normal), so reading it
    // per layer would copy all of them per instance.
    const std::vector<psd::LinkedFile>& linkedFiles()
    {
        if (!linkedFileCache)
            linkedFileCache = psd::readLinkedFiles(file);
        return *linkedFileCache;
    }
};

// Smart Object sources are converted by re-entering the document-level
// conversion; beyond this many levels the nested source is imported as its
// own flattened pixels instead.
inline constexpr int kMaxSmartObjectDepth = 6;

// Node transform placing a layer whose own rendered image is exactly
// `rectPx.size()` pixels at document-pixel rect `rectPx` (top-left origin, Y
// down) — the inverse of the image-pixel -> NDC mapping every render path
// (DocumentCompositor::buildImgToCanvas, ImageController::createTextLayer)
// assumes for a layer's own full-size image. Group nodes stay identity: PSD
// layer rects are already absolute document coordinates regardless of
// nesting, so composing group transforms would double-apply the offset.
QTransform ndcTransformForPixelRect(const QRectF& rectPx, const QSize& docSize);

// ndcTransformForPixelRect generalized to any placement of the layer's own
// image: `imagePxToDocPx` maps the image's pixel coordinates (origin
// top-left, Y down) to document pixels, so a rotated/skewed/flipped
// placement — a Smart Object's `Trnf`, say — becomes a node transform. Must
// be affine: the GPU path (qTransformToMatrix4x4 in GPUViewport.hpp) drops
// m13/m23, so a projective node transform would make the interactive frame
// disagree with the CPU composite, which is the visual source of truth.
QTransform ndcTransformForImageMap(const QTransform& imagePxToDocPx, const QSize& imageSizePx,
                                   const QSize& docSize);

// Maps a psd::BlendMode onto Hazor's BlendMode. The two sets are the same 27
// layer modes under the same names; what is left over is group Pass Through
// (and any key this reader does not know), which maps to Normal and is
// reported once per affected layer via ctx.warn().
BlendMode mapBlendMode(const psd::BlendMode& mode, Context& ctx, const QString& layerName);

// Record-level PSD features that have no Hazor equivalent and that no
// content-type converter owns: "Blend If" blending ranges, knockout, a clipped
// GROUP, and a fill opacity on a node that has no Fill. Reported once per
// affected layer.
// `clippingHonoured` says whether the record's clipping flag was acted on — a
// layer carries it natively (LayerTreeNode::clipped) and an adjustment is nested
// as a Single-Layer-Mode child, so only a group drops it here. A layer whose
// flag survives this but finds no base to clip to is reported by
// buildSiblingLevel instead, which is where the base is resolved.
// `fillOpacityHonoured` likewise: a Layer node and an Adjustment node both
// carry `iOpa` natively (LayerTreeNode::fillOpacity), while a group has no Fill
// in this model — the same line the editors it follows draw — so only a group
// reports it instead.
void reportUnsupportedLayerProperties(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                                      bool clippingHonoured, bool fillOpacityHonoured);

// A descriptor colour value ("Clr ", "FillColor", ...): RGBC / CMYC / Grsc /
// LbCl class ids, falling back to black.
QColor colorFromDescriptor(const psd::Descriptor& d);

// Lock flags (LockTransparent/LockImage/LockPosition) from the layer
// record's `lspf` block and its own transparency-protected flag bit.
int lockFlagsFromRecord(const psd::LayerRecord& rec);

// Builds `layer.maskImage`/`maskOrigin`/`maskDensity`/`maskFeather`/
// `maskVisible` from the record's user mask (channel -3 if present, else
// -2), positioned relative to `ownerRectPx` (the coordinate space the mask
// is being attached to: the layer's own rect for an ordinary layer, or the
// clipped-to layer's rect for a Single-Layer-Mode adjustment). No-op when
// the record has no mask.
void applyLayerMask(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                     Layer& layer, const QRect& ownerRectPx);

// applyLayerMask for a layer whose own image is NOT the document-pixel rect
// the mask was authored in: `imageToDocPx` maps the layer's own image pixels
// to document pixels (for a Smart Object, the placement read from `Trnf`), and
// the mask is resampled through its inverse so it keeps covering the same
// document pixels once the node transform is applied. PSD layer masks live in
// document space and are NOT deformed by the placement, which is exactly what
// the resample reproduces.
void applyLayerMaskThroughTransform(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                                     Layer& layer, const QSize& imageSizePx, const QTransform& imageToDocPx);

// Decodes a layer's own channels to a Format_RGBA8888 QImage; null when the
// record has no pixel data (empty rect, or every channel failed to decode).
QImage decodeLayerRgba(const psd::LayerRecord& rec, const psd::PsdFile& file,
                        Context& ctx, const QString& layerName);

// Layer effects (`lfx2`/`lmfx`/`lfxs`) -> Hazor LayerEffect list. Bevel &
// Emboss and Satin have no Hazor equivalent and are reported, not dropped
// silently.
void convertLayerEffects(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                          std::vector<LayerEffect>& outEffects);

// Tries to map the record's adjustment/fill block onto one of Hazor's
// adjustment types (curves, colorbalance, huesaturation, solidcolor). Fills
// `node` (already Type::Adjustment) on success. Returns false when the
// record's adjustment/fill has no Hazor equivalent (caller drops the node
// and reports it).
bool tryBuildAdjustment(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                        LayerTreeNode& node);

// Resizes/repositions an Adjustment node's mask-coordinate-space image after
// it is nested as a Single-Layer-Mode child of `host` (clipping != 0):
// shrinks `cpuImage`/mask from document-sized to the host's own image size,
// re-anchoring maskOrigin into the host's local pixel space. `host`'s
// current document-pixel rect is recovered from its own (pure scale +
// translate) node transform, so this works whether the host is a plain
// raster layer (image size == its original PSD rect) or a shape/text layer
// whose rendered raster was recomputed from vector/text content.
void reclipAdjustmentToHost(LayerTreeNode& adjustmentNode, const LayerTreeNode& host, const QSize& docSize);

// The document-pixel rect a leaf node's own image currently occupies,
// recovered from its (pure scale + translate) transform — the inverse of
// ndcTransformForPixelRect().
QRect currentImageRectPx(const LayerTreeNode& node, const QSize& docSize);

// Shape layer (vector mask + fill/stroke) -> ShapeData, rasterized via
// ShapeLayerUpdater. Returns false when the record has a vector mask but no
// usable geometry (caller falls back to a raster import).
bool tryBuildShape(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                   LayerTreeNode& node, Document& doc);

// Smart Object layer (`SoLd`/`SoLE`/`PlLd` + the document's `lnk2`/`lnk3`/
// `lnkD` linked-file table) -> a real Hazor Smart Object instance whose
// content document is the ORIGINAL embedded file, recovered and converted.
// Returns false when the record carries no usable pixels at all.
//
// `maskApplied` is set when the placement is not an axis-aligned rect and the
// function therefore had to resample the record's mask through it itself; the
// caller must then not apply the mask a second time.
bool tryBuildSmartObject(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                         LayerTreeNode& node, bool& maskApplied);

// An embedded Smart Object source that is itself a PSD/PSB, converted.
struct EmbeddedPsdSource {
    // The source's real layer tree at its own canvas size. Null when the
    // bytes are not a readable PSD/PSB, or when kMaxSmartObjectDepth is hit.
    std::shared_ptr<Document> document;
    // The source file's OWN merged composite: the render the authoring
    // application last stored for that document, at its own size. Used as the instance's provisional
    // pixels so a freshly imported Smart Object is never blank while the
    // render service composites `document` for real. Null when the file
    // carries no usable merged image.
    QImage mergedImage;
};

// Converts an embedded PSD/PSB Smart Object source. Implemented in
// PsdImportService.cpp, which owns the document-level conversion this
// re-enters; the nested document's own Smart Object content documents and its
// (name-prefixed) warnings are appended to `ctx`.
EmbeddedPsdSource importEmbeddedSmartObjectDocument(std::span<const uint8_t> bytes, Context& ctx,
                                                    const QString& layerName);

// Text layer (`TySh` + EngineData) -> TextLayerData, rendered via
// TextRenderer. Returns false when TySh could not be read at all (caller
// falls back to a raster import of the layer's stored pixel cache).
//
// `maskApplied`: see tryBuildSmartObject -- set when the TySh placement turned
// out to be rotated/skewed and the mask was resampled through it here.
bool tryBuildText(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                  LayerTreeNode& node, bool& maskApplied);

// The document's extra channels (Alpha + Spot) from the merged Image Data
// Section's extra planes plus the alpha-metadata image resources. Implemented
// in PsdImportService_Common.cpp; see the comment there for which PSD
// structures are read and what is deliberately NOT treated as a document
// channel.
std::vector<DocumentChannel> importDocumentChannels(const psd::PsdFile& file,
                                                     const QSize& canvasSize,
                                                     QStringList& warnings);

} // namespace psdimport
