# Render pipeline map — CPU and GPU

This document maps the two render paths in Hazor Studio, all of their
sub-paths, and the shared specification layer that keeps both of them
visually equivalent.

**Golden rule:** the CPU compositor (`DocumentCompositor`) is the *visual source
of truth* — commit, export, merge, flatten, and the projection displayed while
idle all come from it. The GPU path exists for *interactive frames* (painting,
dragging, transforms, mask editing) and must produce the same visual result,
pixel by pixel, within the technical limits.

```
                        ┌────────────────────────────────────────┐
                        │        Shared layer (contract)         │
                        │  BlendRules.hpp        (mode → id)     │
                        │  BlendMath.hpp         (C++ formulas)  │
                        │  BlendShaderLib.hpp    (GLSL formulas) │
                        │  SamplingShaderLib.hpp (GLSL resample) │
                        │  LayerTreeNode.hpp     (group model,   │
                        │                         clipping run)  │
                        └────────────┬───────────────────────────┘
                       used by       │      used by
              ┌──────────────────────┴──────────────────────┐
              ▼                                             ▼
   CPU — DocumentCompositor                      GPU — LayerCompositor
   (visual truth, doc-space)                     (live preview, screen-space)
              │                                             │
   idle projection / export / merge /            interaction frames
   flatten / clone sampling                      (brush, drag, mask, preview)
```

---

## 1. Frame entry point: `GPUViewport::render()` (chooses the path)

File: [src/renderer/GPUViewport.cpp](../src/renderer/GPUViewport.cpp)

Every canvas frame passes through here. Decision order:

```
render(RenderParams)
 ├─ renderCanvasDecorations()          shadow, checkerboard, border
 ├─ syncLayersToGpu() if needed        texture/mask/bake uploads
 ├─ valid RenderCache?  ──yes──►  replay cached frame (post color management)
 ├─ beginManagedComposite()            scene FBO for display color management (3D LUT)
 ├─ useProjection && drawProjection()  ──►  PATH A (CPU projection displayed by GPU)
 ├─ otherwise LayerCompositor::composite() ──►  PATH B (per-layer GPU compositor)
 │        (or renderGrayscaleMaskView for mask view)
 ├─ endManagedComposite()              resolve scene → screen via LUT
 └─ overlays                           selection, crop, bounding box, text cursor
```

**Path A — projection (idle frames):** the result from `DocumentCompositor` is
displayed as a single full-canvas texture (`drawProjection`), mapped with
zoom/pan. This is used when there is no `liveEdit`, mask editing, filter preview,
or mask overlay.

**Path B — per-layer GPU (interactive frames):** during brush strokes,
drag/transform, mask editing, filter preview, etc., the layer tree is drawn
layer by layer on the GPU (`LayerCompositor`).

The A↔B switch is where any CPU/GPU divergence appears as a visual "pop" — this
is why both sides share the same specification (§4).

---

## 2. GPU path (live preview)

### 2.1 `LayerCompositor` — tree walker

File: [src/renderer/LayerCompositor.cpp](../src/renderer/LayerCompositor.cpp)

```
composite()
 ├─ needs isolation? (blend mode in any node, stack adjustment, or non-empty doc)
 │   └─ pushGroupFbo(doc)  →  renderNodes(root, canvas-NDC)  →  popGroupFbo(<live-root>)
 └─ renderNodes(nodes, bottom→top):
     ├─ Type::Adjustment  → drawAdjustmentPass()      (§2.3)
     ├─ Type::Group       → one of the three group models (§4.5): direct, or a
     │                      group FBO (pushGroupFbo → children in canvas-NDC →
     │                      popGroupFbo), transparent for Isolated and seeded
     │                      with the backdrop for Pass Through
     └─ Type::Layer:
         ├─ clipping mask (isClippedLayer, §4.6): the base below is captured
         │    into a dedicated canvas-sized alpha buffer (soleLayer re-render),
         │    which every draw below multiplies its source alpha by
         ├─ Single Layer Mode (clippedAdjustmentsOnGpu):
         │    FBO per layer: drawLayerBaseIsolated() + clipped adjustments
         │    as GPU passes; popGroupFbo applies the real opacity/blend once
         ├─ rasterStorage (dab layer): tiles → drawShaderBlend per tile
         │    (blend ≠ Normal) or instanced/fixed (Normal), with mask per tile
         ├─ blend ≠ Normal → drawShaderBlend (blend shader, §2.2)
         └─ Normal → applyFixedBlend + main shader (tiled/instanced or single quad)
```

Space conventions: on screen, MVP = pan/zoom × `canvasHalfExtents` × accumulated
node transform; inside a group FBO, pure canvas-NDC is used (pan/zoom is applied
only once in `popGroupFbo`) — mirroring how the CPU projection is displayed.

### 2.2 Shaders

File: [src/renderer/GPUViewport_Shaders.cpp](../src/renderer/GPUViewport_Shaders.cpp)

| Program | Function |
|---|---|
| **main** | textured quad + mask; premultiplied box resampling from the shared sampler lib (§4.2); instanced for tiles |
| **blend** | blend modes that fixed-function cannot express: target snapshot in `m_blendTex` (dst) + `bm_blend()` from the shared lib + W3C compositing; samples the source through the same shared sampler as **main** |
| **adjust** | stack adjustment (Normal-Mode) as a pass over the backdrop: grayscale, curves/color balance (256×1 LUT), hue/saturation (HSL model mirrored from CPU), solid color (blends with `bm_blend`) |
| **solid** | solid color (decorations, overlays) |
| **select / rubylith / gray** | marching ants, mask overlay, grayscale mask view |
| **display** | final color management stage: 3D LUT doc→monitor over the scene FBO |

The fixed-function blend (`applyFixedBlend`) only implements Normal, with
`glBlendFuncSeparate` to keep alpha correct in transparent FBOs. All other modes
use the blend shader.

### 2.3 GPU adjustments

`drawAdjustmentPass` redraws the quad by sampling the backdrop copy
(`m_blendTex`) and emits `mix(backdrop, adjusted, opacity × mask)`. The shader
branches **must mirror** `adjustments::apply()`
([src/core/AdjustmentTypes.cpp](../src/core/AdjustmentTypes.cpp)) — same LUT
rule, same HSL model (`core/HueSaturationData`), same mask density ramp.

### 2.4 Tiles, LOD, and uploads

- [src/renderer/TileRenderer.cpp](../src/renderer/TileRenderer.cpp): resolves
  which tiles to draw (culling by viewport or by the whole document when inside a
  group FBO) and uploads dirty tiles (`uploadDirtyTiles`,
  `uploadDirtyRasterTiles`).
- [src/renderer/MipmapCache.cpp](../src/renderer/MipmapCache.cpp) +
  `RenderScheduler::decideLOD`: LOD for zoom-out on tiled layers (never in
  group FBO / preview / effected — those require full resolution).
- `syncLayersToGpu` uploads: base texture (or shape sprite), mask (R8),
  `effectedTexture` (bake of effects/clipped adjustments),
  `styledBaseTexture` (base with style for live Single Layer Mode).

There are **two tile systems, and they cull differently.** `TileManager`
([src/core/TileManager.hpp](../src/core/TileManager.hpp)) is a dense grid over a
document-sized `cpuImage`: every cell exists and is "visible" to
`computeTilesForLayer` whatever it contains, and the layer additionally carries
one document-sized RGBA8 texture. `RasterLayerStorage`
([src/core/RasterLayerStorage.hpp](../src/core/RasterLayerStorage.hpp)) is a
sparse map that only materialises a tile once something is painted into it
(`replaceWithImage` skips contentless tiles), so an empty layer resolves to zero
draws and zero VRAM. Because the live compositor redraws the *whole* stack into
a canvas-sized FBO on every brush dab, a dense-grid layer costs a full-canvas
quad per dab even when it is blank — which is why `ImageController::newLayer`
creates new layers on the sparse storage, the same representation the first
brush stroke would convert them to anyway.

### 2.4.1 Async layer-style bake

Files: [src/renderer/StyleBakePool.\*](../src/renderer/StyleBakePool.hpp),
[src/renderer/AsyncStyleBaker.\*](../src/renderer/AsyncStyleBaker.hpp)

`computeStyledBaseImage()` (`LayerTreeNode.hpp`) runs on a worker thread
instead of inline on `syncLayersToGpu`, mirroring `ProjectionCache`/
`AsyncProjectionBuilder` but keyed per node rather than per document (Layer
Styles editing targets one layer). The **whole** bake goes off-thread, not just
`applyEffectStack`'s OpenCV blur: `maskedBaseImage()` is a full-resolution tile
composite plus a per-pixel mask pass, so assembling the pre-blur source on the
UI thread would stall it nearly as much as the blur does.

```
syncLayersToGpu, per node with effects:
 StyleBakePool::ensureFresh(node, canDefer, &interim)
  ├─ node not styledBaseDirty() / no effects → true (nothing to bake, normal
  │    computeStyledBaseImage()/computeEffectedImage() call below is a cache
  │    hit; any result a worker still owes is dropped, not hoarded)
  ├─ a worker result is waiting → commit it (LayerTreeNode::commitStyledBaseBake)
  │    and return true, if it still matches node->styledBaseRevision(); if a
  │    newer edit landed meanwhile the result is kept OUT of the cache (export,
  │    the projection and the thumbnails must not show a style the user already
  │    dragged past) and handed back as `interim` for a preview-only upload,
  │    then a fresh build is dispatched and this returns false
  ├─ canDefer == false (no texture uploaded for this node yet) → true, so the
  │    caller bakes synchronously: there is nothing to keep showing and a
  │    deferred bake would draw the layer unstyled for a frame
  └─ nothing in flight → dispatch one (AsyncStyleBaker::request over
       node->shallowClone() — a COW snapshot, Layer struct copy with the GPU
       handles zeroed and no pixel copy, so the worker never reads the live
       node) and return false
```

`false` means the caller uploads `interim` if it got one and otherwise keeps the
previous `effectedTexture`/`styledBaseTexture` — a Layer Styles slider drag
never blocks the UI thread. `AsyncStyleBaker::ready()` (via
`GPUViewport::setStyleBakeRepaintCallback`, wired in `CanvasView::initializeGL`
like the projection's) requests a repaint once the bake lands, so the next
`needsLayerSync` poll uploads it. `styledBaseRevision()` (bumped by
`invalidateEffects()`) is what lets a superseded bake be kept out of the cache
instead of frozen in as the final result.

The interim upload is what keeps the preview *moving*. Every edit bumps the
revision, so during a continuous gesture — a slider drag, but equally a brush
stroke on a styled layer, which invalidates the node per dab — every bake that
lands is already superseded. Without handing those over the texture would stay
frozen for the whole gesture: the preview (and the dabs) would only appear once
the user paused longer than one bake. Uploaded interim pixels are a preview
only: the node stays dirty and `effectedTextureOutdated` / `styledBaseTextureOutdated`
stay set, so the fresh bake still replaces them. For the `effectedTexture` path
the interim is only requested when the node has no Single-Layer-Mode adjustment
children — the styled base equals the effected image exactly then.

A bake that outlives the gesture also surfaces the app's standard delayed
loading dialog: `StyleBakePool::setBusyCallback` →
`GPUViewport::setStyleBakeBusyCallback` → `CanvasView` (queued — the callback
fires from inside `paintGL`, and a modal dialog spins a nested event loop) →
`ImageController::setStyleBakeBusy` → `progressOperationStarted/Finished`, the
same contract the async document operations use, so `MainWindow::
beginProgressDelayed` shows its `ProgressDialog` only when the bake is still
running after 220 ms. The policy lives in the controller and is deliberately
narrow: only while a Layer Styles edit is waiting for its result — never the
per-dab bakes of a brush stroke on a styled layer, which would put a modal
dialog over every stroke — and only outside the gesture, because the dialog is
application-modal and would steal the slider's mouse grab. The idle edge is
debounced 150 ms so the gaps between a gesture's back-to-back bakes neither
flicker the dialog nor restart its show delay.

Three rules keep the UI thread out of that bake while it is in flight — break
any one of them and the drag stalls again even though the worker is running:

1. **Place the textures with `styledBaseTextureBounds()` /
   `effectedTextureBounds()`**, never with `styledBaseImageBounds()` /
   `effectedImageBounds()`. The `*Image*` pair rebuilds the bake inline when the
   node is dirty — which is precisely its state mid-bake — so `LayerCompositor`
   asking for it ran the full style bake on the UI thread every frame. The
   `*Texture*` pair reports `uploadedEffectedBounds` /
   `uploadedStyledBaseBounds`, recorded by the upload itself, which is the only
   correct placement for the pixels the GPU actually holds: the padded bounds
   move with the effect params, and the cache's own bounds run ahead of the
   texture whenever a bake is in flight, an interim was uploaded, or a CPU
   consumer (export, the histogram panel's composite of the *live* document)
   baked the node inline without uploading anything.
2. **A style change must not set `layer->textureOutdated`** (see
   `ImageController::previewLayerEffects`): styles touch no pixel, and that flag
   makes `syncLayersToGpu` `markAllGpuDirty()` the whole raster — a full
   re-upload of every tile of the layer per slider frame.
3. **The lazy `needsLayerSync` poll skips nodes with `StyleBakePool::isBuilding`**.
   Their style texture stays outdated by design until the bake lands; without
   the guard every frame re-entered the full `syncLayersToGpu`, which re-uploads
   every mask and drops the projection cache for nothing.

The drag itself is additionally bracketed as a live composite edit
(`LayerStylesDialog::liveEditChanged` → `ImageController::setAdjustmentLiveEdit`,
the same hold an adjustment slider drag uses), so the CPU projection is not
re-snapshotted and the layer panel does not rebuild its full-resolution
thumbnails on every slider step.

### 2.5 Frame caches

- [src/cache/RenderCache.cpp](../src/cache/RenderCache.cpp): replays the last
  complete frame (post color management) when nothing changed and no interactive
  tool is active.
- [src/renderer/ProjectionCache.\*](../src/renderer/ProjectionCache.hpp): GL
  texture for the CPU projection, keyed by `doc + compositionGeneration`.

Neither cache covers an interactive frame, which is the point of §2.6.

### 2.6 What a live frame costs — known limitation

**A live frame is O(layers × document pixels), and nothing is reused between
frames.** `p.liveEdit` (brush/eraser stroke, move, transform, shape drag,
adjustment drag, text editing) disables both caches above and routes the frame
through `LayerCompositor::composite()`, which clears a canvas-sized group FBO
and redraws the *entire* stack into it from scratch. A brush stroke calls
`update()` per input event, so that whole-stack recomposite runs **per dab**.

Three properties of that cost are worth stating plainly, because each one
surprises people reading the code for the first time:

- **It does not scale with zoom.** The group FBO is `doc->size`, not the
  viewport, so painting zoomed out to 25% costs exactly what painting at 100%
  costs.
- **It does not scale with the dab.** A 3 px brush and a 300 px brush recompose
  the same full canvas; only the CPU-side dab rasterisation
  ([BrushRenderer.cpp](../src/brush/BrushRenderer.cpp), which touches just the
  active layer's tiles) tracks the brush size.
- **It scales with the layer count, content or not.** Every visible layer is one
  more full-canvas pass. A layer that is *empty* is only free if it owns no
  tiles — see the sparse/dense distinction in §2.4, which is why
  `ImageController::newLayer` uses `RasterLayerStorage`.

Two things have been done about the constant factor. The sparse storage above
takes blank layers out of the loop entirely, and the grid-aligned 1:1 shortcut
in the shared sampler (§4.2) cut each remaining layer's pass from 16
`texelFetch` per document pixel to 1 — measured 7.5× on the per-frame sampling
cost, at any layer count. **The O(layers) term itself is still there.**

The fix that removes it is a **dirty-rect live composite**: during a stroke the
only thing that changes is the dab's bounding rect, so the frame should
recompose that rect rather than the canvas. The shape it would take:

1. Promote the live composite's root FBO out of `pushGroupFbo`'s pool
   ([GPUViewport.cpp](../src/renderer/GPUViewport.cpp)) into a dedicated member
   keyed on `(doc, size)`, and stop clearing it per frame. It is in **document
   space**, so pan and zoom do not invalidate it — only pixel mutation does.
2. Accumulate the dirty rect. `CanvasView` already computes a per-segment rect
   for the dense-tile path (`layer->dirtyRegion`); extend that to the
   `rasterStorage` path and carry the accumulated rect in `RenderParams`.
3. Scissor the viewport to that rect and hand it to the tile cullers in place of
   `QRectF(0, 0, docW, docH)`. `renderNodes` is otherwise unchanged, so blend
   semantics stay exact — the whole stack still runs, just clipped.
4. Invalidate the whole FBO on anything that is not a dab: active-layer change,
   visibility/opacity/blend edits, a `compositionGeneration` bump, resize. Those
   frames fall back to today's full recomposite.

Related, and worth fixing in the same pass: `drawShaderBlend` blits the whole
target into `m_blendTex` on **every call**, and the raster-storage path calls it
**per tile**, so a dab layer with a non-Normal blend mode already costs
`tiles × full-canvas blit` per frame. A dirty rect shrinks that blit to the same
rect.

---

## 3. CPU path (source of truth)

### 3.1 `DocumentCompositor` — API

File: [src/renderer/DocumentCompositor.cpp](../src/renderer/DocumentCompositor.cpp)

| Function | Usage |
|---|---|
| `composite(doc, ctx)` | full flatten of visible layers (projection, export, flatten) |
| `compositeOnlyFlatIndex` | a single layer (clone/heal sampling, without stack adjustments) |
| `compositeFromFlatIndex` | from the index down (clone "current & below") |
| `compositeSubset(doc, layers, applyAdjustments, ctx, ancestorGroupsDirect)` | subset in the real stack order — basis for Merge Down. With `ancestorGroupsDirect=true`, ancestor groups are **not** isolated (the group opacity/blend/effects stay live in the tree, not baked into the pixels). Unrelated to `GroupBlendMode::PassThrough`: the flag overrides whatever model the groups themselves ask for |
| `compositeIsolatedNode(doc, node, trimToLayerBounds)` | one Layer/Group/Solid-Color-adjustment node alone, against a transparent backdrop — Export Layer/Group As... (§3.3) |

Output is always in document space (`doc->size`), `Format_RGBA8888`. This
includes `compositeIsolatedNode`: it renders at full canvas size/alignment
even though only one node's content ends up in the result, because a nested
Normal-Mode adjustment's mask (and any future group mask) is anchored in
absolute document-pixel space — shifting or resizing the render target would
misalign it. Its Layer Bounds mode crops the *result* to its non-transparent
pixels afterward instead; a node with no on-canvas pixels (e.g. entirely
outside the canvas) has nothing to crop to and comes back null.

### 3.2 Internal flow

```
compositeFiltered
 └─ renderNodesFiltered(root, bottom→top)
     ├─ Adjustment (Normal-Mode) → applyAdjustmentNode over the accumulated result
     │    (solidcolor = fill composited with the node blend; others via adjustments::apply;
     │     blend ≠ Normal: the entire adjusted backdrop is re-composited with the mode)
     ├─ Group → one of the three group models (§4.5):
     │    direct merge when !needsIsolatedComposite (incl. a full-opacity Pass Through);
     │    pass-through stage = copy of the target → children → compositePassThroughStage;
     │    isolated: stage → group mask (future) → group effects
     │    (applyEffectStack, same stack used by layers) → compositeStage(blend, opacity)
     └─ Layer → drawLayerNode (= buildLayerStage + compositeStage):
         ├─ source: computeEffectedImage() (bake of effects + clipped adjustments,
         │    mask included) or renderImage() (shape base); dabs = rasterStorage.toImage()
         ├─ transform: buildImgToCanvas (img→NDC→canvas, same chain as io/ImageIO
         │    and the GPU MVP; shape sprite uses spriteTransform)
         ├─ anything but a whole-pixel copy → resampleAffine: ONE pass evaluated at
         │    the inverse-mapped position, premultiplied, per axis an exact box
         │    (downscale) or a Keys cubic (upscale/rotate/sub-pixel offset) (§4.2);
         │    whole-pixel copy and projective transforms → QPainter
         ├─ mask: DestinationIn with density ramp mix(1, m, density)
         ├─ clipping mask (§4.6): the stage's premultiplied channels are scaled
         │    by the base layer's own stage alpha (applyClipAlpha)
         └─ compositeStage(target, stage, blendMode, opacity):
              modes with QPainter equivalent → native;
              Hue/Saturation/Color/Luminosity → compositeNonSeparable
              (blendmath::blendHsl + manual W3C compositing)
```

### 3.3 Consumers of the CPU path

| Consumer | File | What it uses |
|---|---|---|
| Idle projection | [ProjectionCache](../src/renderer/ProjectionCache.hpp) + [AsyncProjectionBuilder](../src/renderer/AsyncProjectionBuilder.hpp) | `composite` on a worker thread over a COW snapshot (`shallowClone`); result becomes the texture displayed by Path A |
| Export | [src/io/ImageIO.cpp](../src/io/ImageIO.cpp) | `composite` |
| Export Layer/Group As... | [src/io/ImageIO.cpp](../src/io/ImageIO.cpp) (`compositeNodeImage`) | `compositeIsolatedNode`: one Layer/Group/Solid-Color-adjustment node against a transparent backdrop, no other node in the document, rendered canvas-sized/aligned like any other composite. Layer Bounds mode then alpha-trims the result to its non-transparent pixels (null if there are none, e.g. the node sits entirely outside the canvas). A non-Solid-Color Adjustment has no content of its own (`LayerTreeNode::hasIsolatedExportContent`) and is rejected before rendering. |
| Merge Down | `applyMergeLayersState` ([ImageController.cpp](../src/controller/ImageController.cpp)) | `compositeSubset` of the pair, siblings from the same container, ancestor groups pass-through; blend mode survives when only one side has pixels |
| Merge Visible / Flatten | `applyMergeVisibleState` / `applyFlattenImageState` | full `composite`; result is hoisted to the root |
| Clone/Heal sampling | `compositeOnlyFlatIndex` / `compositeFromFlatIndex` | without stack adjustments (do not "bake" twice) |
| Thumbnails | `computeEffectedThumbnail` (LayerTreeNode) | reduced bake, off-thread |

Destructive operations run through the *async document-state path*
(`runDocumentStateOperationAsync`): document COW snapshot → deep clone in the
worker → operation → state applied back + undo.

---

### 3.4 Smart Objects — the cache is not the source of truth

A Smart Object Layer displays another document's composite. The rule that keeps
that from becoming a render-pipeline problem:

> **For a Smart Object Layer, the referenced document is the *only* source of
> truth. The layer's `cpuImage` is a discardable, regenerable representation
> of it.**

Everything else about the feature follows from that sentence.

**The compositors know nothing about it, and must keep knowing nothing.** A
Smart Object is an ordinary `Type::Layer` whose `Layer` carries a
`smartObjectData` payload ([src/smartobject/SmartObjectData.hpp](../src/smartobject/SmartObjectData.hpp)).
Its `cpuImage` holds the referenced document's render — exactly the way
`rawData` + `cpuImage` hold a RAW file's developed result. So it composites as a
plain raster layer, and `DocumentCompositor`, `LayerCompositor`, `GPUViewport`,
`ProjectionCache` and `RenderCache` contain **zero lines** about it. Mask,
blend, opacity, instance transform and layer styles work on it for free, and
CPU/GPU consistency (§3, invariant 1) holds by construction: both paths read the
same pixels.

**Why the payload carries a `docId` and never a `Document*`.** This is the part
that will look like an easy optimization later, so: the CPU projection is built
on a **worker thread** over a `shallowClone()` snapshot
(`ProjectionCache::makeSnapshot`). That snapshot is only thread-safe because
every payload a layer shares is immutable or copy-on-write. A `Document` pointer
in the payload would hand the compositor a live, UI-thread-mutated object graph
to walk — a data race whose symptom appears nowhere near this file. Resolving a
reference during a composite is therefore not a shortcut; it is the bug. The
payload is also held as `shared_ptr<const SmartObjectData>`, so no clone,
snapshot, paste or undo path can mutate through it: a change builds a new value
and swaps the pointer.

**The render is per `(document, destination profile)`, shared by instances.**
`cpuImage` depends only on the referenced document and the profile it has to
land in — the instance's transform, mask and effects are applied by the
compositor, downstream. So `SmartObjectRenderService`
([src/smartobject/SmartObjectRenderService.hpp](../src/smartobject/SmartObjectRenderService.hpp))
keys work by `(docId, linkedPath, profile fingerprint)`, not by instance, and
three instances of one document share a single `QImage` (free, via Qt's implicit
sharing) instead of three identical composites. Nothing writes to that buffer —
`hasDirectlyEditablePixels()` is false for these layers — so no instance can
detach it.

| Step | Where | Notes |
|---|---|---|
| Snapshot | `SmartObjectRenderService::request` (UI thread) | `shallowClone()`, same technique as `ProjectionCache::makeSnapshot`; the referenced document's `compositionGeneration` is captured **at scheduling time** as the job's identity |
| Composite | worker | `DocumentCompositor::composite` with `RenderTargetType::Flatten` — the source of truth, so an instance can never disagree with an export |
| Colour | worker | converted into the parent document's profile through `ColorManagementService`, the same path an imported image takes; the referenced document keeps its own profile |
| Commit | `ImageController::commitSmartObjectRender` path (UI thread) | drops a result older than the last committed for the key, then hands the one `QImage` to every live instance and bumps each affected document's `compositionGeneration` |

**No pending job holds a node pointer.** A job's identity is
`(key, jobGeneration)`; the instances are resolved by walking the live tree at
commit time. Keying pending work by `LayerTreeNode*` — which `m_pendingRawDevelops`
does — is only narrowly safe for RAW (it needs the layer deleted mid-render *and*
a reallocation at the same address); with a nested cascade re-entering the same
keys, that window is wide open.

**A nested cascade is chained by commit, not merely ordered.** If A displays B
and B displays C, editing C must re-render B and then A. Enqueuing `[B, A]`
together is not enough: run concurrently, A's snapshot still holds B's *old*
pixels, and both outcomes are bad — if B's commit bumps `B.compositionGeneration`
A re-renders (wasted work plus a visible flash of wrong content); if it does not,
A stays stale forever. So A is scheduled only **after B commits**:

```
C changed  →  render B
              B commits  →  ++B.compositionGeneration  →  render A
                                                          A commits
```

The next level is *discovered* from what actually changed — the commit reports
which documents it touched — rather than precomputed, so the order and the
correctness come from the same mechanism. The cycle guard is separate and sits
at resolve time: `DocumentContainer::canReference()` refuses an edge whose
target can already reach its host, so a malformed file containing a loop renders
nothing instead of cascading forever.

**Updating the cache is never an undo step.** It mirrors the layer-style bake
(`StyleBakePool` → `commitStyledBaseBake`, §2.4.1): off-thread over a COW
snapshot, revision-discarded, committed with no command. Recording it would
fight the very check that triggers it — an undo would restore old pixels, the
staleness test would see them and re-render, undoing the undo. This does not
weaken AGENTS.md invariant 5: that invariant covers *destructive edits*, and
refreshing a regenerable cache is not one. `ProjectionCache`, `effectedTexture`
and `styledBaseTexture` are the existing precedents.

The one exception is an **unresolvable linked reference**. Its persisted render
is then all that is left, so the instance keeps displaying it, is marked
`linkBroken`, cannot be edited, and regenerates the moment the link resolves
again. That is a fallback for a reference that cannot be resolved — not a second
source of truth. It is also the only reason the render is persisted in the
`.hazor` at all. See [SMART-OBJECTS.md](SMART-OBJECTS.md).

---

## 4. Shared specification layer

Four files, one contract — three for blending, one for resampling — plus two
decisions that live on the data model instead (the group compositing model,
§4.5, and the clipping-mask run, §4.6):

| File | Role |
|---|---|
| [src/renderer/BlendRules.hpp](../src/renderer/BlendRules.hpp) | maps `BlendMode` → primitive for each backend: shader id (`shaderId`), QPainter mode (`painterMode`), what requires shader/manual compositing |
| [src/renderer/BlendMath.hpp](../src/renderer/BlendMath.hpp) | C++ formulas: non-separable HSL (lum/clipColor/setLum/sat/setSat W3C) and the separable modes QPainter lacks, both behind `blend(mode, cb, cs)`; `coverage` (Dissolve), `maskDensityRamp = mix(1, m, density)` |
| [src/renderer/BlendShaderLib.hpp](../src/renderer/BlendShaderLib.hpp) | the same formulas in GLSL (`bm_*`), injected into the blend shader and adjust shader; `bm_blend(mode, cb, cs)` covers the 26 colour modes, `bm_coverage` the 27th (Dissolve) |
| [src/renderer/SamplingShaderLib.hpp](../src/renderer/SamplingShaderLib.hpp) | the resampling kernels in GLSL (`sm_*`), injected into **both** the main shader and the blend shader; C++ mirror is `boxWeights`/`cubicWeights`/`resampleAffine` in `DocumentCompositor.cpp` (§4.2) |

**Maintenance rule:** when adding/changing a blend mode, update all three blend
files together — `BlendMath.hpp` and `BlendShaderLib.hpp` are kept comparable
function by function. ColorDodge/ColorBurn follow the edge semantics of Qt's
raster engine (which executes the natively-supported separable modes on the
CPU), without NaN; Vivid Light is expressed *through* them so the two agree at
those edges. A fourth table lives in `BlendRules.hpp`: `pickerEntries()`, the
grouped order and labels every blend-mode combo is built from — `BlendMode`
values are appended (they are persisted in `.hzr` and in the tool API), so the
enum order is not a display order. The same rule holds for the sampler:
`SamplingShaderLib.hpp` and `resampleAffine` move together.

### 4.1 Alpha and compositing

- CPU composites in `ARGB32_Premultiplied`; GPU composites straight-alpha in
  fixed-function (with `glBlendFuncSeparate` for correct alpha) and
  premultiplied inside group FBOs — `uSourcePremultiplied`/`uDstPremultiplied`
  control conversion during sampling.
- Blend modes read the backdrop: over a transparent backdrop, both sides apply
  the W3C fade `Cs' = (1-ab)·Cs + ab·B(Cb,Cs)` (shader: `mix(srcColor, Cr, da)`;
  QPainter: native). Consequence for merges: see §5.
- Modes QPainter has no `CompositionMode` for (the HSL modes, the
  light/arithmetic modes, Dissolve) run per pixel through
  `DocumentCompositor::compositeStage` — the one CPU execution of the spec, also
  used by the gradient tool so a tool blend matches a layer blend.
- **Dissolve is a coverage mode, not a colour formula.** Both sides convert the
  effective source alpha (opacity × mask × soft edges) into a 0/1 dither keyed
  on the *document* pixel with the same 32-bit integer hash
  (`blendmath::ditherNoise` / `bm_ditherNoise`); a float hash would not be
  reproducible across the two. The shader reads that pixel from `gl_FragCoord`
  with Y flipped, which is the document grid because a live stack is always
  isolated into a canvas-sized FBO (§2.1) — so the pattern does not move with
  pan/zoom and does not change when the frame is replaced by the projection.
  The threshold still compares against each side's own alpha, so on a soft edge
  the documented sampling tolerance (§4.2) can flip an individual pixel between
  the two paths; the dither pattern itself is identical.

### 4.2 Sampling / antialiasing (equivalence)

Both backends run the **same two kernels over premultiplied samples**, picked
**per axis** by the screen-pixel footprint (source texels per output pixel along
that axis), never by a user setting:

| footprint | Kernel | Behaviour |
|---|---|---|
| `> 1` | exact box of `footprint` texels | minification: the area average (what `cv::INTER_AREA` computes). Degenerates *continuously* into plain bilinear as the footprint approaches 1, so there is no seam to put a threshold at |
| `≤ 1` | Keys cubic, `A = -0.75` | magnification, rotation, or a merely sub-pixel offset. At an integral sample position the weights are exactly `(0,1,0,0)`, so a grid-aligned 1:1 frame is still a **bit-exact copy** — which `sm_sampleSurface` detects and serves with a single fetch |

`A = -0.75` is not a taste call: it is exactly `cv::INTER_CUBIC`, which every
destructive path already bakes (§4.3), so a non-destructive layer scale and the
same scale applied for real land on the same pixels. Measured against
`cv::resize(INTER_CUBIC)`: mean 0.02/255, max 1/255.

The cubic's negative lobes can leave the premultiplied range (alpha above 1, or
a colour channel above alpha), which unpremultiplies to brighter than white and
rings as a halo — in Qt's 8-bit `qUnpremultiply` it wraps to a dark speck
instead. Both sides therefore clamp colour to alpha before dividing
(`sm_clampPremultiplied`; `toByte` + `min` on the CPU). Measured on a 2.5×
upscale of random RGBA: 10 789 pixels would exceed alpha unclamped, worst excess
48/255.

| Situation | GPU | CPU |
|---|---|---|
| Layer → document (any transform) | `sm_sampleSurface` with `uSharpMagnify = 0`: box above footprint 1, cubic at or below it | `resampleAffine` (one pass, same two kernels); QPainter only for a whole-pixel copy (not a resample) and for a projective transform |
| Document → screen (the *view* step) | `uSharpMagnify = 1`: the box stays, and its width may drop below 1 — never the cubic | n/a — this step has no CPU counterpart; the projection **is** the CPU truth at document resolution |
| Masks | bilinear R8 texture, ramp `mix(1, m, density)`, outside UV = 1 | same via DestinationIn + `maskDensityRamp` |

Five consequences worth knowing:

- **The view step is the only place that sharpens *below* footprint 1.** There
  the box is narrower than a texel, so texel interiors stay flat and the blend
  is confined to the texel boundary (crisp like GIMP's magnified view, still
  antialiased rather than hard nearest). Scaling a layer up, by contrast, is a
  *document* edit the CPU bakes with the cubic, so the GPU must use the cubic
  too or the live preview diverges from the export. `uSharpMagnify` is enabled
  at exactly two draw sites — `drawProjection` and the top-level `popGroupFbo` —
  and `setMainUniforms` defaults it off so no layer draw can pick it up.
- **The footprint is a gradient norm, not `fwidth()`.** `fwidth` sums
  `|∂u/∂x| + |∂u/∂y|`, which is the *bounding box* of a rotated footprint and
  reaches 1.41 texels for a 45° rotation at scale 1 — prefiltering a layer that
  needs none. `sm_footprint` measures `length(vec2(dFdx(u), dFdy(u)))` per axis,
  which equals the CPU's per-axis transform scale for any scale+rotation and is
  bit-identical to `fwidth` whenever the draw is axis-aligned (so every view
  step is unchanged). A genuine *shear* is the one case where the two measures
  still differ; there the shader's is the physically correct one and the CPU
  under-filters.
- **The 0.1% slack at footprint 1 is load-bearing.** The kernel flips at
  `footprint ≤ 1.001`, not `≤ 1`: a rotated layer at scale 1 lands exactly on 1,
  and the shader derives the footprint from screen-space derivatives while the
  CPU derives it from the transform — they agree only to within rounding, and
  without the slack the two backends could pick different kernels for the same
  draw. An axis minifying by less than 0.1% needs no prefilter anyway.
- **The grid-aligned 1:1 draw short-circuits to one fetch.** When both axes are
  on the cubic at footprint 1 (within the same 0.1%) *and* the sample lands on a
  texel centre, `sm_sampleSurface` returns that texel instead of running the
  separable pass. This is not an approximation — the Keys weights there are
  exactly `(0,1,0,0)`, so the shortcut is the separable result without the
  rounding the weight polynomials carry, and it is the same case the CPU hands
  to QPainter as a whole-pixel copy. It matters because it is the live
  compositor's *steady state*, not a corner case: `LayerCompositor` draws every
  untransformed layer 1:1 into the canvas-sized group FBO and redraws the whole
  stack per brush dab, so without it the stack cost 16 `texelFetch` per document
  pixel per layer on every interactive frame. The footprint half of the test is
  what keeps the branch warp-coherent — at footprint 1 either every fragment of
  the primitive has an integral sample position or none has, whereas under
  magnification only the fragments that happen to land on a texel centre would
  qualify, splitting warps for no saving.
- **Minification uses a pyramid where one exists.** A fixed tap budget spread
  over an ever-wider footprint undersamples (moiré at low zoom), so when the
  bound texture carries a premultiplied mip pyramid the sampler picks the level
  whose residual footprint lands in `[1,2)` and boxes *that* — the GEGL/GIMP
  pyramid idea (`gimpprojection.c`). Only the projection texture has one
  (`ProjectionCache`, built with `glGenerateMipmap` over
  `Format_RGBA8888_Premultiplied`); everywhere else `uMipLevels = 0` and level 0
  is boxed directly, with the box width capped at `SM_MAX_WIDTH` so the weights
  still sum to 1.
  Deliberately **not** `GL_LINEAR_MIPMAP_LINEAR`: trilinear cross-fades level L
  and L+1, which is *blurrier* than a direct area filter at the non-power-of-two
  scales "fit to window" actually lands on.

A pyramid level always leaves a residual footprint above 1, so the pyramid and
the cubic never meet.

The pyramid is why the CPU and GPU minification are "the same kernel" rather
than bit-identical: a cascade of box reductions is very slightly wider than one
box of the combined width. Measured against the ideal single box, the pyramid
path deviates by ≤ 2.3/255 on average (exact at power-of-two ratios); the
level-0 path matches the CPU to ≤ 1/255 (float32 vs float64 rounding) and is
bit-identical at 1:1. The cubic half matches to the same ≤ 1/255, including
anisotropic draws that take the box on one axis and the cubic on the other, and
rotations at scale 1 (where only the quad-edge pixels differ, because the GPU
rasterises the layer quad while the CPU tests the destination pixel centre).

### 4.3 Resampling outside the compositors

§4.2 covers the two compositors. Everything *else* that resamples layer pixels —
destructive edits, clipboard copies, the distort warp, filter previews — must
still obey one rule: **mix pixels in premultiplied alpha**. With straight alpha
the colour stored in a fully transparent pixel is arbitrary (usually black) yet
still gets its share of the kernel weight, so every soft or antialiased edge
picks up a dark fringe — the same defect the projection pyramid is premultiplied
to avoid.

The helpers live in
[src/engine/ImageEngine.hpp](../src/engine/ImageEngine.hpp):
`premultiplyInPlace` / `unpremultiplyInPlace`, plus
`warpAffinePremultiplied` / `resizePremultiplied`. The round trip is 8-bit, to
match Qt's `Format_RGBA8888_Premultiplied` and `ProjectionCache`'s upload; it
costs precision in the colour of near-transparent pixels, which is where it is
least visible. A float path belongs with the linear-light work, not here.

Two things that are easy to get wrong:

- **Masks must not go through it.** Single-channel data has no alpha to
  premultiply by, so `SelectionMask`, `Document::docMaskToLayer`, `LayerEffect`
  and the `warpMask*` helpers stay plain `cv::warpAffine`/`cv::resize`. The
  helpers forward anything that is not 4-channel `CV_8U` untouched, but routing
  a mask through them "for consistency" is a regression, not a cleanup.
- **Cubic and Lanczos overshoot.** Their negative lobes can push a colour
  channel above alpha, which breaks the premultiplied invariant. Unpremultiplied
  naively that lands above white and rings as a bright halo — and Qt's
  `qUnpremultiply()` masks rather than clamps, so it wraps to a speck.
  `unpremultiplyInPlace` clamps colour to alpha first; use it instead of
  converting through `QImage` after a cubic warp.

The Clone Stamp and Healing Brush do not go through `ImageEngine`: they point
sample the frozen document snapshot per dab pixel
([`sampleBilinearRgbaRaw`](../src/brush/BrushRenderer.cpp)), as does the
clone-source preview overlay ([`sampleCanvasBilinear`](../src/renderer/CanvasView.cpp)).
The Eyedropper's 3x3/5x5/11x11 sample sizes average a neighbourhood too, in
[`ColorSamplerService::averageRegion`](../src/engine/ColorSamplerService.cpp),
and weight colour by alpha for the same reason — a plain RGBA mean reported a
white stroke's edge as mid-grey. Its alpha stays a plain mean, since that is the
coverage of the region rather than a colour.

They carry the premultiplied rule themselves — the sampler mixes its four taps
that way, and the Healing Brush's box blur stages premultiplied copies of both
patches so each low-frequency base is an alpha-weighted average,
`blur(colour * a) / blur(a)`, divided by its own blurred alpha. The patches stay
straight, because the detail term is a *difference* and a difference only
unpremultiplies cleanly when both halves are normalised by the same alpha.

Plus one rule that is specific to a point sampler:

- **Document-pixel coordinates put integers on texel *edges*.** That is the
  space `layerPixelToDocumentPixel()` and `screenToDocument()` return, so the
  centre of texel `i` is `i + 0.5`; a tap index instead has the integer *on* the
  centre. A sampler that skips the half-texel shift does not merely come out
  offset. The dab loops sample at pixel centres and `beginCloneStampStroke()`
  rounds the clone offset to whole pixels, so every tap lands at fraction 0.5 on
  **both** axes and each dab pixel is a flat 2x2 box average of the source: the
  detail of a pure copy, destroyed by a filter with nothing to filter. With the
  shift a whole-pixel offset gives fraction 0, which both samplers short-circuit
  to a texel copy — the §4.4 principle applied to a sampler rather than a pass.
- **The same convention decides which pixel a click acts on.** A tool that
  turns a continuous coordinate into a pixel index — the Eyedropper, the Magic
  Wand's seed, Quick Select, the selection hit test, the Hue/Saturation and
  Curves pickers — must use `floor`, like `fillBucket()` does. `round`/`lround`
  reports the right/bottom neighbour as soon as a fraction reaches 0.5, so only
  the top-left quadrant of a pixel (25% of its area) reports itself, and it
  mishandles negative coordinates outside the canvas as a bonus.
- **Clamp the taps, do not widen the guard.** The shift moves the first tap of
  the outer half texel outside the buffer, where there is no neighbour to blend
  with; clamping to edge there keeps the sampler in bounds while a coordinate
  outside the document rect still returns transparent.

Kernel choice follows direction, not taste: OpenCV's `INTER_AREA` degenerates to
nearest-neighbour when an axis *grows*, so it is used only when both axes shrink
(that is what the Image Size dialog's "Bicubic Automatic" now selects) and
`INTER_CUBIC` otherwise. `cv::warpAffine` has no area mode at all, so the
affine layer-pixel warps use cubic throughout.

### 4.4 One pass per step, and the one that is left

The layer→document step is **one** resample. It used to be two: the box
prefiltered the source down to a rounded integer size and QPainter then placed
that result, so the fractional destination offset cost a bilinear tent on top of
pixels that were already correctly filtered. `resampleAffine` evaluates the box
once at the inverse-mapped position instead. Measured against an ideal
double-precision box, the old pair deviated by a mean of 5–41/255 depending on
the offset (worst case: a complete miss on high-contrast pixels at 0.5× with a
half-pixel offset); one pass lands at 0.25/255, i.e. the 8-bit quantisation
floor. It is also ~15–18% faster, because dropping the QPainter pass more than
pays for evaluating weights per sample.

One deliberate difference from the shader remains, in `resampleAffine`: the CPU
tap count is **not** capped. `SM_MAX_TAPS` is a GPU budget, and this is the path
that writes the export, so a 20× downscale stays exact instead of degrading to a
narrower centred box. (The second divergence, `fwidth` vs the per-axis scale, is
gone — the shader now measures the footprint the same way; see §4.2.)

**The cubic costs taps.** It is 4×4 where bilinear is 2×2, and this path is
scalar C++ against QPainter's SIMD bilinear, so a *transformed* layer is
noticeably more expensive to bake than it was: measured on a 2000×2000 layer,
a 2× magnification into a 4000×4000 document goes from ~57 ms (QPainter) to
~1200 ms, and a 23° rotation from ~28 ms to ~376 ms. Minification is unchanged
(it already used this path). Only transformed layers pay it, and only on the
async projection thread plus one-off export/flatten/merge — but it is the reason
the obvious next optimisation is a two-pass separable evaluation for the
axis-aligned case (4+4 taps instead of 16, mathematically identical).

**The remaining limit is structural.** A transformed layer is still resampled
twice on the way to the screen — layer→canvas at document resolution, then
canvas→screen when the projection is displayed — versus once on the GPU
(layer→screen). At zoom ≠ 100% the projection is therefore slightly softer than
the live path. That is inherent to having a document-resolution projection
(export **is** at that resolution) and is accepted.

### 4.5 Group compositing models (Isolated / Pass Through)

A group's `GroupBlendMode` ([core/LayerTreeNode.hpp](../src/core/LayerTreeNode.hpp))
picks between two compositing models, and both compositors read the *same*
predicates off the node so they cannot drift:

| Predicate | Meaning |
|---|---|
| `needsIsolatedComposite()` | the children must compose against transparency first |
| `isPassThroughGroup()` | the children compose against the backdrop |
| `passThroughNeedsStage()` | ...and the group has an opacity/mask of its own to apply to that result |
| `groupScopeReadsBackdrop()` | some node in the group's own scope reads the backdrop |
| `mergeBlendMode()` | the mode a *composed* group result merges with (Normal for Pass Through) |

That resolves to three paths, in both compositors:

```
direct          children straight into the current target, no buffer
pass-through    children into a COPY of the target → interpolate back by opacity
isolated        children into a transparent buffer → mask → effects → blend/opacity
```

**Isolated (the default).** The group is a unit: its children see only each
other, and the composed result takes the group's mask, effects, opacity and
blend mode once. The "direct" path is an optimisation of this model — it is
taken when merging the children into the parent is *provably* the same picture,
which needs more than an opacity-1 Normal group: no node in the group's own
scope may read the backdrop. `groupScopeReadsBackdrop()` is that test, and the
word *scope* is load-bearing — a nested group that isolates itself contains its
own backdrop readers, so only the mode it merges with counts, while a nested
group that does **not** isolate puts its children in this scope and is walked
into. Skipping that test is what used to make a plain Normal group behave as
Pass Through whenever a child had a blend mode.

**Pass Through.** The children take part in the composite *around* the group:
an inner blend mode reads the layers below the group, and an inner Normal-Mode
adjustment reaches them. The group's blend slot is unused in this model
(`mergeBlendMode()` reads Normal — the UI offers Pass Through as one of the
group's blend-mode choices, so the two can never be set at once), while
opacity, visibility and the group mask still apply.

At full opacity with no mask the model *is* the direct path — no buffer at all.
With an opacity or a mask the children go into a copy of the backdrop (so they
keep blending against the outer content) and that copy is then **interpolated**
back over the untouched backdrop:

```
out = mix(backdrop, backdropWithGroupApplied, opacity × mask)   premultiplied, incl. alpha
```

Interpolation, not source-over. The group has no isolated result that could be
drawn *over* the backdrop, and compositing at `opacity` would re-apply the
backdrop already inside the stage: a 0.5-alpha backdrop under an empty 50%
pass-through group would read 0.75 instead of staying 0.5. The CPU form is
`compositePassThroughStage` ([DocumentCompositor.cpp](../src/renderer/DocumentCompositor.cpp));
the GPU reaches the same result in fixed function, with the group's opacity in
*both* `uOpacity` and `glBlendColor`'s constant alpha —

```
rgb: GL_SRC_ALPHA, GL_ONE_MINUS_CONSTANT_ALPHA   →  Cs·(As·t) + Cd·(1-t) = premul(src)·t + dst·(1-t)
a:   GL_ONE,       GL_ONE_MINUS_CONSTANT_ALPHA   →  As·t      + Ad·(1-t)
```

— because the main shader emits straight-alpha colour with `uOpacity` folded
into its alpha (§2.2). Measured against the CPU form over 216
backdrop/stage/opacity combinations: agreement within 1/255.

The GPU seeds the stage by blitting the current render target into the group
FBO (`pushGroupFbo(w, h, seedFromTarget=true)`), which is a 1:1 copy because a
live stack is always isolated into a canvas-sized FBO (§2.1) and every group
therefore renders inside one.

**Two things Pass Through deliberately does not do.**

- **Layer effects demote the group to Isolated.** A shadow/glow/stroke is
  generated from the node's *own* alpha, which only exists once the children
  are composed against transparency; a pass-through stage is seeded with the
  backdrop, so the same code would grow a shadow around everything underneath
  the group. `isPassThroughGroup()` returns false when an effect is *enabled*
  (`hasEnabledEffects()`, not `!effects.empty()` — the Layer Styles dialog
  keeps the styles it has turned off), and PSD import reports it as the one
  Pass Through case it cannot carry across.
- **The group mask belongs in the interpolation factor, never in the stage.**
  Masks are not on the data model yet (`hasGroupMask()` is `false`), and the
  extension point for the isolated path is `applyGroupMask`. A pass-through
  stage must not go through it: destination-in would punch the *backdrop* away
  wherever the mask is black instead of leaving it unchanged there.

**Nesting falls out of the recursion.** A pass-through group inside a
pass-through group reaches the document backdrop; a pass-through group inside
an isolated one reaches the isolation boundary and stops; an isolated group
inside a pass-through one keeps isolating. One pruning rule needs the model
explicitly, though: `subtreeAffectsComposite` descends into a group that holds
no included layer when it is pass-through and carries a visible Normal-Mode
adjustment, because that adjustment acts on the backdrop — an adjustment-only
pass-through group over the stack below it is a normal way to work, and the
old layer-only test dropped it silently.

### 4.6 Clipping masks

A Layer node with `clipped` set contributes only where its **base** — the first
sibling below it that is not itself clipped — is opaque:

```
alphaClipped(x, y) = alphaSource(x, y) · alphaBase(x, y)      in [0, 1]
```

Both compositors read the same three predicates off the data model
([core/LayerTreeNode.hpp](../src/core/LayerTreeNode.hpp)), so the run cannot be
resolved two ways:

| Predicate | Meaning |
|---|---|
| `isClippedLayer()` | this node clips to the base below it (a `Type::Layer` with the flag) |
| `canBeClipBase()` | the node may *be* a base — any `Type::Layer`, or a `Type::Group` |
| `resolveClipBase(siblings, i)` / `isClipBase(siblings, i)` | the run's base, and the reverse question |

**What `alphaBase` is.** The alpha of the base layer's own canvas-space stage —
its stored transparency, its mask (with the density ramp), its layer styles and
Single-Layer-Mode adjustments, its transform — times the base node's *opacity*.
Three things it deliberately is **not**:

- **Not the accumulated backdrop.** The render target already holds every layer
  below; using its alpha would clip to the union of the whole stack. Both paths
  build the base in a buffer of its own for exactly this reason.
- **Not the base's blend mode.** W3C alpha compositing is `αo = αs + αb(1-αs)`,
  independent of the colour blend function, so against a transparent buffer the
  alpha is `αs` whatever the mode is. That is why the GPU capture can re-render
  the base through the ordinary draw path without overriding anything, and why
  the CPU reads `buildLayerStage` (which has no blend step at all).
- **Not a raster mask.** Nothing is created, stored or baked; the clipped
  layer's own pixels are untouched. It keeps its own mask, opacity, blend mode
  and styles, and several layers can share one base.

Base *opacity* is folded in, once, because fading the base fades everything
clipped to it — the behaviour this kind of editor established. It lands in
`alphaBase`, never in the clipped layer's own `compositeStage`/`uOpacity`, so it
cannot be applied twice.

**A group base's alpha is its children composed against transparency** —
`buildGroupStage` (CPU), which is the isolated model's stage: children, then the
group mask, then the group effects, before the group's opacity and blend mode.
Only an *isolated* composite holds that, so the isolated path hands its stage
over and the other two models (§4.5) pay one extra pass for the alpha alone. For
a **Pass Through** group that is deliberately not the stage it composites with:
its children see the backdrop when it is composited for real, while a clip asks
for the group's own silhouette — the same choice layer effects make
(`isPassThroughGroup()`). On the GPU all three models reduce to the same thing,
because the capture buffer is transparent: `soleNode = group` renders it through
whichever model it asks for and the result is "its own content × its opacity".

Real files rely on this: a layer sitting directly above a group, clipped to the
group's combined shape, is a normal way to mask artwork with a set of shapes,
and PSDs in the wild are authored that way.

**CPU** ([DocumentCompositor.cpp](../src/renderer/DocumentCompositor.cpp)):
`buildLayerStage` was split out of `drawLayerNode` to serve this — the base's
stage is built once, while the base is being drawn (the walk is bottom→top, so
it reaches the base first), and `clipAlphaFromStage` keeps its alpha as one byte
per document pixel. `applyClipAlpha` then scales **all four** premultiplied
channels of each clipped layer's stage, which is what keeps it premultiplied;
rounding is monotonic, so `colour ≤ alpha` survives it.

**GPU** ([LayerCompositor.cpp](../src/renderer/LayerCompositor.cpp) +
`GPUViewport::beginClipAlphaCapture`): the base is re-rendered into a dedicated
canvas-sized FBO through `renderNodes(..., soleNode = base)` — the *same* draw
code the stack uses, so the alpha the clip reads cannot disagree with the pixels
the base shows. The filter sits above the Adjustment and Group branches, which is
what lets a Group base go through it with its children following. The capture target is pushed onto the group-FBO stack (so a
nested push/pop inside it — a Single-Layer-Mode base isolating itself — restores
the capture target, not the stack entry below it) but is never composited back:
the texture is the result. `setClipAlphaEnabled` then arms the main and blend
shaders, which multiply their source alpha by `texture(uClipTexture,
gl_FragCoord.xy / uClipSize).a`.

Four properties of that shader lookup are load-bearing:

- **It is indexed by `gl_FragCoord`, not by a UV.** The clip lives in document
  space while a layer quad's UVs are in the layer's own space. A live stack is
  always isolated into a canvas-sized FBO (§2.1), so the fragment grid *is* the
  document grid — the same property Dissolve's dither depends on (§4.1). It is
  also what makes the per-tile draws of a `rasterStorage` layer each sample the
  right texels with no extra bookkeeping.
- **No V flip.** Both the clip buffer and the render target are GL render
  targets, so their row orders already agree.
- **One buffer is enough.** A clipping run is contiguous, so between a capture
  and the last draw that uses it only that run's own layers are drawn, and none
  of them captures. `clipAlphaOwner()` identifies whose alpha the buffer holds
  (compared, never dereferenced), so one capture serves the whole run, and
  `resetClipAlpha()` drops it once per frame — a capture is only valid for the
  frame it was taken in, since the base may have been painted on since.
- **The arm is disarmed around anything that is not the merge.** For a clipped
  layer that isolates itself for Single-Layer-Mode adjustments, the clip belongs
  to the `popGroupFbo` composite-back — once, to the finished result — and must
  be off while the base and the adjustment passes render *into* that FBO. Every
  `popGroupFbo` branch pushes the current arm state through
  `applyMainClipUniforms`/`applyBlendClipUniforms`, so no main-shader draw can
  miss a clip or inherit one.

**Two deliberate limits.**

- **A group can be a base but cannot itself be clipped.** Being a base only
  needs the group's own alpha, which `buildGroupStage` produces. Clipping the
  group *itself* would mean scaling the alpha of whatever it merges with, and
  two of its three models (§4.5) have no merge of their own to scale — the
  children go straight into the target. PSD import reports a clipped group as
  the one clipping case it cannot carry.
- **The base's blend mode does not apply to the run.** Elsewhere a clipping
  group composites as a unit through the base's blend mode; here each clipped
  layer composites individually with its own mode, against the real backdrop.

**Where the clip does not apply.** A filtered composite that leaves the base out
of the walk draws the layer unclipped rather than inventing a base: clone/heal
sampling of a single layer (`compositeOnlyFlatIndex`), a merge subset that
excludes it, and `compositeIsolatedNode` (Export Layer As..., which calls
`drawLayerNode` directly and documents itself as excluding every other node).
A clipped layer whose base does not *resolve* — the bottom of a container, or an
adjustment directly below it — is likewise drawn unclipped, which is what keeps a
stale flag harmless after the base was deleted or reordered away. PSD import
reports that case from `buildSiblingLevel`, using this same resolution, so the
warning can never disagree with what is rendered. A **hidden** base is the
one case that is not a fallback: its alpha is zero everywhere, so the whole run
is skipped.

The flag is pure composite state: `ClippingMaskCommand`
([controller/Commands.hpp](../src/controller/Commands.hpp)) flips it and bumps
`compositionGeneration`, which is what makes the clipped layers follow an edit to
the base automatically (§2.5, invariant 4).

---

## 5. Merge semantics (consumes the CPU path)

File: [src/controller/ImageController.cpp](../src/controller/ImageController.cpp)

- **Merge Down** (`applyMergeLayersState`): pair composed by `compositeSubset`
  in isolation, *only siblings from the same container*
  (`mergeDownTargetFlat`: Layer = target, Adjustment = skip, Group = unavailable).
  Ancestor groups are pass-through (props stay live); the ancestor transform is
  canceled in the result (`node->transform = parentAccum.inverted()`). Hidden
  endpoints are blocked (they would destroy pixels).
  **Blend:** if exactly one side has pixels, the result inherits that side's
  blend mode (over a transparent backdrop the blend is not baked — W3C fade —
  and the appearance against lower layers is preserved). If both have pixels,
  the source blend is baked against the destination content and the result
  becomes Normal (a known limitation in areas where dst is transparent).
- **Merge Visible** (`applyMergeVisibleState`): uses the full composite (real
  backdrop for all blends). It consumes only *effectively* visible nodes (node +
  ancestors). The result is hoisted to the root (props from surviving groups are
  not applied twice) and emptied groups are pruned.
- **Flatten** (`applyFlattenImageState`): full composite → single Background
  layer.

---

## 6. Brush engine (single rasterizer)

Files: [src/brush/BrushRenderer.\*](../src/brush/BrushRenderer.hpp),
[src/core/RasterLayerStorage.\*](../src/core/RasterLayerStorage.hpp)

The brush **does not** have two rasterizers — dabs are painted once and both
render paths display the same pixels:

- **Painting layers (rasterStorage):** `drawDabToRasterTiles` /
  `drawCloneDabToRasterTiles` / `drawHealingDabToRasterTiles` paint on the CPU
  directly into tiles (`RasterLayerStorage`), with the *brush* blend
  (`BrushBlendMode`, baked into the pixel), spacing/opacity/flow/hardness via
  `DynamicsEvaluator`. Dirty tiles are uploaded to the GPU
  (`uploadDirtyRasterTiles`) and `LayerCompositor` draws them tile by tile
  (with the *layer* blend via blend shader when ≠ Normal).
- **Mask dabs:** painted via GL into the layer `maskFbo` (GPU is the truth during
  the stroke) and read back (`syncLayerMaskFromGpu`).
- At the end of the stroke, `compositionGeneration` advances →
  `ProjectionCache` rebuilds the CPU projection on a worker thread → canvas
  switches from Path B to Path A without visual difference.

`flushLayerToCpuImage` (ImageController) flattens tiles + cpuImage when a
destructive operation needs a positioned full-size snapshot.

Rasterising a dab is cheap and touches only the active layer. What a dab costs
is the *frame* it triggers, which recomposites the whole stack — see §2.6 before
profiling anything here.

---

## 7. Filter preview

Files: [src/processing/PreviewRenderer.\*](../src/processing/PreviewRenderer.hpp),
[src/processing/FilterProcessor.\*](../src/processing/FilterProcessor.hpp)

A filter preview is computed on the CPU (possibly at reduced resolution for the
viewport), uploaded as `previewTexture`, and replaces the base of the active
layer in the GPU path (`usePreview` in `LayerCompositor`, including inside the
isolated Single-Layer-Mode). The commit runs the filter at full resolution
through the normal CPU path.

---

## 8. File index

| Area | Files |
|---|---|
| Frame entry / GL | `renderer/GPUViewport.cpp` (+ `_Shaders`, `_Overlays`, `_Crop`), `renderer/RenderParams.hpp`, `renderer/OpenGLBackend.hpp` |
| GPU compositor | `renderer/LayerCompositor.cpp`, `renderer/TileRenderer.cpp`, `renderer/MipmapCache.cpp` |
| CPU compositor | `renderer/DocumentCompositor.cpp`, `renderer/RenderContext.hpp` |
| Projection | `renderer/ProjectionCache.*`, `renderer/AsyncProjectionBuilder.*` |
| Async layer-style bake | `renderer/StyleBakePool.*`, `renderer/AsyncStyleBaker.*` (§2.4.1) |
| Shared specification | `renderer/BlendRules.hpp`, `renderer/BlendMath.hpp`, `renderer/BlendShaderLib.hpp`, `renderer/SamplingShaderLib.hpp`, `renderer/ShaderCompat.hpp`; group compositing model (§4.5) and clipping-mask predicates (§4.6) in `core/LayerTreeNode.hpp` |
| Resampling outside the compositors | `engine/ImageEngine.*` (`premultiplyInPlace`, `warpAffinePremultiplied`, `resizePremultiplied` — §4.3) |
| Caches | `cache/RenderCache.*` |
| Layer data | `core/Layer.*`, `core/LayerTreeNode.hpp` (`computeEffectedImage`, `computeStyledBaseImage`, `clone`/`shallowClone`), `core/RasterLayerStorage.*`, `core/TileManager.*` |
| Smart Objects (§3.4) | `smartobject/SmartObjectData.hpp`, `smartobject/SmartObjectRenderService.*`, `smartobject/DocumentContainer.*`, `smartobject/ContainerRegistry.*`, `controller/ImageController_SmartObjects.cpp` — **nothing under `renderer/`** |
| Adjustments | `core/AdjustmentTypes.*`, `core/CurvesData.*`, `core/ColorBalanceData.*`, `core/HueSaturationData.*`, `core/SolidColorData.hpp` |
| Effects | `core/LayerEffect.*` (`applyEffectStack`, shared by layers and groups) |
| Brush | `brush/BrushRenderer.*`, `brush/BrushEngine.*`, `brush/DynamicsEvaluator.*` |
| Merges / operations | `controller/ImageController.cpp` (`applyMerge*State`, `mergeDownTargetFlat`, `runDocumentStateOperationAsync`) |
| Export | `io/ImageIO.cpp` |
| Color management | `color/ColorManagementService.*`, `color/DisplayProfileService.*` (3D LUT in the `display` shader) |