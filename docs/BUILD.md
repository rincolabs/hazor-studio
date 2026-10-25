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

**The Flatpak ships the full feature set** — AI inference and the native RAW
engine, Lens Corrections included. `ENABLE_ONNXRUNTIME`, `ENABLE_RAWENGINE` and
`RAWENGINE_WITH_LENSFUN` are all `ON`.

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
