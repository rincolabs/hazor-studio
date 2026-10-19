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

#include <QCoreApplication>
#include <QImage>
#include <QTransform>

#include <array>
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
    node.blendMode = mapBlendMode(effective, ctx, node.name);
    node.lockFlags = lockFlagsFromRecord(rec);
    node.collapsed = divider && divider->kind.kind() == psd::SectionType::Kind::ClosedFolder;
    convertLayerEffects(rec, ctx, node.name, node.effects);
    if (divider && divider->blendMode && divider->blendMode->kind() == psd::BlendMode::Kind::PassThrough) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Group \"%1\" uses Pass Through blending; Hazor always isolates group contents, "
            "so layers below the group may look different.").arg(node.name));
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
        const bool clipped = leaf->type == LayerTreeNode::Type::Adjustment && rec.clipping != 0;
        entries.push_back({std::move(leaf), clipped});
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
    return out;
}

// A Smart Object layer -> a real, editable Hazor Smart Object instance: a
// fresh one-layer content Document holding the layer's own cached/rendered
// pixels (the only source this library can recover — see PROVENANCE.md;
// PlLd/SoLd/SoLE's linked/embedded original file is not re-extracted),
// registered under a new docId that the caller must add to the destination
// container (PsdImportResult::embeddedSmartObjects). `node->layer`'s own
// cpuImage becomes that same render, exactly like any already-rendered
// instance — e.g. one just loaded from a .hazor file, see
// ProjectFileService's "sourceGeneration" round-trip — so nothing here
// touches the async SmartObjectRenderService at all.
bool tryBuildSmartObject(const psd::LayerRecord& rec, Context& ctx, const QString& name, LayerTreeNode& node)
{
    QImage pixels = decodeLayerRgba(rec, ctx.file, ctx, name);
    if (pixels.isNull())
        return false;

    auto content = std::make_shared<Document>();
    content->name = name;
    content->size = pixels.size();
    content->resolutionDpi = ctx.dpi;
    content->selection.resize(pixels.width(), pixels.height());

    auto contentNode = std::make_unique<LayerTreeNode>();
    contentNode->type = LayerTreeNode::Type::Layer;
    contentNode->name = name;
    contentNode->layer = std::make_shared<Layer>();
    contentNode->layer->name = name;
    contentNode->layer->owner = contentNode.get();
    contentNode->layer->cpuImage = pixels;
    contentNode->layer->resetTransform = QTransform();
    contentNode->layer->hasResetTransform = true;
    content->roots.push_back(std::move(contentNode));
    content->activeFlatIndex = 0;
    content->selectedFlatIndices = {0};

    const SmartObjectDocId docId = DocumentContainer::generateDocId();
    ctx.embeddedDocs.emplace_back(docId, content);

    auto soData = std::make_shared<SmartObjectData>();
    soData->mode = SmartObjectStorageMode::Embedded;
    soData->docId = docId;
    soData->state.sourceSize = content->size;
    soData->state.sourceDpi = content->resolutionDpi;
    // Matches the fresh content Document's own (untouched) compositionGeneration
    // (0): the staleness test (sourceGeneration != source->compositionGeneration)
    // then reads this instance as already up to date, so it keeps showing
    // `pixels` instead of queuing a redundant re-render on first open.
    soData->state.sourceGeneration = 0;
    node.layer->smartObjectData = std::move(soData);
    node.layer->cpuImage = pixels;

    const QRectF rectPx(rec.rect.left, rec.rect.top,
                        static_cast<qreal>(rec.rect.width()), static_cast<qreal>(rec.rect.height()));
    node.transform = ndcTransformForPixelRect(rectPx, ctx.docSize);

    ctx.warn(QCoreApplication::translate("PsdImportService",
        "Layer \"%1\": its Smart Object was imported as an embedded Smart Object containing a "
        "flattened copy of the rendered pixels; the original linked/embedded source file was "
        "not preserved.").arg(name));
    return true;
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

    bool built = false;
    if (hasText)
        built = tryBuildText(rec, ctx, name, *node);
    if (!built && hasVectorMask && (socoBlock || rec.block(psdKey("vstk"))))
        built = tryBuildShape(rec, ctx, name, *node, scratchDoc);
    if (!built && (gdflBlock || ptflBlock) && !socoBlock) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its %2 fill is not supported and was imported as pixels.")
            .arg(name, gdflBlock ? QStringLiteral("Gradient Fill") : QStringLiteral("Pattern Fill")));
    }
    if (!built && hasSmart)
        built = tryBuildSmartObject(rec, ctx, name, *node);
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
        const QRect ownerRect = currentImageRectPx(*node, ctx.docSize);
        applyLayerMask(rec, ctx, name, *node->layer, ownerRect);
        node->layer->resetTransform = node->transform;
        node->layer->hasResetTransform = true;
    }
    return node;
}

} // namespace

PsdImportResult PsdImportService::import(const psd::PsdFile& file)
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
