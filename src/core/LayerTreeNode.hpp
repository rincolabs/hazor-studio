#pragma once

#include <QDebug>
#include <QString>
#include <QVariantMap>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <cmath>
#include <cstdint>
#include <algorithm>
#include <memory>
#include <vector>
#include "AdjustmentTypes.hpp"
#include "Layer.hpp"
#include "LayerEffect.hpp"
#include "text/TextTypes.hpp"
#include "ShapeTypes.hpp"
#include "transform/TransformTypes.hpp"
#include "raw/RawLayerData.hpp"

class LayerTreeNode {
public:
    enum class Type { Layer, Group, Adjustment, Effect };

    // Compositing model for Group nodes.
    //  - Isolated (default): children are composited into an offscreen buffer,
    //    then the group's mask/effects/opacity/blend are applied once to that
    //    composed result before it is merged into the parent.
    //  - PassThrough: the children composite directly against the backdrop the
    //    group sits on, so an inner blend mode reads the layers *below* the
    //    group and an inner Normal-Mode adjustment reaches them as well. The
    //    group then has no composed result of its own to blend, so `blendMode`
    //    is unused in this mode: Pass Through is offered as one of the group's
    //    blend-mode choices (ImageController::setNodeGroupBlendMode clears
    //    `blendMode` back to Normal), so the two can never be set at once.
    //    Opacity and the group mask still apply — to the pass-through result as
    //    a whole, see passThroughNeedsStage().
    enum class GroupBlendMode { Isolated, PassThrough };

    Type type = Type::Layer;
    QString name;

    // True while `name` is still a system-generated label (e.g. a freshly
    // created Text Layer). Manual renames clear this flag so automatic naming
    // (deriving the label from the text content on commit) never overwrites a
    // name the user chose themselves.
    bool nameIsAuto = false;

    std::shared_ptr<Layer> layer;

    LayerTreeNode* parent = nullptr;
    std::vector<std::unique_ptr<LayerTreeNode>> children;

    QTransform transform;
    float opacity = 1.0f;
    // Fill Opacity — the layer panel's "Fill" control
    // (docs/RENDER-PIPELINE.md §4.7).
    // `opacity` fades the layer's finished contribution, content *and* layer
    // styles; this fades only the content, so a Drop Shadow keeps its full
    // strength while the pixels casting it go invisible. On an Adjustment node
    // the "content" is the contribution the adjustment makes to the composite,
    // which Fill fades without touching the adjustment's own parameters. A
    // Group has no Fill (nor does it in the editors this model follows — see
    // hasFillOpacity), and for a layer carrying an enabled layer style the
    // value lives in the style bake rather than in the composite —
    // which is why every consumer reads it through compositeFillOpacity() /
    // bakeFillOpacity() and never off this field, so it cannot be applied
    // twice or to a node that has no Fill.
    float fillOpacity = 1.0f;
    bool visible = true;
    BlendMode blendMode = BlendMode::Normal;
    GroupBlendMode groupBlendMode = GroupBlendMode::Isolated;

    bool collapsed = false;
    int lockFlags = LockNone;
    // Clipping mask: draw only where the base layer below is opaque. See the
    // isClippedLayer()/resolveClipBase() block further down for the semantics —
    // the flag alone is not the whole rule.
    bool clipped = false;

    std::vector<LayerEffect> effects;

    // Non-destructive adjustment payload (Type::Adjustment nodes only). The
    // node's `layer` then carries no drawable pixels — only the adjustment's
    // mask (plus a transparent cpuImage sized to the mask coordinate space:
    // the document in Normal Mode, the parent layer in Single Layer Mode).
    std::shared_ptr<AdjustmentData> adjustment;

    bool isAdjustmentLayer() const {
        return type == Type::Adjustment && adjustment != nullptr;
    }

    // True when this node has rendered content of its own that an isolated
    // composite (Export Layer/Group As...) can show with no other layer in
    // the document as backdrop. Most Normal-Mode adjustments (Curves,
    // Levels, Hue/Saturation, ...) only modify whatever is already
    // composited beneath them and fail this; Solid Color is the one
    // Adjustment type that is itself a fill, independent of any backdrop.
    bool hasIsolatedExportContent() const {
        if (type == Type::Layer) return layer != nullptr;
        if (type == Type::Group) return true;
        if (type == Type::Adjustment)
            return adjustment && adjustment->type == QLatin1String("solidcolor");
        return false;
    }

    // Single Layer Mode: visible adjustments nested under this Layer node.
    bool hasVisibleAdjustmentChildren() const {
        if (type != Type::Layer) return false;
        for (const auto& c : children) {
            if (c && c->visible && c->isAdjustmentLayer())
                return true;
        }
        return false;
    }

    // UI-only: true when this Layer node has Single-Layer-Mode adjustment
    // children regardless of their own visibility toggle. The layer panel
    // uses this (not hasVisibleAdjustmentChildren()) to decide whether the
    // row still gets an expand arrow, so hiding a nested adjustment doesn't
    // strand it as an unreachable child with no way to expand back to it.
    bool hasAdjustmentChildren() const {
        if (type != Type::Layer) return false;
        for (const auto& c : children) {
            if (c && c->isAdjustmentLayer())
                return true;
        }
        return false;
    }

    // True when at least one entry of `effects` actually changes pixels.
    // `!effects.empty()` is NOT the same question: the Layer Styles dialog
    // commits every style it knows about and keeps the unchecked ones (so their
    // params survive a re-enable), so the vector stays non-empty forever once
    // the dialog has been accepted once. Reading emptiness as "has effects"
    // therefore pinned the layer to the baked effected pipeline with every
    // style turned off — which, among other things, killed the live mask dab
    // preview for good (see previewBakedMaskEdit).
    bool hasEnabledEffects() const {
        return std::any_of(effects.begin(), effects.end(),
                           [](const LayerEffect& e) { return e.enabled; });
    }

    // True when at least one enabled effect is a *layer style* — a shadow,
    // glow, stroke or overlay — rather than a filter or an adjustment effect.
    // This is the predicate Fill Opacity branches on: a style is composited
    // *with* the content into a single raster (computeStyledBaseImage), so
    // separating "content" from "effects" can only happen inside that bake,
    // whereas a filter or adjustment effect merely transforms the content and
    // leaves Fill a composite-time factor.
    bool hasEnabledLayerStyles() const {
        return std::any_of(effects.begin(), effects.end(), [](const LayerEffect& e) {
            return e.enabled
                && LayerEffect::effectCategory(e.type)
                       == LayerEffect::EffectCategory::LayerStyle;
        });
    }

    // Does this node type have a Fill control at all? A Layer and an Adjustment
    // both do: a layer's Fill fades its pixels, an adjustment's fades the
    // contribution its adjustment makes to the composite — same control, same
    // field, the thing being faded is what differs. A Group has none, here as
    // in the editors this model follows, because a group's "content" is its
    // children and each of those carries its own Fill already.
    //
    // The layer panel and the four ImageController entry points gate on this,
    // so the enabled control, the undoable edit and what layerFillOpacity() is
    // willing to report cannot drift apart.
    bool hasFillOpacity() const {
        return type == Type::Layer || type == Type::Adjustment;
    }

    // Fill Opacity as the data model means it, clamped. Reads 1 for a Group,
    // which has no Fill.
    float layerFillOpacity() const {
        return hasFillOpacity() ? std::clamp(fillOpacity, 0.0f, 1.0f) : 1.0f;
    }

    // True when this node's Fill is applied *inside* the style bake instead of
    // by the compositor — the one split the two accessors below arbitrate.
    //
    // Only a Type::Layer can be in that position: it is the only node whose
    // `effects` are baked into a styled base (computeStyledBaseImage), where
    // content and styles become one raster no later factor could separate. An
    // Adjustment node's effects are never rendered at all, so routing its Fill
    // into a bake that does not run would silently drop the control.
    bool fillLivesInStyleBake() const {
        return type == Type::Layer && hasEnabledLayerStyles();
    }

    // Fill as the *style bake* has to apply it. Only a layer style forces it in
    // there: content and styles are interleaved in one raster, and no factor
    // applied to that raster afterwards could fade one without the other.
    float bakeFillOpacity() const {
        return fillLivesInStyleBake() ? layerFillOpacity() : 1.0f;
    }

    // Fill as the *compositor* has to apply it — the complement of the above, so
    // the two multiply to the node's Fill exactly once. Without a layer style
    // this is the whole of it, which is what keeps a Fill drag free of any
    // re-bake: the whole bake is content, so one factor on the stage is exact.
    // An Adjustment node is always this case — it has no style bake to split
    // with, so its Fill is applied wherever its contribution is composited
    // (applyAdjustmentNode, drawAdjustmentPass, and the Single-Layer-Mode bake
    // in computeEffectedImage).
    float compositeFillOpacity() const {
        return fillLivesInStyleBake() ? 1.0f : layerFillOpacity();
    }

    // True when this layer's render must go through computeEffectedImage():
    // layer effects and/or Single-Layer-Mode adjustments are baked into one
    // image (mask included). The GPU then draws effectedTexture and the
    // compositors skip their separate mask pass.
    bool usesEffectedPipeline() const {
        return hasEnabledEffects() || hasVisibleAdjustmentChildren();
    }

    // True when this Layer node's Single-Layer-Mode (clipped) adjustments are
    // previewed live on the GPU — isolated into a per-layer FBO and applied with
    // drawAdjustmentPass — instead of being baked per-frame on the CPU by
    // computeEffectedImage(). Gated to raster hosts and sprite-renderable shape
    // hosts; text and masked shapes keep the CPU effected pipeline because their
    // live base draw uses a different coordinate contract.
    //
    // Layers WITH layer styles also take this path: the isolated base is the
    // cached styledBaseTexture (the style baked once, see computeStyledBaseImage)
    // rather than the raw tiles, so the clipped adjustment runs entirely on the
    // GPU and the expensive style blur does NOT re-run every drag frame — the
    // whole point. This is why the styled and unstyled cases now behave the same.
    //
    // IMPORTANT: this only diverts the LIVE GPU preview path (LayerCompositor +
    // GPUViewport sync). The DocumentCompositor commit/export — the visual source
    // of truth — still bakes these via usesEffectedPipeline(), so the live frame
    // and the settled projection stay pixel-consistent.
    bool clippedAdjustmentsOnGpu() const {
        return type == Type::Layer
            && layer
            && !layer->isTextLayer()
            && (!layer->isShapeLayer() || layer->shapeSpriteRenderable())
            && hasVisibleAdjustmentChildren();
    }

    // The masked base with the layer-style/filter stack applied, but WITHOUT the
    // clipped Single-Layer-Mode adjustments. This is the costly step (OpenCV blur
    // for shadows/glows). It is cached and only rebuilt when m_styledBaseDirty
    // (the layer's pixels, mask, or styles changed) — a clipped-adjustment drag
    // does NOT invalidate it, so the GPU live path (styledBaseTexture) reuses it
    // across frames and the blur never re-runs per frame. computeEffectedImage()
    // (CPU projection / export) layers the clipped adjustments on top of this.
    QImage computeStyledBaseImage() const {
        if (!m_styledBaseDirty && !m_styledBaseImage.isNull())
            return m_styledBaseImage;

        // Everything below is full-resolution work — the tile composite and the
        // mask pass in maskedBaseImage() just as much as the OpenCV blur in
        // applyEffectStack(). On a large layer the whole function costs hundreds
        // of milliseconds, so the live-preview path never calls it on the UI
        // thread: StyleBakePool runs it on a COW node snapshot off-thread and
        // hands the result back through commitStyledBaseBake().
        //
        // It must fire on a style/pixel/mask change but stay SILENT during an
        // adjustment-layer drag — that silence is the proof the per-frame style
        // re-render is gone.
        QImage source = maskedBaseImage();
        if (source.isNull()) {
            commitStyledBaseBake(QImage(), QRectF(), m_styledBaseRevision);
            return m_styledBaseImage;
        }

        const QMargins padding = LayerEffect::effectPadding(effects);
        QImage result(source.width() + padding.left() + padding.right(),
                      source.height() + padding.top() + padding.bottom(),
                      QImage::Format_RGBA8888);
        result.fill(Qt::transparent);
        {
            QPainter p(&result);
            p.drawImage(padding.left(), padding.top(),
                        source.convertToFormat(QImage::Format_RGBA8888));
        }

        // Filters run first on the padded source so soft effects can grow
        // beyond tight raster/dab bounds before layer styles are composited.
        // Group effects reuse this exact stack via applyEffectStack().
        result = applyEffectStack(result, effects, bakeFillOpacity());

        const QRectF baseBounds = baseRenderImageBounds();
        commitStyledBaseBake(result,
                             QRectF(baseBounds.left() - padding.left(),
                                    baseBounds.top() - padding.top(),
                                    result.width(), result.height()),
                             m_styledBaseRevision);
        return m_styledBaseImage;
    }

    // True while computeStyledBaseImage() would actually have to recompute
    // (as opposed to returning the cached bake). Lets GPUViewport/
    // StyleBakePool decide whether a node needs the (potentially costly)
    // async bake path at all before touching it.
    bool styledBaseDirty() const { return m_styledBaseDirty; }

    // Bumped by invalidateEffects() every time the node's pixels/mask/styles
    // actually change. StyleBakePool tags each dispatched bake with the
    // revision it was built for; commitStyledBaseBake() discards a bake whose
    // revision is no longer the latest — a param change that lands while a bake
    // is in flight leaves the node dirty so a fresh bake gets dispatched for
    // the new params.
    uint64_t styledBaseRevision() const { return m_styledBaseRevision; }

    // Stores a styled-base bake — computed either inline by
    // computeStyledBaseImage() or off-thread by StyleBakePool from a COW node
    // snapshot — back into the cache. `bakedRevision` must be the
    // styledBaseRevision() that was current when the bake was started.
    //
    // Revision discard (same rule as ProjectionCache): a bake whose revision is
    // no longer the current one — the user moved the slider again, or painted
    // another dab, while it was running — is dropped, never stored. This cache
    // feeds the CPU consumers that must show the truth (computeEffectedImage →
    // export, the projection, the panel thumbnails), so a style the user has
    // already dragged past cannot live here. The GPU may still *display* such a
    // bake: StyleBakePool hands it to the upload as an interim preview, which
    // carries its own bounds (uploadedEffectedBounds) and leaves the node dirty,
    // and dispatches a fresh bake for the current params.
    void commitStyledBaseBake(const QImage& image, const QRectF& bounds,
                              uint64_t bakedRevision) const {
        if (bakedRevision != m_styledBaseRevision)
            return;
        m_styledBaseImage = image;
        m_styledBaseBounds = bounds;
        m_styledBaseDirty = false;
    }

    // Padded bounds of the styled base (== effected bounds — clipped adjustments
    // don't change image size). Rebuilds the bake when stale, so it is only for
    // CPU consumers that want the current-params geometry (projection, export).
    QRectF styledBaseImageBounds() const {
        if (m_styledBaseDirty || m_styledBaseImage.isNull())
            computeStyledBaseImage();
        return m_styledBaseBounds;
    }

    // Geometry of the styled base sitting in the cache, without rebuilding it.
    // Only for the GPU upload site, which copies it into
    // uploadedStyledBaseBounds together with the pixels it uploads.
    QRectF styledBaseBakeBounds() const { return m_styledBaseBounds; }

    // Bounds styledBaseTexture/effectedTexture were actually uploaded FROM —
    // the only correct placement for them, and never a rebuild. The cache's own
    // bounds are the wrong answer here in three states, all normal: an async
    // bake in flight (the node is dirty but the texture still holds the previous
    // bake), an interim upload of a superseded bake, and a CPU consumer (export,
    // the histogram's composite of the live document) baking the node inline
    // without uploading anything. Padding grows and shrinks with the effect
    // params, so placing a texture by bounds it wasn't built for stretches it.
    // Calling styledBaseImageBounds() here would also run the full-res style
    // bake on the UI thread — the stall StyleBakePool exists to avoid.
    QRectF styledBaseTextureBounds() const { return uploadedStyledBaseBounds; }

    QImage computeEffectedImage() const {
        if (!usesEffectedPipeline()) {
            if (layer)
                m_effectedBounds = baseRenderImageBounds();
            return layer ? baseRenderImage() : QImage();
        }
        if (!m_effectsDirty && !m_effectedImage.isNull())
            return m_effectedImage;

        // Reuse the cached styled base (the expensive style blur) and layer only
        // the clipped adjustments on top. A clipped-adjustment edit invalidates
        // just the bake (invalidateEffectedBake), so the styled base is reused.
        const QImage styled = computeStyledBaseImage();
        if (styled.isNull()) {
            m_effectedImage = QImage();
            m_effectedBounds = QRectF();
            m_effectsDirty = false;
            return m_effectedImage;
        }

        const QMargins padding = LayerEffect::effectPadding(effects);
        const QRectF baseBounds = baseRenderImageBounds();

        // Single Layer Mode: bake nested adjustments over a copy of the styled
        // base, bottom-up (last child first) to mirror stack order. Each child
        // adjustment's mask lives in this layer's pixel space (maskOrigin is a
        // layer-pixel coordinate), so offset it into the padded result.
        // `result` shares the cache via COW; adjustments::apply detaches it on its
        // first write, leaving m_styledBaseImage intact for the next frame.
        QImage result = styled;
        const QPoint resultOrigin(
            static_cast<int>(std::floor(baseBounds.left())) - padding.left(),
            static_cast<int>(std::floor(baseBounds.top())) - padding.top());
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            const LayerTreeNode* adj = it->get();
            if (!adj || !adj->visible || !adj->isAdjustmentLayer())
                continue;
            QImage mask;
            QPoint maskTopLeft;
            float density = 1.0f;
            if (adj->layer && adj->layer->maskVisible
                && !adj->layer->maskImage.isNull()
                && adj->layer->maskDensity > 0.0f) {
                mask = adj->layer->maskImage;
                maskTopLeft = adj->layer->maskOrigin - resultOrigin;
                density = adj->layer->maskDensity;
            }
            // A clipped adjustment composites Normal-mode, where its Fill and
            // its opacity are the same factor on its contribution, so the two
            // fold into one mix strength (§4.7). Fill is applied here and not
            // on the adjustment's parameters: a Fill of 50% is half as much of
            // the same curve, not half the curve.
            adjustments::apply(result, *adj->adjustment,
                               adj->opacity * adj->compositeFillOpacity(),
                               mask, maskTopLeft, density);
        }

        m_effectedImage = result;
        m_effectedBounds = m_styledBaseBounds;
        m_effectsDirty = false;
        return result;
    }

    // Thumbnail-resolution effected bake for the layer panel. Worker-safe (only
    // QImage + OpenCV, no GL / GUI singletons), so it can run off the UI thread
    // (LayerPanel async thumbnails) — that is what stops the settle freeze.
    //
    // For pure clipped-adjustment layers (no styles/filters) the masked base is
    // downscaled BEFORE the per-pixel adjustment pass: the freeze was that pass
    // running over millions of full-res pixels only to shrink the result to
    // ~40px. Adjustments are point operations and the masks scale cleanly, so the
    // small bake matches the full-res one. Layers carrying styles/filters fall
    // back to the full-resolution computeEffectedImage() (params are in pixels) —
    // still off the UI thread, so no freeze there either.
    QImage computeEffectedThumbnail(int maxDim) const {
        if (!layer || maxDim <= 0)
            return baseRenderImage();

        // Thumbnails are tiny (≤64px) and always baked off the UI thread, so use
        // fast (nearest) scaling — reduced quality is imperceptible at this size
        // and keeps a huge merged layer's downscale cheap on the worker.
        auto clampToMax = [maxDim](const QImage& img) -> QImage {
            if (img.isNull())
                return img;
            if (std::max(img.width(), img.height()) <= maxDim)
                return img;
            return img.scaled(maxDim, maxDim, Qt::KeepAspectRatio,
                              Qt::FastTransformation);
        };

        if (!usesEffectedPipeline())
            return clampToMax(baseRenderImage());
        if (hasEnabledEffects())
            return clampToMax(computeEffectedImage());

        QImage source = maskedBaseImage();
        if (source.isNull())
            return QImage();

        const int srcMax = std::max(source.width(), source.height());
        const double f = (srcMax > maxDim) ? double(maxDim) / double(srcMax) : 1.0;
        QImage result = (f < 1.0)
            ? source.scaled(std::max(1, static_cast<int>(std::lround(source.width()  * f))),
                            std::max(1, static_cast<int>(std::lround(source.height() * f))),
                            Qt::IgnoreAspectRatio, Qt::FastTransformation)
            : source;
        // Actual per-axis factors after rounding — used to place adjustment masks.
        const double fx = double(result.width())  / double(source.width());
        const double fy = double(result.height()) / double(source.height());

        const QRectF baseBounds = baseRenderImageBounds();
        const QPoint resultOriginFull(static_cast<int>(std::floor(baseBounds.left())),
                                      static_cast<int>(std::floor(baseBounds.top())));
        for (auto it = children.rbegin(); it != children.rend(); ++it) {
            const LayerTreeNode* adj = it->get();
            if (!adj || !adj->visible || !adj->isAdjustmentLayer())
                continue;
            QImage mask;
            QPoint maskTopLeft;
            float density = 1.0f;
            if (adj->layer && adj->layer->maskVisible
                && !adj->layer->maskImage.isNull()
                && adj->layer->maskDensity > 0.0f) {
                const QImage& mfull = adj->layer->maskImage;
                mask = (f < 1.0)
                    ? mfull.scaled(std::max(1, static_cast<int>(std::lround(mfull.width()  * fx))),
                                   std::max(1, static_cast<int>(std::lround(mfull.height() * fy))),
                                   Qt::IgnoreAspectRatio, Qt::FastTransformation)
                    : mfull;
                const QPoint offFull = adj->layer->maskOrigin - resultOriginFull;
                maskTopLeft = QPoint(static_cast<int>(std::lround(offFull.x() * fx)),
                                     static_cast<int>(std::lround(offFull.y() * fy)));
                density = adj->layer->maskDensity;
            }
            // Same opacity × Fill fold as computeEffectedImage, so the panel
            // thumbnail shows the Fill the canvas does.
            adjustments::apply(result, *adj->adjustment,
                               adj->opacity * adj->compositeFillOpacity(),
                               mask, maskTopLeft, density);
        }
        return clampToMax(result);
    }

    // Applies an ordered effect stack to `src` and returns the result. Shared
    // by computeEffectedImage() (layer effects) and the document compositor
    // (group effects) so both go through one code path — no duplicated effect
    // ordering. Filters run first, then layer styles inner->outer.
    static QImage applyEffectStack(const QImage& src,
                                   const std::vector<LayerEffect>& effects,
                                   float fillOpacity = 1.0f)
    {
        QImage result = src;
        for (const auto& e : effects) {
            if (LayerEffect::effectCategory(e.type) != LayerEffect::EffectCategory::LayerStyle)
                result = e.apply(result);
        }

        // ── Fill Opacity (docs/RENDER-PIPELINE.md §4.7) ──
        // Fill fades the layer's own content and must leave the styles at full
        // strength, so from here on the image being composited (`result`) and
        // the silhouette the styles are generated from (`shape`) are two
        // different pictures: the shadow is still cast by pixels Fill has made
        // invisible. They are the same picture at Fill 100%, which is why
        // `shape` is only materialised below it — the default path is
        // byte-for-byte the stack this function has always run.
        //
        // `shape` accumulates the styles as well, so a Stroke still grows the
        // silhouette the Drop Shadow is then cast from and a combination of
        // styles behaves as it does at Fill 100%.
        const float fill = std::clamp(fillOpacity, 0.0f, 1.0f);
        QImage shape;
        if (fill < 0.999f) {
            shape = result;   // COW; scaleAlpha detaches `result` from it
            result = LayerEffect::scaleAlpha(result, fill);
        }

        // render order: inner effects -> overlays -> stroke -> outer effects -> shadow
        static const QLatin1String kOrder[] = {
            QLatin1String("inner_shadow"),
            QLatin1String("inner_glow"),
            QLatin1String("color_overlay"),
            QLatin1String("gradient_overlay"),
            QLatin1String("stroke"),
            QLatin1String("outer_glow"),
            QLatin1String("drop_shadow")
        };
        for (const auto& effectType : kOrder) {
            for (const auto& e : effects) {
                if (e.type != effectType)
                    continue;
                if (shape.isNull()) {
                    result = e.apply(result);
                } else {
                    result = e.applyOver(result, shape);
                    shape  = e.apply(shape);
                }
            }
        }
        return result;
    }

    // Invalidate only the effected *bake* — the clipped-adjustment pass and the
    // GPU effectedTexture — while KEEPING the cached styled base and its GPU
    // texture. Use this when a clipped adjustment changes but the host layer's
    // own pixels, mask, and styles do not, so the expensive style blur and the
    // styledBaseTexture upload are skipped on every live-drag frame. This is the
    // whole reason a styled clipped-adjustment drag is now as cheap as an
    // unstyled one: the per-frame work is just the GPU drawAdjustmentPass.
    void invalidateEffectedBake() {
        m_effectsDirty = true;
        // The GPU's effectedTexture must be re-uploaded too. This is tracked
        // separately from m_effectsDirty because m_effectsDirty is also cleared
        // by CPU-only consumers (the layer-panel thumbnail calls
        // computeEffectedImage() on the same imageChanged that drives a live
        // adjustment drag). If the GPU upload keyed off m_effectsDirty, a
        // thumbnail repaint that ran first would clear it and the live frame
        // would draw a stale effectedTexture — Single-Layer-Mode live preview
        // froze for exactly this reason. effectedTextureOutdated is only cleared
        // when the texture is actually uploaded (GPUViewport::syncLayersToGpu).
        effectedTextureOutdated = true;
    }

    void invalidateEffects() {
        // The layer's own pixels / mask / styles changed, so the styled base (and
        // its GPU texture) must be rebuilt too — not just the adjustment bake.
        m_styledBaseDirty = true;
        styledBaseTextureOutdated = true;
        // See styledBaseRevision(): bumped on every invalidation so
        // StyleBakePool can tell an in-flight async bake was started for
        // params that are no longer the latest.
        ++m_styledBaseRevision;
        invalidateEffectedBake();
        // A Single-Layer-Mode adjustment is baked into its parent layer's
        // effected image — invalidating the child must invalidate that bake
        // (mask edits, opacity, visibility, parameter changes all route here).
        // Only the bake is stale, though: the parent's styled base (its own
        // pixels + layer style) is untouched by a child adjustment, so a clipped-
        // adjustment drag does NOT trigger a per-frame style re-render or a
        // styledBaseTexture re-upload.
        if (parent && parent->type == Type::Layer && parent != this) {
            parent->thumbnailDirty = true;
            parent->invalidateEffectedBake();
        }
    }
    bool effectsDirty() const { return m_effectsDirty; }
    QRectF effectedImageBounds() const {
        if (!usesEffectedPipeline())
            return layer ? baseRenderImageBounds() : QRectF();
        if (m_effectsDirty || m_effectedImage.isNull())
            computeEffectedImage();
        return m_effectedBounds;
    }

    // effectedTexture's placement — see styledBaseTextureBounds().
    QRectF effectedTextureBounds() const {
        if (!usesEffectedPipeline())
            return layer ? baseRenderImageBounds() : QRectF();
        return uploadedEffectedBounds;
    }

    // Geometry of the effected bake sitting in the cache, without rebuilding it
    // — the upload-site counterpart of styledBaseBakeBounds().
    QRectF effectedBakeBounds() const { return m_effectedBounds; }

    unsigned int effectedTexture = 0;
    // Bounds effectedTexture was uploaded FROM (see effectedTextureBounds()).
    QRectF uploadedEffectedBounds;
    // GPU-side staleness for effectedTexture, distinct from the CPU m_effectsDirty
    // (see invalidateEffects). Set on every effect/adjustment invalidation,
    // cleared only when the texture is uploaded.
    bool effectedTextureOutdated = true;

    // GPU texture holding computeStyledBaseImage() — the styled base WITHOUT the
    // clipped adjustments — for the live GPU clipped-adjustment path on layers
    // that carry styles (LayerCompositor::drawLayerBaseIsolated). Uploaded only
    // when styledBaseTextureOutdated (style / pixel / mask change), NOT per
    // adjustment-drag frame, so the style blur and upload happen once.
    unsigned int styledBaseTexture = 0;
    // Bounds styledBaseTexture was uploaded FROM (see styledBaseTextureBounds()).
    QRectF uploadedStyledBaseBounds;
    bool styledBaseTextureOutdated = true;

    QImage cachedThumb;

    // Structured cache invalidation flags.
    // sourceDirty:    pixel source data changed (brush, filter, paste)
    // gpuCacheDirty:  GPU texture needs re-upload
    // thumbnailDirty: UI thumbnail needs update
    // compositeDirty: composite output is outdated (transform, opacity, blend, visibility)
    bool sourceDirty    = true;
    bool gpuCacheDirty  = true;
    bool thumbnailDirty = true;
    bool compositeDirty = true;

    mutable QImage m_effectedImage;
    mutable QRectF m_effectedBounds;
    mutable bool m_effectsDirty = true;

    // Cached styled base (masked base + layer style, WITHOUT clipped adjustments).
    // Rebuilt only when m_styledBaseDirty (layer pixels / mask / styles changed);
    // reused across clipped-adjustment live-drag frames so the costly style blur
    // runs once. Feeds both computeEffectedImage() (CPU) and styledBaseTexture
    // (GPU live path).
    mutable QImage m_styledBaseImage;
    mutable QRectF m_styledBaseBounds;
    mutable bool m_styledBaseDirty = true;
    // See styledBaseRevision()/invalidateEffects() — identifies which params
    // the cached/in-flight styled base was (or is being) built for.
    mutable uint64_t m_styledBaseRevision = 0;

    QTransform accumulatedTransform() const {
        QTransform xf;
        const LayerTreeNode* n = this;
        while (n) {
            xf = xf * n->transform;
            n = n->parent;
        }
        return xf;
    }

    LayerType layerType() const {
        if (type == Type::Adjustment) return LayerType::Adjustment;
        if (type == Type::Group) return LayerType::Group;
        if (layer && layer->textData) return LayerType::Text;
        if (layer && layer->shapeData) return LayerType::Shape;
        return LayerType::Raster;
    }

    // ── Lock state (centralized) ────────────────────────────────────────────
    // These fold the LockAll master bit into each partial lock so callers never
    // have to test raw bits. Tools/operations gate on the canEdit*() queries;
    // the is*Locked() forms are handy for UI feedback. See LockFlag in Layer.hpp.
    bool isFullyLocked() const { return (lockFlags & LockAll) != 0; }
    bool isPositionLocked() const { return (lockFlags & (LockPosition | LockAll)) != 0; }
    bool isPixelEditingLocked() const { return (lockFlags & (LockImage | LockAll)) != 0; }
    bool isTransparencyLocked() const { return (lockFlags & (LockTransparent | LockAll)) != 0; }

    // Can raster pixels be edited (brush, eraser, fill, clone, filters, clear)?
    // Folds in Layer::hasDirectlyEditablePixels() so text/shape/RAW layers are
    // blocked here too, not just by lock state — the single gate every raster
    // tool should route through (see Layer.hpp's doc comment on that method).
    bool canEditPixels() const {
        return !isPixelEditingLocked() && (!layer || layer->hasDirectlyEditablePixels());
    }
    // Can the node be moved/transformed/resized/rotated/flipped/nudged/aligned?
    // Adjustment layers are non-spatial (they apply per-pixel over the backdrop
    // / their parent layer), so they are never transformable — no move,
    // bounding box, or transform handles.
    bool canTransform() const {
        return type != Type::Adjustment && !isPositionLocked();
    }
    // Can a layer's own non-destructive content change (text string/style,
    // shape geometry/fill/stroke, RAW develop params)? Deliberately does NOT
    // fold in hasDirectlyEditablePixels(): that predicate is false for
    // exactly these layer types, since their content model is the
    // alternative to direct pixel edits, not a second restriction on top of
    // it. Callers already know the layer is one of these types (they switch
    // on textData/shapeData/rawData first); this only gates lock state.
    bool canEditContent() const { return !isPixelEditingLocked(); }
    // Can the layer mask be painted/edited? Only the master lock blocks the mask;
    // Lock Image Pixels protects the image, not the mask.
    bool canEditMask() const { return !isFullyLocked(); }

    bool isLeaf() const {
        return type != Type::Group;
    }

    // True when at least one node inside this Group's own composite scope
    // reads the backdrop — a non-Normal blend mode, or a Normal-Mode
    // adjustment. Such a node is exactly what makes the group's isolation
    // observable: it is the difference between blending against the group's
    // own content and blending against the whole stack below the group.
    //
    // "Scope" is not "subtree". A child group that isolates itself contains
    // its backdrop readers, so only the mode it merges with counts; a child
    // group that does *not* isolate (a Pass Through group, or a plain
    // transparent one) puts its children in this scope, so they are walked
    // too. Mutual recursion with needsIsolatedComposite() is what encodes
    // that, and it terminates on tree depth.
    bool groupScopeReadsBackdrop() const {
        if (type != Type::Group) return false;
        for (const auto& c : children) {
            if (!c || !c->visible) continue;
            if (c->isAdjustmentLayer()) return true;
            if (c->mergeBlendMode() != BlendMode::Normal) return true;
            if (c->type == Type::Group && !c->needsIsolatedComposite()
                && c->groupScopeReadsBackdrop())
                return true;
        }
        return false;
    }

    // True when a Group must be composited into an offscreen buffer before
    // being merged into its parent — i.e. its mask/effects/opacity/blend must
    // apply to the *composed* group result, not to each child, or its contents
    // must blend against nothing but themselves. When false the compositor may
    // merge the children straight into the parent, which is then visually
    // identical — and is what a Pass Through group wants by definition.
    bool needsIsolatedComposite() const {
        if (type != Type::Group) return false;
        // Pass Through is *defined* by not isolating: its children have to see
        // the backdrop. Its own opacity/mask are handled by the pass-through
        // stage instead (passThroughNeedsStage), not by isolation.
        if (isPassThroughGroup()) return false;
        return blendMode != BlendMode::Normal
            || opacity < 0.999f
            || !effects.empty()
            || hasGroupMask()
            // The reason the "merge straight into the parent" shortcut is not
            // free: a child that reads the backdrop — a Normal-Mode adjustment
            // or a blend mode — would read the layers *below the group*
            // instead of the group's own content, which is Pass Through's job,
            // not an Isolated group's.
            || groupScopeReadsBackdrop();
    }

    // True when this Group composites its children straight against whatever
    // is already in the render target instead of isolating them — the Pass
    // Through model (see GroupBlendMode).
    //
    // Layer effects are the one group property that model cannot carry, so an
    // effect demotes the group back to Isolated: a shadow/glow/stroke is
    // generated from the node's *own* alpha, which only exists once the
    // children are composed against transparency. A pass-through buffer is
    // seeded with the backdrop instead, so the same code would grow a shadow
    // around everything underneath the group. hasEnabledEffects() rather than
    // `!effects.empty()` because the Layer Styles dialog keeps the styles it
    // has turned off — see hasEnabledEffects().
    bool isPassThroughGroup() const {
        return type == Type::Group
            && groupBlendMode == GroupBlendMode::PassThrough
            && !hasEnabledEffects();
    }

    // True when a Pass Through group still needs an intermediate buffer: one
    // of its own properties — opacity, or its mask — has to apply to the
    // pass-through result *as a whole*. The children are then composited into
    // a copy of the backdrop (so they keep blending against the outer content)
    // and that copy is interpolated back over the untouched backdrop by
    // opacity x mask; see DocumentCompositor's compositePassThroughStage.
    //
    // At opacity 1 with no mask the interpolation is the identity, so the
    // children can be drawn straight into the target with no buffer at all —
    // which is the cheap path, and the common one.
    bool passThroughNeedsStage() const {
        return isPassThroughGroup() && (opacity < 0.999f || hasGroupMask());
    }

    // The blend mode this node's composed result is merged into its parent
    // scope with. Identical to `blendMode` for everything except a Pass
    // Through group, which has no composed result of its own to blend and
    // therefore leaves the slot unused — it reads as Normal here so a stale
    // value (an older project file, a hand-edited one) can never blend a group
    // that is not supposed to blend. See GroupBlendMode.
    BlendMode mergeBlendMode() const {
        return groupBlendMode == GroupBlendMode::PassThrough
            ? BlendMode::Normal : blendMode;
    }

    // Group masks are not part of the data model yet (masks live on Layer).
    // This single hook lets the compositor route groups through the isolated
    // path once group masks exist — no other compositor change is required.
    bool hasGroupMask() const { return false; }

    // ── Clipping masks (docs/RENDER-PIPELINE.md §4.6) ─────────────────────
    //
    // `clipped` means: limit this node's contribution to the effective alpha of
    // its *base* — the first sibling below it that is not itself clipped. The
    // base's alpha is read per pixel (its own transparency, its mask, its
    // transform, its layer styles, its opacity), so partial transparency and
    // antialiased edges survive. Nothing is baked: no raster mask is created and
    // the clipped layer keeps its own pixels, mask, opacity, blend mode and
    // layer styles.
    //
    // It is a flag on a *sibling*, not a parent relationship — which is what
    // separates it from the other clipping mechanism in this model: an
    // adjustment clipped to a layer becomes that layer's child (Single Layer
    // Mode, see hasVisibleAdjustmentChildren). That is why `clipped` is only
    // meaningful on Type::Layer nodes, and why the two never interact.
    bool isClippedLayer() const {
        return clipped && type == Type::Layer && layer != nullptr;
    }

    // May this node serve as the base a clipping run reads its alpha from?
    // Pixel, Text, Shape, RAW and Smart Object layers all qualify — they are
    // Type::Layer with drawable pixels of their own — and so does a Group,
    // whose alpha is its children composed against transparency (§4.6). An
    // Adjustment node does not: it has no alpha of its own, only a modifier on
    // the backdrop.
    bool canBeClipBase() const {
        return type == Type::Group || (type == Type::Layer && layer != nullptr);
    }

    // Resolves the clipping base of `siblings[index]`: the first node BELOW it
    // that is not part of the same clipping run. Child vectors are stored
    // top-to-bottom, so "below" is a higher index.
    //
    // nullptr when that node cannot be a base (a group, an adjustment) or when
    // there is none at all (the bottom of the container). A node whose base does
    // not resolve renders unclipped, which is also what keeps a `clipped` flag
    // harmless after the layer it clipped to was deleted or reordered away.
    //
    // Both compositors call this, so the run cannot be resolved two ways.
    static const LayerTreeNode* resolveClipBase(
        const std::vector<std::unique_ptr<LayerTreeNode>>& siblings, size_t index)
    {
        for (size_t j = index + 1; j < siblings.size(); ++j) {
            const LayerTreeNode* candidate = siblings[j].get();
            if (!candidate) continue;
            if (candidate->isClippedLayer()) continue;   // same run — keep going down
            return candidate->canBeClipBase() ? candidate : nullptr;
        }
        return nullptr;
    }

    // True when the sibling directly above `siblings[index]` clips to it, i.e.
    // the node is the base of a clipping run. The run is contiguous, so the one
    // neighbour decides it. Lets a compositor build the base's alpha in the same
    // pass that draws the base, and lets Release Clipping Mask on a base release
    // everything clipped to it.
    static bool isClipBase(const std::vector<std::unique_ptr<LayerTreeNode>>& siblings,
                           size_t index)
    {
        if (index == 0 || index >= siblings.size()) return false;
        const LayerTreeNode* self = siblings[index].get();
        if (!self || !self->canBeClipBase() || self->isClippedLayer()) return false;
        const LayerTreeNode* above = siblings[index - 1].get();
        return above && above->isClippedLayer();
    }

    std::unique_ptr<LayerTreeNode> clone(bool preserveName = false) const {
        auto node = std::make_unique<LayerTreeNode>();
        node->type = type;
        node->name = preserveName ? name : name;
        node->nameIsAuto = nameIsAuto;
        node->opacity = opacity;
        node->fillOpacity = fillOpacity;
        node->visible = visible;
        node->blendMode = blendMode;
        node->groupBlendMode = groupBlendMode;
        node->lockFlags = lockFlags;
        node->collapsed = collapsed;
        node->transform = transform;
        node->clipped = clipped;
        node->effects.clear();
        for (auto& e : effects)
            node->effects.push_back(e);
        if (adjustment)
            node->adjustment = std::make_shared<AdjustmentData>(*adjustment);
        node->invalidateEffects();

        if (layer) {
            node->layer = std::make_shared<Layer>();
            node->layer->name = layer->name;
            node->layer->cpuImage = layer->cpuImage.copy();
            if (layer->rasterStorage.isEnabled()) {
                QRect bounds;
                QImage image = layer->rasterStorage.toImage(&bounds);
                node->layer->rasterStorage.replaceWithImage(
                    image, bounds.topLeft(), layer->rasterStorage.tileSize());
                node->layer->rasterStorage.setBaseSize(layer->rasterStorage.baseSize());
            }
            node->layer->owner = node.get();
            node->layer->resetTransform = layer->resetTransform;
            node->layer->hasResetTransform = layer->hasResetTransform;
            if (!layer->maskImage.isNull())
                node->layer->maskImage = layer->maskImage.copy();
            if (!layer->maskRawImage.isNull())
                node->layer->maskRawImage = layer->maskRawImage.copy();
            node->layer->maskOrigin = layer->maskOrigin;
            node->layer->maskVisible = layer->maskVisible;
            node->layer->maskDensity = layer->maskDensity;
            node->layer->maskFeather = layer->maskFeather;
            node->layer->maskTextureOutdated = true;
            node->layer->maskThumbDirty = true;
            if (layer->textData)
                node->layer->textData = std::make_shared<TextLayerData>(*layer->textData);
            if (layer->shapeData)
                node->layer->shapeData = std::make_shared<ShapeData>(*layer->shapeData);
            if (layer->distortData)
                node->layer->distortData = std::make_shared<DistortData>(*layer->distortData);
            if (layer->rawData)
                // Copies RawLayerData by value (cheap: a QString + a
                // RawGeometry + the revision counter) while sharing the
                // immutable RawSource — see RawLayerData.hpp.
                node->layer->rawData = std::make_shared<RawLayerData>(*layer->rawData);
            // Shares the same immutable payload instead of copying it: both
            // instances genuinely reference the same document, mode and render
            // generation, and because the value is const neither can mutate the
            // other's view of it. What distinguishes two instances (transform,
            // opacity, blend, mask, effects) lives on the node and the Layer.
            node->layer->smartObjectData = layer->smartObjectData;
        }

        for (auto& child : children) {
            auto childClone = child->clone();
            childClone->parent = node.get();
            node->children.push_back(std::move(childClone));
        }

        return node;
    }

    // Cheap, thread-safe snapshot for off-thread CPU compositing
    // (AsyncProjectionBuilder). Unlike clone(), this does NOT deep-copy pixels:
    // a member-wise Layer copy COW-shares every QImage (cpuImage, mask,
    // rasterStorage/tileManager tile images) with the live layer — only the
    // small tile containers and metadata are copied, so the snapshot owns its
    // own structure (no data race on the tile vector) while pixel buffers are
    // shared via QImage's atomic copy-on-write (a concurrent UI-thread write
    // detaches the original, leaving the snapshot's view intact). GPU handles are
    // zeroed because the snapshot is CPU-only and must never free the live
    // layer's textures. Safe to build on the UI thread (no memcpy) and to destroy
    // on the worker thread (Layer has no GL cleanup in its destructor).
    std::unique_ptr<LayerTreeNode> shallowClone() const {
        auto node = std::make_unique<LayerTreeNode>();
        node->type = type;
        node->name = name;
        node->nameIsAuto = nameIsAuto;
        node->opacity = opacity;
        node->fillOpacity = fillOpacity;
        node->visible = visible;
        node->blendMode = blendMode;
        node->groupBlendMode = groupBlendMode;
        node->lockFlags = lockFlags;
        node->collapsed = collapsed;
        node->transform = transform;
        node->clipped = clipped;
        node->effects = effects;
        if (adjustment)
            node->adjustment = std::make_shared<AdjustmentData>(*adjustment);
        node->invalidateEffects();

        if (layer) {
            node->layer = std::make_shared<Layer>(*layer); // COW: no pixel copy
            node->layer->owner = node.get();
            // CPU-only snapshot: drop the live layer's GPU handles so the
            // snapshot can never delete or sample them.
            node->layer->textureId = 0;
            node->layer->fbo = 0;
            node->layer->maskTextureId = 0;
            node->layer->maskFbo = 0;
            for (unsigned int& lod : node->layer->lodTextures)
                lod = 0;
        }

        for (auto& child : children) {
            auto childClone = child->shallowClone();
            childClone->parent = node.get();
            node->children.push_back(std::move(childClone));
        }
        return node;
    }

    static void flatten(const std::vector<std::unique_ptr<LayerTreeNode>>& roots,
                        std::vector<LayerTreeNode*>& out, int depth = 0)
    {
        for (auto& node : roots) {
            out.push_back(node.get());
            // Groups nest anything; Layer nodes nest Single-Layer-Mode
            // adjustments — both must appear in the flat index space.
            if (!node->children.empty())
                flatten(node->children, out, depth + 1);
        }
    }

    static LayerTreeNode* findByFlatIndex(
        const std::vector<std::unique_ptr<LayerTreeNode>>& roots, int target)
    {
        std::vector<LayerTreeNode*> flat;
        flatten(roots, flat);
        if (target >= 0 && target < static_cast<int>(flat.size()))
            return flat[target];
        return nullptr;
    }

    static Layer* layerByFlatIndex(
        const std::vector<std::unique_ptr<LayerTreeNode>>& roots, int target)
    {
        auto* node = findByFlatIndex(roots, target);
        // Adjustment nodes also carry a Layer (mask storage), so mask
        // commands/operations addressed by flat index work on them too.
        const bool carriesLayer = node
            && (node->type == Type::Layer || node->type == Type::Adjustment);
        if (!carriesLayer)
            return nullptr;
        if (!node->layer) {
            node->layer = std::make_shared<Layer>();
            node->layer->name = node->name;
            node->layer->owner = node;
        }
        return node->layer.get();
    }

public:
    // Pre-effects/pre-adjustment render of this layer's own pixels — what
    // computeEffectedImage()/effectedImageBounds() return when
    // usesEffectedPipeline() is false. Exposed (unlike those two, these were
    // previously private) so clone/heal sampling can render a layer from its
    // raw pixels when it's also the stroke's paint destination, bypassing its
    // own baked effects/adjustments — see DocumentCompositor::drawLayerNode.
    QImage baseRenderImage() const {
        if (!layer)
            return QImage();
        if (layer->shapeSpriteRenderable())
            return layer->shapeCache.image;
        return layer->renderImage();
    }

    QSize baseRenderImageSize() const {
        if (!layer)
            return {};
        if (layer->shapeSpriteRenderable())
            return layer->shapeCache.image.size();
        return layer->renderImage().size();
    }

    QRectF baseRenderImageBounds() const {
        if (!layer)
            return {};
        if (layer->shapeSpriteRenderable())
            return QRectF(QPointF(0, 0), layer->shapeCache.image.size());
        return layer->renderImageBounds();
    }

private:
    QImage maskedBaseImage() const {
        if (!layer)
            return QImage();

        QImage base = baseRenderImage().convertToFormat(QImage::Format_RGBA8888);
        const QRectF baseBounds = baseRenderImageBounds();
        if (!layer->maskVisible || layer->maskImage.isNull()
            || layer->maskDensity <= 0.0f)
            return base;

        const int w = base.width();
        const int h = base.height();
        if (w <= 0 || h <= 0)
            return base;

        QImage mask = layer->maskImage.convertToFormat(QImage::Format_Grayscale8);

        for (int y = 0; y < h; ++y) {
            uchar* out = base.scanLine(y);
            for (int x = 0; x < w; ++x) {
                const int layerX = static_cast<int>(baseBounds.left()) + x;
                const int layerY = static_cast<int>(baseBounds.top()) + y;
                const int maskX = layerX - layer->maskOrigin.x();
                const int maskY = layerY - layer->maskOrigin.y();
                int val = 255;
                if (maskX >= 0 && maskY >= 0
                    && maskX < mask.width() && maskY < mask.height()) {
                    val = mask.constScanLine(maskY)[maskX];
                }
                if (layer->maskDensity < 1.0f) {
                    val = static_cast<int>(255.0f * layer->maskDensity
                                           + val * (1.0f - layer->maskDensity));
                }
                out[x * 4 + 3] = static_cast<uchar>((out[x * 4 + 3] * val) / 255);
            }
        }
        return base;
    }
};
