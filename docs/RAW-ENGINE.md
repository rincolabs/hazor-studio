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
(Fase 5), once, before the main window is created, and matched by a
`RawEngine::shutdown()` after the last document is gone.

`resourceDir`/`userDir`
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
  engine's). Nothing retains the float buffer past that conversion: at three
  floats per pixel a 24 MP frame is ~280 MB, not worth holding per layer for
  a higher-depth consumer that does not exist yet.
- `renderPreview()`'s optional `resolvedParams` hands back the processor's
  effective `ProcParams` after the render. That is the only way to read what
  the engine *decided*: `ImProcCoordinator` writes the temperature/green it
  used back into `WBParams` for every method, and `getAutoExp()` /
  `getAutoMatchedToneCurve()` likewise fill in `ToneCurveParams`.
  `RawDevelopParams::adoptResolvedWhiteBalance()` /
  `adoptResolvedToneCurve()` take those values and report whether anything
  moved, so a UI can stop re-syncing once they settle.

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
`ImageController::rerevealRawLayer()`. Its `white_balance_method` — like
every other white-balance entry point — must be one of
`WBParams::getWbEntries()`' own names; the engine does not reject an unknown
string, it quietly falls through to the generic temperature+green path, so a
made-up name looks applied while changing nothing.

`rasterize_layer` already covered RAW layers from Fase 4 onward
(`ImageController::rasterizeNode()`), so it did not need a separate tool.

## Hazor Develop

Hazor Develop keeps its PP3 working copy outside the document until **Apply**.
Its RGB histogram is rebuilt independently through `HistogramCacheManager`;
camera metadata, profile selector, presets, collapsible panels, zoom/pan,
Before/After, and clipping overlays are all local UI state.
The Curve panel reuses the application's complete curves editor in its
controller-free mode: RGB channel selection, presets, point and pencil editing,
smoothing, input/output values, black/white points, Auto, and the asynchronously
computed preview histogram all operate on the PP3 diagonal curves through a
small `RawDevelopParams` adapter. Adjustment-layer-only actions remain hidden.
Editing the RGB curve explicitly disables an active auto-matched histogram
curve, preventing that preset mode from regenerating and replacing the user's
manual points on each preview.
Spline, NURBS, and Catmull-Rom PP3 point curves are decoded into the shared
editor and retain their original curve kind after edits, so bundled presets
such as the Pop profiles do not appear linear or change interpolation mode.
Image-specific Auto-Matched Curve presets are read back from the staged
processor after their first preview and synchronized into the curve editor;
manual editing therefore starts from the generated curve rather than an
identity placeholder. Auto-exposure results come back the same way, so the
Light sliders show the exposure/contrast/black/recovery values the engine
computed instead of the zeroes the profile carried in. Both read-backs stop
re-syncing the panel once the engine's values settle, so they cannot reset a
control mid-edit.
The Temperature and Tint sliders are fed the white balance the engine
*resolved*, not the two PP3 fields: under **As Shot** and the automatic
methods the engine derives its own multipliers and ignores those fields, so
showing `ProcParams`' hand-wired 6504 K would both misreport the shot and
make the first Temperature drag jump the image to generic daylight.
The interactive image is rendered through one persistent `RawSession` on a
dedicated serial worker with debounce. The decoded RAW and rtengine's staged
preview caches therefore survive between slider changes; the preview edge is
bounded by the visible viewport instead of always rendering a fixed oversized
buffer. Light and Color edits also use rtengine's matching refresh masks, so
they do not rerun demosaic and unrelated upstream stages. Apply is the only
operation that calls
`ImageController::rerevealRawLayer()`. That final render is full quality,
asynchronous, and creates the existing `RawDevelopCommand` for undo/redo.

Because the window is independent, the layer it edits can be reordered,
inserted ahead of, or deleted while it is open — so a flat index cannot
identify it. `RawDevelopWindow` therefore carries none: `MainWindow` keys the
window on the layer's `LayerTreeNode`, re-resolves the live index at Apply
time, and `ImageController::rawLayerDevelopFinished` carries the same node so
the completion reaches the right window. `MainWindow` also keeps one window
per node, raising an already-open one instead of starting a second
`RawSession` on the same file.

Bundled RawTherapee `.pp3` profiles and user presets saved in `QSettings` are
listed separately from camera input DCP/ICC profiles. The bundled list is
recursive: most of `rtdata/profiles` lives in subdirectories (`Pop/`,
`Pixel Shift/`, `Non-raw/`), and the path relative to `profiles/` is the
display name. A custom DCP/ICC is selected only through the Camera Profile
control, never as a processing preset.

### Camera Raw terminology versus engine operations

The layout follows Camera Raw, but controls are wired only where RawTherapee
has a real PP3 operation. The UI calls out the non-equivalences instead of
inventing a conversion: **Whites** drives Highlight Compression (recovery),
**Blacks** drives RawTherapee Black Level, **Texture** drives Sharpen
Microcontrast, **Clarity** drives Local Contrast, and color-grading wheels
compose RawTherapee Color Toning's Split Color (`Splitco`) RGB mixer values.
Grain, Vignette **Midpoint**, Color Grading **Balance**, an Adobe-style
parametric-region curve, Camera Raw Masking, and white-balance eyedropper
sampling do not have compatible operations in the currently embedded API, so
they are explicitly unavailable rather than connected to an arbitrary effect.
Balance in particular *looks* available — `ColorToningParams` has the field —
but `mixerToCurve()`, the only consumer of the low/mid/high RGB values, never
reads it; it belongs to the two-colour `RGBSliders`/`Splitlr`/`Lab` modes.

The Color Mixer's eight bands are control points on the HSV Equalizer's hue
axis, placed on the hues they are named after (0/30/60/120/180/240/280/320°),
not on eight equal slices of the circle — `rgbProc()` normalizes hue before
sampling the curve, so a point only affects the hue it sits on. Magenta stops
at 320° rather than reaching 1.0 because `FlatCurve` is periodic and
replicates the first point at `x + 1.0`, where it would collide with Red's
own wrap-around copy. An eight-band UI still cannot represent an arbitrary
curve: a profile authored in RawTherapee's own HSV Equalizer is read at the
closest control point per band, and editing any band re-encodes the curve at
those eight positions.

Every white-balance preset is an exact `WBParams::getWbEntries()` name, which
is why the combo reads "Fluorescent (F2)" and "Flash (5500 K)": the engine
carries spectral data per illuminant (`ColorTemp::spectMap`), with no generic
"Fluorescent" or "Flash" entry, and a name it does not know is not rejected —
it degrades silently to the generic temperature path. The two automatic
methods are likewise named for what they are, `autitcgreen` (temperature
correlation, what **Auto** uses) and `autold` (RGB grey).

Several sliders are one-directional strength controls in RawTherapee, unlike
their bipolar Camera Raw counterparts, so their default sits at the left edge
rather than a centered zero — this mirrors RawTherapee's own tool panels
exactly (e.g. Shadows/Highlights' `highlights`/`shadows`, 0..100) and is not a
layout bug. **Exposure** (-5..12 EV) and **Blacks** (-16384..32768) use
RawTherapee's own asymmetric ranges verbatim for the same reason.
