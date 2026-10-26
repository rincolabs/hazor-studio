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
    (just the `include/` + `src/` subset it bundles). `cJSON` and `klt` are
    unmodified; `jpeg_ijg` carries one local fix, below.

### Local fix in `external/jpeg_ijg`

`my_jpeg_stdio_src()` (`src/jdatasrc.cc`) and `jpeg_memory_src()`
(`src/jpeg_memsrc.cc`) allocated their source manager straight from libjpeg's
`alloc_small()`, which only guarantees `ALIGN_SIZE` alignment — 8 bytes for
the non-SIMD libjpeg-turbo this links. Both managers embed a `jmp_buf`, and on
MinGW x86-64 that is `__m128`-based, so the structs are 16-byte aligned and GCC
writes their function-pointer fields with aligned SSE stores (`movaps`). On a
pool address that was only 8-byte aligned those stores faulted —
`EXCEPTION_ACCESS_VIOLATION (0xc0000005)`, Windows only, since glibc's
`jmp_buf` leaves the same structs at alignof 8. Both call sites now go through
`rt_jpeg_alloc_aligned()` (`include/jpeg_ijg/jpeg.h`), which over-allocates and
rounds the address up; see that function's comment for the full reasoning.

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

## Status: Fase 1 + Fase 2 of `tmp/feat-raw-therapee-plan.md` (Linux)

Fase 1 (vendoring + build skeleton) and Fase 2 (the `rtcompat/` compatibility
layer) are done on Linux: `rawengine` builds and links cleanly, with OpenMP
enabled, and `grep -r 'glibmm\|gtkmm\|cairomm\|<glib' src/rawengine/rtengine/`
(excluding `rtcompat/` include lines) returns empty.

What `rtcompat/` covers, concretely:

- `rtcompat/glib/` — `Glib::ustring` (over `std::string`, ASCII-fold), the
  PP3 `Glib::KeyFile` codec (locale-C numeric formatting via
  `<charconv>`), `g_fopen`/`g_open`/`g_stat`/`g_remove`/`g_file_read_link`,
  UTF-8↔UTF-16 conversion, `Glib::Dir`/`file_test`/`build_filename`,
  `Glib::Thread`/`Regex`/`ArrayHandle`/`TimeVal`, and the `Gio::File` slice
  ffmanager.cc/dfmanager.cc/metadata.cc need (directory enumeration,
  mtime-based cache invalidation).
- `rtcompat/rtgui/` — a reduced `Options` (the ~20 fields rtengine actually
  reads, including the ones Hazor's own `App::get().options()` refactor in
  `rtengine/rtapp.{h,cc}` added on top of upstream), `M()` as identity,
  `threadutils.h`/`paramsedited.{h,cc}` relocated unchanged,
  `EditSubscriber`/`EditDataProvider` ported with real (not stubbed) logic
  — Hazor just never hands them a non-null provider yet — plus the two
  free constants `rtgui/labgrid.h` exposes outside its GTK widget class.
- `rtengine/alpha.{h,cc}` — reimplemented over `std::vector<unsigned char>`
  (Cairo FORMAT_A8's own 4-byte-aligned stride), since cairomm is dropped
  outright rather than shimmed. `improcfun.h`'s `locImage` field now holds
  one; `iplab2rgb.cc`/`improccoordinator.cc` no longer pull in
  `rtgui/labgrid.h` for the GTK widget they don't use.
- `fmt::println` (3 debug call sites in `iplab2rgb.cc`) replaced with
  `printf` — `fmt` was never part of the vendored dependency set.
- `ashift_lsd.c` is now vendored too: it turned out to be a real,
  unconditional dependency of `ashift_dt.c` (pulled into
  `perspectivecorrection.cc` by `#include`), not GUI-only as this file
  previously claimed.
- Lensfun: the root `CMakeLists.txt` now runs the same
  `lfDatabase::LoadDirectory()` detection upstream's own
  `cmake/Dependencies.cmake` does, and adds glib-2.0's split include dirs
  — `rtlensfun.h` pulls in the *real* `<lensfun.h>`/`<glib.h>` (linked via
  `GLIB2_LIBRARIES`), not `rtcompat/`, since lensfun's own public headers
  need real GLib C types.
- OpenMP: linked via `find_package(OpenMP)` on GCC/Clang. MSVC is
  deliberately left unwired (Fase 2 item 8's "W1" milestone — MSVC's
  `/openmp` is 2.0 and can't express the `reduction(max:/min:)` clauses
  71 sites use; W2's `RT_OMP(...)` macro is not done). MSVC also gets
  `/wd4068` and `-D__SSE2__` as safe, additive, `if(MSVC)`-gated flags.

**Not attempted: MSVC/Windows.** Everything above is verified on Linux only
— there is no Windows toolchain in this environment. Fase 2 items 7
(the handful of real MSVC-only language fixes: `__typeof__` in
`dcraw.cc`, `__attribute__((always_inline))` in two headers) and 8's W2
OpenMP milestone are not done; see the plan file for what's left.

## Status: Fase 3 of `tmp/feat-raw-therapee-plan.md` (Linux)

`src/raw/` (outside this directory, see its own header comments) is the
native API surface built on top of `rawengine` — the first thing to actually
**link** it into `Hazor`, which surfaced two build-system gaps (neither in
the vendored sources themselves, both fixed at the root/`rtcompat` level):

- The root `CMakeLists.txt` never enabled the C language
  (`project(... LANGUAGES CXX)`). `external/cJSON/src/cJSON.c` is the only
  standalone `.c` source in the whole vendored tree — CMake silently drops a
  `.c` source from a target's compile rules instead of erroring when no C
  compiler is registered, so this only surfaced as undefined `cJSON_*`
  references at the first real link of an executable needing them. Fixed by
  `project(... LANGUAGES CXX C)`.
- `rtcompat/rtgui/options.hpp`'s `rtengine::Settings rtSettings` member
  needed `{}` (value-initialization). Upstream leaves several `Settings`
  fields unset in its own `setDefaults()`, relying on `Options` being a
  static-duration global (zero-initialized for free); our `Options` lives on
  the heap (`App::m_options`), which doesn't get that guarantee without the
  `{}`. See `tmp/feat-raw-therapee-plan.md`'s Fase 3 status for the detail.

## DCP camera profiles

`rtdata/dcpprofiles/` (161 `.dcp` + `camera_model_aliases.json`, ~80 MB) is
**not** copied into this directory — like the ONNX models and RealESRGAN
binaries elsewhere in this repo, it lives under `3rdparty/rawengine/rtdata/`
(not tracked in git). See [`licenses/RAWENGINE-DCP-PROFILES.md`](../../licenses/RAWENGINE-DCP-PROFILES.md)
for the attribution/licensing note on those profiles.
