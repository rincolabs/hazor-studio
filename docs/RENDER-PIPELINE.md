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
                        ┌──────────────────────────────────┐
                        │     Shared layer (contract)      │
                        │  BlendRules.hpp   (mode → id)    │
                        │  BlendMath.hpp    (C++ formulas) │
                        │  BlendShaderLib.hpp (GLSL formulas)│
                        └────────────┬─────────────────────┘
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
| **main** | textured quad + mask; alpha-weighted sampling (§4.2); instanced for tiles |
| **blend** | blend modes that fixed-function cannot express: target snapshot in `m_blendTex` (dst) + `bm_blend()` from the shared lib + W3C compositing |
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

Output is always in document space (`doc->size`), `Format_RGBA8888`.

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
         ├─ downscale < 0.8 → resampleLikeGpu (4×4 premultiplied area, §4.2);
         │    otherwise bilinear (SmoothPixmapTransform)
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
| Merge Down | `applyMergeLayersState` ([ImageController.cpp](../src/controller/ImageController.cpp)) | `compositeSubset` of the pair, siblings from the same container, ancestor groups pass-through; blend mode survives when only one side has pixels |
| Merge Visible / Flatten | `applyMergeVisibleState` / `applyFlattenImageState` | full `composite`; result is hoisted to the root |
| Clone/Heal sampling | `compositeOnlyFlatIndex` / `compositeFromFlatIndex` | without stack adjustments (do not "bake" twice) |
| Thumbnails | `computeEffectedThumbnail` (LayerTreeNode) | reduced bake, off-thread |

Destructive operations run through the *async document-state path*
(`runDocumentStateOperationAsync`): document COW snapshot → deep clone in the
worker → operation → state applied back + undo.

---

## 4. Shared specification layer

Three files, one contract:

| File | Role |
|---|---|
| [src/renderer/BlendRules.hpp](../src/renderer/BlendRules.hpp) | maps `BlendMode` → primitive for each backend: shader id (`shaderId`), QPainter mode (`painterMode`), what requires shader/manual compositing |
| [src/renderer/BlendMath.hpp](../src/renderer/BlendMath.hpp) | C++ formulas: non-separable HSL (lum/clipColor/setLum/sat/setSat W3C), `blendHsl`, `maskDensityRamp = mix(1, m, density)` |
| [src/renderer/BlendShaderLib.hpp](../src/renderer/BlendShaderLib.hpp) | the same formulas in GLSL (`bm_*`), injected into the blend shader and adjust shader; `bm_blend(mode, cb, cs)` covers the 15 modes |

**Maintenance rule:** when adding/changing a blend mode, update all three files
together — `BlendMath.hpp` and `BlendShaderLib.hpp` are kept comparable function
by function. ColorDodge/ColorBurn follow the edge semantics of Qt's raster
engine (which executes separable modes on the CPU), without NaN.

### 4.1 Alpha and compositing

- CPU composites in `ARGB32_Premultiplied`; GPU composites straight-alpha in
  fixed-function (with `glBlendFuncSeparate` for correct alpha) and
  premultiplied inside group FBOs — `uSourcePremultiplied`/`uDstPremultiplied`
  control conversion during sampling.
- Blend modes read the backdrop: over a transparent backdrop, both sides apply
  the W3C fade `Cs' = (1-ab)·Cs + ab·B(Cb,Cs)` (shader: `mix(srcColor, Cr, da)`;
  QPainter: native). Consequence for merges: see §5.

### 4.2 Sampling / antialiasing (equivalence)

| Situation | GPU | CPU |
|---|---|---|
| Magnification / footprint ≤ 1.25 texel | premultiplied bilinear (manual in shader) | premultiplied bilinear (QPainter Smooth) |
| Minification (scale < 0.8) | 4×4 alpha-weighted area in shader (`sampleAlphaWeighted`) | `resampleLikeGpu` — the same 4×4 premultiplied average |
| Masks | bilinear R8 texture, ramp `mix(1, m, density)`, outside UV = 1 | same via DestinationIn + `maskDensityRamp` |

The thresholds are intentionally mirrored: 1/1.25 = 0.8.

### 4.3 Known technical limit

**Transformed** layers undergo *two* resamples in the CPU path (layer→canvas at
document resolution, then canvas→screen when displaying the projection) versus
*one* in the GPU path (layer→screen). At zoom ≠ 100%, this causes slightly more
softening in the projection. This is inherent to the document-resolution
projection architecture (export **is** at that resolution) and is accepted as a
limit.

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
| Shared specification | `renderer/BlendRules.hpp`, `renderer/BlendMath.hpp`, `renderer/BlendShaderLib.hpp`, `renderer/ShaderCompat.hpp` |
| Caches | `cache/RenderCache.*` |
| Layer data | `core/Layer.*`, `core/LayerTreeNode.hpp` (`computeEffectedImage`, `computeStyledBaseImage`, `clone`/`shallowClone`), `core/RasterLayerStorage.*`, `core/TileManager.*` |
| Adjustments | `core/AdjustmentTypes.*`, `core/CurvesData.*`, `core/ColorBalanceData.*`, `core/HueSaturationData.*`, `core/SolidColorData.hpp` |
| Effects | `core/LayerEffect.*` (`applyEffectStack`, shared by layers and groups) |
| Brush | `brush/BrushRenderer.*`, `brush/BrushEngine.*`, `brush/DynamicsEvaluator.*` |
| Merges / operations | `controller/ImageController.cpp` (`applyMerge*State`, `mergeDownTargetFlat`, `runDocumentStateOperationAsync`) |
| Export | `io/ImageIO.cpp` |
| Color management | `color/ColorManagementService.*`, `color/DisplayProfileService.*` (3D LUT in the `display` shader) |