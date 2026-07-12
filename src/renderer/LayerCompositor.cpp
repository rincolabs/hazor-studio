#include "LayerCompositor.hpp"
#include "GPUViewport.hpp"
#include "core/AdjustmentTypes.hpp"
#include "core/HueSaturationData.hpp"
#include "core/SolidColorData.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/ViewportCamera.hpp"
#include "core/RenderScheduler.hpp"

#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <QDebug>

#include <array>

// Per-frame compositor tracing. These run for every layer on every composite,
// so they are gated behind an env flag (BLEND_DEBUG=1) to keep brush strokes
// fluid — the logging is preserved, just off by default. The idiom evaluates the
// "<<" chain only when enabled, costing nothing on the hot path otherwise.
namespace {
const bool gBlendDebug = qEnvironmentVariableIntValue("BLEND_DEBUG") != 0;
}
#define BLENDDBG() if (!gBlendDebug) {} else qDebug()

// True if any visible node anywhere in the tree uses a non-Normal blend mode.
// Such a node reads the backdrop, so the live composite must isolate the whole
// stack into an offscreen buffer (see composite()) to avoid blending against the
// canvas decorations. When nothing blends, we skip that and render straight to
// the screen exactly as before.
static bool treeHasBlendMode(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes)
{
    for (const auto& n : nodes) {
        if (!n->isVisible()) continue;
        if (n->blendMode() != BlendMode::Normal) return true;
        if (n->type == LayerTreeNode::Type::Group && treeHasBlendMode(n->children))
            return true;
    }
    return false;
}

// True if any visible Normal-Mode adjustment exists in the stack (root level
// or inside groups). Like a blend mode, an adjustment reads the backdrop, so
// the live composite must isolate the whole stack into an offscreen buffer —
// the adjustment then applies to actual layer content over a transparent
// backdrop, never the canvas checkerboard/decorations. Single-Layer-Mode
// adjustments (children of Layer nodes) are baked into the parent's effected
// texture and don't need isolation, hence the Group-only recursion.
static bool treeHasStackAdjustment(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes)
{
    for (const auto& n : nodes) {
        if (!n->isVisible()) continue;
        if (n->isAdjustmentLayer()) return true;
        if (n->type == LayerTreeNode::Type::Group && treeHasStackAdjustment(n->children))
            return true;
    }
    return false;
}

static QMatrix4x4 layerRectSubMatrix(const QRect& bounds, const QSize& baseSize)
{
    const float baseW = static_cast<float>(baseSize.width());
    const float baseH = static_cast<float>(baseSize.height());
    const float left = static_cast<float>(bounds.x()) / baseW * 2.0f - 1.0f;
    const float right = static_cast<float>(bounds.x() + bounds.width()) / baseW * 2.0f - 1.0f;
    const float top = 1.0f - static_cast<float>(bounds.y()) / baseH * 2.0f;
    const float bottom = 1.0f - static_cast<float>(bounds.y() + bounds.height()) / baseH * 2.0f;

    QMatrix4x4 m;
    m.translate((left + right) * 0.5f, (top + bottom) * 0.5f);
    m.scale((right - left) * 0.5f, (top - bottom) * 0.5f);
    return m;
}

static bool maskOverlayGeometry(const Layer* layer,
                                QMatrix4x4* mvpSubMatrix,
                                QVector2D* maskUvOffset,
                                QVector2D* maskUvScale)
{
    if (!layer || !mvpSubMatrix || !maskUvOffset || !maskUvScale)
        return false;

    const QSize base = layer->rasterBaseSize();
    const int mW = layer->maskImage.width();
    const int mH = layer->maskImage.height();
    if (layer->isShapeLayer()) {
        mvpSubMatrix->setToIdentity();
        *maskUvOffset = QVector2D(0.0f, 0.0f);
        *maskUvScale = QVector2D(1.0f, 1.0f);
        return mW > 0 && mH > 0;
    }

    const QRect bounds = layer->maskTargetBounds();
    if (base.width() <= 0 || base.height() <= 0 || mW <= 0 || mH <= 0 || bounds.isEmpty())
        return false;

    *mvpSubMatrix = layerRectSubMatrix(bounds, base);
    *maskUvOffset = QVector2D(static_cast<float>(bounds.left() - layer->maskOrigin.x()) / mW,
                              static_cast<float>(bounds.top() - layer->maskOrigin.y()) / mH);
    *maskUvScale = QVector2D(static_cast<float>(bounds.width()) / mW,
                             static_cast<float>(bounds.height()) / mH);
    return true;
}

void LayerCompositor::composite(Document* doc,
                                 const QMatrix4x4& viewMvp,
                                 const QPointF& canvasHalfExtents,
                                 bool editingMask,
                                 bool grayscaleMaskView,
                                 bool hasPreview,
                                 unsigned int previewTexture,
                                 bool forceFullResLod,
                                 bool showMaskOverlay,
                                 float maskOverlayOpacity,
                                 GPUViewport* gpu)
{
    Q_UNUSED(grayscaleMaskView);
    const QTransform identity;

    const bool isolate = treeHasBlendMode(doc->roots)
                      || treeHasStackAdjustment(doc->roots)
                      || !doc->roots.empty();
    BLENDDBG() << "[BLENDBUG] composite path=" << (isolate ? "ISOLATION-FBO" : "direct-to-screen")
             << "roots=" << doc->roots.size()
             << "editingMask=" << editingMask
             << "hasPreview=" << hasPreview;
    if (!isolate) {
        // Empty document: nothing needs the canvas FBO.
        renderNodes(doc->roots, doc, viewMvp, canvasHalfExtents,
                    editingMask, hasPreview, previewTexture, forceFullResLod,
                    showMaskOverlay, maskOverlayOpacity, gpu,
                    false, identity, identity);
        return;
    }

    // Render the whole live stack into an offscreen, canvas-NDC FBO (transparent
    // backdrop) and composite it back over the canvas decorations — mirroring
    // the CPU projection. This keeps move/opacity/adjustment live frames in the
    // same document-pixel space as the committed CPU render instead of sampling
    // each layer directly through the screen viewport transform.
    gpu->pushGroupFbo(doc->size.width(), doc->size.height());
    renderNodes(doc->roots, doc, viewMvp, canvasHalfExtents,
                editingMask, hasPreview, previewTexture, forceFullResLod,
                showMaskOverlay, maskOverlayOpacity, gpu,
                true, identity, identity);

    LayerTreeNode liveRoot;
    liveRoot.type = LayerTreeNode::Type::Group;
    liveRoot.name = QStringLiteral("<live-root>");
    liveRoot.setBaseBlendMode(BlendMode::Normal);
    liveRoot.setBaseOpacity(1.0f);
    gpu->popGroupFbo(&liveRoot, canvasHalfExtents, viewMvp, editingMask);
    gpu->mainProgram()->bind();
    gpu->bindMainVao();
}

void LayerCompositor::renderNodes(
    const std::vector<std::unique_ptr<LayerTreeNode>>& nodes,
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
    const QTransform& invTargetAccum)
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()
                               ? QOpenGLContext::currentContext()->functions()
                               : nullptr;
    if (!gl) return;

    // canvasHalfExtents (= docPx / viewportPx) is the canvas→viewport footprint
    // applied when drawing to the screen. When we are rendering into a group FBO
    // (hasTargetAccum), we draw in pure canvas-NDC instead: the FBO is canvas
    // sized with clip range [-1,1], so baking hx,hy in here would push content
    // past the FBO edges and clip it whenever the canvas is larger than the
    // viewport (hx or hy > 1). The hx,hy footprint is re-applied once when the
    // FBO is composited back to the screen (popGroupFbo), mirroring how the CPU
    // projection texture is drawn.
    float hx = hasTargetAccum ? 1.0f : static_cast<float>(canvasHalfExtents.x());
    float hy = hasTargetAccum ? 1.0f : static_cast<float>(canvasHalfExtents.y());

    for (int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i) {
        auto* node = nodes[i].get();
        if (!node->isVisible()) continue;

        // ── Normal-Mode adjustment: full-canvas pass over the backdrop ──
        // Everything below this node in the current scope is already in the
        // render target; the pass re-draws the canvas quad with the
        // adjustment applied (composite() isolates the stack into a
        // canvas-NDC FBO whenever a stack adjustment exists, so the backdrop
        // is real layer content, not the canvas decorations). Known limit:
        // a non-Normal blend mode on the adjustment node renders as Normal
        // on live frames; the CPU projection (source of truth) shows the
        // blended result as soon as the interaction settles.
        if (node->type == LayerTreeNode::Type::Adjustment) {
            if (!node->isAdjustmentLayer()) continue;
            auto* lyr = node->layer.get();

            if (lyr && (lyr->maskTextureOutdated || lyr->maskTextureId == 0)
                && !lyr->maskImage.isNull()) {
                gpu->uploadMaskTexture(lyr);
            }

            const bool hasMask = lyr && lyr->maskVisible
                && lyr->maskTextureId != 0 && !lyr->maskImage.isNull()
                && lyr->maskDensity > 0.0f;

            // A clipped adjustment (Single Layer Mode) has a Layer parent and is
            // rendered inside that layer's isolation FBO (clippedAdjustmentsOnGpu).
            // Its pass covers the parent layer's base rect, not the whole canvas,
            // so the quad's interpolated UV maps to parent-layer pixel space — and
            // therefore the adjustment's mask stays aligned even when the layer is
            // rotated/scaled. A Normal-Mode adjustment instead covers the canvas in
            // document space.
            const bool clipped = node->parent
                && node->parent->type == LayerTreeNode::Type::Layer
                && node->parent->layer;

            // Quad MVP + mask UV mapping (mask declared here, used by the pass and
            // the rubylith overlay below).
            QMatrix4x4 mvp;
            QVector2D maskUvScale(1.0f, 1.0f);
            QVector2D maskUvOffset(0.0f, 0.0f);
            if (clipped) {
                // Parent base transform in the canvas-NDC FBO (hx=hy=1, no
                // pan/zoom — the children always render inside the FBO).
                const auto* parentLayer = node->parent->layer.get();
                const bool parentShapeSprite = parentLayer->shapeSpriteRenderable();
                const QTransform parentBaseTransform = parentShapeSprite
                    ? parentLayer->shapeCache.spriteTransform
                    : node->parent->accumulatedTransform();
                mvp = qTransformToMatrix4x4(parentBaseTransform);
                // Map base-local UV (0..1 over the parent's base) onto the
                // adjustment's mask buffer: mirrors computeEffectedImage's
                // maskTopLeft = adj.maskOrigin - resultOrigin (resultOrigin = base
                // top-left). Outside the mask the shader applies fully (white),
                // matching adjustments::apply().
                if (hasMask) {
                    const QSize base = parentShapeSprite
                        ? parentLayer->shapeCache.image.size()
                        : parentLayer->rasterBaseSize();
                    const float mw = static_cast<float>(lyr->maskImage.width());
                    const float mh = static_cast<float>(lyr->maskImage.height());
                    maskUvScale = QVector2D(base.width() / mw, base.height() / mh);
                    maskUvOffset = QVector2D(-lyr->maskOrigin.x() / mw,
                                             -lyr->maskOrigin.y() / mh);
                }
            } else {
                // Canvas quad MVP (same construction as the layer draws below).
                if (!hasTargetAccum) {
                    QMatrix4x4 vm;
                    vm.translate(static_cast<float>(doc->panOffset.x()),
                                 static_cast<float>(doc->panOffset.y()));
                    vm.scale(doc->zoom);
                    mvp = vm;
                }
                mvp.scale(hx, hy);

                // Map the canvas quad's UVs onto the document-space mask buffer
                // (the mask can be larger than the document after brush
                // expansion, with a non-zero origin).
                if (hasMask) {
                    const float mw = static_cast<float>(lyr->maskImage.width());
                    const float mh = static_cast<float>(lyr->maskImage.height());
                    maskUvScale = QVector2D(doc->size.width() / mw,
                                            doc->size.height() / mh);
                    maskUvOffset = QVector2D(-lyr->maskOrigin.x() / mw,
                                             -lyr->maskOrigin.y() / mh);
                }
            }

            GLint vp[4] = {0, 0, doc->size.width(), doc->size.height()};
            gl->glGetIntegerv(GL_VIEWPORT, vp);

            // Build the per-channel LUT texture payload. Curves and Color
            // Balance both resolve to per-channel transfer LUTs; Color Balance
            // also flags the luma-restoring pass applied in the shader.
            std::array<unsigned char, 256 * 4> curveLut{};
            const unsigned char* curveLutPtr = nullptr;
            bool preserveLuminosity = false;
            {
                std::array<unsigned char, 256> r{}, g{}, b{};
                const bool haveLut =
                    adjustments::buildCurveLuts(*node->adjustment, r, g, b)
                    || adjustments::buildColorBalanceLuts(*node->adjustment, r, g, b,
                                                          preserveLuminosity);
                if (haveLut) {
                    for (int i = 0; i < 256; ++i) {
                        curveLut[i * 4 + 0] = r[i];
                        curveLut[i * 4 + 1] = g[i];
                        curveLut[i * 4 + 2] = b[i];
                        curveLut[i * 4 + 3] = 255;
                    }
                    curveLutPtr = curveLut.data();
                }
            }

            // Hue/Saturation resolves to per-range slider uniforms (Master + 6
            // hue bands) + the global Colorize flag, mirroring the CPU model.
            std::array<float, huesaturation::kRangeCount> hsHue{}, hsSat{}, hsLight{};
            // 6 bands × vec4 (outerStart, innerStart, innerEnd, outerEnd).
            std::array<float, huesaturation::kBandCount * 4> hsBandRange{};
            const float* hsHuePtr = nullptr;
            const float* hsSatPtr = nullptr;
            const float* hsLightPtr = nullptr;
            const float* hsBandRangePtr = nullptr;
            bool hsColorize = false;
            if (node->adjustment->type == QLatin1String("huesaturation")) {
                const auto hs = huesaturation::HueSaturationData::fromParams(
                    node->adjustment->params);
                for (int i = 0; i < huesaturation::kRangeCount; ++i) {
                    hsHue[i]   = static_cast<float>(hs.ranges[i].hue);
                    hsSat[i]   = static_cast<float>(hs.ranges[i].saturation);
                    hsLight[i] = static_cast<float>(hs.ranges[i].lightness);
                }
                // Zero a disabled band's sliders so it is a no-op on the GPU too
                // (the CPU model skips disabled bands — keep parity).
                for (int i = 0; i < huesaturation::kBandCount; ++i) {
                    const huesaturation::HueRange& b = hs.bands[i];
                    if (!b.enabled) {
                        hsHue[i + 1] = hsSat[i + 1] = hsLight[i + 1] = 0.0f;
                    }
                    hsBandRange[i * 4 + 0] = b.outerStart;
                    hsBandRange[i * 4 + 1] = b.innerStart;
                    hsBandRange[i * 4 + 2] = b.innerEnd;
                    hsBandRange[i * 4 + 3] = b.outerEnd;
                }
                hsColorize = hs.colorize;
                hsHuePtr = hsHue.data();
                hsSatPtr = hsSat.data();
                hsLightPtr = hsLight.data();
                hsBandRangePtr = hsBandRange.data();
            }

            // Solid Color resolves to a single fill colour the shader composites
            // over the backdrop with the node's blend mode, so the live frame
            // matches the CPU projection for every blend mode (incl. opacity).
            QColor solidColor;
            int solidBlendMode = -1;
            if (node->adjustment->type == QLatin1String("solidcolor")) {
                solidColor = solidcolor::SolidColorData::fromParams(
                                 node->adjustment->params).color;
                solidBlendMode = blend::shaderId(node->blendMode());
            }

            gpu->drawAdjustmentPass(adjustments::shaderId(node->adjustment->type),
                                    node->opacity(),
                                    hasMask ? lyr->maskTextureId : 0,
                                    hasMask,
                                    lyr ? lyr->maskDensity : 1.0f,
                                    maskUvScale, maskUvOffset,
                                    mvp, vp[2], vp[3],
                                    preserveLuminosity, curveLutPtr,
                                    hsHuePtr, hsSatPtr, hsLightPtr, hsColorize,
                                    hsBandRangePtr,
                                    solidColor, solidBlendMode);

            // Rubylith overlay while editing this adjustment's mask.
            if (showMaskOverlay && lyr && lyr->maskTextureId != 0
                && !lyr->maskImage.isNull() && node == doc->activeNode()) {
                gpu->renderRubylithOverlay(lyr->maskTextureId, 1, mvp,
                                           maskOverlayOpacity,
                                           maskUvOffset, maskUvScale);
            }
            continue;
        }

        if (node->type == LayerTreeNode::Type::Group) {
            // Isolate into a group FBO when the group's blend or opacity must
            // apply to the composed result. Group effects/masks are applied
            // only by the CPU projection (DocumentCompositor), the visual
            // source of truth; the live GPU path mirrors blend + opacity so the
            // hand-off to the projection stays seamless.
            const bool isolate = node->blendMode() != BlendMode::Normal
                              || node->opacity() < 0.999f
                              // A Normal-Mode adjustment inside the group must
                              // only affect the group's own content (mirrors
                              // needsIsolatedComposite on the CPU path).
                              || node->hasVisibleAdjustmentDirectChild();
            BLENDDBG() << "[GroupCompose] name=" << node->name
                     << "isolate=" << isolate
                     << "blendMode=" << static_cast<int>(node->blendMode())
                     << "opacity=" << node->opacity()
                     << "groupAccum=" << node->accumulatedTransform()
                     << "hasTargetAccum(parent)=" << hasTargetAccum
                     << "halfExtents=" << canvasHalfExtents
                     << "docSize=" << doc->size
                     << "zoom=" << doc->zoom
                     << "pan=" << doc->panOffset;
            if (!isolate) {
                renderNodes(node->children, doc, viewMvp, canvasHalfExtents,
                            editingMask, hasPreview, previewTexture, forceFullResLod,
                            showMaskOverlay, maskOverlayOpacity, gpu,
                            hasTargetAccum, targetAccum, invTargetAccum);
            } else {
                // Isolate the group's children into a canvas-sized FBO, then
                // composite that result once with the group's blend + opacity so
                // an isolated group matches a non-isolated one pixel-for-pixel.
                //
                // The children are rendered in the same canvas-NDC space the main
                // framebuffer uses, minus the view's pan/zoom (viewMvp), which is
                // applied a single time when the FBO is composited back. We do NOT
                // fold the group's own transform into the children: each child's
                // accumulatedTransform() already includes its ancestor groups, so
                // dividing it out here and re-applying it on pop only introduces a
                // scale/translate mismatch (the moved/transformed group lands at
                // the wrong offset because scale(hx,hy) does not commute with a
                // translated group transform). Passing identity keeps the FBO an
                // exact, untransformed canvas-space capture; popGroupFbo then maps
                // it to the screen with viewMvp (top level) or 1:1 into the parent
                // group FBO (nested).
                gpu->pushGroupFbo(doc->size.width(), doc->size.height());
                const QTransform identity;
                renderNodes(node->children, doc, viewMvp, canvasHalfExtents,
                            editingMask, hasPreview, previewTexture, forceFullResLod,
                            showMaskOverlay, maskOverlayOpacity, gpu,
                            true, identity, identity);
                gpu->popGroupFbo(node, canvasHalfExtents, viewMvp, editingMask);
                gpu->mainProgram()->bind();
                gpu->bindMainVao();
            }
            continue;
        }

        if (!node->layer) continue;
        {
            const bool emptyCel = node->layer->hasEvaluatedRasterContent()
                && node->layer->evaluatedCelId().isNull();
            const bool wouldSkip = emptyCel || (!node->layer->textureId
                                && !node->layer->renderRasterStorage().isEnabled());
            BLENDDBG() << "[BLENDBUG] layer=" << node->name
                     << "inFbo=" << hasTargetAccum
                     << "blendMode=" << static_cast<int>(node->blendMode())
                     << "needsShaderBlend=" << gpu->needsShaderBlend(static_cast<int>(node->blendMode()))
                     << "textureId=" << node->layer->textureId
                     << "rasterStorage=" << node->layer->renderRasterStorage().isEnabled()
                     << "tiledSystem=" << node->layer->tiledSystem
                     << "textureOutdated=" << node->layer->textureOutdated
                     << "cpuImage=" << node->layer->renderCpuImage().size()
                     << "cpuNull=" << node->layer->renderCpuImage().isNull()
                     << "=> " << (wouldSkip ? "SKIP (no texture, no rasterStorage)" : "draw");
        }
        if ((node->layer->hasEvaluatedRasterContent()
             && node->layer->evaluatedCelId().isNull())
            || (!node->layer->textureId
                && !node->layer->renderRasterStorage().isEnabled())) continue;

        // Ensure the GPU mask texture exists before it is sampled. A mask created
        // on an already-uploaded layer (textureId != 0) only lives on the CPU
        // until some edit forces a sync — without this the rubylith overlay could
        // never see it (maskTextureId == 0). Also covers re-upload after undo
        // restored the CPU mask (maskTextureOutdated).
        if ((node->layer->maskTextureOutdated || node->layer->maskTextureId == 0)
            && !node->layer->maskImage.isNull()) {
            gpu->uploadMaskTexture(node->layer.get());
            if (!node->effects.empty())
                node->invalidateEffects();
        }

        // ── Single Layer Mode (clipped adjustment) — live GPU preview ──
        // A layer hosting visible adjustment children is composited like a
        // one-layer group: isolate the masked base into a canvas-sized FBO, run
        // the clipped adjustments as GPU passes over it (drawAdjustmentPass,
        // full quality), then composite the FBO back with the layer's real
        // opacity + blend. This is the GPU equivalent of computeEffectedImage()
        // (maskedBase → clipped adjustments with their own opacity/mask →
        // result, drawn with the layer's opacity/blend), keeping the live drag
        // off the per-frame CPU bake.
        if (node->clippedAdjustmentsOnGpu()) {
            const QTransform identity;
            // A filter dialog's live preview replaces this layer's base pixels
            // with the filtered (downscaled) preview texture. Forward it into the
            // isolated base draw so the clipped Single-Layer-Mode adjustments run
            // over the previewed pixels — without this the base is drawn from the
            // unfiltered tiles and the preview is invisible until commit.
            const bool previewThisLayer = hasPreview && previewTexture
                && node == doc->activeNode();
            gpu->pushGroupFbo(doc->size.width(), doc->size.height());
            drawLayerBaseIsolated(node, doc, gpu, previewThisLayer, previewTexture);
            // Clipped adjustments draw over the isolated base in canvas-NDC, the
            // same target convention group children use (hasTargetAccum=true);
            // the adjustment branch maps each adjustment's mask in this layer's
            // pixel space (see the Type::Adjustment handling above).
            renderNodes(node->children, doc, viewMvp, canvasHalfExtents,
                        editingMask, /*hasPreview*/false, 0, forceFullResLod,
                        showMaskOverlay, maskOverlayOpacity, gpu,
                        /*hasTargetAccum*/true, identity, identity);
            gpu->popGroupFbo(node, canvasHalfExtents, viewMvp, editingMask);
            gpu->mainProgram()->bind();
            gpu->bindMainVao();

            // Rubylith overlay for the BASE layer's own mask, drawn on top of the
            // composited result (the effected pipeline used to draw it after the
            // layer draw). Uses the layer's on-screen MVP, same construction as the
            // normal layer draw below.
            if (showMaskOverlay && node->layer->maskTextureId != 0
                && !node->layer->maskImage.isNull()
                && node == doc->activeNode()) {
                QTransform totalXf = hasTargetAccum
                    ? (node->accumulatedTransform() * invTargetAccum)
                    : node->accumulatedTransform();
                QMatrix4x4 baseMvp;
                if (!hasTargetAccum) {
                    QMatrix4x4 vm;
                    vm.translate(static_cast<float>(doc->panOffset.x()),
                                 static_cast<float>(doc->panOffset.y()));
                    vm.scale(doc->zoom);
                    baseMvp = vm;
                }
                baseMvp.scale(hx, hy);
                baseMvp = baseMvp * qTransformToMatrix4x4(totalXf);

                QMatrix4x4 sub;
                QVector2D rubyOff;
                QVector2D rubyScl;
                if (maskOverlayGeometry(node->layer.get(), &sub, &rubyOff, &rubyScl))
                    gpu->renderRubylithOverlay(node->layer->maskTextureId, 1,
                                               baseMvp * sub, maskOverlayOpacity,
                                               rubyOff, rubyScl);
            }
            continue;
        }

        // ── Compute per-layer MVP ──
        QTransform totalTransform = hasTargetAccum
            ? (node->accumulatedTransform() * invTargetAccum)
            : node->accumulatedTransform();
        if (node->layer->shapeSpriteRenderable()) {
            totalTransform = hasTargetAccum
                ? (node->layer->shapeCache.spriteTransform * invTargetAccum)
                : node->layer->shapeCache.spriteTransform;
        }

        QMatrix4x4 mvp;
        if (!hasTargetAccum) {
            QMatrix4x4 vm;
            vm.translate(static_cast<float>(doc->panOffset.x()),
                         static_cast<float>(doc->panOffset.y()));
            vm.scale(doc->zoom);
            mvp = vm;
        }
        mvp.scale(hx, hy);
        mvp = mvp * qTransformToMatrix4x4(totalTransform);
        QMatrix4x4 baseMvp = mvp; // save before effect transforms for rubylith overlay

        BLENDDBG() << "[LayerDraw] name=" << node->name
                 << "inFbo(hasTargetAccum)=" << hasTargetAccum
                 << "accum=" << node->accumulatedTransform()
                 << "totalTransform=" << totalTransform
                 << "hx=" << hx << "hy=" << hy
                 << "mvp.col3(translate)=" << mvp.column(3)
                 << "mvp.row0=" << mvp.row(0)
                 << "mvp.row1=" << mvp.row(1);

        // ── Texture selection ──
        bool usePreview = hasPreview && previewTexture
            && node == doc->activeNode();
        // Effected pipeline covers layer effects and Single-Layer-Mode
        // adjustments — both are baked into effectedTexture (mask included).
        bool hasEffects = !usePreview && node->usesEffectedPipeline()
            && node->effectedTexture;
        GLuint texId = node->layer->textureId;

        // Decide LOD for tiled layers (not effects/preview)
        core::RenderScheduler::LOD activeLod = core::RenderScheduler::LOD::Full;
        if (node->layer->tiledSystem && !usePreview && !hasEffects
            && !forceFullResLod && !hasTargetAccum) {
            activeLod = doc->scheduler.decideLOD(doc->zoom, doc->tileSize());
            if (activeLod != core::RenderScheduler::LOD::Full) {
                texId = gpu->lodTextureId(node->layer.get(), activeLod);
            }
        }

        if (hasEffects)
            texId = node->effectedTexture;
        if (usePreview)
            texId = previewTexture;

        const bool shapeSpriteActive = node->layer->shapeSpriteRenderable();
        QSize baseTextureSize = node->layer->rasterBaseSize();
        if (shapeSpriteActive) {
            baseTextureSize = node->layer->shapeCache.image.size();
        }

        bool shapeEffectTransformApplied = false;
        if (hasEffects && shapeSpriteActive
            && baseTextureSize.width() > 0
            && baseTextureSize.height() > 0) {
            const QRectF bounds = node->effectedImageBounds();
            const QTransform& spriteXf = node->layer->shapeCache.spriteTransform;
            const double baseW = baseTextureSize.width();
            const double baseH = baseTextureSize.height();
            const double scaleX = bounds.width() / baseW;
            const double scaleY = bounds.height() / baseH;
            const double localCenterX = -1.0 + (bounds.left() + bounds.right()) / baseW;
            const double localCenterY = 1.0 - (bounds.top() + bounds.bottom()) / baseH;

            QTransform effectWorld;
            effectWorld.setMatrix(
                spriteXf.m11() * scaleX, spriteXf.m12() * scaleX, 0.0,
                spriteXf.m21() * scaleY, spriteXf.m22() * scaleY, 0.0,
                spriteXf.m31() + spriteXf.m11() * localCenterX + spriteXf.m21() * localCenterY,
                spriteXf.m32() + spriteXf.m12() * localCenterX + spriteXf.m22() * localCenterY,
                1.0);

            totalTransform = hasTargetAccum
                ? (effectWorld * invTargetAccum)
                : effectWorld;

            if (!hasTargetAccum) {
                QMatrix4x4 vm;
                vm.translate(static_cast<float>(doc->panOffset.x()),
                             static_cast<float>(doc->panOffset.y()));
                vm.scale(doc->zoom);
                mvp = vm;
            } else {
                mvp = QMatrix4x4();
            }
            mvp.scale(hx, hy);
            mvp = mvp * qTransformToMatrix4x4(totalTransform);
            shapeEffectTransformApplied = true;
        }

        if (hasEffects && baseTextureSize.width() > 0
            && baseTextureSize.height() > 0
            && !shapeEffectTransformApplied) {
            const QRectF bounds = node->effectedImageBounds();
            const double baseW = baseTextureSize.width();
            const double baseH = baseTextureSize.height();
            const double left = -1.0 + 2.0 * bounds.left() / baseW;
            const double right = -1.0 + 2.0 * bounds.right() / baseW;
            const double top = 1.0 - 2.0 * bounds.top() / baseH;
            const double bottom = 1.0 - 2.0 * bounds.bottom() / baseH;

            QMatrix4x4 effectLocal;
            effectLocal.translate(static_cast<float>((left + right) * 0.5),
                                  static_cast<float>((top + bottom) * 0.5),
                                  0.0f);
            effectLocal.scale(static_cast<float>((right - left) * 0.5),
                              static_cast<float>((top - bottom) * 0.5),
                              1.0f);
            mvp = mvp * effectLocal;
        }

        // Both checks also require a CPU-side mask: after an undo removes the
        // mask the stale GPU texture id must not resurrect it on live frames
        // (the CPU projection — the source of truth — no longer applies it).
        bool hasMask = !hasEffects
            && node->layer->maskVisible
            && node->layer->maskTextureId != 0
            && !node->layer->maskImage.isNull();
        bool layerHasMask = node->layer->maskTextureId != 0
            && !node->layer->maskImage.isNull();
        if (node->layer->isShapeLayer() && layerHasMask) {
            BLENDDBG() << "[MASK-SHAPE live.pre]"
                     << "node=" << node->name
                     << "shapeSpriteActive=" << shapeSpriteActive
                     << "hasEffects=" << hasEffects
                     << "hasMask=" << hasMask
                     << "layerHasMask=" << layerHasMask
                     << "docSize=" << doc->size
                     << "baseTextureSize=" << baseTextureSize
                     << "rasterBaseSize=" << node->layer->rasterBaseSize()
                     << "cpuImage=" << node->layer->renderCpuImage().size()
                     << "shapeCacheSize=" << node->layer->shapeCache.image.size()
                     << "maskSize=" << node->layer->maskImage.size()
                     << "maskOrigin=" << node->layer->maskOrigin
                     << "maskTargetBounds=" << node->layer->maskTargetBounds()
                     << "accum=" << node->accumulatedTransform()
                     << "totalTransform=" << totalTransform
                     << "shapeSpriteTransform=" << node->layer->shapeCache.spriteTransform
                     << "mvp.row0=" << mvp.row(0)
                     << "mvp.row1=" << mvp.row(1)
                     << "defaultMaskUvScale=" << QVector2D(1.0f, 1.0f)
                     << "defaultMaskUvOffset=" << QVector2D(0.0f, 0.0f);
        }
        auto logShapeRuby = [&](const char* branch,
                                const QMatrix4x4& sub,
                                const QVector2D& rubyOff,
                                const QVector2D& rubyScl) {
            if (!node->layer->isShapeLayer())
                return;
            const QMatrix4x4 rubyMvp = baseMvp * sub;
            BLENDDBG() << "[MASK-SHAPE ruby.draw]"
                     << "branch=" << branch
                     << "node=" << node->name
                     << "editingMask=" << editingMask
                     << "shapeSpriteActive=" << shapeSpriteActive
                     << "baseTextureSize=" << baseTextureSize
                     << "rasterBaseSize=" << node->layer->rasterBaseSize()
                     << "cpuImage=" << node->layer->renderCpuImage().size()
                     << "maskSize=" << node->layer->maskImage.size()
                     << "maskOrigin=" << node->layer->maskOrigin
                     << "maskTargetBounds=" << node->layer->maskTargetBounds()
                     << "accum=" << node->accumulatedTransform()
                     << "shapeSpriteTransform=" << node->layer->shapeCache.spriteTransform
                     << "baseMvp.row0=" << baseMvp.row(0)
                     << "baseMvp.row1=" << baseMvp.row(1)
                     << "sub.row0=" << sub.row(0)
                     << "sub.row1=" << sub.row(1)
                     << "rubyMvp.row0=" << rubyMvp.row(0)
                     << "rubyMvp.row1=" << rubyMvp.row(1)
                     << "rubyUvOffset=" << rubyOff
                     << "rubyUvScale=" << rubyScl;
        };

        // ── Draw ──
        if (node->layer->renderRasterStorage().isEnabled() && !usePreview && !hasEffects) {
            BLENDDBG() << "[BLENDBUG] draw layer=" << node->name << "path=RASTER-STORAGE"
                     << "shaderBlend=" << gpu->needsShaderBlend(static_cast<int>(node->blendMode()))
                     << "hasMask=" << hasMask;
            if (node->layer->pendingGpuUpload || node->layer->textureOutdated) {
                gpu->uploadDirtyRasterTiles(node->layer.get());
                node->layer->pendingGpuUpload = false;
                node->layer->textureOutdated = false;
            }

            // Which document region needs tiles. When drawing into a group FBO
            // (hasTargetAccum) the target is the whole canvas in NDC [-1,1], not
            // the on-screen viewport — so cull against the full document. Using
            // the screen-visible rect here dropped the边 tiles whenever the doc
            // was larger than the viewport, leaving a symmetric transparent
            // border on blend-mode (FBO) frames.
            QRectF vpPix;
            if (hasTargetAccum) {
                vpPix = QRectF(0, 0, doc->size.width(), doc->size.height());
            } else {
                core::ViewportCamera tmpCam;
                tmpCam.zoom = doc->zoom;
                tmpCam.panOffset = doc->panOffset;
                tmpCam.canvasHalfExtents = canvasHalfExtents;
                vpPix = tmpCam.visiblePixelRect(
                    doc->size.width(), doc->size.height());
            }

            auto tiles = gpu->computeRasterTilesForLayer(
                node->layer.get(),
                doc->size.width(), doc->size.height(),
                vpPix, mvp);

            // A non-Normal blend must go through the blend shader even WITH a
            // mask: the shader samples the mask per tile (uHasMask + the tile's
            // own mask UVs) exactly like the flat FLAT-SHADERBLEND path. Routing
            // the masked case to applyFixedBlend() below instead lost the blend
            // mode entirely — applyFixedBlend only implements Normal — so a
            // masked dab layer rendered SourceOver on the GPU while the CPU
            // projection (DocumentCompositor) applied both mask and blend,
            // diverging on every non-Normal mode.
            const bool shaderBlend = gpu->needsShaderBlend(static_cast<int>(node->blendMode()));
            if (shaderBlend) {
                GLint vp[4] = {0, 0, doc->size.width(), doc->size.height()};
                gl->glGetIntegerv(GL_VIEWPORT, vp);
                for (const auto& tile : tiles) {
                    gpu->drawShaderBlend(tile.textureId,
                                         hasMask ? node->layer->maskTextureId : 0,
                                         tile.mvp,
                                         static_cast<int>(node->blendMode()),
                                         node->opacity(),
                                         hasMask,
                                         node->layer->maskDensity,
                                         vp[2],
                                         vp[3],
                                         tile.maskUvScale,
                                         tile.maskUvOffset);
                }
            } else {
                gpu->applyFixedBlend(static_cast<int>(node->blendMode()));
                gpu->mainProgram()->bind();
                gpu->bindMainVao();
                for (const auto& tile : tiles) {
                    gpu->setMainUniforms(tile.mvp, node->opacity(), hasMask,
                                         node->layer->maskDensity);
                    gpu->setMainTexture(GL_TEXTURE0, tile.textureId);
                    gpu->setUVUniforms(tile.uvScale, tile.uvOffset);
                    gpu->setMaskUVUniforms(tile.maskUvScale, tile.maskUvOffset);
                    if (hasMask) {
                        gl->glActiveTexture(GL_TEXTURE1);
                        gl->glBindTexture(GL_TEXTURE_2D, node->layer->maskTextureId);
                    }
                    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                }
            }

            if (showMaskOverlay && layerHasMask && node == doc->activeNode()) {
                const auto* lyr = node->layer.get();
                QMatrix4x4 sub;
                QVector2D rubyOff;
                QVector2D rubyScl;
                if (maskOverlayGeometry(lyr, &sub, &rubyOff, &rubyScl)) {
                    logShapeRuby("rasterStorage", sub, rubyOff, rubyScl);
                    gpu->renderRubylithOverlay(lyr->maskTextureId, 1, baseMvp * sub,
                                               maskOverlayOpacity, rubyOff, rubyScl);
                }
            }
            continue;
        }

        if (gpu->needsShaderBlend(static_cast<int>(node->blendMode()))) {
            GLint vp[4] = {0, 0, doc->size.width(), doc->size.height()};
            gl->glGetIntegerv(GL_VIEWPORT, vp);
            BLENDDBG() << "[BLENDBUG] draw layer=" << node->name << "path=FLAT-SHADERBLEND"
                     << "texId=" << texId << "hasMask=" << hasMask
                     << "tiledSystem=" << node->layer->tiledSystem
                     << "vp=" << vp[0] << vp[1] << vp[2] << vp[3];
            QVector2D maskOff(0.0f, 0.0f);
            QVector2D maskScl(1.0f, 1.0f);
            if (hasMask) {
                QMatrix4x4 maskSub;
                maskOverlayGeometry(node->layer.get(), &maskSub, &maskOff, &maskScl);
            }
            gpu->drawShaderBlend(texId,
                                 hasMask ? node->layer->maskTextureId : 0,
                                 mvp,
                                 static_cast<int>(node->blendMode()),
                                 node->opacity(),
                                 hasMask,
                                 node->layer->maskDensity,
                                 vp[2],
                                 vp[3],
                                 maskScl,
                                 maskOff);
            if (showMaskOverlay && layerHasMask && node == doc->activeNode()) {
                const auto* lyr = node->layer.get();
                QMatrix4x4 sub;
                QVector2D rubyOff;
                QVector2D rubyScl;
                if (maskOverlayGeometry(lyr, &sub, &rubyOff, &rubyScl)) {
                    logShapeRuby("shaderBlend", sub, rubyOff, rubyScl);
                    gpu->renderRubylithOverlay(lyr->maskTextureId, 1, baseMvp * sub, maskOverlayOpacity, rubyOff, rubyScl);
                }
            }

        } else {
            // Simple blend path (Normal, Multiply, Screen, Darken, Lighten)
            BLENDDBG() << "[BLENDBUG] draw layer=" << node->name << "path=FLAT-SIMPLE-FIXED"
                     << "texId=" << texId << "tiledSystem=" << node->layer->tiledSystem;
            gpu->applyFixedBlend(static_cast<int>(node->blendMode()));

            {
                GLint rvp[4] = {0,0,0,0};
                gl->glGetIntegerv(GL_VIEWPORT, rvp);
                BLENDDBG() << "[simpleDraw] node=" << node->name
                         << "hasTargetAccum=" << hasTargetAccum
                         << "tiled=" << node->layer->tiledSystem
                         << "rasterStorage=" << node->layer->renderRasterStorage().isEnabled()
                         << "cpuImage=" << node->layer->renderCpuImage().size()
                         << "realVp=" << rvp[0] << rvp[1] << rvp[2] << rvp[3]
                         << "mvp.row0=" << mvp.row(0) << "mvp.row1=" << mvp.row(1);
            }

            gpu->mainProgram()->bind();
            gpu->bindMainVao();
            gpu->setMainUniforms(mvp, node->opacity(), hasMask,
                                 node->layer->maskDensity);
            gpu->setMainTexture(GL_TEXTURE0, texId);
            gpu->setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));

            if (hasMask) {
                // The mask buffer can be larger than the layer base (expanded
                // layers, pasted/duplicated masks with a non-zero origin): map
                // quad UVs into mask UVs with the same bounds math the rubylith
                // overlay uses, instead of assuming a 1:1 base-sized mask.
                // (The tiled branch below resets these to identity — its mask
                // UVs are baked per instance.)
                QMatrix4x4 maskSub;
                QVector2D maskOff;
                QVector2D maskScl;
                if (maskOverlayGeometry(node->layer.get(), &maskSub, &maskOff, &maskScl))
                    gpu->setMaskUVUniforms(maskScl, maskOff);
                gl->glActiveTexture(GL_TEXTURE1);
                gl->glBindTexture(GL_TEXTURE_2D, node->layer->maskTextureId);
            }

            if (node->layer->tiledSystem) {
                // Tiled draw
                if (node->layer->pendingGpuUpload) {
                    gpu->uploadDirtyTiles(node->layer.get());
                    node->layer->pendingGpuUpload = false;
                }

                // Upload dirty LOD levels for the current LOD
                if (activeLod != core::RenderScheduler::LOD::Full)
                    gpu->uploadDirtyMipmaps(node->layer.get());

                // See the raster-storage path above: cull against the full
                // document when rendering into a group FBO, else the screen view.
                QRectF vpPix;
                if (hasTargetAccum) {
                    vpPix = QRectF(0, 0, doc->size.width(), doc->size.height());
                } else {
                    core::ViewportCamera tmpCam;
                    tmpCam.zoom = doc->zoom;
                    tmpCam.panOffset = doc->panOffset;
                    tmpCam.canvasHalfExtents = canvasHalfExtents;
                    vpPix = tmpCam.visiblePixelRect(
                        doc->size.width(), doc->size.height());
                }

                auto tiles = gpu->computeTilesForLayer(
                    node->layer.get(),
                    doc->size.width(), doc->size.height(),
                    vpPix, mvp, activeLod);

                if (!tiles.empty()) {
                    gpu->setMainTexture(GL_TEXTURE0, texId);
                    if (hasMask) {
                        gl->glActiveTexture(GL_TEXTURE1);
                        gl->glBindTexture(GL_TEXTURE_2D, node->layer->maskTextureId);
                    }
                    // Instanced rendering: single draw call for all tiles
                    gpu->setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
                    gpu->uploadInstanceData(tiles);
                    gpu->drawInstanced(static_cast<int>(tiles.size()));
                }
            } else {
                gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            }

            if (showMaskOverlay && layerHasMask && node == doc->activeNode()) {
                const auto* lyr = node->layer.get();
                QMatrix4x4 sub;
                QVector2D rubyOff;
                QVector2D rubyScl;
                if (maskOverlayGeometry(lyr, &sub, &rubyOff, &rubyScl)) {
                    logShapeRuby("simple", sub, rubyOff, rubyScl);
                    gpu->renderRubylithOverlay(lyr->maskTextureId, 1, baseMvp * sub, maskOverlayOpacity, rubyOff, rubyScl);
                }
            }
        }
    }
}

void LayerCompositor::drawLayerBaseIsolated(LayerTreeNode* node,
                                            Document* doc,
                                            GPUViewport* gpu,
                                            bool hasPreview,
                                            unsigned int previewTexture)
{
    QOpenGLFunctions* gl = QOpenGLContext::currentContext()
                               ? QOpenGLContext::currentContext()->functions()
                               : nullptr;
    if (!gl || !node->layer) return;
    Layer* layer = node->layer.get();

    // Base MVP in the canvas-NDC group FBO: hx=hy=1, no pan/zoom. Shape sprites
    // already bake the current shape/world transform into spriteTransform; other
    // layers use the node transform. The layer's own opacity/blend are NOT
    // applied here; popGroupFbo re-applies them once to the composed
    // (base + adjustments) FBO, mirroring computeEffectedImage.
    const bool shapeSpriteActive = layer->shapeSpriteRenderable();
    const QTransform baseTransform = shapeSpriteActive
        ? layer->shapeCache.spriteTransform
        : node->accumulatedTransform();
    const QMatrix4x4 mvp = qTransformToMatrix4x4(baseTransform);

    // ── Filter live-preview base ──
    // The preview texture is the full layer composite (compositeImage) with the
    // filter applied, downscaled to viewport resolution — it covers the layer's
    // whole base rect, so it maps to a single quad over the base transform with
    // UV 0..1, same footprint the tiled/rasterStorage draws fill. The layer's own
    // mask still clips it (its pixels are unmasked in compositeImage), matching
    // maskedBaseImage(). Styles/effects are intentionally not shown during the
    // preview, mirroring the non-clipped usePreview path which also overrides them.
    if (hasPreview && previewTexture) {
        // Same construction as the generic non-tiled base draw below: the base
        // quad stays at `mvp`; the (possibly larger) mask buffer is aligned to it
        // via the mask UV scale/offset rather than by transforming the quad.
        const bool hasMask = layer->maskVisible
            && layer->maskTextureId != 0
            && !layer->maskImage.isNull();
        QMatrix4x4 maskSub;
        QVector2D maskOff(0.0f, 0.0f);
        QVector2D maskScl(1.0f, 1.0f);
        if (hasMask)
            maskOverlayGeometry(layer, &maskSub, &maskOff, &maskScl);

        gpu->applyFixedBlend(static_cast<int>(BlendMode::Normal));
        gpu->mainProgram()->bind();
        gpu->bindMainVao();
        gpu->setMainUniforms(mvp, 1.0f, hasMask, layer->maskDensity);
        gpu->setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
        gpu->setMaskUVUniforms(maskScl, maskOff);
        if (hasMask) {
            gl->glActiveTexture(GL_TEXTURE1);
            gl->glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
            gl->glActiveTexture(GL_TEXTURE0);
        }
        gpu->setMainTexture(GL_TEXTURE0, previewTexture);
        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        return;
    }

    // Styled host: draw the cached styled-base texture (the layer style + the
    // layer's own mask are already baked in by computeStyledBaseImage) instead of
    // the raw tiles, positioned by its padded bounds — the same placement the
    // effected-texture draw uses. The clipped adjustments then run over it via
    // drawAdjustmentPass, mirroring computeEffectedImage (styled base → clipped
    // adjustments). The styled base is uploaded once per style/pixel change, so
    // the drag itself does no CPU bake and no re-upload.
    if (!node->effects.empty() && node->styledBaseTexture) {
        const QRectF bounds = node->styledBaseImageBounds();
        const QSize baseSize = shapeSpriteActive
            ? layer->shapeCache.image.size()
            : layer->rasterBaseSize();
        if (bounds.isValid() && baseSize.width() > 0 && baseSize.height() > 0) {
            const double baseW = baseSize.width();
            const double baseH = baseSize.height();
            const double left   = -1.0 + 2.0 * bounds.left()   / baseW;
            const double right  = -1.0 + 2.0 * bounds.right()  / baseW;
            const double top    =  1.0 - 2.0 * bounds.top()    / baseH;
            const double bottom =  1.0 - 2.0 * bounds.bottom() / baseH;
            QMatrix4x4 effectLocal;
            effectLocal.translate(static_cast<float>((left + right) * 0.5),
                                  static_cast<float>((top + bottom) * 0.5), 0.0f);
            effectLocal.scale(static_cast<float>((right - left) * 0.5),
                              static_cast<float>((top - bottom) * 0.5), 1.0f);
            const QMatrix4x4 styledMvp = mvp * effectLocal;

            gpu->applyFixedBlend(static_cast<int>(BlendMode::Normal));
            gpu->mainProgram()->bind();
            gpu->bindMainVao();
            gpu->setMainUniforms(styledMvp, 1.0f, /*hasMask*/false, 1.0f);
            gpu->setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
            gpu->setMaskUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
            gpu->setMainTexture(GL_TEXTURE0, node->styledBaseTexture);
            gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        return;
    }

    if (shapeSpriteActive) {
        gpu->applyFixedBlend(static_cast<int>(BlendMode::Normal));
        gpu->mainProgram()->bind();
        gpu->bindMainVao();
        gpu->setMainUniforms(mvp, 1.0f, /*hasMask*/false, 1.0f);
        gpu->setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
        gpu->setMaskUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
        gpu->setMainTexture(GL_TEXTURE0, layer->textureId);
        gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        return;
    }

    // The layer's own mask is baked into the base (== maskedBaseImage); the
    // clipped adjustments above keep their own separate masks.
    const bool hasMask = layer->maskVisible
        && layer->maskTextureId != 0
        && !layer->maskImage.isNull();

    QMatrix4x4 maskSub;
    QVector2D maskOff(0.0f, 0.0f);
    QVector2D maskScl(1.0f, 1.0f);
    if (hasMask)
        maskOverlayGeometry(layer, &maskSub, &maskOff, &maskScl);

    gpu->applyFixedBlend(static_cast<int>(BlendMode::Normal));
    gpu->mainProgram()->bind();
    gpu->bindMainVao();
    gpu->setMainUniforms(mvp, 1.0f, hasMask, layer->maskDensity);
    gpu->setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
    gpu->setMaskUVUniforms(maskScl, maskOff);
    if (hasMask) {
        gl->glActiveTexture(GL_TEXTURE1);
        gl->glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
        gl->glActiveTexture(GL_TEXTURE0);
    }

    // Cull against the whole document: the FBO is the canvas in NDC [-1,1], not
    // the on-screen viewport (mirrors the hasTargetAccum branches in renderNodes).
    const QRectF vpPix(0, 0, doc->size.width(), doc->size.height());

    if (layer->renderRasterStorage().isEnabled()) {
        if (layer->pendingGpuUpload || layer->textureOutdated) {
            gpu->uploadDirtyRasterTiles(layer);
            layer->pendingGpuUpload = false;
            layer->textureOutdated = false;
        }
        auto tiles = gpu->computeRasterTilesForLayer(
            layer, doc->size.width(), doc->size.height(), vpPix, mvp);
        for (const auto& tile : tiles) {
            gpu->setMainUniforms(tile.mvp, 1.0f, hasMask, layer->maskDensity);
            gpu->setMainTexture(GL_TEXTURE0, tile.textureId);
            gpu->setUVUniforms(tile.uvScale, tile.uvOffset);
            gpu->setMaskUVUniforms(tile.maskUvScale, tile.maskUvOffset);
            if (hasMask) {
                gl->glActiveTexture(GL_TEXTURE1);
                gl->glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
                gl->glActiveTexture(GL_TEXTURE0);
            }
            gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        return;
    }

    if (layer->tiledSystem) {
        if (layer->pendingGpuUpload) {
            gpu->uploadDirtyTiles(layer);
            layer->pendingGpuUpload = false;
        }
        // Full LOD: the isolated base must match the committed projection.
        auto tiles = gpu->computeTilesForLayer(
            layer, doc->size.width(), doc->size.height(), vpPix, mvp,
            core::RenderScheduler::LOD::Full);
        if (!tiles.empty()) {
            gpu->setMainTexture(GL_TEXTURE0, layer->textureId);
            if (hasMask) {
                gl->glActiveTexture(GL_TEXTURE1);
                gl->glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
                gl->glActiveTexture(GL_TEXTURE0);
            }
            // Tiled mask UVs are baked per instance by computeTilesForLayer.
            gpu->setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
            gpu->uploadInstanceData(tiles);
            gpu->drawInstanced(static_cast<int>(tiles.size()));
        }
        return;
    }

    // Non-tiled full texture.
    gpu->setMainTexture(GL_TEXTURE0, layer->textureId);
    gl->glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
