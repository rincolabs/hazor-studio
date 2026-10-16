# Provenance — `src/rawengine/`

## Upstream

- Project: [RawTherapee](https://rawtherapee.com/) — `rtengine`, the non-GUI
  RAW processing core.
- Version vendored: **5.13-dev** (per `RELEASE_NOTES.txt` in the source used).
- Source: a local working copy of the upstream tree, not a git checkout — no
  upstream commit SHA is available. If a more precise pin is needed later,
  re-vendor from an actual `RawTherapee/RawTherapee` git clone and record the
  commit here.
- Vendored on: 2026-10-16.
- License: GPL-3.0 (see [`LICENSE`](LICENSE)), same license as Hazor itself.

## What was copied

- `rtengine/` → [`rtengine/`](rtengine/), **minus**:
  - `previewimage.{cc,h}` — GUI-only (Cairo/GTK preview surface).
  - `svgrender.{cc,h}` — GUI-only (renders SVG icons for the GTK UI); has no
    consumer inside `rtengine` itself.
  - `ashift_lsd.c` — not referenced by `rtengine`'s own `CMakeLists.txt`
    source list upstream.
  - `CMakeLists.txt` — upstream's own build file; replaced by
    [`../CMakeLists.txt`](CMakeLists.txt) (see below).
- `external/{libraw,cJSON,jpeg_ijg,klt}` → [`external/`](external/), **minus**
  the upstream build glue (`external/CMakeLists.txt`, `external/LibRaw.cmake`,
  `external/LibRaw-make.sh`, `external/libraw.README`), replaced by
  [`external/CMakeLists.txt`](external/CMakeLists.txt).
  - `external/libraw/` is copied **unmodified** (upstream calls it a git
    subtree of the LibRaw project); only the *build* of it is reimplemented —
    see below.
  - `external/{cJSON,jpeg_ijg,klt}` are copied as curated by RawTherapee
    (just the `include/` + `src/` subset it bundles), unmodified.

## Build system: reimplemented, not ported

Upstream builds `rtengine` via its own `CMakeLists.txt` (dropped — see above)
and builds LibRaw via `external/LibRaw.cmake`, which shells out to
`autoreconf`/`./configure`/`make` (autotools). That does not run under MSVC.

Replaced with:

- [`external/CMakeLists.txt`](external/CMakeLists.txt) — plain `add_library()`
  targets for `cjson`, `jpeg_ijg`, `klt`, and `rawengine_libraw`, enumerating
  the exact source lists upstream's own `Makefile.am` / `external/CMakeLists.txt`
  compile (no `configure`, no generated files). The LibRaw source list mirrors
  `external/libraw/Makefile.am`'s `lib_libraw_r_a_SOURCES` — i.e. the
  reentrant/thread-safe `libraw_r` variant upstream RawTherapee actually links
  (no `-DLIBRAW_NOTHREADS`), **not** the three `*_ph.cpp` files, which
  `Makefile.am` does not compile either.
- [`CMakeLists.txt`](CMakeLists.txt) — `add_library(rawengine STATIC ...)`
  over `rtengine/*.cc`, mirroring upstream's `RTENGINESOURCEFILES` list minus
  the two excluded GUI files above.

## Status: Fase 1 of `tmp/feat-raw-therapee-plan.md`

This is **vendoring + build skeleton only**. `rtengine/*.cc` still `#include`
`glibmm`/`gtkmm`/`cairomm`/`rtgui/*.h` headers exactly as upstream wrote them —
the `rtcompat/` compatibility layer that replaces those includes is Fase 2 and
does not exist yet. Do not expect `rawengine` to link cleanly until Fase 2
lands; see the repo's build log / Fase 2 tracking for current status instead
of assuming this file is out of date.

## DCP camera profiles

`rtdata/dcpprofiles/` (161 `.dcp` + `camera_model_aliases.json`, ~80 MB) is
**not** copied into this directory — like the ONNX models and RealESRGAN
binaries elsewhere in this repo, it lives under `3rdparty/rawengine/rtdata/`
(not tracked in git). See [`licenses/RAWENGINE-DCP-PROFILES.md`](../../licenses/RAWENGINE-DCP-PROFILES.md)
for the attribution/licensing note on those profiles.
