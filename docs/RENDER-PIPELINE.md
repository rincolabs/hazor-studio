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
     ├─ Type::Group       → isolate into group FBO if blend/opacity/child adjustment
     │                      (pushGroupFbo → children in canvas-NDC → popGroupFbo)
     └─ Type::Layer:
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

### 2.5 Frame caches

- [src/cache/RenderCache.cpp](../src/cache/RenderCache.cpp): replays the last
  complete frame (post color management) when nothing changed and no interactive
  tool is active.
- [src/renderer/ProjectionCache.\*](../src/renderer/ProjectionCache.hpp): GL
  texture for the CPU projection, keyed by `doc + compositionGeneration`.

---

## 3. CPU path (source of truth)

### 3.1 `DocumentCompositor` — API

File: [src/renderer/DocumentCompositor.cpp](../src/renderer/DocumentCompositor.cpp)

| Function | Usage |
|---|---|
| `composite(doc, ctx)` | full flatten of visible layers (projection, export, flatten) |
| `compositeOnlyFlatIndex` | a single layer (clone/heal sampling, without stack adjustments) |
| `compositeFromFlatIndex` | from the index down (clone "current & below") |
| `compositeSubset(doc, layers, applyAdjustments, ctx, ancestorGroupsPassThrough)` | subset in the real stack order — basis for Merge Down. With `ancestorGroupsPassThrough=true`, ancestor groups are **not** isolated (the group opacity/blend/effects stay live in the tree, not baked into the pixels) |
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
     ├─ Group → fast path (direct merge) when !needsIsolatedComposite or passThrough;
     │    otherwise: isolate into stage → group mask (future) → group effects
     │    (applyEffectStack, same stack used by layers) → compositeStage(blend, opacity)
     └─ Layer → drawLayerNode:
         ├─ source: computeEffectedImage() (bake of effects + clipped adjustments,
         │    mask included) or renderImage() (shape base); dabs = rasterStorage.toImage()
         ├─ transform: buildImgToCanvas (img→NDC→canvas, same chain as io/ImageIO
         │    and the GPU MVP; shape sprite uses spriteTransform)
         ├─ anything but a whole-pixel copy → resampleAffine: ONE pass evaluated at
         │    the inverse-mapped position, premultiplied, per axis an exact box
         │    (downscale) or a Keys cubic (upscale/rotate/sub-pixel offset) (§4.2);
         │    whole-pixel copy and projective transforms → QPainter
         ├─ mask: DestinationIn with density ramp mix(1, m, density)
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

## 4. Shared specification layer

Four files, one contract — three for blending, one for resampling:

| File | Role |
|---|---|
| [src/renderer/BlendRules.hpp](../src/renderer/BlendRules.hpp) | maps `BlendMode` → primitive for each backend: shader id (`shaderId`), QPainter mode (`painterMode`), what requires shader/manual compositing |
| [src/renderer/BlendMath.hpp](../src/renderer/BlendMath.hpp) | C++ formulas: non-separable HSL (lum/clipColor/setLum/sat/setSat W3C), `blendHsl`, `maskDensityRamp = mix(1, m, density)` |
| [src/renderer/BlendShaderLib.hpp](../src/renderer/BlendShaderLib.hpp) | the same formulas in GLSL (`bm_*`), injected into the blend shader and adjust shader; `bm_blend(mode, cb, cs)` covers the 15 modes |
| [src/renderer/SamplingShaderLib.hpp](../src/renderer/SamplingShaderLib.hpp) | the resampling kernels in GLSL (`sm_*`), injected into **both** the main shader and the blend shader; C++ mirror is `boxWeights`/`cubicWeights`/`resampleAffine` in `DocumentCompositor.cpp` (§4.2) |

**Maintenance rule:** when adding/changing a blend mode, update all three blend
files together — `BlendMath.hpp` and `BlendShaderLib.hpp` are kept comparable
function by function. ColorDodge/ColorBurn follow the edge semantics of Qt's
raster engine (which executes separable modes on the CPU), without NaN. The
same rule holds for the sampler: `SamplingShaderLib.hpp` and `resampleAffine`
move together.

### 4.1 Alpha and compositing

- CPU composites in `ARGB32_Premultiplied`; GPU composites straight-alpha in
  fixed-function (with `glBlendFuncSeparate` for correct alpha) and
  premultiplied inside group FBOs — `uSourcePremultiplied`/`uDstPremultiplied`
  control conversion during sampling.
- Blend modes read the backdrop: over a transparent backdrop, both sides apply
  the W3C fade `Cs' = (1-ab)·Cs + ab·B(Cb,Cs)` (shader: `mix(srcColor, Cr, da)`;
  QPainter: native). Consequence for merges: see §5.

### 4.2 Sampling / antialiasing (equivalence)

Both backends run the **same two kernels over premultiplied samples**, picked
**per axis** by the screen-pixel footprint (source texels per output pixel along
that axis), never by a user setting:

| footprint | Kernel | Behaviour |
|---|---|---|
| `> 1` | exact box of `footprint` texels | minification: the area average (what `cv::INTER_AREA` computes). Degenerates *continuously* into plain bilinear as the footprint approaches 1, so there is no seam to put a threshold at |
| `≤ 1` | Keys cubic, `A = -0.75` | magnification, rotation, or a merely sub-pixel offset. At an integral sample position the weights are exactly `(0,1,0,0)`, so a grid-aligned 1:1 frame is still a **bit-exact copy** |

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

Three consequences worth knowing:

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
  becomes Normal (Photoshop-style limitation in areas where dst is transparent).
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
| Shared specification | `renderer/BlendRules.hpp`, `renderer/BlendMath.hpp`, `renderer/BlendShaderLib.hpp`, `renderer/SamplingShaderLib.hpp`, `renderer/ShaderCompat.hpp` |
| Resampling outside the compositors | `engine/ImageEngine.*` (`premultiplyInPlace`, `warpAffinePremultiplied`, `resizePremultiplied` — §4.3) |
| Caches | `cache/RenderCache.*` |
| Layer data | `core/Layer.*`, `core/LayerTreeNode.hpp` (`computeEffectedImage`, `computeStyledBaseImage`, `clone`/`shallowClone`), `core/RasterLayerStorage.*`, `core/TileManager.*` |
| Adjustments | `core/AdjustmentTypes.*`, `core/CurvesData.*`, `core/ColorBalanceData.*`, `core/HueSaturationData.*`, `core/SolidColorData.hpp` |
| Effects | `core/LayerEffect.*` (`applyEffectStack`, shared by layers and groups) |
| Brush | `brush/BrushRenderer.*`, `brush/BrushEngine.*`, `brush/DynamicsEvaluator.*` |
| Merges / operations | `controller/ImageController.cpp` (`applyMerge*State`, `mergeDownTargetFlat`, `runDocumentStateOperationAsync`) |
| Export | `io/ImageIO.cpp` |
| Color management | `color/ColorManagementService.*`, `color/DisplayProfileService.*` (3D LUT in the `display` shader) |