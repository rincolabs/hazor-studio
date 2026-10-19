// PSD/PSB -> Document conversion (document properties, the layer tree, the
// Background layer, and the no-layer-info fallback). Per-content-type
// conversion (adjustments, shapes, text, layer effects) lives in the sibling
// PsdImportService_*.cpp files; the shared placement/mask/blend-mode/color
// helpers they all use are in PsdImportService_Common.cpp.
//
// Conversion policy: a PSD feature is converted when Hazor already has an
// equivalent, using the existing model/types (never a parallel
// representation). A feature with no Hazor equivalent — or one Hazor can
// only approximate — is named in PsdImportResult::warnings (the
// "Compatibility Report" the UI shows after opening) instead of being
// silently dropped or faked. See src/psd/PROVENANCE.md for how this service
// relates to the (independent) psd parser library.

#include "PsdImportService_Internal.hpp"
#include "PsdImportService.hpp"

#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"
#include "psd/Pixels.hpp"
#include "psd/Resources.hpp"
#include "psd/Tagged.hpp"
#include "psd/Tree.hpp"
#include "smartobject/DocumentContainer.hpp"

#include <QByteArray>
#include <QCoreApplication>
#include <QImage>
#include <QTransform>

#include <algorithm>
#include <array>
#include <cmath>
#include <span>

using namespace psdimport;

namespace {

QString qs(const std::string& s) { return QString::fromStdString(s); }

// Deep enough for any real file; layerTreeMaxDepth() prunes anything beyond
// this so a hostile/malformed file cannot make the builder recurse further.
constexpr size_t kMaxGroupDepth = 64;

QImage rgbaImageToQImage(const psd::RgbaImage& img)
{
    if (img.width == 0 || img.height == 0)
        return QImage();
    QImage out(reinterpret_cast<const uchar*>(img.data.data()),
                static_cast<int>(img.width), static_cast<int>(img.height),
                static_cast<int>(img.width) * 4, QImage::Format_RGBA8888);
    return out.copy();
}

const psd::TaggedBlock* findAny(const psd::LayerRecord& rec, std::initializer_list<std::array<uint8_t, 4>> keys)
{
    for (const auto& k : keys) {
        if (const auto* b = rec.block(k))
            return b;
    }
    return nullptr;
}

std::vector<std::unique_ptr<LayerTreeNode>> buildSiblingLevel(std::span<const psd::LayerNode> nodes, size_t depth,
                                                               Context& ctx, Document& scratchDoc);
std::unique_ptr<LayerTreeNode> buildGroupNode(const psd::LayerNode& pn, size_t depth, Context& ctx, Document& scratchDoc);
std::unique_ptr<LayerTreeNode> buildLeafNode(const psd::LayerRecord& rec, Context& ctx, Document& scratchDoc);

void applyCommonGroupProps(const psd::LayerRecord& rec, Context& ctx, LayerTreeNode& node)
{
    node.name = qs(rec.resolvedName());
    node.visible = rec.isVisible();
    node.opacity = static_cast<float>(rec.opacity) / 255.0f;
    const auto divider = rec.sectionDivider();
    const psd::BlendMode effective = (divider && divider->blendMode) ? *divider->blendMode : rec.blendMode;
    const bool passThrough = effective.kind() == psd::BlendMode::Kind::PassThrough;
    // Pass Through is a group's compositing model in Hazor as well
    // (LayerTreeNode::GroupBlendMode), not a blend mode, so it maps onto that
    // field and leaves the blend slot at Normal. Routing it through
    // mapBlendMode would look for a BlendMode that does not exist.
    node.groupBlendMode = passThrough ? LayerTreeNode::GroupBlendMode::PassThrough
                                      : LayerTreeNode::GroupBlendMode::Isolated;
    node.blendMode = passThrough ? BlendMode::Normal : mapBlendMode(effective, ctx, node.name);
    node.lockFlags = lockFlagsFromRecord(rec);
    node.collapsed = divider && divider->kind.kind() == psd::SectionType::Kind::ClosedFolder;
    convertLayerEffects(rec, ctx, node.name, node.effects);
    reportUnsupportedLayerProperties(rec, ctx, node.name, /*clippingHonoured=*/false);
    if (passThrough && node.hasEnabledEffects()) {
        // The one case the native model cannot carry: an effect needs the
        // group's own alpha, which only an isolated composite produces — see
        // LayerTreeNode::isPassThroughGroup().
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Group \"%1\" combines Pass Through blending with layer effects; Hazor isolates "
            "the group's contents in that case, so layers below it may look different.")
            .arg(node.name));
    }
}

std::unique_ptr<LayerTreeNode> buildGroupNode(const psd::LayerNode& pn, size_t depth, Context& ctx, Document& scratchDoc)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->type = LayerTreeNode::Type::Group;
    node->transform = QTransform(); // identity: children carry their own absolute placement

    const auto& layers = ctx.file.layers();
    if (pn.index() < layers.size())
        applyCommonGroupProps(layers[pn.index()], ctx, *node);

    node->children = buildSiblingLevel(pn.children(), depth + 1, ctx, scratchDoc);
    for (auto& child : node->children)
        child->parent = node.get();
    return node;
}

// Bottom-to-top sibling list (PSD file order) at one tree level, paired with
// whether it is a clipped (Single-Layer-Mode-eligible) adjustment.
struct BuiltEntry {
    std::unique_ptr<LayerTreeNode> node;
    bool clippedAdjustment = false;
};

std::vector<std::unique_ptr<LayerTreeNode>> buildSiblingLevel(std::span<const psd::LayerNode> nodes, size_t depth,
                                                               Context& ctx, Document& scratchDoc)
{
    const auto& layers = ctx.file.layers();
    std::vector<BuiltEntry> entries;
    entries.reserve(nodes.size());

    for (const auto& pn : nodes) {
        if (pn.isGroup()) {
            if (depth >= kMaxGroupDepth) {
                ctx.warn(QCoreApplication::translate("PsdImportService",
                    "A group nests deeper than %1 levels; its contents were not imported.").arg(kMaxGroupDepth));
                continue;
            }
            entries.push_back({buildGroupNode(pn, depth, ctx, scratchDoc), false});
            continue;
        }
        if (pn.index() >= layers.size())
            continue;
        const psd::LayerRecord& rec = layers[pn.index()];
        auto leaf = buildLeafNode(rec, ctx, scratchDoc);
        if (!leaf)
            continue; // unsupported adjustment/fill content; already reported
        // PSD's clipping flag maps onto the two Hazor mechanisms: an
        // adjustment/fill layer becomes its host's Single-Layer-Mode child
        // (resolved below), while a pixel/text/shape layer keeps the flag it was
        // already given in buildLeafNode and clips to the sibling below it.
        const bool clippedAdjustment =
            leaf->type == LayerTreeNode::Type::Adjustment && rec.clipping != 0;
        entries.push_back({std::move(leaf), clippedAdjustment});
    }

    // Resolve clipping (PSD "clip to layer below" on an adjustment/fill
    // layer == Hazor's Single-Layer-Mode nesting): nest into the nearest
    // preceding Layer-type entry already placed at this level. A clipped
    // adjustment with no host below it (e.g. the bottom of the stack) stays
    // a normal (unclipped) adjustment instead of being dropped.
    std::vector<BuiltEntry> resolved;
    resolved.reserve(entries.size());
    for (auto& entry : entries) {
        if (entry.clippedAdjustment) {
            LayerTreeNode* host = nullptr;
            for (auto it = resolved.rbegin(); it != resolved.rend(); ++it) {
                if (it->node->type == LayerTreeNode::Type::Layer) {
                    host = it->node.get();
                    break;
                }
            }
            if (host) {
                reclipAdjustmentToHost(*entry.node, *host, ctx.docSize);
                entry.node->parent = host;
                host->children.push_back(std::move(entry.node));
                continue;
            }
        }
        resolved.push_back(std::move(entry));
    }

    // PSD file order is bottom-to-top; Hazor's child-vector convention is
    // top-to-bottom (DocumentCompositor::renderNodesFiltered walks it back to
    // front, index 0 drawn last == on top).
    std::vector<std::unique_ptr<LayerTreeNode>> out;
    out.reserve(resolved.size());
    for (auto it = resolved.rbegin(); it != resolved.rend(); ++it)
        out.push_back(std::move(it->node));

    // Report the clipping flags the native model could not act on, using the
    // compositors' OWN resolution (LayerTreeNode::resolveClipBase) so the
    // warning can never disagree with what is rendered. What is left after a
    // layer or group base resolves is the bottom of a container and an
    // adjustment sitting directly below — both are drawn unclipped.
    for (size_t i = 0; i < out.size(); ++i) {
        if (!out[i] || !out[i]->isClippedLayer())
            continue;
        if (LayerTreeNode::resolveClipBase(out, i))
            continue;
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": it is clipped to the layer below it, but at its level there is "
            "no layer or group below it to clip to, so it was imported unclipped and covers "
            "more than it did in the original file.").arg(out[i]->name));
    }
    return out;
}

// Content-type dispatch for one ordinary (non-group) layer record.
std::unique_ptr<LayerTreeNode> buildLeafNode(const psd::LayerRecord& rec, Context& ctx, Document& scratchDoc)
{
    const QString name = qs(rec.resolvedName());

    const bool hasAdjustmentKey = findAny(rec, {psdKey("levl"), psdKey("curv"), psdKey("hue2"),
                                                psdKey("hue "), psdKey("brit"), psdKey("nvrt"),
                                                psdKey("thrs"), psdKey("post"), psdKey("expA"),
                                                psdKey("vibA"), psdKey("blnc"), psdKey("mixr"),
                                                psdKey("grdm"), psdKey("phfl"), psdKey("selc"),
                                                psdKey("blwh"), psdKey("clrL")}) != nullptr;
    const auto* socoBlock = rec.block(psdKey("SoCo"));
    const auto* gdflBlock = rec.block(psdKey("GdFl"));
    const auto* ptflBlock = rec.block(psdKey("PtFl"));
    const bool hasVectorMask = findAny(rec, {psdKey("vsms"), psdKey("vmsk")}) != nullptr;
    const bool hasText = rec.block(psdKey("TySh")) != nullptr;
    const bool hasSmart = findAny(rec, {psdKey("SoLd"), psdKey("SoLE"), psdKey("PlLd")}) != nullptr;
    // A SoCo fill without a vector mask is a plain full-layer solid-color
    // fill — Hazor's "solidcolor" adjustment. With a vector mask it is a
    // shape's fill instead (handled by tryBuildShape below).
    const bool isSolidFill = socoBlock != nullptr && !hasVectorMask;

    auto node = std::make_unique<LayerTreeNode>();

    if (hasAdjustmentKey || isSolidFill) {
        node->type = LayerTreeNode::Type::Adjustment;
        node->name = name;
        if (!tryBuildAdjustment(rec, ctx, name, *node))
            return nullptr; // no Hazor equivalent; reported by tryBuildAdjustment
        node->visible = rec.isVisible();
        node->opacity = static_cast<float>(rec.opacity) / 255.0f;
        node->blendMode = mapBlendMode(rec.blendMode, ctx, name);
        node->lockFlags = lockFlagsFromRecord(rec);
        node->transform = QTransform();
        reportUnsupportedLayerProperties(rec, ctx, name, /*clippingHonoured=*/true);
        if (node->layer)
            applyLayerMask(rec, ctx, name, *node->layer, QRect(QPoint(0, 0), ctx.docSize));
        return node;
    }

    node->type = LayerTreeNode::Type::Layer;
    node->name = name;
    node->visible = rec.isVisible();
    node->opacity = static_cast<float>(rec.opacity) / 255.0f;
    node->blendMode = mapBlendMode(rec.blendMode, ctx, name);
    node->lockFlags = lockFlagsFromRecord(rec);
    convertLayerEffects(rec, ctx, name, node->effects);
    node->layer = std::make_shared<Layer>();
    node->layer->name = name;
    node->layer->owner = node.get();

    // "Clip to the layer below" on a pixel/text/shape layer is a native flag on
    // the node; the compositors resolve the base from the sibling order, exactly
    // like PSD does (LayerTreeNode::resolveClipBase). A clipped adjustment goes
    // the other way — see buildSiblingLevel.
    node->clipped = rec.clipping != 0;
    reportUnsupportedLayerProperties(rec, ctx, name, /*clippingHonoured=*/true);

    bool built = false;
    // Set by the converters whose placement may not be an axis-aligned rect
    // (rotated type, a transformed Smart Object): they anchor the record's
    // mask themselves, because currentImageRectPx() below cannot.
    bool maskApplied = false;
    if (hasText)
        built = tryBuildText(rec, ctx, name, *node, maskApplied);
    // `vscg` is where a CS6+ shape layer keeps its fill; `vstk` its stroke.
    // Either one (or the legacy `SoCo`) means the vector mask is a shape's
    // outline rather than a mask over pixels.
    if (!built && hasVectorMask
        && (socoBlock || rec.block(psdKey("vstk")) || rec.block(psdKey("vscg"))))
        built = tryBuildShape(rec, ctx, name, *node, scratchDoc);
    if (!built && (gdflBlock || ptflBlock) && !socoBlock) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its %2 fill is not supported and was imported as pixels.")
            .arg(name, gdflBlock ? QStringLiteral("Gradient Fill") : QStringLiteral("Pattern Fill")));
    }
    if (!built && hasSmart)
        built = tryBuildSmartObject(rec, ctx, name, *node, maskApplied);
    if (!built) {
        QImage pixels = decodeLayerRgba(rec, ctx.file, ctx, name);
        if (pixels.isNull()) {
            // Nothing to show for this record: keep the layer (preserves
            // stacking/grouping) but with no pixels, matching an empty
            // layer in the source file.
            node->layer->cpuImage = QImage(1, 1, QImage::Format_RGBA8888);
            node->layer->cpuImage.fill(Qt::transparent);
            node->transform = QTransform();
        } else {
            node->layer->cpuImage = pixels;
            const QRectF rectPx(rec.rect.left, rec.rect.top,
                                static_cast<qreal>(rec.rect.width()), static_cast<qreal>(rec.rect.height()));
            node->transform = ndcTransformForPixelRect(rectPx, ctx.docSize);
        }
        built = true;
    }

    if (node->layer) {
        // The node's own current placement, not the original PSD rect: a
        // shape/text layer's rendered image may not match PSD's stored rect
        // exactly (it is rebuilt from vector/text content), and the mask
        // must be anchored to whatever rect the layer actually occupies.
        if (!maskApplied) {
            const QRect ownerRect = currentImageRectPx(*node, ctx.docSize);
            applyLayerMask(rec, ctx, name, *node->layer, ownerRect);
        }
        node->layer->resetTransform = node->transform;
        node->layer->hasResetTransform = true;
    }
    return node;
}

// The whole document-level conversion, parameterized by how many Smart Object
// sources deep it already is. PsdImportService::import() is this at depth 0;
// importEmbeddedSmartObjectDocument() re-enters it one level deeper for an
// embedded PSD/PSB source.
PsdImportResult importDocument(const psd::PsdFile& file, int smartObjectDepth)
{
    PsdImportResult result;
    const psd::Header& header = file.header;
    result.canvasSize = QSize(static_cast<int>(header.width), static_cast<int>(header.height));

    const auto modeKind = header.colorMode.kind();
    if (modeKind == psd::ColorMode::Kind::Lab || modeKind == psd::ColorMode::Kind::Multichannel) {
        result.ok = false;
        result.errorMessage = QCoreApplication::translate("PsdImportService",
            "This document uses %1 color, which Hazor Studio does not support.")
            .arg(modeKind == psd::ColorMode::Kind::Lab ? QStringLiteral("Lab")
                                                        : QStringLiteral("Multichannel"));
        return result;
    }

    if (const auto res = file.resolution())
        result.resolutionDpi = res->hRes() > 0.0 ? res->hRes() : 72.0;
    else
        result.resolutionDpi = 72.0;

    Context ctx{file, result.canvasSize, result.resolutionDpi, {}};
    ctx.smartObjectDepth = smartObjectDepth;

    switch (modeKind) {
    case psd::ColorMode::Kind::Grayscale:
        result.colorMode = QStringLiteral("Grayscale");
        break;
    case psd::ColorMode::Kind::Cmyk:
        result.colorMode = QStringLiteral("CMYK Color");
        break;
    case psd::ColorMode::Kind::Rgb:
        result.colorMode = QStringLiteral("RGB Color");
        break;
    default: {
        result.colorMode = QStringLiteral("RGB Color");
        const QString modeName = modeKind == psd::ColorMode::Kind::Bitmap ? QStringLiteral("Bitmap")
            : modeKind == psd::ColorMode::Kind::Indexed ? QStringLiteral("Indexed Color")
            : modeKind == psd::ColorMode::Kind::Duotone ? QStringLiteral("Duotone")
                                                         : QStringLiteral("This document's");
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "%1 color could not be preserved; the document was imported as RGB.").arg(modeName));
        break;
    }
    }
    result.bitDepth = header.depth == 16 ? 16 : 8;
    if (header.depth == 32) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "32-bit (HDR) pixel data was converted to 8-bit for editing."));
    }

    if (const auto icc = file.iccProfile()) {
        QByteArray bytes(reinterpret_cast<const char*>(icc->data()), static_cast<int>(icc->size()));
        ColorProfile profile = ColorProfile::fromIccBytes(bytes);
        if (profile.isValid()) {
            profile.setSource(ColorProfileSource::EmbeddedInFile);
            result.colorProfile = profile;
            result.profileSource = ColorProfileSource::EmbeddedInFile;
        }
    }

    if (const auto* guideRes = file.resource(1032)) {
        const auto& d = guideRes->data;
        auto rd32 = [&](size_t at) -> std::optional<uint32_t> {
            if (at + 4 > d.size()) return std::nullopt;
            return (uint32_t(d[at]) << 24) | (uint32_t(d[at + 1]) << 16) | (uint32_t(d[at + 2]) << 8) | uint32_t(d[at + 3]);
        };
        if (const auto count = rd32(12)) {
            int nextId = 1;
            for (uint32_t i = 0; i < *count; ++i) {
                const size_t at = 16 + static_cast<size_t>(i) * 5;
                const auto loc = rd32(at);
                if (!loc || at + 4 >= d.size())
                    break;
                const uint8_t dir = d[at + 4];
                const double pos = static_cast<int32_t>(*loc) / 32.0;
                Guide g;
                g.orientation = dir == 1 ? GuideOrientation::Horizontal : GuideOrientation::Vertical;
                g.position = pos;
                g.id = nextId++;
                result.guides.push_back(g);
            }
        }
    }

    Document scratchDoc;
    scratchDoc.size = result.canvasSize;

    if (!file.layers().empty()) {
        const auto tree = file.layerTreeMaxDepth(kMaxGroupDepth);
        result.roots = buildSiblingLevel(tree, 0, ctx, scratchDoc);

        // The source file's Background layer: the bottom-most record stored
        // without a transparency (-1) channel is locked, whatever its name
        // says. Only trusted when that record is itself a top-level
        // (non-grouped) leaf — i.e. the first tree entry really is record 0
        // — so a mismatch between the PSD's bottom record and Hazor's bottom
        // root (e.g. the whole bottom of the stack sits inside a group)
        // never locks the wrong layer. It ends up last in Hazor's
        // (top-to-bottom) roots.
        const auto& rec0 = file.layers().front();
        const bool opaqueRecord = !rec0.channels.empty()
            && std::all_of(rec0.channels.begin(), rec0.channels.end(), [](const psd::ChannelData& c) { return c.id >= 0; });
        if (opaqueRecord && !tree.empty() && !tree.front().isGroup() && tree.front().index() == 0 && !result.roots.empty()) {
            auto* bottom = result.roots.back().get();
            if (bottom->type == LayerTreeNode::Type::Layer && bottom->layer && !bottom->layer->isTextLayer()
                && !bottom->layer->isShapeLayer()) {
                bottom->lockFlags |= LockTransparent | LockPosition;
            }
        }
    }

    if (result.roots.empty()) {
        // No layer info at all (flattened PSD), or every layer record was
        // dropped as unsupported: fall back to the merged composite.
        const auto composite = file.compositeRgba8();
        if (composite.hasValue()) {
            QImage img = rgbaImageToQImage(composite.value());
            if (!img.isNull()) {
                auto node = std::make_unique<LayerTreeNode>();
                node->type = LayerTreeNode::Type::Layer;
                node->name = QStringLiteral("Background");
                node->layer = std::make_shared<Layer>();
                node->layer->name = node->name;
                node->layer->owner = node.get();
                node->layer->cpuImage = img;
                node->transform = QTransform();
                node->layer->resetTransform = node->transform;
                node->layer->hasResetTransform = true;
                if (!file.mergedHasAlpha())
                    node->lockFlags = LockTransparent | LockPosition;
                result.roots.push_back(std::move(node));
                if (!file.layers().empty()) {
                    ctx.warn(QCoreApplication::translate("PsdImportService",
                        "The document's layers could not be read; it was imported as a single "
                        "flattened Background layer."));
                }
            }
        } else {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Neither the document's layers nor its merged image could be read; the document "
                "was imported with no content."));
        }
    }

    result.ok = true;
    result.warnings = ctx.warnings;
    result.embeddedSmartObjects = std::move(ctx.embeddedDocs);
    return result;
}

} // namespace

namespace psdimport {

namespace {

// "8BPS": the magic every PSD and PSB starts with. An embedded Smart Object
// source is whatever file was placed (another PSD, a PNG, a JPEG, an AI), so
// the bytes have to be sniffed before they reach the PSD parser.
bool looksLikePsd(std::span<const uint8_t> bytes)
{
    return bytes.size() >= 4 && bytes[0] == '8' && bytes[1] == 'B'
        && bytes[2] == 'P' && bytes[3] == 'S';
}

// The affine mapping a placed source's own pixel grid (origin top-left, Y
// down, `sourceSize` pixels) onto the destination quad the file recorded.
// Corners are TL, TR, BR, BL; a parallelogram is exactly an affine image of
// the source rectangle, which is why PlacedQuad::isAffine() is the
// precondition for using this at all.
QTransform quadToTransform(const psd::PlacedQuad& quad, const QSize& sourceSize)
{
    const double w = std::max(1, sourceSize.width());
    const double h = std::max(1, sourceSize.height());
    return QTransform((quad.x(1) - quad.x(0)) / w, (quad.y(1) - quad.y(0)) / w,
                      (quad.x(3) - quad.x(0)) / h, (quad.y(3) - quad.y(0)) / h,
                      quad.x(0), quad.y(0));
}

// Can this placement be expressed as the plain pixel RECT that
// ndcTransformForPixelRect()/currentImageRectPx() assume? Rotation, skew and
// mirroring cannot, and have to go through ndcTransformForImageMap() instead.
bool isUprightScale(const QTransform& t)
{
    return std::abs(t.m12()) < 1e-6 && std::abs(t.m21()) < 1e-6
        && t.m11() > 0.0 && t.m22() > 0.0
        && std::abs(t.m13()) < 1e-12 && std::abs(t.m23()) < 1e-12;
}

// A one-layer content Document holding `pixels`: the shape a placed
// raster source (PNG/JPEG) takes, and the shape of the fallback when no
// source file can be recovered at all.
std::shared_ptr<Document> singleLayerDocument(const QString& name, const QImage& pixels, double dpi)
{
    auto doc = std::make_shared<Document>();
    doc->name = name;
    doc->size = pixels.isNull() ? QSize(1, 1) : pixels.size();
    doc->resolutionDpi = dpi > 0.0 ? dpi : 72.0;
    doc->selection.resize(doc->size.width(), doc->size.height());

    auto contentNode = std::make_unique<LayerTreeNode>();
    contentNode->type = LayerTreeNode::Type::Layer;
    contentNode->name = name;
    contentNode->layer = std::make_shared<Layer>();
    contentNode->layer->name = name;
    contentNode->layer->owner = contentNode.get();
    contentNode->layer->cpuImage = pixels;
    contentNode->layer->resetTransform = QTransform();
    contentNode->layer->hasResetTransform = true;
    doc->roots.push_back(std::move(contentNode));
    doc->activeFlatIndex = 0;
    doc->selectedFlatIndices = {0};
    return doc;
}

} // namespace

EmbeddedPsdSource importEmbeddedSmartObjectDocument(std::span<const uint8_t> bytes, Context& ctx,
                                                     const QString& layerName)
{
    EmbeddedPsdSource out;
    const int depth = ctx.smartObjectDepth + 1;
    if (depth >= kMaxSmartObjectDepth) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its Smart Object sources nest more than %2 levels deep; the innermost "
            "one was imported as flattened pixels.").arg(layerName).arg(kMaxSmartObjectDepth));
        return out;
    }

    const auto parsed = psd::PsdFile::fromBytes(bytes);
    if (!parsed.hasValue()) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its Smart Object's source document could not be read (%2); the layer's "
            "flattened pixels were used instead.")
            .arg(layerName, QString::fromStdString(parsed.error().message())));
        return out;
    }
    const psd::PsdFile& file = parsed.value();

    PsdImportResult nested = importDocument(file, depth);
    if (!nested.ok) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its Smart Object's source document could not be converted (%2); the "
            "layer's flattened pixels were used instead.").arg(layerName, nested.errorMessage));
        return out;
    }

    auto doc = std::make_shared<Document>();
    doc->name = layerName;
    doc->size = nested.canvasSize;
    doc->resolutionDpi = nested.resolutionDpi;
    doc->colorMode = nested.colorMode;
    doc->bitDepth = nested.bitDepth;
    doc->setColorProfile(nested.colorProfile);
    doc->setProfileSource(nested.profileSource);
    if (!doc->size.isEmpty())
        doc->selection.resize(doc->size.width(), doc->size.height());
    doc->guideManager.setGuides(std::move(nested.guides));
    doc->roots = std::move(nested.roots);
    doc->activeFlatIndex = doc->roots.empty() ? -1 : 0;
    doc->selectedFlatIndices.clear();
    if (!doc->roots.empty())
        doc->selectedFlatIndices.insert(0);

    // A Smart Object inside a Smart Object source is still an instance of a
    // document in THIS file's container (SmartObjectData holds a docId, not a
    // pointer — see SmartObjectData.hpp), so its content document has to
    // travel up to the one container the caller will register everything in.
    for (auto& entry : nested.embeddedSmartObjects)
        ctx.embeddedDocs.push_back(std::move(entry));
    for (const QString& line : nested.warnings) {
        ctx.warn(QCoreApplication::translate("PsdImportService", "Smart Object \"%1\" → %2")
                     .arg(layerName, line));
    }

    out.document = std::move(doc);
    // The source's own last stored render: shown until Hazor has composited
    // the content document itself, so the instance is never blank.
    if (const auto composite = file.compositeRgba8(); composite.hasValue())
        out.mergedImage = rgbaImageToQImage(composite.value());
    return out;
}

// A Smart Object layer -> a real, editable Hazor Smart Object instance whose
// content document is the ORIGINAL embedded file (`lnk2`/`lnk3`/
// `lnkD` + the layer's `Idnt`), converted in full: a placed PSD keeps its own
// layer tree, a placed PNG/JPEG becomes a one-layer document. Only when no
// source can be recovered does it fall back to a flattened copy of the
// layer's cached render, and it says so.
//
// The content document is registered under a fresh docId the caller must add
// to the destination container (PsdImportResult::embeddedSmartObjects).
bool tryBuildSmartObject(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                         LayerTreeNode& node, bool& maskApplied)
{
    maskApplied = false;

    const auto placedResult = psd::readPlacedLayer(rec);
    const psd::PlacedLayer* placed = placedResult.hasValue() ? &placedResult.value() : nullptr;
    const psd::LinkedFile* linked = (placed && !placed->uniqueId.empty())
        ? psd::findLinkedFile(ctx.linkedFiles(), placed->uniqueId) : nullptr;

    std::shared_ptr<Document> content;
    QImage provisional;

    // Re-placing the source is only an improvement when the placement itself
    // can be reproduced. A warped or perspective-placed Smart Object — the
    // normal case in a mockup template — cannot be, so such a layer keeps the
    // record's own cached render: the stored pixels, warp already baked in,
    // placed at the rect it stored them for. That is not a degraded fallback,
    // it is the SAME representation Hazor's own Distort produces (warpDistort()
    // bakes cv::warpPerspective into cpuImage and leaves an axis-aligned
    // placementTransform() behind), and an instance composites as an ordinary
    // raster layer, so the perspective renders correctly everywhere.
    //
    // Why not have both — the real source document AND those baked pixels?
    // It works until the first re-render, and then breaks unrecoverably:
    // onSmartObjectRenderFinished() writes the content document's own
    // (unwarped) composite straight into cpuImage without touching
    // node->transform and without passing through any warp stage — nothing in
    // the Smart Object path knows about distortData — so the layer would flatten
    // AND take the source's aspect ratio into a rect sized for the warped
    // footprint. Render commits are caches that deliberately bypass undo, so
    // there would be no way back. Reproducing a warped Smart Object needs that
    // render commit to re-apply the stored quad, which is a Hazor feature, not
    // something this converter can stand in for.
    const bool placementReproducible = placed && placed->isPlainAffine();
    if (!placementReproducible) {
        const bool perspective = placed && placed->nonAffineTransform && placed->transform
            && !placed->nonAffineTransform->equals(*placed->transform);
        ctx.warn(placed && placed->warped
            ? QCoreApplication::translate("PsdImportService",
                  "Layer \"%1\": its Smart Object is warped, which Hazor cannot reproduce. It was "
                  "imported as a Smart Object holding a flattened copy of the warped render "
                  "stored in the file, so it looks right but its original source file was not "
                  "preserved.")
                  .arg(layerName)
            : perspective
            ? QCoreApplication::translate("PsdImportService",
                  "Layer \"%1\": its Smart Object is placed in perspective, which Hazor layer "
                  "transforms cannot express. It was imported as a Smart Object holding a "
                  "flattened copy of the render stored in the file, so it looks right but its "
                  "original source file was not preserved.").arg(layerName)
            : QCoreApplication::translate("PsdImportService",
                  "Layer \"%1\": its Smart Object's placement could not be read; it was imported "
                  "as a Smart Object holding a flattened copy of the rendered pixels.")
                  .arg(layerName));
    }

    if (placementReproducible && linked && linked->isEmbedded()) {
        const std::span<const uint8_t> bytes(linked->data.data(), linked->data.size());
        if (looksLikePsd(bytes)) {
            EmbeddedPsdSource source = importEmbeddedSmartObjectDocument(bytes, ctx, layerName);
            content = std::move(source.document);
            provisional = std::move(source.mergedImage);
        } else {
            QImage decoded;
            if (decoded.loadFromData(QByteArray::fromRawData(
                    reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size())))) {
                provisional = decoded.convertToFormat(QImage::Format_RGBA8888);
                content = singleLayerDocument(qs(linked->fileName), provisional,
                                              placed->sourceResolution > 0.0 ? placed->sourceResolution : ctx.dpi);
            } else {
                ctx.warn(QCoreApplication::translate("PsdImportService",
                    "Layer \"%1\": its Smart Object's source file (\"%2\") is in a format Hazor "
                    "cannot decode; the layer's flattened pixels were used instead.")
                    .arg(layerName, qs(linked->fileName)));
            }
        }
    } else if (placementReproducible && linked) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its Smart Object links to the external file \"%2\", which is not stored "
            "inside this document; the layer's flattened pixels were used instead.")
            .arg(layerName, qs(linked->fileName)));
    } else if (placementReproducible && !linked) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": the file behind its Smart Object is not recorded in this document; the "
            "layer's flattened pixels were used instead.").arg(layerName));
    }

    const bool recovered = content != nullptr;
    if (!recovered) {
        QImage pixels = decodeLayerRgba(rec, ctx.file, ctx, layerName);
        if (pixels.isNull())
            return false;
        provisional = pixels;
        content = singleLayerDocument(layerName, pixels, ctx.dpi);
    }

    const QSize sourceSize = content->size.isEmpty() ? QSize(1, 1) : content->size;
    if (provisional.size() != sourceSize) {
        // The instance's pixels have to cover exactly the content document's
        // canvas: that is the extent the placement below maps. A mismatch
        // means the file's merged image is unusable here, so start transparent
        // and let the first real render fill it in.
        provisional = QImage(sourceSize, QImage::Format_RGBA8888);
        provisional.fill(Qt::transparent);
    }

    // Where the source lands. `Trnf` is a quad in destination pixels covering
    // the source's FULL extent, which is exactly the instance image's extent.
    QTransform imageToDocPx;
    if (recovered && placementReproducible && placed->transform) {
        imageToDocPx = quadToTransform(*placed->transform, sourceSize);
    } else {
        // No usable placement (or a flattened fallback whose pixels ARE the
        // stored rect): put the image where the file last rendered it.
        const QRectF stored(rec.rect.left, rec.rect.top,
                            static_cast<qreal>(rec.rect.width()), static_cast<qreal>(rec.rect.height()));
        const QRectF target = stored.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(sourceSize)) : stored;
        imageToDocPx = QTransform::fromScale(target.width() / sourceSize.width(),
                                             target.height() / sourceSize.height());
        imageToDocPx *= QTransform::fromTranslate(target.left(), target.top());
    }

    const SmartObjectDocId docId = DocumentContainer::generateDocId();
    ctx.embeddedDocs.emplace_back(docId, content);

    auto soData = std::make_shared<SmartObjectData>();
    soData->mode = SmartObjectStorageMode::Embedded;
    soData->docId = docId;
    soData->state.sourceSize = content->size;
    soData->state.sourceDpi = content->resolutionDpi;
    // `provisional` is PHOTOSHOP's render of the source, not Hazor's, so the
    // instance really is stale: leaving it marked never-rendered is what makes
    // refreshAllSmartObjectInstances() composite the content document for real
    // on open, exactly as ImageController::makeSmartObjectNode relies on.
    soData->state.sourceGeneration = kSmartObjectNeverRendered;
    node.layer->smartObjectData = std::move(soData);
    node.layer->cpuImage = provisional;

    if (isUprightScale(imageToDocPx)) {
        node.transform = ndcTransformForPixelRect(
            QRectF(imageToDocPx.dx(), imageToDocPx.dy(),
                   imageToDocPx.m11() * sourceSize.width(), imageToDocPx.m22() * sourceSize.height()),
            ctx.docSize);
    } else {
        node.transform = ndcTransformForImageMap(imageToDocPx, sourceSize, ctx.docSize);
        applyLayerMaskThroughTransform(rec, ctx, layerName, *node.layer, sourceSize, imageToDocPx);
        maskApplied = true;
    }

    return true;
}

} // namespace psdimport

PsdImportResult PsdImportService::import(const psd::PsdFile& file)
{
    return importDocument(file, 0);
}

PsdImportResult PsdImportService::importFromBytes(const QByteArray& bytes)
{
    const auto parsed = psd::PsdFile::fromBytes(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.constData()), static_cast<size_t>(bytes.size())));
    if (!parsed.hasValue()) {
        PsdImportResult result;
        result.ok = false;
        result.errorMessage = QString::fromStdString(parsed.error().message());
        return result;
    }
    return import(parsed.value());
}
