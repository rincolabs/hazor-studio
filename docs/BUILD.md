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
| Qt 6 | 6.8.x recommended | Components: `Widgets`, `OpenGLWidgets`, `Network`, `Concurrent`, `WebEngineWidgets`, `WebChannel` |
| OpenCV 4 | — | Components: `core`, `imgproc`, `imgcodecs` |
| zlib | — | Brush import (`.kpp` / `.bundle`) |
| OpenGL | 3.3 Core | Runtime GPU viewport |
| ONNX Runtime | 1.26.x | **Optional** — enables AI selection / background removal |
| Node.js + npm | Node ≥ 20.19 | **Optional, developers only** — builds the embedded web interfaces in [`embedded-ui/`](../embedded-ui/); the output is compiled into the binary, so the end user needs neither |
| zlib, libjpeg-turbo, libpng, libtiff, expat, lcms2, fftw3f (+threads), exiv2, lensfun | — | **Optional** — [`src/rawengine/`](../src/rawengine/) (native RAW engine); all 9 are vendored under `3rdparty/rawengine/`, independent of Hazor's own system `zlib`/OpenCV above, so RawEngine's dependency versions never drift with the host system |

The target-aware scripts additionally require `ninja`, `git`, `curl`,
`sha256sum`, `python3` with `venv`, `unzip`, `zip` and `file`. AppImage
packaging requires `patchelf`; Flatpak packaging requires `flatpak` and
`flatpak-builder`. Windows x64 requires MinGW-w64 GCC 13, while macOS targets
require the user-supplied OSXCross/SDK setup described below.

The current AI Agent WebEngine proof of concept directly requires the official
`qtwebengine` and `qtwebchannel` modules from the same Qt distribution. For the
Linux developer build, install those modules into the prepared Qt 6.8.3 prefix;
CMake intentionally fails if either package is missing. The test does not add a
feature flag or a second panel implementation. Packaging/deployment of
`QtWebEngineProcess` and Chromium resources is not part of this UI experiment.

> The editor **builds and runs without ONNX Runtime**. The AI inference layer
> (SAM object selection, matting / background removal) simply reports itself as
> unavailable at runtime. CPU/CUDA/TensorRT are chosen as runtime *Execution
> Providers* in Settings — they are never separate builds. The GPU providers
> ship in every package but additionally need the *machine's* CUDA 12 + cuDNN 9
> libraries, which Hazor probes through the platform loader before selecting one:
> without them it stays on the CPU and says so in one log line, instead of
> letting ONNX Runtime fail the provider load with a native error dump.

> The editor **builds and runs without the RAW engine** (`ENABLE_RAWENGINE=OFF`,
> or with its dependencies missing): `Hazor` just builds without
> [`src/raw/`](../src/raw/)'s `HAVE_RAWENGINE` support, same pattern as ONNX
> Runtime above. See [`docs/RAW-ENGINE.md`](RAW-ENGINE.md) for the native API
> surface and [`src/rawengine/PROVENANCE.md`](../src/rawengine/PROVENANCE.md)
> for what is vendored and the current port status. Every target this
> repository builds enables it; `ENABLE_RAWENGINE=OFF` is an escape hatch, not
> a packaging default.

## Local target matrix

Each target has independent build, dependency, distribution and artifact
directories. Packages land in `artifacts/<target>/` named the way the published
release assets are — `HazorStudio-<platform>-v<version>.<ext>`, as in
`HazorStudio-linux-x86_64-v0.0.2-beta.AppImage`. Linux ships the executable
packages themselves (`.AppImage`, `.flatpak`); only Windows is distributed as a
ZIP, and macOS as a ZIP of the `.app`.

`<version>` comes from two adjacent lines in
[`CMakeLists.txt`](../CMakeLists.txt) — the same ones that generate
`src/core/Version.hpp`, so the file name, the splash screen, the About
diagnostics and the MCP server info never disagree:

```cmake
project(ImageEditor VERSION 0.0.2 LANGUAGES CXX C)
set(HAZOR_VERSION_SUFFIX "beta")  # "beta", "rc1", … or empty for a final release
```

`project(... VERSION ...)` takes digits only, which is why the pre-release label
is a separate line. Clearing it to `""` is what promotes a build from
`0.0.2-beta` to `0.0.2`, in the package name and in the application alike.

`HAZOR_APP_VERSION` overrides the file name alone, for a one-off build that must
not touch `CMakeLists.txt` — the binary still reports what it was compiled with,
so a real release gets its label from the line above instead:

```bash
HAZOR_APP_VERSION=0.0.2-rc1 ./scripts/build.sh \
    --target linux-x86_64 --config Release --package appimage
```

| Target | Compiler / ABI | Package | Current boundary |
|--------|----------------|---------|------------------|
| `linux-x86_64` | native GCC/Clang, x86_64 ELF | AppImage + Flatpak | Configured; Flatpak explicitly excludes ONNX/RawEngine until those sandbox modules are added |
| `windows-x86_64` | MinGW-w64 GCC 13.1, x86_64 PE | ZIP distribution | Qt 6.8.3 MinGW + source-built OpenCV; the cross toolchain is provisioned from source (see below) because no distribution package matches both Qt's ABI and this host |
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

The no-argument command is the developer build: host target
(`linux-x86_64`), `Debug`, no packaging. It is what you want to check that a
change compiles, and it is what agents are told to use (see
[`AGENTS.md`](../AGENTS.md) rule 3).

```bash
./scripts/build.sh
```

`Release` is opt-in. Nothing infers it from the target or the package format,
so a cross build or a packaging run stays Debug unless asked — a distributable
needs `--config Release` spelled out:

```bash
# Prepare once, then build incrementally.
./scripts/build.sh --target linux-x86_64 --prepare
./scripts/build.sh --target linux-x86_64

# Limit CPU/RAM pressure.
./scripts/build.sh --target linux-x86_64 --jobs 4

# Package already prepared targets. Distributables are Release builds.
./scripts/build.sh --target linux-x86_64 --config Release --package appimage
./scripts/build.sh --target linux-x86_64 --config Release --package flatpak
./scripts/build.sh --target windows-x86_64 --config Release --package zip
./scripts/build.sh --target macos-x86_64 --config Release --package bundle
./scripts/build.sh --target macos-arm64 --config Release --package bundle

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
is no target or merge step combining the two architectures, and none is planned
— a universal binary is explicitly out of scope.

Qt publishes one universal (x86_64 + arm64) macOS package, so both targets link
a single shared install under `3rdparty/common/qt/` instead of downloading the
same archives per architecture. Only that download is shared: compilation,
deployment, bundle and ZIP stay per architecture.

### Windows cross-toolchain

Qt built its official `win64_mingw` libraries with the toolchain it publishes at
[download.qt.io](https://download.qt.io/development_releases/prebuilt/mingw_64/):
`MinGW-w64-x86_64-13.1.0-release-posix-seh-msvcrt-rt_v11-rev1` — GCC 13.1.0,
posix threads, SEH exceptions, MSVCRT runtime. Linking Qt's libraries with a
different GCC major version does not work: GCC 10's libstdc++ cannot satisfy
what GCC 13 emitted.

No distribution package provides that combination here. Ubuntu 22.04 packages
MinGW GCC 10, and the Ubuntu 24.04 packages require glibc 2.38 while this host
has 2.35, so they do not even start. `scripts/build/prepare.sh` therefore builds
the cross toolchain from pinned sources into
`3rdparty/toolchains/mingw-w64-x86_64-gcc-13.1.0-msvcrt/` and puts it in front
of `PATH` for the Windows target, so an older host MinGW stops interfering
without being uninstalled.

| Component | Version | Source |
|-----------|---------|--------|
| binutils | 2.41 | [ftp.gnu.org](https://ftp.gnu.org/gnu/binutils/) |
| GCC | 13.1.0 | [ftp.gnu.org](https://ftp.gnu.org/gnu/gcc/gcc-13.1.0/), GPG-verified against the GCC release signature |
| mingw-w64 | 11.0.1 (`rt_v11`, matching Qt's toolchain) | [SourceForge](https://downloads.sourceforge.net/project/mingw-w64/) |

Building GCC needs a few host packages that are not part of a normal desktop
install:

```bash
sudo apt install flex bison texinfo libgmp-dev libmpfr-dev libmpc-dev
./scripts/build.sh --target windows-x86_64 --config Release --prepare --package zip
```

The six bootstrap stages (binutils, mingw-w64 headers, GCC stage 1, CRT,
winpthreads, GCC stage 2) each skip themselves when already installed, so an
interrupted provisioning run resumes instead of restarting. Expect roughly an
hour the first time; afterwards the toolchain is reused like any other
dependency.

### Flatpak prerequisites

Install `flatpak`, `flatpak-builder`, `org.kde.Platform//6.8` and
`org.kde.Sdk//6.8`. The manifest is
[`packaging/flatpak/com.rincolabs.HazorStudio.yml`](../packaging/flatpak/com.rincolabs.HazorStudio.yml).

**The Flatpak ships AI inference and the native RAW engine**, Lens Corrections
included: `ENABLE_ONNXRUNTIME`, `ENABLE_RAWENGINE` and
`RAWENGINE_WITH_LENSFUN` are all `ON`. The one feature it leaves out is the
experimental local Stable Diffusion module, which ships in the AppImage only —
see [What each package ships](#what-each-package-ships).

What the sandbox compiles itself, because the host copies under
`3rdparty/targets/` and `3rdparty/rawengine/` were built against this machine's
ABI and the KDE runtime is a different one:

- **OpenCV 4.5.4**, from its immutable commit.
- **The nine RawEngine libraries**, each at the version
  [the table below](#rawengine-dependencies--optional) pins, installed into
  `/app/rawengine/<lib>` — the layout `RAWENGINE_DEPS_DIR` expects, so no CMake
  change is needed to consume them.

Both set `builddir: true`: flatpak-builder configures in the source directory
by default and OpenCV refuses in-source builds. OpenCV also needs
`CMAKE_INSTALL_LIBDIR=lib`, and so does every RawEngine module — GNUInstallDirs
resolves to `lib64` inside this SDK, Flatpak puts only `/app/lib` on the loader
path, and `_rawengine_add_imported_lib()` only ever looks in `<prefix>/lib`.
`PSD_BUILD_TESTS=OFF` is passed as well: `src/psd` has its own test option,
defaulting to ON, whose tests need Boost — which the KDE runtime does not ship
(and which strict host builds were silently taking from the host).

What the sandbox takes as prebuilt bytes, staged out of the prepared host tree
by `stage_flatpak_assets()` in
[`scripts/build/package.sh`](../scripts/build/package.sh):

- **ONNX Runtime 1.26.0**, the same manylinux release the AppImage ships, into
  `/app/lib`. CPU, CUDA and TensorRT provider libraries all ship: they are
  runtime execution providers chosen in Settings, never separate builds.
- **Bundled models, the Real-ESRGAN ncnn process and its weights, and
  RawEngine's `rtdata`**, into `/app/3rdparty` — where
  `AppPaths::thirdPartyDir()` finds them, since it probes `<appdir>/../3rdparty`
  and the binary lands in `/app/bin`. Staging reuses the AppImage's own
  `copy_runtime_assets()` so the two packages cannot drift.

This means **`--package flatpak` requires the Linux dependencies to have been
prepared first** (`--target linux-x86_64 --prepare`), exactly as the AppImage
does.

Qt, zlib and `glib-2.0` come from the KDE SDK/runtime, not the Linux host. glib
is deliberately the runtime's: lensfun links it, Qt 6 links it too, and a second
private copy in one process would be the defect rather than an isolation win.

The source copied into the sandbox excludes `3rdparty/`, `build/`, `dist/`,
`artifacts/`, `tmp/` and `.git` — the sandbox build needs none of them — and
`flatpak-builder` keeps its git mirrors and downloads in
`build/flatpak-state/`, so repeated runs do not re-clone the sandbox sources.

`package_flatpak()` probes whether `rofiles-fuse` can mount and passes
`--disable-rofiles-fuse` when it cannot. Containers, CI runners and sandboxed
shells routinely withhold `/dev/fuse`, and without the probe the failure
surfaces several modules later as `Build directory ... not initialized`, which
says nothing about FUSE.

## Dependency inventory and isolation

[`scripts/build/versions.sh`](../scripts/build/versions.sh) is the local
version/source manifest. Release archives have SHA-256 checks and git sources
use immutable commits. Downloads live under `3rdparty/downloads/`, dependency
build trees under `3rdparty/build-deps/<target>/`, and installed prefixes under
`3rdparty/targets/<target>/`. Rebuilding Hazor does not rebuild them.

| Component | Version / source | ABI and transitive boundary | Distributed? |
|-----------|------------------|-----------------------------|--------------|
| Qt | 6.8.3 via aqt 3.3.0 | Separate Linux host tools and per-target libraries/plugins; Windows target requires MinGW 13.1; the universal macOS package is installed once and linked by both macOS targets | Runtime libraries and required plugins |
| OpenCV | 4.5.4, commit `4223495e...` | Built per target with core/imgproc/imgcodecs; JPEG/PNG/TIFF/WebP come from the pinned OpenCV tree and zlib from the target prefix | Shared libraries/DLLs |
| zlib | 1.3.2, commit `da607da7...` | Static per-target app dependency, built position-independent so OpenCV's shared libraries can link it; RawEngine's vendored zlib of the same version is a non-PIC build and is not reused here | Linked into Hazor |
| ONNX Runtime | 1.26.0 | Linux GPU archive; Windows GPU archive linked through a GNU import library generated from the DLL's export table; macOS arm64 archive | Runtime and all execution providers |
| RealESRGAN ncnn Vulkan | 20220424 / release 0.2.5.0 | Per-platform process executable and runtime siblings; the `.param`/`.bin` weights are plain data and shared by every target | Executable, DLLs and models |
| Mobile SAM | 20230629 at immutable HF revision | Platform-neutral ONNX encoder/decoder | Model files + metadata |
| BiRefNet Lite | 20240101 at revision `f82954f...` | Platform-neutral FP16 ONNX model | Model + metadata |
| RawEngine libraries | versions below | Linux adopts the pre-existing vendored tree; cross targets build all nine from pinned sources into `3rdparty/targets/<target>/rawengine/`. Windows also builds a private glib-2.0 for lensfun, since it has no platform one | Static, except exiv2 (shared); `rtdata` is packaged |
| OS ABI | target SDK/sysroot | Linux libc/OpenGL/window ABI, Windows system DLLs, or Apple frameworks | Supplied by target OS/runtime |

Strict target builds pass exact Qt, OpenCV, zlib and target-root paths to CMake.
Configuration stops if these resolve outside `3rdparty/targets/<target>`.
Cross-toolchain programs execute on Linux, while target libraries/includes are
resolved through the target prefix/sysroot. Packaged builds also omit the
development-only absolute source fallback. `CMakePresets.json` mirrors the
Release configurations for manual inspection; `scripts/build.sh` remains the
supported interface.

## Artifact verification

Every target build ends in `scripts/build/verify.sh` unless `--no-verify` is
given. It checks the binary's real format for the target (ELF/PE/Mach-O and
architecture) and that `Qt6_DIR`, `OpenCV_DIR` and `ZLIB_INCLUDE_DIR` in the
CMake cache resolve under `3rdparty/targets/<target>/`.

Packaged trees are then scanned for references to the local checkout. The two
kinds are not equivalent, so they are not treated alike:

| Kind | Verdict |
|------|---------|
| `RPATH`/`RUNPATH` entry, symlink, script or config naming the local tree | fatal, with the offending file named |
| The path left inside a compiled binary's diagnostic strings | reported, not fatal |

Only the first kind can send another machine back to this checkout. The second
is build-path residue, and the build removes what it can at the source:
`-ffile-prefix-map` keeps `__FILE__` out of Hazor and its dependencies,
`CMAKE_SKIP_INSTALL_RPATH` keeps the local prefix out of the OpenCV libraries,
and OpenCV's `cv::getBuildInformation()` dump — which embeds the whole compiler
command line — is replaced with a one-line provenance string. What remains is
OpenCV's own `OPENCV_INSTALL_PREFIX`/`OPENCV_BUILD_DIR`, which it generates with
no option to disable and only uses for `cv::samples::findFile`; Hazor never
calls it.

Two sources on the Windows target need more than `-ffile-prefix-map`, since the
flag only rewrites `__FILE__`:

- **The MinGW runtime libraries.** libstdc++, the mingw-w64 CRT and
  libwinpthreads bake `__FILE__` into their `assert()` strings, and every PE
  links them, so `prepare_mingw_toolchain()` passes the map through
  `CFLAGS_FOR_TARGET`/`CXXFLAGS_FOR_TARGET`; the plain variants reach only the
  build-host tools. Those strings live in `.rdata`, not in a debug section, so
  stripping does not remove them. The same assignment has to repeat `-g -O2`,
  which autoconf substitutes only when `CFLAGS` is unset.
- **Install prefixes compiled in as string literals.** glib derives
  `GLIB_LOCALE_DIR` from its install prefix and lensfun derives `CONF_DATADIR`
  from its own; no compiler flag touches either. glib is therefore built
  against a fictitious prefix and installed through `DESTDIR` (its `.pc` files
  are rewritten back to the real tree afterwards), and lensfun is built with
  `-DPLATFORM_WINDOWS=ON` so that it takes the relative `share/lensfun` of its
  Windows branch instead of an absolute `CMAKE_INSTALL_FULL_DATAROOTDIR`.

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

Qt 6.8's supported Windows desktop architecture is x86_64 and there is no
official Qt 6.8.3 Windows x86 package, so 32-bit Windows is out of scope: no
target, directory or code path exists for it. ONNX Runtime and Real-ESRGAN do
not publish x86 builds either.

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
| lensfun | 0.3.4 | Built from [source](https://github.com/lensfun/lensfun/archive/refs/tags/v0.3.4.tar.gz) (CMake): `-DBUILD_STATIC=ON -DBUILD_TESTS=OFF -DBUILD_DOC=OFF -DINSTALL_PYTHON_MODULE=OFF`, plus `-DPLATFORM_WINDOWS=ON` on the Windows target; also needs `glib-2.0` (system) |

#### Where each target's copies come from

`linux-x86_64` adopts the pre-existing `3rdparty/rawengine/` tree wholesale
(`migrate_legacy_linux()` links it into the target prefix), so the table above
is a record of how it was produced rather than a recipe that runs.

Every other target builds all nine from those same pinned sources —
`prepare_rawengine()` in [`prepare.sh`](../scripts/build/prepare.sh) — into
`3rdparty/targets/<target>/rawengine/<lib>/{include,lib}`. The Flatpak builds
them a third time, inside its own sandbox, for the same ABI reason.

#### lensfun and glib-2.0

RAW development is a shipped feature and Lens Corrections is part of it, so
`RAWENGINE_WITH_LENSFUN` is `ON` everywhere.

lensfun links `glib-2.0` unconditionally, and the three kinds of build resolve
it differently:

| Build | glib-2.0 comes from |
|-------|---------------------|
| Native Linux (`./scripts/build.sh`, AppImage) | the host/platform — Qt 6 links it too, so a second private copy in one process would be the defect rather than an isolation win. The same OS/runtime-ABI exemption that covers libc, libGL and libX11 |
| Flatpak | the KDE runtime, for exactly that reason |
| `windows-x86_64` | a private build, because Windows has no platform glib. `prepare_rawengine_glib()` cross-builds glib 2.82.5 with meson, plus libffi and pcre2, into `3rdparty/targets/windows-x86_64/rawengine/glib/`. NLS is disabled so libintl does not become a fourth dependency; lensfun never asks glib to translate |

`scripts/build.sh` points `PKG_CONFIG_LIBDIR` at that vendored prefix when it
exists and falls back to the platform otherwise. It deliberately does **not**
set `PKG_CONFIG_SYSROOT_DIR`: those `.pc` files come from our own cross build
and already carry absolute paths inside the target prefix, so a sysroot would
be prepended a second time and every `-I`/`-L` would point at a path that does
not exist.

Lens Corrections also needs lensfun's camera/lens database, which is data
rather than a library. `package.sh` stages it as `share/lensfun/version_1`
beside the executable on the Windows target, and
[`AppPaths::lensfunDatabaseDir()`](../src/core/AppPaths.cpp) resolves it from
there and hands it to `RawEngine::initialize()`, which sets rtengine's
`lensfunDbBundleDirectory`. rtengine falls back to that only when lensfun's own
search comes up empty, so a native build keeps using the platform database.
Lensfun's module-relative lookup cannot be used here: it resolves against a
`liblensfun.dll`, and we link lensfun statically.

`RAWENGINE_WITH_LENSFUN=OFF` is meant to drop the requirement (Lens Corrections
becomes unavailable, nothing else is affected), but **it does not build today**
and is not used by any target:
[`rtlensfun.h`](../src/rawengine/rtengine/rtlensfun.h) includes `<lensfun.h>`
with no guard and is pulled in by `init.cc`, `iptransform.cc` and
`rawimagesource.cc`, while `rtengine/rtlensfun.cc` stays in the source list, so
`init.cc` fails with `fatal error: lensfun.h: No such file or directory`.
Fixing that is a prerequisite for ever offering a lensfun-free build;
`HAZOR_ENABLE_RAWENGINE=OFF` remains the only working way to leave the engine
out, and it is an escape hatch, not a packaging default.

### stable-diffusion.cpp — *optional, Linux development only*

Local image generation: a Stable Diffusion module under
[`src/ai/sd/`](../src/ai/sd/) — model registry, inference runtime, generation
service, job controller and a JSON workflow executor — driven by the
**Stable Diffusion** panel
([`src/ui/StableDiffusionPanel.cpp`](../src/ui/StableDiffusionPanel.cpp)),
which runs a fixed `txt2img` workflow through
[leejet/stable-diffusion.cpp](https://github.com/leejet/stable-diffusion.cpp)
and inserts the result as an ordinary pixel layer.

The same workflow executor also backs **Neural Flow**, the node editor opened
from that panel's *Neural Flow* button. Its interface is a Vue application
compiled into the binary — see
[Embedded web interfaces](#embedded-web-interfaces-embedded-ui-optional-developer-toolchain)
below.

`ENABLE_STABLE_DIFFUSION` is on by default and the module ships in **one**
package: the AppImage. The feature is experimental, so this release keeps it to
the package that can carry every GPU backend rather than to two with different
capabilities — the Flatpak manifest does not enable it (see
[What each package ships](#what-each-package-ships)). The module is also
restricted to `--target linux-x86_64`: that is the only target `--prepare`
builds the dependency for, so the Windows and macOS builds are unaffected and
log that they skipped it. Set `HAZOR_ENABLE_STABLE_DIFFUSION=OFF` to leave it
out of a build explicitly.

The dependency is not vendored in git and is not built as part of Hazor's own
configure. `./scripts/build.sh --prepare` clones the pinned
stable-diffusion.cpp revision — and only the `ggml` submodule it actually
needs, not the WebP/WebM/frontend ones a manual `git clone --recursive` would
also pull in — builds it once as a Release, installs it as an ordinary CMake
package, and caches it under
`3rdparty/targets/linux-x86_64/stable-diffusion.cpp/<version>/`:

```bash
./scripts/build.sh --prepare
```

| Pinned | Revision |
|--------|----------|
| `stable-diffusion.cpp` | `687915518efd69548965d24818f0d3bbbb903718` |
| `ggml` (submodule) | `a5915eb0e47adef21c3f25b889a008925ea838c2` |

Then build the host developer target:

```bash
./scripts/build.sh
```

`ENABLE_STABLE_DIFFUSION` (the CMake option) is turned on by
[`build.sh`](../scripts/build.sh) for `--target linux-x86_64` when the package
above was actually prepared — otherwise it logs why and builds without the
module rather than failing the configure. The Flatpak could not consume this
prefix even if it wanted to — its `skip:` list excludes `3rdparty/`, and the
host copy was compiled against this machine's ABI — so a Flatpak that shipped
the module would have to build its own inside the sandbox; this release does
not (see [What each package ships](#what-each-package-ships)).

`prepare_stable_diffusion()` (in
[`scripts/build/prepare.sh`](../scripts/build/prepare.sh)) builds
stable-diffusion.cpp itself as a static library — examples, CLI, server and
WebP/WebM I/O disabled — against **shared** ggml libraries, always as a
Release build: optimized, with no debug symbols, regardless of `--config
Debug` on the Hazor side. That last point is what used to make a Debug
Hazor's executable enormous: the in-tree `add_subdirectory()` this replaced
compiled ggml/stable-diffusion with Hazor's own `-g` (plus a forced `-O2`,
since `CMAKE_CXX_FLAGS_DEBUG` alone carries no optimisation), baking its full
debug info — and, with the CUDA backend, its multi-architecture fat binary —
directly into the Hazor binary.

#### Backends: one package, every backend

This dependency is built for *other people's machines*, not for the one doing
the build. All three backends are compiled, all of them ship, and which one
runs is decided on the user's machine:

| Backend | Needs at build time | Needs on the user's machine |
|---------|---------------------|------------------------------|
| CPU | nothing beyond Hazor's own toolchain | nothing — 14 x86 variants ship and ggml picks the best one the CPU supports |
| Vulkan | `glslc` and `libvulkan.so`; headers are vendored | a Vulkan driver (NVIDIA, AMD, Intel) |
| CUDA | a CUDA toolkit (`nvcc`) | the NVIDIA driver *and* the CUDA runtime (`libcudart`, `libcublas`, `libcublasLt`) |

Three CMake settings make that safe, and none of them should be changed
without reading this:

- **`GGML_BACKEND_DL=ON`** — each backend is a separate `.so` that ggml
  `dlopen()`s at run time from the directory holding the executable. A module
  whose libraries are missing is skipped instead of killing the process, which
  is the only reason CPU, Vulkan and CUDA can sit side by side in one package.
  It also forces shared ggml libraries (`BUILD_SHARED_LIBS=ON`,
  `SD_BUILD_SHARED_GGML_LIB=ON`), which is required anyway: ggml's own install
  rules are `install(TARGETS ggml LIBRARY)`, and that installs *nothing* for a
  static ggml, so `find_package(stable-diffusion)` could never be satisfied.
- **`GGML_CPU_ALL_VARIANTS=ON`** — 14 CPU backends, from the plain x86-64
  baseline through `haswell` (AVX2) to `sapphirerapids` (AMX). ggml scores them
  against the running CPU and loads the best match.
- **`GGML_NATIVE=OFF`** — never `-march=native`. With it on the CPU backends
  are tuned for the build machine's instruction set, and on CMake ≥ 3.24
  ggml-cuda narrows to the build machine's compute capability alone: that is
  exactly the binary that dies with `SIGILL` on a slightly older CPU, or
  refuses to run on a different GPU. **Do not turn it on**, not even to shorten
  a build.

`HAZOR_SD_BACKENDS` narrows the set by hand for a faster developer prepare.
It is read at **prepare** time, not at Hazor's configure time; `CPU` is always
included because ggml needs it for the operators no GPU backend implements.

```bash
./scripts/build.sh --prepare                               # ALL (default)
HAZOR_SD_BACKENDS=CPU         ./scripts/build.sh --prepare
HAZOR_SD_BACKENDS=CPU,VULKAN  ./scripts/build.sh --prepare
```

Each GPU backend is preflighted and **dropped with a logged reason** when its
toolchain is absent — no `nvcc`, no CUDA backend — rather than failing
`--prepare`. If the combined build fails outright, it is retried with the CPU
backends only, and if that fails too the error is logged and the rest of
`--prepare` carries on; `build.sh` then builds Hazor without the module. What
was actually built is recorded in `.hazor-sd-backends` next to the installed
package and, authoritatively, in ggml's own installed
`GGML_AVAILABLE_BACKENDS`, which is what `CMakeLists.txt` reads back into
`HAZOR_SD_BACKENDS` for [`SdBackend.cpp`](../src/ai/sd/SdBackend.cpp) to
report.

##### Vulkan build dependencies

ggml-vulkan needs Vulkan headers with `VK_HEADER_VERSION` ≥ 272
(`VK_EXT_layer_settings`; an older SDK fails deep inside `ggml-vulkan.cpp` on
`vk::` types that do not exist yet) plus a matching SPIRV-Headers CMake
package. Both are **vendored** by `--prepare` — pinned to one Vulkan SDK
release, header-only, installed in seconds under
`3rdparty/targets/<target>/{vulkan-headers,spirv-headers}/` — because the
floor is unreachable otherwise: Ubuntu 22.04 ships `VK_HEADER_VERSION` 204,
and the KDE Flatpak SDK has no SPIRV-Headers at all.

What still has to come from the system is `glslc` (ggml-vulkan compiles its
shaders at build time) and `libvulkan.so` to link against. On Ubuntu:

```bash
sudo apt install libvulkan-dev
wget -qO- https://packages.lunarg.com/lunarg-signing-key-pub.asc \
  | sudo tee /etc/apt/trusted.gpg.d/lunarg.asc > /dev/null
sudo wget -qO /etc/apt/sources.list.d/lunarg-vulkan-jammy.list \
  https://packages.lunarg.com/vulkan/lunarg-vulkan-jammy.list
sudo apt update && sudo apt install shaderc
```

##### CUDA architectures

`--prepare` deliberately does not pin `CMAKE_CUDA_ARCHITECTURES`. With
`GGML_NATIVE=OFF`, ggml-cuda picks its own portable list — PTX for Maxwell
through Hopper plus device code for the common consumer GPUs — so the package
keeps working on a machine with a different NVIDIA GPU than the one that built
it. It is also the long pole of the build and the bulk of the module's size.
`HAZOR_SD_CUDA_ARCHS` pins it for a faster local iteration
(`HAZOR_SD_CUDA_ARCHS=61 ./scripts/build.sh --prepare`); a package built that
way runs CUDA on that compute capability only and must not be shipped.

##### What each package ships

| | AppImage | Flatpak |
|-|----------|---------|
| Local Stable Diffusion | yes | **no** — the manifest does not enable it |
| Built | from the prepared host tree | — |
| Backends | CPU + Vulkan + CUDA | — |
| Backend modules | staged into `usr/bin`, next to the executable | — |

The AppImage is the only package with the module while the feature is
experimental: it is built from the prepared host tree, so it carries every
backend the host's toolchain could produce, and `AppRun` can set up the run
time the module needs. The Flatpak is left as it was — without
`-DENABLE_STABLE_DIFFUSION=ON` the hazor-studio module logs a CMake warning
about the missing prefix and builds without it, which also leaves out the
Neural Flow panel (`EMBEDDED_UI_FOUND` gates it on `STABLE_DIFFUSION_FOUND`).

Adding it there later is not symmetrical work, and the asymmetry is not a
preference: the KDE SDK has no CUDA toolkit, and a CUDA-enabled package cannot
even be *consumed* in the sandbox — `stable-diffusion-config.cmake` calls
`find_dependency(CUDAToolkit REQUIRED)` when `SD_CUDA` was on. Flatpak's NVIDIA
story is the `org.freedesktop.Platform.GL.nvidia-*` extension, which carries
the driver but not cuBLAS, so a CUDA module could never `dlopen` there anyway.
A Flatpak would therefore be CPU + Vulkan, built from source inside the
sandbox, with its modules installed into `/app/bin` next to the executable.

The AppImage does not bundle the GPU stack itself — no CUDA runtime, no cuBLAS
(≈860 MB), no Vulkan loader. Those belong to the user's system: the loader is
the component that enumerates their own drivers, and a bundled driver library
would be matched to the build machine's kernel module. `package_appimage()`
deletes them if linuxdeploy drags them in.

The one exception is the **CUDA driver stub**. `SD_CUDA` makes
`libstable-diffusion.a` call the driver API directly (`cuInit`, for the
compute-capability gate on the MMA kernels), so the Hazor executable itself
carries `DT_NEEDED libcuda.so.1` — and on a machine with no NVIDIA driver
there is no such library, which would stop the *editor* from starting over an
optional backend. `--prepare` stages the CUDA toolkit's own 66 KB stub into the
package, and the AppImage's `AppRun` hook puts it on `LD_LIBRARY_PATH` only
when the host has no real driver; where one exists, the host's is used and CUDA
works for real. The hook checks `ldconfig -p`, then the caller's own
`LD_LIBRARY_PATH` (a driver reachable only that way still has to win over the
stub), then the usual driver directories — Debian's alternatives path, NixOS's
`/run/opengl-driver/lib`, the `lib64` split.

##### The AppRun hooks have to be re-generated

`package_appimage()` **replaces** the `AppRun` linuxdeploy generated, through
`finalise_apprun_hooks()`, and that is not cosmetic. linuxdeploy writes one
hardcoded `source` line per hook that exists *at the moment it runs* — it never
scans `apprun-hooks/` — so every hook this repository adds afterwards was
written into the AppDir and then silently never executed. Two consequences, one
of them fatal:

- `QT_QPA_PLATFORMTHEME=xdgdesktopportal` never applied in a released
  AppImage. linuxdeploy's own hook had already set `gtk2` on GNOME/XFCE, and
  the plugin it names is not bundled.
- With the Stable Diffusion module linked, `libcuda.so.1` is a `DT_NEEDED` of
  the executable, so the un-sourced CUDA stub hook meant the **editor would not
  start at all** on any machine without an NVIDIA driver — `error while loading
  shared libraries: libcuda.so.1`, before a single line of Hazor's own code.

The replacement sources the directory: linuxdeploy's `linuxdeploy-plugin-*.sh`
hooks first, then this repository's, so ours win where both set the same
variable. `verify_appimage_apprun_hooks()` is what keeps it from regressing —
nothing else about the package would look different if it did — and it also
sources each hook under the same `set -e` `AppRun` uses, because a hook whose
last command fails stops the application from starting.

##### What the user's machine decides

Nothing about the GPU is fixed at build time. One AppImage resolves this on
every machine it is started on, and every step down is a logged fallback rather
than a failure:

| On the user's machine | What happens |
|-----------------------|--------------|
| NVIDIA driver + CUDA runtime installed | `libggml-cuda.so` loads, CUDA device used |
| NVIDIA driver, no CUDA runtime | the module fails to `dlopen` (`libcudart`/`libcublas` absent), Vulkan is used |
| No NVIDIA driver at all | the stub satisfies the executable's `DT_NEEDED`, `cuInit` returns `CUDA_ERROR_STUB_LIBRARY`, CUDA registers no device, Vulkan is used |
| AMD / Intel GPU with a Vulkan driver | `libggml-vulkan.so` loads, GPU device used |
| No Vulkan driver, or a broken/absent ICD | ggml-vulkan registers nothing (its init catches the failure), CPU is used |
| Software-only Vulkan (lavapipe) | rejected on purpose — ggml ignores CPU-type Vulkan devices, so the CPU backend runs instead of a slower emulation of a GPU |
| Pre-AVX2 CPU | the baseline `libggml-cpu-x64.so` variant is scored and loaded; `GGML_NATIVE=OFF` is what keeps it from being a `SIGILL` |

[`SdBackend.cpp`](../src/ai/sd/SdBackend.cpp) reports which of those really
happened — compiled backends, enumerated devices and the effective device are
three separate questions there, deliberately.

#### Registering a model

There is no model picker and no visual model manager. Weights are registered
with [`SdModelManager`](../src/ai/sd/SdModelManager.hpp), which records *where
models already live* — nothing is copied, downloaded or scanned at Hazor's
startup — in a plain JSON file:

```
~/.local/share/Rincolabs/Hazor Studio/sd-models.json
```

Two environment variables cover the common development cases without editing
that file. They are applied on top of it and are not persisted:

| Variable | Effect |
|----------|--------|
| `HAZOR_SD_MODEL` | Registers one checkpoint and makes it the default model |
| `HAZOR_SD_MODEL_DIR` | Registers a directory of checkpoints (a ComfyUI `models/checkpoints`, for example) |
| `HAZOR_SD_VAE_DIR` | Registers a directory of VAEs |
| `HAZOR_SD_LORA_DIR` | Registers a directory of LoRAs |

```bash
HAZOR_SD_MODEL=/models/v1-5-pruned-emaonly.safetensors \
  ./scripts/build.sh
```

No absolute model path is committed in the source — it would be valid on
exactly one machine. With nothing registered, the panel reports that no model
is available and names the registry file.

### Embedded web interfaces (`embedded-ui/`) — *optional, developer toolchain*

The Vue 3 + TypeScript interfaces Hazor embeds through Qt WebEngine live in
[`embedded-ui/`](../embedded-ui/) — currently the **Neural Flow** node editor,
opened from the Stable Diffusion panel. One Vite project with one entry point
per panel, not one Node project per panel.

The build output is compiled into the Hazor binary as a Qt resource, which is
what lets the editor load with no HTTP server, no Vite dev server and no
network access, and keeps `node_modules` out of everything distributed.

Build it on its own:

```bash
cd embedded-ui
npm ci
npm run build          # vue-tsc --noEmit && vite build
```

CMake does the same thing as part of the Hazor build, in four steps:

1. `npm ci`, keyed on `package-lock.json` through a stamp file — **not** on
   every C++ build. This step needs network access the first time a build
   directory is configured.
2. `npm run build`, keyed on everything under `embedded-ui/src` plus
   `package.json`, `tsconfig.json` and `vite.config.ts`.
3. [`cmake/GenerateEmbeddedUiQrc.cmake`](../cmake/GenerateEmbeddedUiQrc.cmake)
   lists `embedded-ui/dist` into a generated `.qrc`. Generated rather than
   committed because Vite's asset names carry a content hash.
4. `rcc` compiles that into the binary under the `/embedded-ui` prefix, so the
   panel loads `qrc:/embedded-ui/neural-flow/index.html`.

`rcc` is invoked explicitly instead of through `AUTORCC`, because the `.qrc`
does not exist until the frontend has been built and `AUTORCC` resolves its
inputs too early to wait for that.

`ENABLE_EMBEDDED_UI` defaults to `ON` but only does anything when
`ENABLE_STABLE_DIFFUSION` is also on, since the node editor is currently its
only consumer. With npm absent, an existing `embedded-ui/dist` is used as-is —
which is what makes a source tree with the build output included compilable
without a JavaScript toolchain. With neither, CMake warns and builds Hazor
without the Neural Flow panel; the button that opens it is then not added to
the Stable Diffusion panel.

`embedded-ui/node_modules` and `embedded-ui/dist` are build artefacts and are
not tracked in git.

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

Nothing is downloaded twice. What is fetched once and reused by every target:

| Payload | Stored at | Why it is shared |
|---------|-----------|------------------|
| ONNX models (Mobile SAM, BiRefNet Lite) | `common/onnx/models/` | Model files are platform-neutral data |
| RealESRGAN ncnn weights | `common/realesrgan/<version>/models/` | The per-OS archives ship identical `.param`/`.bin` files |
| macOS Qt | `common/qt/<version>/macos/` | The official package is a universal x86_64 + arm64 build |
| Release archives and pinned git sources | `downloads/` | Checksum/commit verified once, extracted or built per target |
| Host Qt tools, aqt, AppImage utilities | `host/linux-x86_64/` | They run on the build machine, not on the target |

Targets reach shared payloads through a symlink inside their own prefix, so
`HAZOR_STRICT_DEPENDENCIES` still resolves every path under
`3rdparty/targets/<target>/`, and packaging copies the real files
(never the links) into the artifact. A model already installed anywhere under
`common/onnx/models/` — including a pre-existing install adopted by
`--migrate-only` — is detected and reused rather than downloaded again; the
model registry scans that whole tree, so a second copy would also list the
model twice in the UI.

```
3rdparty/
├── common/
│   ├── onnx/models/                               # platform-neutral models
│   ├── realesrgan/20220424/models/                # platform-neutral weights
│   └── qt/6.8.3/macos/                            # universal macOS Qt
├── downloads/                                     # archives + pinned sources
├── host/linux-x86_64/
│   ├── aqt/3.3.0/
│   ├── appimage/
│   └── qt/6.8.3/gcc_64/                           # host moc/rcc/uic
├── targets/
│   ├── linux-x86_64/
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
├── toolchains/                                    # provisioned or supplied compilers
│   └── mingw-w64-x86_64-gcc-13.1.0-msvcrt/        # built by --prepare
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
| `STABLE_DIFFUSION_PREFIX` | `$THIRD_PARTY_DIR/targets/<target>/stable-diffusion.cpp/<version>` |
| `VULKAN_HEADERS_PREFIX`, `SPIRV_HEADERS_PREFIX` | `$THIRD_PARTY_DIR/targets/<target>/<name>/<sdk version>` |
| `HAZOR_SD_BACKENDS` | `ALL` (`CPU`, `VULKAN`, `CUDA` or a comma-separated mix) |
| `HAZOR_SD_CUDA_ARCHS` | unset — ggml-cuda's own portable architecture list |
| `ONNX_MODELS_DIR` | `$HAZOR_COMMON_ROOT/onnx/models` (shared) |
| `REALESRGAN_MODELS_DIR` | `$HAZOR_COMMON_ROOT/realesrgan/<version>/models` (shared) |
| `HAZOR_ENABLE_RAWENGINE` | `AUTO` (`OFF` leaves the RAW engine out of a target build) |
| `MINGW_TOOLCHAIN_DIR` | `$THIRD_PARTY_DIR/toolchains/mingw-w64-x86_64-gcc-<ver>-msvcrt` |
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
| `ENABLE_STABLE_DIFFUSION` | `ON` | Local Stable Diffusion module and panel (`linux-x86_64` only — cross-target builds force it off; needs the prepared package, see above) |
| `STABLE_DIFFUSION_DIR` | — (set by `build.sh`) | Install prefix of the stable-diffusion.cpp package built by `./scripts/build.sh --prepare`; `ENABLE_STABLE_DIFFUSION` looks for `$STABLE_DIFFUSION_DIR/lib/cmake/stable-diffusion` |
| `ENABLE_EMBEDDED_UI` | `ON` | Compile the Vue interfaces in [`embedded-ui/`](../embedded-ui/) into the binary (needs npm, or a prebuilt `dist`); only used when `ENABLE_STABLE_DIFFUSION` is on |
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
