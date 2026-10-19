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

#include <QColor>
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
#include <vector>

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

    void warn(const QString& line) { warnings.push_back(line); }
};

// Node transform placing a layer whose own rendered image is exactly
// `rectPx.size()` pixels at document-pixel rect `rectPx` (top-left origin, Y
// down) — the inverse of the image-pixel -> NDC mapping every render path
// (DocumentCompositor::buildImgToCanvas, ImageController::createTextLayer)
// assumes for a layer's own full-size image. Group nodes stay identity: PSD
// layer rects are already absolute document coordinates regardless of
// nesting, so composing group transforms would double-apply the offset.
QTransform ndcTransformForPixelRect(const QRectF& rectPx, const QSize& docSize);

// Maps a psd::BlendMode onto Hazor's BlendMode. Every Hazor mode has a
// same-named PSD counterpart; a PSD mode with no Hazor equivalent (Dissolve,
// Linear Burn/Dodge, Darker/Lighter Color, Vivid/Linear/Pin Light, Hard Mix,
// Subtract, Divide, group Pass Through) maps to Normal and is reported once
// per affected layer via ctx.warn().
BlendMode mapBlendMode(const psd::BlendMode& mode, Context& ctx, const QString& layerName);

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

// Text layer (`TySh` + EngineData) -> TextLayerData, rendered via
// TextRenderer. Returns false when TySh could not be read at all (caller
// falls back to a raster import of the layer's stored pixel cache).
bool tryBuildText(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                  LayerTreeNode& node);

} // namespace psdimport
