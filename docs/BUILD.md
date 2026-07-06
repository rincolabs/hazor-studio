# Building Hazor Studio

This document covers how to build Hazor Studio from source on Linux. Windows and
Linux AppImage builds are also produced automatically in CI — see
[`.github/workflows/`](../.github/workflows/).

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

> The editor **builds and runs without ONNX Runtime**. The AI inference layer
> (SAM object selection, matting / background removal) simply reports itself as
> unavailable at runtime. CPU/CUDA/TensorRT are chosen as runtime *Execution
> Providers* in Settings — they are never separate builds.

## Dependency downloads

The exact versions and download sources used by CI are listed below, per
platform. These are the same artifacts the
[CI workflows](../.github/workflows/) fetch, so pinning to them reproduces the
official builds.

### Qt 6.8.3

Installed via [`aqtinstall`](https://github.com/miurahr/aqtinstall) (the
`jurplel/install-qt-action` GitHub Action wraps it). Alternatively use the
[official Qt Online Installer](https://www.qt.io/download-qt-installer).

| OS | Host / Arch |
|----|-------------|
| Linux | `linux` / `linux_gcc_64` → `Qt/6.8.3/gcc_64` |
| Windows | `windows` / `win64_msvc2022_64` |

### ONNX Runtime 1.26.0 (GPU) — *optional*

Released by Microsoft on the [ONNX Runtime releases page](https://github.com/microsoft/onnxruntime/releases/tag/v1.26.0).

| OS | Download |
|----|----------|
| Linux | [`onnxruntime-linux-x64-gpu-1.26.0.tgz`](https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-linux-x64-gpu-1.26.0.tgz) |
| Windows | [`onnxruntime-win-x64-gpu-1.26.0.zip`](https://github.com/microsoft/onnxruntime/releases/download/v1.26.0/onnxruntime-win-x64-gpu-1.26.0.zip) |

### OpenCV 4.5.4

| OS | Source |
|----|--------|
| Linux | System package `libopencv-dev` (apt), or [OpenCV 4.5.4 release](https://github.com/opencv/opencv/releases/tag/4.5.4) |
| Windows | [`opencv-4.5.4-vc14_vc15.exe`](https://github.com/opencv/opencv/releases/download/4.5.4/opencv-4.5.4-vc14_vc15.exe) (self-extracting) |

### zlib

| OS | Source |
|----|--------|
| Linux | System package `zlib1g-dev` (apt) |
| Windows | vcpkg: `vcpkg install zlib:x64-windows-static-md` |

### RealESRGAN ncnn Vulkan (20220424)

From the [Real-ESRGAN v0.2.5.0 release](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0).

| OS | Download |
|----|----------|
| Linux | [`realesrgan-ncnn-vulkan-20220424-ubuntu.zip`](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-ubuntu.zip) |
| Windows | [`realesrgan-ncnn-vulkan-20220424-windows.zip`](https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/realesrgan-ncnn-vulkan-20220424-windows.zip) |

### Bundled ONNX models (optional, cross-platform)

Downloaded once and placed under the app's model directory; not required to
compile.

| Model | Version | Source |
|-------|---------|--------|
| Mobile SAM (object selection) | `20230629` | [vietanhdev/segment-anything-onnx-models](https://huggingface.co/vietanhdev/segment-anything-onnx-models) → `mobile_sam_20230629.zip` |
| BiRefNet Lite (matting) | `20240101` | [onnx-community/BiRefNet_lite-ONNX](https://huggingface.co/onnx-community/BiRefNet_lite-ONNX) → `onnx/model_fp16.onnx` |
| Material Design Icons | `3.0.1` | [google/material-design-icons](https://github.com/google/material-design-icons/releases/tag/3.0.1) |

## Vendored third-party layout

The provided build script ([`scripts/build.sh`](../scripts/build.sh)) expects
third-party dependencies under `3rdparty/`. These directories are **not** checked
into git (only skeleton folders are tracked); populate them locally or override
the paths with environment variables.

```
3rdparty/
├── Qt/6.8.3/gcc_64/                              # Qt 6 SDK
├── onnx/runtime/onnxruntime-linux-x64-gpu-1.26.0/ # ONNX Runtime (include/ + lib/)
└── realesrgan/                                    # RealESRGAN binaries
    └── models/                                    # RealESRGAN models
```

Each path can be overridden via environment variable:

| Variable | Default |
|----------|---------|
| `BUILD_DIR` | `<root>/build` |
| `THIRD_PARTY_DIR` | `<root>/3rdparty` |
| `QT_DIR` | `$THIRD_PARTY_DIR/Qt/6.8.3/gcc_64` |
| `ONNXRUNTIME_DIR` | `$THIRD_PARTY_DIR/onnx/runtime/onnxruntime-linux-x64-gpu-1.26.0` |
| `REALESRGAN_DIR` | `$THIRD_PARTY_DIR/realesrgan` |
| `REALESRGAN_MODELS_DIR` | `$THIRD_PARTY_DIR/realesrgan/models` |

## Quick build (Linux)

The convenience script configures and builds the `Hazor` target in Release:

```bash
./scripts/build.sh
```

Run the resulting binary:

```bash
./scripts/run.sh          # runs build/Hazor
```

## Manual build with CMake

If you have Qt, OpenCV, and ONNX Runtime installed on system paths (or want full
control over the configure step):

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
| `USE_RHI` | `OFF` | Use the QRhi rendering backend (Vulkan/Metal/D3D12) instead of OpenGL |
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

These are the canonical references for reproducing a full packaged build on each
platform.
