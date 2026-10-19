#pragma once

#include <QMatrix4x4>
#include <QPointF>
#include <QTransform>
#include <vector>
#include <memory>

class Document;
class LayerTreeNode;
class GPUViewport;

class LayerCompositor {
public:
    LayerCompositor() = default;

    // Main entry: walks all visible layers bottom→top, handles
    // group FBOs, blend modes, masks, and delegating tile draws
    // to GPUViewport.
    void composite(Document* doc,
                   const QMatrix4x4& viewMvp,
                   const QPointF& canvasHalfExtents,
                   bool editingMask,
                   bool grayscaleMaskView,
                   bool hasPreview,
                   unsigned int previewTexture,
                   bool forceFullResLod,
                   bool showMaskOverlay,
                   float maskOverlayOpacity,
                   GPUViewport* gpu);

private:
    // soleNode: when non-null, every node but that one is skipped. Used to
    // re-render a single clipping base into the clip-alpha buffer through the
    // very same draw code the stack uses, so the alpha the clip reads cannot
    // disagree with the pixels the base actually shows (§4.6). Its own
    // opacity/mask stay in — alphaBase includes them — while its blend mode has
    // no effect on the alpha reaching a transparent buffer, so nothing has to be
    // overridden for the capture.
    //
    // A Group base works through the very same mechanism: the filter runs before
    // the Group branch, so the group renders with whichever of the three models
    // (§4.5) it asks for, and its children follow with the filter cleared. Over
    // the transparent capture buffer all three reduce to "the group's own content
    // × its opacity", which is what DocumentCompositor's buildGroupStage builds.
    void renderNodes(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes,
                     Document* doc,
                     const QMatrix4x4& viewMvp,
                     const QPointF& canvasHalfExtents,
                     bool editingMask,
                     bool hasPreview,
                     unsigned int previewTexture,
                     bool forceFullResLod,
                     bool showMaskOverlay,
                     float maskOverlayOpacity,
                     GPUViewport* gpu,
                     bool hasTargetAccum,
                     const QTransform& targetAccum,
                     const QTransform& invTargetAccum,
                     const LayerTreeNode* soleNode = nullptr);

    // Draws ONLY a layer's masked base into the current canvas-NDC group FBO
    // (Normal blend, opacity 1). The GPU equivalent of maskedBaseImage() and the
    // base of the Single-Layer-Mode (clipped adjustment) FBO isolation — the
    // layer's real opacity/blend are applied once by popGroupFbo. A lean copy of
    // the live layer-draw block (no effects/preview/blend-mode paths) so that
    // path stays unchanged for every other layer.
    void drawLayerBaseIsolated(LayerTreeNode* node,
                               Document* doc,
                               GPUViewport* gpu,
                               bool hasPreview = false,
                               unsigned int previewTexture = 0);
};
