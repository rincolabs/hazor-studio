#pragma once

#include <QImage>
#include <QRectF>

#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

class LayerTreeNode;
class AsyncStyleBaker;

// ─────────────────────────────────────────────────────────────────────────
// StyleBakePool — keeps the costly layer-style bake (tile composite + mask
// pass + drop shadow/glow/stroke blur) off the UI thread during live preview.
// Per-node counterpart of ProjectionCache/AsyncProjectionBuilder: Layer
// Styles editing targets one layer at a time, so each node that carries
// effects gets its own worker slot instead of sharing one document-wide
// builder.
//
// ensureFresh() is called from GPUViewport::syncLayersToGpu() for every node
// that needs a styled base. It never blocks:
//   - node->styledBaseDirty() already false, or node has no effects
//       -> nothing to do, returns true (caller's normal synchronous
//          computeStyledBaseImage()/computeEffectedImage() call is then a
//          cache hit, not a recompute)
//   - a finished worker result is waiting
//       -> commits it into the node's own cache (LayerTreeNode::
//          commitStyledBaseBake) and returns true, if it still matches the
//          node's current params (styledBaseRevision); if a newer edit landed
//          while the bake was in flight the result is kept out of the cache
//          and handed back as an InterimBake instead, a fresh bake is
//          dispatched for the current params and this returns false
//   - `canDefer` is false (the GPU has no texture for this node yet)
//       -> returns true so the caller bakes synchronously: there is nothing
//          to keep showing, and a deferred bake would draw the layer unstyled
//   - otherwise dispatches a build (or leaves one already in flight) and
//       returns false
// On false the caller uploads the InterimBake if it got one and otherwise
// keeps the previous texture — either way placed by LayerTreeNode::
// styledBaseTextureBounds()/effectedTextureBounds(), which report the bounds
// the texture was uploaded from (never a rebuild). The repaint that picks the
// next result up comes from setReadyCallback().
// ─────────────────────────────────────────────────────────────────────────
class StyleBakePool {
public:
    StyleBakePool();
    ~StyleBakePool();

    // A finished bake that was superseded before it landed: the params moved on
    // (styledBaseRevision changed), so it must NOT enter the node's cache —
    // export, the CPU projection and the panel thumbnails would then show a
    // style the user has already dragged past. It is still much closer to the
    // current state than the texture the GPU is holding, so ensureFresh() hands
    // it over for a PREVIEW-ONLY upload: the caller uploads the pixels, records
    // `bounds` as that texture's geometry (LayerTreeNode::
    // uploadedEffectedBounds / uploadedStyledBaseBounds) and leaves the node
    // dirty with the texture still flagged outdated, so the fresh bake replaces
    // it. Without this hand-over the texture freezes for as long as the user
    // keeps editing — every bake landing mid-gesture is superseded — which is
    // what made brush dabs on a styled layer and the style drag's own preview
    // stop updating until the gesture paused.
    struct InterimBake {
        QImage image;
        QRectF bounds;
        bool isNull() const { return image.isNull(); }
    };

    // `canDefer` must be false while the node has no GPU texture yet (first
    // style application, project load): see the header comment above.
    bool ensureFresh(LayerTreeNode* node, bool canDefer,
                     InterimBake* interim = nullptr);

    // True while this node's bake is running on a worker. GPUViewport's lazy
    // needsLayerSync poll uses it to NOT re-enter the full syncLayersToGpu
    // every frame just because the style texture is still outdated: that sync
    // re-uploads every mask and drops the projection cache, and the bake's
    // ready callback already schedules the repaint that picks the result up.
    bool isBuilding(const LayerTreeNode* node) const;

    // Invoked (UI thread) whenever a pending worker finishes, so the host can
    // request a repaint; the next needsLayerSync poll uploads the result.
    void setReadyCallback(std::function<void()> cb) { m_onReady = std::move(cb); }

    // Drop the pooled baker for one node (document/layer teardown).
    void release(const LayerTreeNode* node);

    // Drop the bakers of nodes that are no longer in the document. Node
    // deletion doesn't go through cleanupDocumentLayers (that runs on tab
    // close only), so without this a deleted node's entry would linger and a
    // later node allocated at the same address could pick up its pending
    // result. A baker still running is moved aside to drain instead of being
    // destroyed, so this never waits on a worker.
    void retain(const std::vector<LayerTreeNode*>& liveNodes);

private:
    struct Entry {
        std::unique_ptr<AsyncStyleBaker> baker;
        uint64_t dispatchedRevision = 0;
    };
    Entry& entryFor(LayerTreeNode* node);
    // Throw away a result nobody needs any more (the node's cache was baked
    // elsewhere meanwhile) instead of holding a full-resolution QImage until
    // the node goes dirty again.
    void discardPending(const LayerTreeNode* node);

    std::unordered_map<const LayerTreeNode*, Entry> m_entries;
    // Bakers of nodes that were deleted mid-bake: unkeyed (so a node later
    // allocated at the same address can never adopt their result) and simply
    // owned until the worker finishes, then destroyed by the next retain().
    std::vector<std::unique_ptr<AsyncStyleBaker>> m_draining;
    std::function<void()> m_onReady;
};
