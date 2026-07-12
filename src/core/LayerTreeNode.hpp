#pragma once

#include <QDebug>
#include <QHash>
#include <QString>
#include <QVariantMap>
#include <QImage>
#include <QPainter>
#include <QRectF>
#include <cmath>
#include <algorithm>
#include <memory>
#include <vector>
#include "AdjustmentTypes.hpp"
#include "Layer.hpp"
#include "LayerId.hpp"
#include "LayerEffect.hpp"
#include "text/TextTypes.hpp"
#include "ShapeTypes.hpp"
#include "transform/TransformTypes.hpp"

class LayerTreeNode {
public:
    enum class Type { Layer, Group, Adjustment, Effect };

    // Compositing model for Group nodes.
    //  - Isolated (default): children are composited into an offscreen buffer,
    //    then the group's mask/effects/opacity/blend are applied once to that
    //    composed result before it is merged into the parent.
    //  - PassThrough: future extension point (children blend with the layers
    //    below the group). Not implemented yet; groups always act as Isolated.
    enum class GroupBlendMode { Isolated, PassThrough };

    Type type = Type::Layer;
    QString name;

    // Stable, persistent identity (see LayerId.hpp). Auto-generated on
    // construction so every fresh node is unique; clone()/shallowClone()
    // PRESERVE it (render/export/undo snapshots keep the same identity), while a
    // real duplication calls assignNewIds() to get fresh ids for the whole
    // duplicated subtree. Serialized as "nodeId" and restored on load.
    LayerId id = QUuid::createUuid();

    // True while `name` is still a system-generated label (e.g. a freshly
    // created Text Layer). Manual renames clear this flag so automatic naming
    // (deriving the label from the text content on commit) never overwrites a
    // name the user chose themselves.
    bool nameIsAuto = false;

    std::shared_ptr<Layer> layer;

    LayerTreeNode* parent = nullptr;
    std::vector<std::unique_ptr<LayerTreeNode>> children;

    // ── Animatable node state ───────────────────────────────────────────────
    // The four properties the animation system can drive live in one struct so
    // a whole frame's evaluation is a single copy (baseState -> evaluatedState).
    //   - m_baseState:      the static values persisted in the document.
    //   - m_evaluatedState: the final values used for the CURRENT frame.
    // In a static document (no animation) they are always kept equal, so the
    // renderers, hit-testing, bounds and export behave exactly as before.
    //
    // Access rules (enforced by making the fields private):
    //   - Renderers / hit-testing / bounds / export read the EVALUATED getters:
    //       transform(), opacity(), isVisible(), blendMode()
    //   - Tools / UI / commands / serialization edit the BASE via setters:
    //       setBaseTransform/Opacity/Visible/BlendMode
    //   - Only the (future) AnimationEvaluator writes the evaluated state via
    //       setEvaluated* / resetEvaluatedState().
    struct LayerNodeState {
        QTransform transform;
        float opacity = 1.0f;
        bool visible = true;
        BlendMode blendMode = BlendMode::Normal;
    };

    // Evaluated getters — the values consumed for the current frame. Equal to
    // the base values in a static document.
    const QTransform& transform() const { return m_evaluatedState.transform; }
    float opacity() const { return m_evaluatedState.opacity; }
    bool isVisible() const { return m_evaluatedState.visible; }
    BlendMode blendMode() const { return m_evaluatedState.blendMode; }

    // Base getters — the persisted, animation-independent values.
    const QTransform& baseTransform() const { return m_baseState.transform; }
    float baseOpacity() const { return m_baseState.opacity; }
    bool baseVisible() const { return m_baseState.visible; }
    BlendMode baseBlendMode() const { return m_baseState.blendMode; }

    // Base setters. While the node has no active animation track for the
    // property, editing the base also updates the evaluated value so a static
    // document behaves exactly as before. (Track-aware gating arrives with the
    // AnimationEvaluator in a later stage; for now these always mirror.)
    void setBaseTransform(const QTransform& v) { m_baseState.transform = v; m_evaluatedState.transform = v; }
    void setBaseOpacity(float v) { m_baseState.opacity = v; m_evaluatedState.opacity = v; }
    void setBaseVisible(bool v) { m_baseState.visible = v; m_evaluatedState.visible = v; }
    void setBaseBlendMode(BlendMode v) { m_baseState.blendMode = v; m_evaluatedState.blendMode = v; }

    // Restore the evaluated state to the base state. The AnimationEvaluator
    // calls this before applying a frame's tracks.
    void resetEvaluatedState() { m_evaluatedState = m_baseState; }

    // Apply an animation result to the EVALUATED state without touching the
    // base. Intended for the AnimationEvaluator only — tools and UI must go
    // through the base setters. (Kept public until the evaluator exists and can
    // befriend this type in a later stage.)
    void setEvaluatedTransform(const QTransform& v) { m_evaluatedState.transform = v; }
    void setEvaluatedOpacity(float v) { m_evaluatedState.opacity = v; }
    void setEvaluatedVisible(bool v) { m_evaluatedState.visible = v; }
    void setEvaluatedBlendMode(BlendMode v) { m_evaluatedState.blendMode = v; }

    GroupBlendMode groupBlendMode = GroupBlendMode::Isolated;

    bool collapsed = false;
    int lockFlags = LockNone;
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

    // Single Layer Mode: visible adjustments nested under this Layer node.
    bool hasVisibleAdjustmentChildren() const {
        if (type != Type::Layer) return false;
        for (const auto& c : children) {
            if (c && c->isVisible() && c->isAdjustmentLayer())
                return true;
        }
        return false;
    }

    // Normal Mode inside a group: a visible adjustment in the group's direct
    // child list forces isolated compositing so the adjustment only affects
    // the group's own content (nested groups isolate themselves).
    bool hasVisibleAdjustmentDirectChild() const {
        if (type != Type::Group) return false;
        for (const auto& c : children) {
            if (c && c->isVisible() && c->isAdjustmentLayer())
                return true;
        }
        return false;
    }

    // True when this layer's render must go through computeEffectedImage():
    // layer effects and/or Single-Layer-Mode adjustments are baked into one
    // image (mask included). The GPU then draws effectedTexture and the
    // compositors skip their separate mask pass.
    bool usesEffectedPipeline() const {
        return !effects.empty() || hasVisibleAdjustmentChildren();
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

        QImage source = maskedBaseImage();
        if (source.isNull()) {
            m_styledBaseImage = QImage();
            m_styledBaseBounds = QRectF();
            m_styledBaseDirty = false;
            return m_styledBaseImage;
        }

        const QMargins padding = LayerEffect::effectPadding(effects);
        // Verification log: this is the costly style blur. It must fire on a
        // style/pixel/mask change but stay SILENT during an adjustment-layer drag
        // — that silence is the proof the per-frame style re-render is gone.
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
        result = applyEffectStack(result, effects);

        const QRectF baseBounds = baseRenderImageBounds();
        m_styledBaseImage = result;
        m_styledBaseBounds = QRectF(baseBounds.left() - padding.left(),
                                    baseBounds.top() - padding.top(),
                                    result.width(), result.height());
        m_styledBaseDirty = false;
        return m_styledBaseImage;
    }

    // Padded bounds of the styled base (== effected bounds — clipped adjustments
    // don't change image size). Used to place styledBaseTexture in the FBO.
    QRectF styledBaseImageBounds() const {
        if (m_styledBaseDirty || m_styledBaseImage.isNull())
            computeStyledBaseImage();
        return m_styledBaseBounds;
    }

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
            if (!adj || !adj->isVisible() || !adj->isAdjustmentLayer())
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
            adjustments::apply(result, *adj->adjustment, adj->opacity(),
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
        if (!effects.empty())
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
            if (!adj || !adj->isVisible() || !adj->isAdjustmentLayer())
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
            adjustments::apply(result, *adj->adjustment, adj->opacity(),
                               mask, maskTopLeft, density);
        }
        return clampToMax(result);
    }

    // Applies an ordered effect stack to `src` and returns the result. Shared
    // by computeEffectedImage() (layer effects) and the document compositor
    // (group effects) so both go through one code path — no duplicated effect
    // ordering. Filters run first, then layer styles inner->outer.
    static QImage applyEffectStack(const QImage& src,
                                   const std::vector<LayerEffect>& effects)
    {
        QImage result = src;
        for (const auto& e : effects) {
            if (LayerEffect::effectCategory(e.type) != LayerEffect::EffectCategory::LayerStyle)
                result = e.apply(result);
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
                if (e.type == effectType)
                    result = e.apply(result);
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

    unsigned int effectedTexture = 0;
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

    QTransform accumulatedTransform() const {
        QTransform xf;
        const LayerTreeNode* n = this;
        while (n) {
            xf = xf * n->transform();
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
    bool canEditPixels() const { return !isPixelEditingLocked(); }
    // Can the node be moved/transformed/resized/rotated/flipped/nudged/aligned?
    // Adjustment layers are non-spatial (they apply per-pixel over the backdrop
    // / their parent layer), so they are never transformable — no move,
    // bounding box, or transform handles.
    bool canTransform() const {
        return type != Type::Adjustment && !isPositionLocked();
    }
    // Can editable content change (text string/style, shape geometry/fill/stroke)?
    bool canEditContent() const { return !isPixelEditingLocked(); }
    // Can the layer mask be painted/edited? Only the master lock blocks the mask;
    // Lock Image Pixels protects the image, not the mask.
    bool canEditMask() const { return !isFullyLocked(); }

    bool isLeaf() const {
        return type != Type::Group;
    }

    // True when a Group must be composited into an offscreen buffer before
    // being merged into its parent — i.e. its mask/effects/opacity/blend must
    // apply to the *composed* group result, not to each child. When false the
    // compositor may merge the children straight into the parent, which is
    // visually identical for an opacity-1, Normal-blend, effect-free, mask-free
    // group. (A future PassThrough group would also take that fast path.)
    bool needsIsolatedComposite() const {
        if (type != Type::Group) return false;
        return blendMode() != BlendMode::Normal
            || opacity() < 0.999f
            || !effects.empty()
            || hasGroupMask()
            // A Normal-Mode adjustment inside the group must only affect the
            // group's own content, so the children compose in isolation.
            || hasVisibleAdjustmentDirectChild();
    }

    // Group masks are not part of the data model yet (masks live on Layer).
    // This single hook lets the compositor route groups through the isolated
    // path once group masks exist — no other compositor change is required.
    bool hasGroupMask() const { return false; }

    // Assign a fresh LayerId to this node and every descendant. Used by the
    // real "duplicate layer" operation so the copy has a distinct identity from
    // the original (its animation tracks are then remapped to these new ids).
    void assignNewIds() {
        id = QUuid::createUuid();
        for (auto& c : children)
            if (c) c->assignNewIds();
    }

    // Same, but records the old->new id mapping so an external owner (e.g. the
    // animation model on paste) can remap data keyed by the old ids.
    void assignNewIds(QHash<LayerId, LayerId>& mapping) {
        const LayerId old = id;
        id = QUuid::createUuid();
        mapping.insert(old, id);
        for (auto& c : children)
            if (c) c->assignNewIds(mapping);
    }

    std::unique_ptr<LayerTreeNode> clone(bool preserveName = false) const {
        auto node = std::make_unique<LayerTreeNode>();
        node->type = type;
        node->name = preserveName ? name : name;
        // Preserve identity: a clone (undo snapshot / render / export) is the
        // SAME logical node. Real duplication re-ids afterwards.
        node->id = id;
        node->nameIsAuto = nameIsAuto;
        // Copy the full animatable state (base + evaluated), not just the
        // current frame's value — a clone must preserve both.
        node->m_baseState = m_baseState;
        node->m_evaluatedState = m_evaluatedState;
        node->groupBlendMode = groupBlendMode;
        node->lockFlags = lockFlags;
        node->collapsed = collapsed;
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
        node->id = id;  // snapshot keeps the live node's identity
        node->nameIsAuto = nameIsAuto;
        // Preserve the full animatable state (base + evaluated) in the snapshot
        // so off-thread compositing sees the same frame the live tree does.
        node->m_baseState = m_baseState;
        node->m_evaluatedState = m_evaluatedState;
        node->groupBlendMode = groupBlendMode;
        node->lockFlags = lockFlags;
        node->collapsed = collapsed;
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

private:
    // Animatable state (see the accessor block near the top of the class).
    LayerNodeState m_baseState;
    LayerNodeState m_evaluatedState;

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
