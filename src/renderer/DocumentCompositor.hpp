#pragma once
#include "RenderContext.hpp"
#include <QImage>
#include <set>

class Document;
class LayerTreeNode;
enum class BlendMode;

class DocumentCompositor {
public:
    // Composites `stage` over `target` (both ARGB32_Premultiplied, same
    // canvas grid) with a node's blend mode and opacity — the one CPU
    // execution of the blend spec (renderer/BlendRules.hpp): modes QPainter
    // implements run through the raster engine, the rest per pixel through
    // blendmath. Public so the tools that blend into layer pixels (the
    // gradient tool) blend exactly like the projection does.
    static void compositeStage(QImage& target, const QImage& stage,
                               BlendMode mode, float opacity);

    // Renders all visible layers into a single QImage at doc's native size.
    // Single source of truth for CPU compositing — used by Export, Flatten, and
    // any other operation that needs a pixel-accurate flattened image.
    static QImage composite(const Document* doc, const RenderContext& ctx);

    // rawPixelNode: when non-null (and included by flatIndex), that node is
    // rendered from its raw stored pixels instead of its baked effected image
    // — its own layer effects and Single-Layer-Mode (clipped) adjustments are
    // skipped. Clone/heal sampling passes the paint destination layer here:
    // those effects re-apply live at render time regardless, so baking them
    // into the sample too would double them. See drawLayerNode.
    static QImage compositeOnlyFlatIndex(const Document* doc, int flatIndex,
                                         const RenderContext& ctx,
                                         const LayerTreeNode* rawPixelNode = nullptr);
    static QImage compositeFromFlatIndex(const Document* doc, int flatIndex,
                                         const RenderContext& ctx,
                                         const LayerTreeNode* rawPixelNode = nullptr);

    // Renders only the given Layer nodes, in their real stacking order, using the
    // exact same pipeline as the full composite (blend modes, opacity, masks,
    // clipped Single-Layer-Mode adjustments baked via computeEffectedImage). When
    // applyAdjustments is true, visible Normal-Mode adjustment siblings are also
    // applied to the composited subset; pass false to consume only the included
    // layers and their own clipped adjustments (e.g. Merge Down). The visual
    // source of truth for layer-merge operations.
    //
    // ancestorGroupsPassThrough: when true, groups reached by the filtered walk
    // composite their children directly — no isolation, so group opacity/blend/
    // effects are NOT baked into the result pixels. Use it when the merge result
    // stays nested inside those groups (Merge Down of two siblings): the group
    // properties remain live on the tree and would otherwise apply twice. Only
    // valid when every traversed group is a common ancestor of all included
    // layers.
    static QImage compositeSubset(const Document* doc,
                                  const std::set<const LayerTreeNode*>& includeLayers,
                                  bool applyAdjustments,
                                  const RenderContext& ctx,
                                  bool ancestorGroupsPassThrough = false);

    // Renders a single Layer/Group/Solid-Color-adjustment node in isolation —
    // its own transform/opacity/mask/effects/blend against a transparent
    // backdrop, excluding every other node in the document (used by Export
    // Layer/Group As...). Rendered at the document's canvas size/alignment,
    // like every other CPU composite (a nested adjustment's mask is anchored
    // in absolute document-pixel space, so the render target can't be
    // shifted off that grid without misaligning it). Node visibility is
    // bypassed for `node` itself but honoured for its descendants. Returns a
    // null QImage when the node has no renderable content of its own (e.g. a
    // Curves/Levels adjustment).
    //
    // trimToLayerBounds selects the two dimension modes: false keeps the
    // image canvas-sized with the node positioned within it (Preserve Canvas
    // Size); true crops to the composited result's own non-transparent
    // bounds (Layer Bounds) — null when the node has no on-canvas pixels at
    // all (e.g. it sits entirely outside the canvas).
    static QImage compositeIsolatedNode(const Document* doc, const LayerTreeNode* node,
                                        bool trimToLayerBounds);
};
