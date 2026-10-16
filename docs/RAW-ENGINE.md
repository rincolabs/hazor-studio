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
`RawEngine::initialize(resourceDir, userDir)` call — not yet wired into
startup (that lands with Fase 5, when `registerDefaultImageCodecs()` /
`MainWindow::onOpenFile()` first need to open a RAW file). `resourceDir` must
point at the bundled runtime resources rtengine reads at startup
(`iccprofiles/`, `profiles/`, `dcpprofiles/`, `camconst.json`,
`cammatrices.json`, `dcraw.json` — currently under
`3rdparty/rawengine/rtdata/`); `userDir` is Hazor's own per-user RAW data
directory (e.g. for user-dropped DCP profiles).

## API surface

- `RawEngine::initialize()` / `shutdown()` / `isAvailable()` — process-wide
  lifecycle.
- `RawEngine::openFile(path) -> std::unique_ptr<RawSession>` — one open RAW
  file, RAII. `RawSession::metadata()` for camera/exposure/orientation info.
- `RawDevelopParams` — an opaque, copyable PP3 develop profile:
  `cameraDefaults()`, `fromPP3String()`/`toPP3String()`,
  `loadPP3File()`/`savePP3File()`, plus typed accessor groups (input profile,
  white balance, exposure — more added as callers need them).
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

## Not yet done

Camera Raw UI (dialog, live preview with sliders, before/after), the RAW
Layer in the document model, and wiring `RawEngine::initialize()` into
startup are later phases — see `tmp/feat-raw-therapee-plan.md`.
