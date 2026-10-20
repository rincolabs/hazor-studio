#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

# shellcheck source=build/common.sh
source "$ROOT_DIR/scripts/build/common.sh"

TARGET="linux-x86_64"
CONFIG="Release"
JOBS="${JOBS:-$(nproc)}"
DO_PREPARE=0
DO_CLEAN=0
DO_BUILD=1
DO_VERIFY=1
MIGRATE_ONLY=0
PACKAGE_FORMAT="none"
ALL_TARGETS=0
LEGACY_COMPAT=0

usage() {
    cat <<'EOF'
Usage: ./scripts/build.sh [options]

With no options, preserves the original Linux Release developer build.

Options:
  --target TARGET       linux-x86_64, windows-x86_64, windows-x86,
                        macos-x86_64, or macos-arm64
  --config CONFIG       Debug or Release (default: Release)
  --prepare             prepare/reuse pinned third-party dependencies first
  --migrate-only        link valid legacy Linux dependencies into the new layout
  --package FORMAT      appimage, flatpak, zip, bundle, all, or none
  --build-only          build the Hazor executable without packaging
  --clean               remove this target/configuration's build artifacts first
  --verify-only         inspect an existing build/package without compiling
  --no-verify           skip post-build artifact checks
  --jobs N              maximum parallel build jobs
  --all                  run every configured target in sequence and summarize
  --help                 show this help

Examples:
  ./scripts/build.sh --target linux-x86_64 --prepare
  ./scripts/build.sh --target linux-x86_64 --package appimage
  ./scripts/build.sh --target linux-x86_64 --package flatpak
  ./scripts/build.sh --target windows-x86_64 --prepare --package zip
  OSXCROSS_TARGET_DIR=/opt/osxcross/target ./scripts/build.sh --target macos-arm64 --prepare
EOF
}

if [[ "$#" -eq 0 ]]; then
    LEGACY_COMPAT=1
fi

while [[ "$#" -gt 0 ]]; do
    case "$1" in
        --target)
            [[ "$#" -ge 2 ]] || die "--target requires a value"
            TARGET="$2"
            shift 2
            ;;
        --config)
            [[ "$#" -ge 2 ]] || die "--config requires a value"
            CONFIG="$2"
            shift 2
            ;;
        --prepare)
            DO_PREPARE=1
            shift
            ;;
        --migrate-only)
            DO_PREPARE=1
            DO_BUILD=0
            DO_VERIFY=0
            MIGRATE_ONLY=1
            shift
            ;;
        --package)
            [[ "$#" -ge 2 ]] || die "--package requires a format"
            PACKAGE_FORMAT="$2"
            shift 2
            ;;
        --build-only)
            PACKAGE_FORMAT="none"
            shift
            ;;
        --clean)
            DO_CLEAN=1
            shift
            ;;
        --verify-only)
            DO_BUILD=0
            DO_VERIFY=1
            shift
            ;;
        --no-verify)
            DO_VERIFY=0
            shift
            ;;
        --jobs)
            [[ "$#" -ge 2 ]] || die "--jobs requires a value"
            JOBS="$2"
            shift 2
            ;;
        --all)
            ALL_TARGETS=1
            shift
            ;;
        --help|-h)
            usage
            exit 0
            ;;
        *)
            die "unknown option: $1"
            ;;
    esac
done

[[ "$CONFIG" == "Debug" || "$CONFIG" == "Release" ]] \
    || die "--config must be Debug or Release"
[[ "$JOBS" =~ ^[1-9][0-9]*$ ]] || die "--jobs must be a positive integer"

run_all_targets() {
    local -a targets=(linux-x86_64 windows-x86_64 windows-x86 macos-x86_64 macos-arm64)
    local -a command
    local current status=0
    for current in "${targets[@]}"; do
        command=("$0" --target "$current" --config "$CONFIG" --jobs "$JOBS")
        [[ "$DO_PREPARE" == "1" ]] && command+=(--prepare)
        [[ "$DO_CLEAN" == "1" ]] && command+=(--clean)
        [[ "$DO_BUILD" == "0" ]] && command+=(--verify-only)
        [[ "$DO_VERIFY" == "0" ]] && command+=(--no-verify)
        [[ "$PACKAGE_FORMAT" != "none" ]] && command+=(--package "$PACKAGE_FORMAT")
        log "all-targets: starting $current"
        if "${command[@]}"; then
            log "all-targets: $current complete"
        else
            log "all-targets: $current blocked or failed"
            status=1
        fi
    done
    return "$status"
}

if [[ "$ALL_TARGETS" == "1" ]]; then
    run_all_targets
    exit $?
fi

legacy_build() {
    local build_dir="${BUILD_DIR:-$ROOT_DIR/build}"
    local thirdparty="${THIRD_PARTY_DIR:-$ROOT_DIR/3rdparty}"
    local qt="${QT_DIR:-$thirdparty/Qt/$HAZOR_QT_VERSION/gcc_64}"
    local ort="${ONNXRUNTIME_DIR:-$thirdparty/onnx/runtime/$HAZOR_ORT_LINUX_PACKAGE}"
    local realesrgan="${REALESRGAN_DIR:-$thirdparty/realesrgan}"
    local models="${REALESRGAN_MODELS_DIR:-$realesrgan/models}"
    local rawengine="${RAWENGINE_DEPS_DIR:-$thirdparty/rawengine}"
    require_dir "$qt" Qt
    require_dir "$ort" "ONNX Runtime"
    require_dir "$realesrgan" RealESRGAN
    require_dir "$models" "RealESRGAN models"
    log "compatibility build: Linux x86_64 Release"
    log "build dir: $build_dir"
    log "Qt: $qt"
    log "ONNX Runtime: $ort"
    log "RawEngine dependencies: $rawengine"
    mkdir -p "$build_dir"
    cmake -S "$ROOT_DIR" -B "$build_dir" \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH="$qt" \
        -DONNXRUNTIME_ROOT="$ort" \
        -DONNXRUNTIME_INCLUDE_DIR="$ort/include" \
        -DONNXRUNTIME_LIBRARY="$ort/lib/libonnxruntime.so" \
        -DRAWENGINE_DEPS_DIR="$rawengine"
    cmake --build "$build_dir" --parallel "$JOBS" --target Hazor
}

if [[ "$LEGACY_COMPAT" == "1" ]]; then
    legacy_build
    exit 0
fi

configure_target
export JOBS MIGRATE_ONLY

if [[ "$DO_CLEAN" == "1" ]]; then
    [[ -e "$BUILD_DIR" ]] && safe_remove_tree "$BUILD_DIR" "$ROOT_DIR"
    [[ -e "$DIST_DIR" ]] && safe_remove_tree "$DIST_DIR" "$ROOT_DIR"
    [[ -e "$ARTIFACT_DIR" ]] && safe_remove_tree "$ARTIFACT_DIR" "$ROOT_DIR"
    log "cleaned build/package outputs for $TARGET ($CONFIG); dependencies were preserved"
fi

if [[ "$DO_PREPARE" == "1" ]]; then
    # shellcheck source=build/prepare.sh
    source "$ROOT_DIR/scripts/build/prepare.sh"
fi

if [[ "$DO_BUILD" == "1" ]]; then
    [[ "$TARGET" != "windows-x86" ]] \
        || die "windows-x86 is blocked: the official Qt 6.8.3 target and supported Qt 6.8 ABI are x86_64 only"
    require_command cmake
    require_command ninja
    require_dir "$QT_TARGET_DIR" "target Qt"
    require_dir "$OPENCV_PREFIX" "target OpenCV"
    require_dir "$ZLIB_PREFIX" "target zlib"
    require_dir "$QT_HOST_DIR" "host Qt tools"
    validate_cross_toolchain

    OPENCV_DIR="$(find_opencv_dir)" || die "OpenCVConfig.cmake not found under $OPENCV_PREFIX"
    ZLIB_LIBRARY="$(find_zlib_library)" || die "zlib library not found under $ZLIB_PREFIX/lib"
    local_onnx=OFF
    ONNXRUNTIME_LIBRARY=""
    case "$TARGET" in
        linux-x86_64)
            if [[ -f "$ONNXRUNTIME_DIR/lib/libonnxruntime.so" ]]; then
                local_onnx=ON
                ONNXRUNTIME_LIBRARY="$ONNXRUNTIME_DIR/lib/libonnxruntime.so"
            fi
            ;;
        macos-arm64)
            ONNXRUNTIME_LIBRARY="$(find "$ONNXRUNTIME_DIR/lib" -maxdepth 1 -type f -name 'libonnxruntime*.dylib' -print -quit 2>/dev/null || true)"
            [[ -n "$ONNXRUNTIME_LIBRARY" ]] && local_onnx=ON
            ;;
    esac

    local_rawengine=OFF
    if [[ "$TARGET" == "linux-x86_64" && -d "$RAWENGINE_DEPS_DIR/exiv2" ]]; then
        local_rawengine=ON
    fi
    rawengine_lensfun="${HAZOR_RAWENGINE_WITH_LENSFUN:-OFF}"
    if [[ "$rawengine_lensfun" == "ON" ]]; then
        export PKG_CONFIG_LIBDIR="$RAWENGINE_DEPS_DIR/glib/lib/pkgconfig:$RAWENGINE_DEPS_DIR/glib/share/pkgconfig"
        export PKG_CONFIG_SYSROOT_DIR="$TARGET_ROOT"
    else
        export PKG_CONFIG_LIBDIR="$TARGET_ROOT/lib/pkgconfig:$TARGET_ROOT/share/pkgconfig"
    fi

    mkdir -p "$BUILD_DIR"
    cmake_args=(
        -S "$ROOT_DIR"
        -B "$BUILD_DIR"
        -G Ninja
        "-DCMAKE_BUILD_TYPE=$CONFIG"
        "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE"
        "-DCMAKE_PREFIX_PATH=$QT_TARGET_DIR;$OPENCV_PREFIX;$ZLIB_PREFIX"
        "-DQt6_DIR=$QT_TARGET_DIR/lib/cmake/Qt6"
        "-DQT_HOST_PATH=$QT_HOST_DIR"
        "-DOpenCV_DIR=$OPENCV_DIR"
        "-DZLIB_ROOT=$ZLIB_PREFIX"
        "-DZLIB_INCLUDE_DIR=$ZLIB_PREFIX/include"
        "-DZLIB_LIBRARY=$ZLIB_LIBRARY"
        "-DHAZOR_DEPENDENCY_ROOT=$TARGET_ROOT"
        -DHAZOR_STRICT_DEPENDENCIES=ON
        -DHAZOR_DEVELOPMENT_PATHS=OFF
        -DBUILD_TESTS=OFF
        "-DENABLE_ONNXRUNTIME=$local_onnx"
        "-DENABLE_RAWENGINE=$local_rawengine"
        "-DRAWENGINE_WITH_LENSFUN=$rawengine_lensfun"
        "-DRAWENGINE_DEPS_DIR=$RAWENGINE_DEPS_DIR"
    )
    if [[ "$local_onnx" == "ON" ]]; then
        cmake_args+=(
            "-DONNXRUNTIME_ROOT=$ONNXRUNTIME_DIR"
            "-DONNXRUNTIME_INCLUDE_DIR=$ONNXRUNTIME_DIR/include"
            "-DONNXRUNTIME_LIBRARY=$ONNXRUNTIME_LIBRARY"
        )
        [[ "$TARGET" == "linux-x86_64" ]] \
            && cmake_args+=("-DHAZOR_ORT_RUNTIME_DIR=$HAZOR_ORT_LINUX_PACKAGE")
    fi
    if command -v ccache >/dev/null 2>&1; then
        cmake_args+=(
            -DCMAKE_C_COMPILER_LAUNCHER=ccache
            -DCMAKE_CXX_COMPILER_LAUNCHER=ccache
        )
    fi

    log "target: $TARGET ($TARGET_ARCH)"
    log "configuration: $CONFIG"
    log "build dir: $BUILD_DIR"
    log "target prefix: $TARGET_ROOT"
    log "Qt target: $QT_TARGET_DIR"
    log "Qt host tools: $QT_HOST_DIR"
    log "OpenCV: $OPENCV_DIR"
    log "zlib: $ZLIB_LIBRARY"
    log "ONNX Runtime enabled: $local_onnx"
    log "RawEngine enabled: $local_rawengine (lensfun: $rawengine_lensfun)"
    HAZOR_TARGET_ROOT="$TARGET_ROOT" HAZOR_TARGET_ARCH="$TARGET_ARCH" \
        cmake "${cmake_args[@]}"
    cmake --build "$BUILD_DIR" --parallel "$JOBS" --target Hazor
fi

if [[ "$PACKAGE_FORMAT" != "none" ]]; then
    # shellcheck source=build/package.sh
    source "$ROOT_DIR/scripts/build/package.sh"
fi

if [[ "$DO_VERIFY" == "1" ]]; then
    # shellcheck source=build/verify.sh
    source "$ROOT_DIR/scripts/build/verify.sh"
fi
