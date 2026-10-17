# RAW engine

Hazor's RAW development support is derived from
[RawTherapee](https://rawtherapee.com/)'s `rtengine`, vendored and ported to
build natively with the rest of the app (no GTK, no external process, no C
ABI boundary). See `tmp/feat-raw-therapee-plan.md` for the full plan and
[`src/rawengine/PROVENANCE.md`](../src/rawengine/PROVENANCE.md) for exactly
what was vendored, what was changed, and the current port status.

## Layering

```
src/raw/           Hazor-owned native API — the only thing the rest of the
                    app may depend on for RAW. See each header's doc comment.
src/rawengine/
├── rtengine/       Vendored engine sources (upstream, minimal changes)
└── rtcompat/       The portability shim that replaces glibmm/gtkmm/cairomm/
                    rtgui so rtengine builds with plain MSVC/GCC/Clang
```

`rtengine` never includes anything from Hazor. `src/raw/` is the only place
in the app that includes `rtengine/` or `rtcompat/`. Everything else only
ever sees `src/raw/`'s own types (`RawEngine`, `RawSession`,
`RawDevelopParams`, `RawMetadata`, `RawGeometry`, `RawImageBuffer`) — no
`Glib::ustring`, `ProcParams`, or other rtengine type crosses that boundary.

## Availability

`ENABLE_RAWENGINE` (CMake option, default `ON`) and the 9 vendored
dependencies under `3rdparty/rawengine/` (see `docs/BUILD.md`) gate whether
`HAVE_RAWENGINE` is compiled in at all. Every `src/raw/*.cpp` compiles either
way — with it off, every operation is an inert no-op/failure. Even then,
`RawEngine::supportedExtensions()` keeps working (it's a plain string list),
so file routing can still recognize a RAW file and report it as unsupported
in this build, rather than mis-handling it as a regular image.

`RawEngine::isAvailable()` additionally requires a successful
`RawEngine::initialize(resourceDir, userDir)` call — wired into `main()`
(Fase 5), once, before the main window is created. `resourceDir`/`userDir`
are `AppPaths::rawEngineResourceDir()` (`3rdparty/rawengine/rtdata/` —
`iccprofiles/`, `profiles/`, `dcpprofiles/`, `camconst.json`,
`cammatrices.json`, `dcraw.json`) and `AppPaths::rawUserDir()` (Hazor's own
per-user RAW data directory, e.g. for user-dropped DCP profiles).

## API surface

- `RawEngine::initialize()` / `shutdown()` / `isAvailable()` — process-wide
  lifecycle.
- `RawEngine::openFile(path) -> std::unique_ptr<RawSession>` — one open RAW
  file, RAII. `RawSession::metadata()` for camera/exposure/orientation info.
- `RawDevelopParams` — an opaque, copyable PP3 develop profile:
  `cameraDefaults()`, `fromPP3String()`/`toPP3String()`,
  `loadPP3File()`/`savePP3File()`, plus typed groups for camera input profile,
  white balance, light, color, effects, curves, HSV color mixer, color grading,
  detail, and optics. These preserve rtengine's own units and PP3 fields.
- `RawSession::renderPreview(maxEdge, params)` — interactive-quality RGB8
  render, scaled to fit `maxEdge`.
- `RawSession::renderFull(params)` — full-resolution float render
  (`[0, 65535]`, output ICC profile applied, not truncated to 8 bits — that
  truncation is the RAW Layer's job at a single named point, not the
  engine's).

See the doc comments in [`src/raw/`](../src/raw/) for exact contracts,
including the two deliberate limitations every render call shares: no
preemptive mid-render cancellation (rtengine has no hook for it — a
`RawCancelToken` is only checked before starting), and `RawImageBuffer::linear`
always reporting `false` (detecting a linear output ICC profile isn't
implemented).

## Input color profile chain

`RawDevelopParams::InputProfileMode` mirrors `ColorManagementParams::inputProfile`'s
four sentinel values (`CameraMatched` / `Embedded` / `None` / `CameraMatrix`),
plus `setCustomInputProfile(path)` for an explicit user DCP/ICC. The actual
resolution — DCP first, then ICC, then the camera's own color matrix as a
last resort — is `RawImageSource::findInputProfile()`'s own cascade, never
reimplemented. `RawEngine::matchedCameraProfile(metadata)` runs that same
DCP→ICC lookup read-only, for the UI to show what a RAW file resolves to.

## Opening RAW files

`RawImageCodec` ([`src/io/ImageCodec.cpp`](../src/io/ImageCodec.cpp)) is
registered first in `registerDefaultImageCodecs()`, ahead of
`QtImageCodec`/`OpenCvImageCodec`, so a RAW extension is always claimed here
instead of risking a generic codec mis-decoding it (e.g. a DNG's embedded
TIFF structure). It feeds `RawEngine::supportedExtensions()` into
`ImageCodecRegistry::openFileFilter()` (the Open dialog's filter) and exists
as a flattened fallback (`RawDevelopParams::cameraDefaults()`) for any path
that reaches the generic codec registry (drag & drop, "Insert into Canvas").

The primary path is `MainWindow::onOpenFile()`, which diverts a RAW
extension to `openRawFileAsNewTab()` *before* reaching the codec registry:
it opens the file and renders camera defaults off the UI thread, then
creates a new tab whose single layer carries a `RawLayerData` (Linked mode —
see "RAW Layer" below) instead of a flattened import. The layer shows a
dedicated thumbnail icon in the Layers panel (same mechanism as a Text
layer's). Double-clicking that thumbnail opens the independent **Hazor
Develop** window (`LayerItemDelegate::rawThumbDoubleClicked` →
`LayerPanel::rawEditRequested` → `RawDevelopWindow`).

`raw_develop` (tool catalog / MCP / agent surface) adjusts a RAW Layer's
white balance and exposure and re-reveals it via
`ImageController::rerevealRawLayer()`; `rasterize_layer` already covered RAW
layers from Fase 4 onward (`ImageController::rasterizeNode()`), so it did not
need a separate tool.

## Hazor Develop

Hazor Develop keeps its PP3 working copy outside the document until **Apply**.
Its RGB histogram is rebuilt independently through `HistogramCacheManager`;
camera metadata, profile selector, presets, collapsible panels, zoom/pan,
Before/After, and clipping overlays are all local UI state.
The interactive image is rendered through one persistent `RawSession` on a
dedicated serial worker with debounce. The decoded RAW and rtengine's staged
preview caches therefore survive between slider changes; the preview edge is
bounded by the visible viewport instead of always rendering a fixed oversized
buffer. Light and Color edits also use rtengine's matching refresh masks, so
they do not rerun demosaic and unrelated upstream stages. Apply is the only
operation that calls
`ImageController::rerevealRawLayer()`. That final render is full quality,
asynchronous, and creates the existing `RawDevelopCommand` for undo/redo.

Bundled RawTherapee `.pp3` profiles and user presets saved in `QSettings` are
listed separately from camera input DCP/ICC profiles. A custom DCP/ICC is
selected only through the Camera Profile control, never as a processing preset.

### Camera Raw terminology versus engine operations

The layout follows Camera Raw, but controls are wired only where RawTherapee
has a real PP3 operation. The UI calls out the non-equivalences instead of
inventing a conversion: **Whites** drives Highlight Compression (recovery),
**Blacks** drives RawTherapee Black Level, **Texture** drives Sharpen
Microcontrast, **Clarity** drives Local Contrast, and color-grading wheels
compose RawTherapee Color Toning's RGB slider values. Grain, an Adobe-style
parametric-region curve, Camera Raw Masking, and white-balance eyedropper
sampling do not have compatible operations in the currently embedded API, so
they are explicitly unavailable rather than connected to an arbitrary effect.

Several sliders are one-directional strength controls in RawTherapee, unlike
their bipolar Camera Raw counterparts, so their default sits at the left edge
rather than a centered zero — this mirrors RawTherapee's own tool panels
exactly (e.g. Shadows/Highlights' `highlights`/`shadows`, 0..100) and is not a
layout bug. **Exposure** (-5..12 EV) and **Blacks** (-16384..32768) use
RawTherapee's own asymmetric ranges verbatim for the same reason.
