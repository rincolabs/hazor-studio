#pragma once
#include "RenderContext.hpp"
#include <QImage>
#include <set>

class Document;
class LayerTreeNode;

class DocumentCompositor {
public:
    // Renders all visible layers into a single QImage at doc's native size.
    // Single source of truth for CPU compositing — used by Export, Flatten, and
    // any other operation that needs a pixel-accurate flattened image.
    static QImage composite(const Document* doc, const RenderContext& ctx);
    static QImage compositeOnlyFlatIndex(const Document* doc, int flatIndex,
                                         const RenderContext& ctx);
    static QImage compositeFromFlatIndex(const Document* doc, int flatIndex,
                                         const RenderContext& ctx);

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
};
