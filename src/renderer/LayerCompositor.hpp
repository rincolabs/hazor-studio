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
                     const QTransform& invTargetAccum);

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
