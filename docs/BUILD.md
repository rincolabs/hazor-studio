# Building Hazor Studio

This document covers native and local cross-builds of Hazor Studio from a Linux
host. The existing GitHub workflows remain release references, while local
builds share pinned component versions through
[`scripts/build.sh`](../scripts/build.sh).

## Requirements

| Dependency | Version | Notes |
|------------|---------|-------|
| C++ compiler | C++20 | GCC 11+ / Clang 14+ / MSVC 2022 |
| CMake | ≥ 3.16 | `AUTOMOC` / `AUTORCC` enabled |
| Qt 6 | 6.8.x recommended | Components: `Widgets`, `OpenGLWidgets`, `Network`, `Concurrent` |
| OpenCV 4 | — | Components: `core`, `imgproc`, `imgcodecs` |
| zlib | — | Brush import (`.kpp` / `.bundle`) |
| OpenGL | 3.3 Core | Runtime GPU viewport |
| ONNX Runtime | 1.26.x | **Optional** — enables AI selection / background removal |
| zlib, libjpeg-turbo, libpng, libtiff, expat, lcms2, fftw3f (+threads), exiv2, lensfun | — | **Optional** — [`src/rawengine/`](../src/rawengine/) (native RAW engine); all 9 are vendored under `3rdparty/rawengine/`, independent of Hazor's own system `zlib`/OpenCV above, so RawEngine's dependency versions never drift with the host system |

The target-aware scripts additionally require `ninja`, `git`, `curl`,
`sha256sum`, `python3` with `venv`, `unzip`, `zip` and `file`. AppImage
packaging requires `patchelf`; Flatpak packaging requires `flatpak` and
`flatpak-builder`. Windows x64 requires MinGW-w64 GCC 13, while macOS targets
require the user-supplied OSXCross/SDK setup described below.

> The editor **builds and runs without ONNX Runtime**. The AI inference layer
> (SAM object selection, matting / background removal) simply reports itself as
> unavailable at runtime. CPU/CUDA/TensorRT are chosen as runtime *Execution
> Providers* in Settings — they are never separate builds.

> The editor **builds and runs without the RAW engine** (`ENABLE_RAWENGINE=OFF`,
> or with its dependencies missing): `Hazor` just builds without
> [`src/raw/`](../src/raw/)'s `HAVE_RAWENGINE` support, same pattern as ONNX
> Runtime above. See [`docs/RAW-ENGINE.md`](RAW-ENGINE.md) for the native API
> surface and [`src/rawengine/PROVENANCE.md`](../src/rawengine/PROVENANCE.md)
> for what is vendored and the current port status (Linux only — MSVC/Windows
> not yet attempted).

## Local target matrix

Each target has independent build, dependency, distribution and artifact
directories.

| Target | Compiler / ABI | Package | Current boundary |
|--------|----------------|---------|------------------|
| `linux-x86_64` | native GCC/Clang, x86_64 ELF | AppImage + Flatpak | Configured; Flatpak explicitly excludes ONNX/RawEngine until those sandbox modules are added |
| `windows-x86_64` | MinGW-w64 GCC 13, x86_64 PE | ZIP distribution | Qt 6.8.3 MinGW + source-built OpenCV; ONNX compile support is off because the workflow archive contains an MSVC import library |
| `windows-x86` | MinGW-w64, x86 PE | blocked | Qt 6.8 supports Windows x86_64 and publishes no 32-bit Qt 6.8.3 target; the script never substitutes x64 |
| `macos-x86_64` | OSXCross + Apple SDK, Mach-O | unsigned `.app` ZIP | Requires an authorized SDK/OSXCross. Qt/OpenCV/plugins are deployed locally on Linux; signing, notarization and runtime validation require macOS |
| `macos-arm64` | OSXCross + Apple SDK, arm64 Mach-O | unsigned `.app` ZIP | Same Apple boundary; ONNX Runtime 1.26.0 publishes macOS arm64 but not macOS x86_64 |

“Configured” means target selection, a CMake toolchain and dependency paths are
implemented. It does not mean every artifact was generated on this machine.
Missing SDKs, compilers and target libraries stop the build with a target-specific
error; incomplete files are not reported as distributions.

The native Linux target uses the host's OS ABI (glibc, libGL and window-system
libraries) while application dependencies come from its target prefix. Build
AppImages on a host/container with the oldest glibc that the release intends to
support; the existing reference workflow uses Ubuntu 22.04. Flatpak instead
inherits the declared KDE 6.8 runtime ABI.

The Windows workflow uses the MSVC x64 ABI (`win64_msvc2022_64`, VC14/VC15
OpenCV, and a vcpkg MSVC zlib). Those C++ binaries are not linked into the local
MinGW cross-build. The local x64 target uses official `win64_mingw` Qt and builds
OpenCV/zlib from the same pinned source versions. RealESRGAN and models are
runtime data and reuse the workflow archives. The ONNX Runtime DLL is staged for
provenance, but linking stays disabled until its GNU import-library/C ABI path is
validated.

### Central build interface

The no-argument command remains the existing Linux developer build:

```bash
./scripts/build.sh
```

New isolated builds use an explicit target:

```bash
# Prepare once, then build incrementally.
./scripts/build.sh --target linux-x86_64 --prepare
./scripts/build.sh --target linux-x86_64

# Select configuration and limit CPU/RAM pressure.
./scripts/build.sh --target linux-x86_64 --config Debug --jobs 4

# Package already prepared targets.
./scripts/build.sh --target linux-x86_64 --package appimage
./scripts/build.sh --target linux-x86_64 --package flatpak
./scripts/build.sh --target windows-x86_64 --package zip
./scripts/build.sh --target macos-x86_64 --package bundle
./scripts/build.sh --target macos-arm64 --package bundle

# Clean only one build/package. Prepared dependencies survive.
./scripts/build.sh --target linux-x86_64 --clean

# Inspect an existing output without rebuilding or launching it.
./scripts/build.sh --target linux-x86_64 --verify-only

# Attempt every target and report blocked/failed targets separately.
./scripts/build.sh --all
```

Run `./scripts/build.sh --help` for all options. The script never launches
Hazor. Ninja provides incremental compilation; `ccache` is used when present,
and `JOBS` or `--jobs` limits parallel work.

### macOS prerequisites

The project does not download or redistribute an Apple SDK. Obtain it under the
applicable Apple license, prepare OSXCross, and expose its target directory:

```bash
export OSXCROSS_TARGET_DIR=/opt/osxcross/target
./scripts/build.sh --target macos-arm64 --prepare
```

OSXCross is pinned in [`versions.sh`](../scripts/build/versions.sh), but is not
cloned automatically because SDK acquisition and license acceptance are user
actions. Linux produces the Mach-O executable and deploys the target Qt
frameworks, plugins, OpenCV libraries and packaged runtime assets into an
architecture-specific `.app`, then writes an unsigned ZIP. OSXCross's
`otool`/`install_name_tool` wrappers are used to replace build-prefix references.
Signing, notarization and runtime validation remain explicit macOS-only steps.
Both targets use macOS 12.0 as their deployment floor, matching Qt 6.8.

Intel and Apple Silicon remain separate targets and separate artifacts; there
is no target or merge step combining the two architectures.

### Flatpak prerequisites and feature boundary

Install `flatpak`, `flatpak-builder`, `org.kde.Platform//6.8` and
`org.kde.Sdk//6.8`. The manifest is
[`packaging/flatpak/com.rincolabs.HazorStudio.yml`](../packaging/flatpak/com.rincolabs.HazorStudio.yml).
It builds OpenCV 4.5.4 from its immutable commit inside the sandbox. Qt and zlib
come from that KDE SDK/runtime, not the Linux host. The current Flatpak sets
`ENABLE_ONNXRUNTIME=OFF` and `ENABLE_RAWENGINE=OFF`; this is an explicit package
capability boundary.

## Dependency inventory and isolation

[`scripts/build/versions.sh`](../scripts/build/versions.sh) is the local
version/source manifest. Release archives have SHA-256 checks and git sources
use immutable commits. Downloads live under `3rdparty/downloads/`, dependency
build trees under `3rdparty/build-deps/<target>/`, and installed prefixes under
`3rdparty/targets/<target>/`. Rebuilding Hazor does not rebuild them.

| Component | Version / source | ABI and transitive boundary | Distributed? |
|-----------|------------------|-----------------------------|--------------|
| Qt | 6.8.3 via aqt 3.3.0 | Separate Linux host tools and per-target libraries/plugins; Windows target requires MinGW 13.1 | Runtime libraries and required plugins |
| OpenCV | 4.5.4, commit `4223495e...` | Built per target with core/imgproc/imgcodecs; JPEG/PNG/TIFF/WebP come from the pinned OpenCV tree and zlib from the target prefix | Shared libraries/DLLs |
| zlib | 1.3.2, commit `da607da7...` | Static per-target app dependency | Linked into Hazor |
| ONNX Runtime | 1.26.0 | Linux GPU archive; macOS arm64 archive; Windows runtime-only; absent for x86/macOS x86_64 | Runtime/providers only where enabled |
| RealESRGAN ncnn Vulkan | 20220424 / release 0.2.5.0 | Platform process executable and runtime siblings | Executable, DLLs and models |
| Mobile SAM | 20230629 at immutable HF revision | Platform-neutral ONNX encoder/decoder | Model files + metadata |
| BiRefNet Lite | 20240101 at revision `f82954f...` | Platform-neutral FP16 ONNX model | Model + metadata |
| RawEngine libraries | versions below | Prepared for Linux only; strict builds disable lensfun by default so GLib cannot leak from the host | Static; `rtdata` is packaged |
| OS ABI | target SDK/sysroot | Linux libc/OpenGL/window ABI, Windows system DLLs, or Apple frameworks | Supplied by target OS/runtime |

Strict target builds pass exact Qt, OpenCV, zlib and target-root paths to CMake.
Configuration stops if these resolve outside `3rdparty/targets/<target>`.
Cross-toolchain programs execute on Linux, while target libraries/includes are
resolved through the target prefix/sysroot. Packaged builds also omit the
development-only absolute source fallback. `CMakePresets.json` mirrors the
Release configurations for manual inspection; `scripts/build.sh` remains the
supported interface.

## Dependency downloads

The exact versions and download sources used by CI are listed below. The local
manifest reuses them where compiler ABIs match and uses the same source version
otherwise. The workflows themselves are unchanged.

### Qt 6.8.3

Installed via [`aqtinstall`](https://github.com/miurahr/aqtinstall) (the
`jurplel/install-qt-action` GitHub Action wraps it). Alternatively use the
[official Qt Online Installer](https://www.qt.io/download-qt-installer).

| OS | Host / Arch |
|----|-------------|
| Linux | `linux` / `linux_gcc_64` → `Qt/6.8.3/gcc_64` |
| Windows workflow | `windows` / `win64_msvc2022_64` |
| Windows local cross-target | `windows` / `win64_mingw` + Linux Qt host tools |
| macOS cross-target | `mac` / `clang_64` + Linux Qt host tools; user-supplied OSXCross SDK |

Qt 6.8's supported Windows desktop architecture is x86_64. There is no official
Qt 6.8.3 Windows x86 target package.

### ONNX Runtime 1.26.0 (GPU) — *optional*

Released by Microsoft on the [ONNX Runtime releases page](https://github.com/microsoft/onnxruntime/releases/tag/v1.26.0).

| OS | Download |
|----|----------|
| Linux | [`onnxruntime-linux-x64-gpu-1.26.0.tgz`](https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-linux-x64-gpu-1.26.0.tgz) |
| Windows | [`onnxruntime-win-x64-gpu-1.26.0.zip`](https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-win-x64-gpu-1.26.0.zip) |
| macOS arm64 | [`onnxruntime-osx-arm64-1.26.0.tgz`](https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-osx-arm64-1.26.0.tgz) |

### OpenCV 4.5.4

| OS | Source |
|----|--------|
| Linux | System package `libopencv-dev` (apt), or [OpenCV 4.5.4 release](https://github.com/opencv/opencv/releases/tag/4.5.4) |
| Windows | [`opencv-4.5.4-vc14_vc15.exe`](https://github.com/opencv/opencv/releases/download/4.5.4/opencv-4.5.4-vc14_vc15.exe) (self-extracting) |

Strict local builds compile commit `4223495e6cd67011f86b8ecd9be1fa105018f3b1`
for each target. The precompiled Windows archive stays CI/MSVC-only.

### zlib

| OS | Source |
|----|--------|
| Linux | System package `zlib1g-dev` (apt) |
| Windows | vcpkg: `vcpkg install zlib:x64-windows-static-md` |

Strict local builds compile zlib 1.3.2 commit
`da607da739fa6047df13e66a2af6b8bec7c2a498` statically per target.

### RealESRGAN ncnn Vulkan (20220424)

From the [Real-ESRGAN v0.2.5.0 release](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0).

| OS | Download |
|----|----------|
| Linux | [`realesrgan-ncnn-vulkan-20220424-ubuntu.zip`](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-ubuntu.zip) |
| Windows | [`realesrgan-ncnn-vulkan-20220424-windows.zip`](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip) |
| macOS | [`realesrgan-ncnn-vulkan-20220424-macos.zip`](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-macos.zip) |

### RawEngine dependencies — *optional*

Native RAW development engine ([`src/rawengine/`](../src/rawengine/)), still
under construction — see
[`src/rawengine/PROVENANCE.md`](../src/rawengine/PROVENANCE.md). All 9
dependencies are vendored under `3rdparty/rawengine/<lib>/` with a pinned
version each, same philosophy as Qt/ONNX Runtime/RealESRGAN above but
deliberately **not** sharing versions with Hazor's own system `zlib`/OpenCV —
RawEngine's only real constraint here is an exact, reproducible version per
dependency, not where the files happen to live. A prebuilt release where
upstream publishes one, built from source into that same layout otherwise:

| Library | Version | How it got into `3rdparty/rawengine/<lib>/` |
|---------|---------|----------------------------------------------|
| exiv2 | 0.28.9 | Prebuilt: [`exiv2-0.28.9-Linux-x86_64.tar.gz`](https://github.com/Exiv2/exiv2/releases/download/v0.28.9/exiv2-0.28.9-Linux-x86_64.tar.gz) extracted as-is |
| zlib | 1.3.2 | Built from [source](https://github.com/madler/zlib/releases/download/v1.3.2/zlib-1.3.2.tar.gz) (CMake): `-DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_EXAMPLES=OFF` |
| libjpeg-turbo | 3.2.0 | Built from [source](https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/3.2.0.tar.gz) (CMake): `-DENABLE_SHARED=OFF -DWITH_SIMD=OFF -DWITH_TURBOJPEG=OFF -DWITH_JPEG8=ON` (`WITH_SIMD=OFF` because this build has no `nasm`) |
| libpng | 1.6.59 | Built from [source](https://github.com/pnggroup/libpng/archive/refs/tags/v1.6.59.tar.gz) (CMake) against the vendored zlib above: `-DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF` |
| libtiff | 4.7.2 | Built from [source](https://download.osgeo.org/libtiff/tiff-4.7.2.tar.gz) (CMake) against the vendored zlib + libjpeg-turbo above, with jbig/lzma/zstd/webp/libdeflate off: `-DBUILD_SHARED_LIBS=OFF -Dtiff-tools=OFF -Dtiff-tests=OFF` |
| expat | 2.9.0 | Built from [source](https://github.com/libexpat/libexpat/releases/download/R_2_9_0/expat-2.9.0.tar.gz) (CMake): `-DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_TESTS=OFF` |
| lcms2 | 2.16 | Built from [source](https://github.com/mm2/Little-CMS/releases/download/lcms2.16/lcms2-2.16.tar.gz): `./configure --prefix=.../lcms2 --disable-shared --enable-static --with-pic && make install` |
| fftw3f | 3.3.10 | Built from [source](https://www.fftw.org/fftw-3.3.10.tar.gz): `./configure --prefix=.../fftw3f --enable-float --enable-threads --disable-shared --enable-static --with-pic && make install` |
| lensfun | 0.3.4 | Built from [source](https://github.com/lensfun/lensfun/archive/refs/tags/v0.3.4.tar.gz) (CMake): `-DBUILD_STATIC=ON -DBUILD_TESTS=OFF -DBUILD_DOC=OFF -DINSTALL_PYTHON_MODULE=OFF`; also needs `glib-2.0` (system) |

`RAWENGINE_WITH_LENSFUN=OFF` drops the lensfun requirement (Lens Corrections
becomes unavailable, nothing else is affected) if the above proves awkward on
a given platform — see the plan's risk notes.

### Bundled ONNX models (optional, cross-platform)

Downloaded once and placed under the app's model directory; not required to
compile.

| Model | Version | Source |
|-------|---------|--------|
| Mobile SAM (object selection) | `20230629` | [vietanhdev/segment-anything-onnx-models](https://huggingface.co/vietanhdev/segment-anything-onnx-models) → `mobile_sam_20230629.zip` |
| BiRefNet Lite (matting) | `20240101` | [onnx-community/BiRefNet_lite-ONNX](https://huggingface.co/onnx-community/BiRefNet_lite-ONNX) → `onnx/model_fp16.onnx` |
| Material Design Icons | `3.0.1` | [google/material-design-icons](https://github.com/google/material-design-icons/releases/tag/3.0.1) |

## Vendored third-party layout

Downloaded contents are not checked into git; only skeleton folders are tracked.
Libraries with different ABIs never share an install prefix. Platform-neutral
models live once under `common/`; Linux tools such as `moc`, `rcc`, `uic`, aqt
and AppImage utilities live under `host/`.

```
3rdparty/
├── common/onnx/models/                            # platform-neutral models
├── downloads/                                     # archives + pinned sources
├── host/linux-x86_64/
│   ├── aqt/3.3.0/
│   ├── appimage/
│   └── qt/6.8.3/gcc_64/                           # host moc/rcc/uic
├── targets/
│   ├── linux-x86_64/
│   ├── windows-x86/
│   ├── windows-x86_64/
│   ├── macos-x86_64/
│   └── macos-arm64/
│       ├── qt/6.8.3/<abi>/
│       ├── opencv/4.5.4/
│       ├── zlib/1.3.2/
│       ├── onnxruntime/1.26.0/<package>/
│       ├── realesrgan/20220424/
│       └── rawengine/                             # supported targets only
├── sysroots/                                      # externally supplied SDKs
├── toolchains/                                    # externally supplied compilers
└── build-deps/<target>/                           # reusable dependency builds
```

The older Linux folders (`3rdparty/Qt`, `onnx`, `realesrgan`, `rawengine`) are
preserved. Link valid installations into the new layout without copying them:

```bash
./scripts/build.sh --target linux-x86_64 --migrate-only
```

Main overrides are:

| Variable | Default |
|----------|---------|
| `BUILD_DIR` | `<root>/build/<target>-<config>` |
| `THIRD_PARTY_DIR` | `<root>/3rdparty` |
| `HAZOR_DOWNLOAD_DIR` | `$THIRD_PARTY_DIR/downloads` |
| `HAZOR_HOST_ROOT` | `$THIRD_PARTY_DIR/host/linux-x86_64` |
| `HAZOR_COMMON_ROOT` | `$THIRD_PARTY_DIR/common` |
| `QT_HOST_DIR` / `QT_TARGET_DIR` | selected from target |
| `OPENCV_PREFIX`, `ZLIB_PREFIX`, `ONNXRUNTIME_DIR` | selected from target |
| `RAWENGINE_DEPS_DIR`, `REALESRGAN_DIR` | selected from target |
| `JOBS` | host CPU count |

## Quick compatibility build (Linux)

The convenience script configures and builds the `Hazor` target in Release:

```bash
./scripts/build.sh
```

Run the resulting binary:

```bash
./scripts/run.sh          # runs build/Hazor
```

## Manual build with CMake

For manual development experiments, dependencies can still be supplied
explicitly. This is not a strict/distributable build; use the target-aware script
for packages.

```bash
cmake -S . -B build \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/path/to/Qt/6.8.3/gcc_64" \
  -DONNXRUNTIME_ROOT="/path/to/onnxruntime" \
  -DONNXRUNTIME_INCLUDE_DIR="/path/to/onnxruntime/include" \
  -DONNXRUNTIME_LIBRARY="/path/to/onnxruntime/lib/libonnxruntime.so"

cmake --build build -j"$(nproc)" --target Hazor
```

### Useful CMake options

| Option | Default | Effect |
|--------|---------|--------|
| `ENABLE_ONNXRUNTIME` | `ON` | Build with ONNX Runtime AI inference support |
| `ENABLE_RAWENGINE` | `ON` | Build the native RAW engine (`rawengine` target) and link it into `Hazor` via [`src/raw/`](../src/raw/) (see [`docs/RAW-ENGINE.md`](RAW-ENGINE.md)) |
| `RAWENGINE_WITH_LENSFUN` | `ON` | Build RawEngine with lensfun lens-correction support |
| `USE_RHI` | `OFF` | Use the QRhi rendering backend (Vulkan/Metal/D3D12) instead of OpenGL |
| `HAZOR_STRICT_DEPENDENCIES` | `OFF` | Reject app dependencies outside `HAZOR_DEPENDENCY_ROOT` |
| `HAZOR_DEVELOPMENT_PATHS` | `ON` | Embed source-tree fallback paths for local development only |
| `CMAKE_BUILD_TYPE` | — | `Release` for optimized builds, `Debug` for tests |

To build **without** AI inference support:

```bash
cmake -S . -B build -DENABLE_ONNXRUNTIME=OFF
cmake --build build -j"$(nproc)" --target Hazor
```

## Tests

> ⚠️ **Warning:** the test suite is currently **outdated** and may not build or
> pass against the current codebase. Treat it as a work in progress rather than a
> reliable gate.

Tests are built in a Debug configuration and run with CTest:

```bash
./scripts/tests.sh        # configures Debug + builds
./scripts/run-tests.sh    # ctest --output-on-failure
```

## Continuous integration

CI build definitions live in [`.github/workflows/`](../.github/workflows/):

- `linux-appimage-build.yml` — Linux AppImage packaging
- `windows-build.yml` — Windows build

They remain references for versions and release conventions. Local build
metadata is centralized in `scripts/build/versions.sh`; these workflows were not
modified by the cross-build implementation.
