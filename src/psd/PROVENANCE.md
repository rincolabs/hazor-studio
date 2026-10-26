# Provenance — `src/psd/`

## Upstream

- Project: [PhotoCraft](https://github.com/storytold/photocraft) —
  `crates/psd` (`photocraft-psd`), a clean-room reader/writer for Adobe
  Photoshop PSD (v1) and PSB (v2) files, plus a handful of bundled Adobe
  ancillary formats. Only the core PSD/PSB reader/writer was ported here —
  see "What was ported" below.
- Source: a local working copy of the upstream tree (`tmp/photocraft-main/`
  in this repo), not a git checkout — no upstream commit SHA available.
- Vendored on: 2026-10-18.
- License: MIT OR Apache-2.0 (see [`LICENSE-MIT`](LICENSE-MIT) and
  [`LICENSE-APACHE`](LICENSE-APACHE)), same dual license as upstream.
  Upstream carries no per-file copyright headers — license is governed by
  the crate/repo-level files, not per-file stamps — so this port follows the
  same convention.
- Upstream is itself a clean-room implementation of Adobe's public
  "Photoshop File Formats Specification"; where that spec is silent, a few
  behaviors follow MIT-licensed `psd-tools` / `ag-psd` documentation, noted
  in the comments that describe them (carried over here unchanged).

## What was ported

The core PSD/PSB module set, rewritten in C++20: `error`, `io`, `header`,
`blend`, `compression` (+ `packbits`), `image_data`, `descriptor`, `tagged`,
`resources`, `metadata`, `layer`, `pixels`, `tree`, `file`, `builder`, and
`filter_effects` (needed by `tagged`'s smart-filter cache block).

`Linked.hpp`/`Linked.cpp` is **not** a port: upstream validates the structure
of the placed-layer (`SoLd`/`SoLE`/`PlLd`) and linked-file
(`lnk2`/`lnk3`/`lnkD`) blocks but never exposes their contents, which the
Smart Object import needs. It is a read-only view over bytes `Tagged` already
owns, written against Adobe's specification plus the field order psd-tools and
ag-psd document — see the file comment. This is
enough to parse and reconstruct a full PSD/PSB document: header, image
resources, the layer/mask section (records, masks, blending ranges, channel
data, nested groups), tagged blocks, the merged image, and per-layer/merged
RGBA8 extraction — plus a builder to construct new files. Each module's
colocated unit tests were ported alongside it (see `tests/`).

The bundled ancillary Adobe formats (brushes `.abr`, swatches `.aco`/`.ase`,
actions `.atn`, gradients `.grd`, contours `.shc`, patterns, vector paths,
HDR toning data) and the layered-TIFF embedding module were **not** ported —
they are unrelated to opening a PSD/PSB document's own layers and pixels.
The crate-level integration tests (round-trip sweeps, property tests,
malformed/truncated-input sweeps, corpus replay, layer-block-order and
merged-image-recovery tests) were likewise not ported, since they mostly
exercise the modules above. Module-level unit tests for every ported module
were ported in full.

## What was reimplemented, not mechanically translated

Rust idioms with no direct C++ equivalent were re-expressed, keeping the
same algorithms and observable behavior:

- `Result<T, PsdError>` → a small hand-rolled `Expected<T, E>` in
  `Error.hpp` (the project targets C++20, no `std::expected`).
- `thiserror`-derived `PsdError` → a plain C++ class with an enum `Kind` and
  the same payload fields, producing the same message text.
- `flate2` (zlib wrapper) → the `zlib.h` C API directly (`z_stream`,
  `inflate`/`deflate`), the same pattern already used elsewhere in Hazor
  (`src/brush/import/BrushImportArchive.cpp`). `ZLIB::ZLIB` is a root-level
  CMake dependency.
- `std::thread::scope`-based parallel RLE row packing → `<thread>` +
  `std::thread::hardware_concurrency()`, same chunk-and-join structure.
- UTF-16 helpers (surrogate-aware truncation, Pascal/legacy name codecs) →
  hand-written in `Io.hpp/.cpp`, with no Qt/ICU dependency (this library
  stays independent of the rest of Hazor).
- `proptest` property-based tests → deterministic seeded-PRNG
  generation/mutation loops with fixed iteration counts, asserting the same
  invariants (round-trip equality; "always `Err`, never crashes" on
  malformed/mutated input). No new fuzzing dependency was introduced.

## Explicitly not ported

- The ancillary Adobe formats (brushes, swatches, actions, gradients,
  contours, patterns, paths, HDR toning data) and the layered-TIFF embedding
  module: out of scope for opening a PSD/PSB document itself; may be
  revisited later if Hazor needs them.
- The crate-level integration tests (round-trip, property, malformed-input,
  corpus, layer-block-order, merged-image-recovery): exercise the modules
  above but were not ported in this phase.
- The `serde` feature (optional JSON-style (de)serialization derives):
  unused until Hazor's document system integrates this library, which is
  out of scope for this phase.
- `fuzz/` (cargo-fuzz harnesses): Rust/Cargo-specific infrastructure.
- `examples/*.rs` (a CLI dump tool, a benchmark, a PSB generator): developer
  tools, not part of the library or its test suite.

## Status

Standalone library + test suite, built via `add_subdirectory(src/psd)` from
the root `CMakeLists.txt`. `Hazor` links `psd` (so `./scripts/build.sh`
exercises the whole library on every build).

Integrated with Hazor's document system via `src/io/PsdImportService*`
(File → Open for `.psd`/`.psb`): the import service is the only Hazor code
that includes this library, keeping the parser itself independent of
Hazor's document model as the modules above do. See
`PsdImportService.cpp`'s file comment for the conversion policy (what has a
Hazor equivalent is converted; the rest is reported, not approximated).
