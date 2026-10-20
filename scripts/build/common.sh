#!/usr/bin/env bash

if [[ -z "${ROOT_DIR:-}" ]]; then
    echo "scripts/build/common.sh must be sourced after ROOT_DIR is set" >&2
    exit 2
fi

# shellcheck source=versions.sh
source "$ROOT_DIR/scripts/build/versions.sh"

THIRD_PARTY_DIR="${THIRD_PARTY_DIR:-$ROOT_DIR/3rdparty}"
HAZOR_DOWNLOAD_DIR="${HAZOR_DOWNLOAD_DIR:-$THIRD_PARTY_DIR/downloads}"
HAZOR_HOST_ROOT="${HAZOR_HOST_ROOT:-$THIRD_PARTY_DIR/host/linux-x86_64}"
HAZOR_COMMON_ROOT="${HAZOR_COMMON_ROOT:-$THIRD_PARTY_DIR/common}"

log() {
    printf '[hazor-build] %s\n' "$*"
}

die() {
    printf '[hazor-build] error: %s\n' "$*" >&2
    exit 1
}

require_command() {
    command -v "$1" >/dev/null 2>&1 || die "required command not found: $1"
}

require_dir() {
    [[ -d "$1" ]] || die "missing $2 directory: $1"
}

require_file() {
    [[ -f "$1" ]] || die "missing $2 file: $1"
}

real_path() {
    readlink -f -- "$1"
}

safe_remove_tree() {
    local path="$1"
    local root="$2"
    local resolved_root resolved_path
    resolved_root="$(real_path "$root")"
    resolved_path="$(real_path "$path")"
    [[ "$resolved_path" == "$resolved_root"/* ]] || die "refusing to remove path outside $resolved_root: $resolved_path"
    [[ "$resolved_path" != "$resolved_root" ]] || die "refusing to remove root directory: $resolved_root"
    rm -rf -- "$resolved_path"
}

target_is_macos() {
    [[ "$TARGET" == macos-* ]]
}

target_is_windows() {
    [[ "$TARGET" == windows-* ]]
}

configure_target() {
    case "$TARGET" in
        linux-x86_64)
            TARGET_OS="linux"
            TARGET_ARCH="x86_64"
            QT_TARGET_ARCH="gcc_64"
            TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/linux-x86_64.cmake"
            ;;
        windows-x86_64)
            TARGET_OS="windows"
            TARGET_ARCH="x86_64"
            QT_TARGET_ARCH="mingw_64"
            TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/windows-mingw-x86_64.cmake"
            ;;
        windows-x86)
            TARGET_OS="windows"
            TARGET_ARCH="x86"
            QT_TARGET_ARCH="unsupported"
            TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/windows-mingw-x86.cmake"
            ;;
        macos-x86_64)
            TARGET_OS="macos"
            TARGET_ARCH="x86_64"
            QT_TARGET_ARCH="macos"
            TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/macos-osxcross.cmake"
            ;;
        macos-arm64)
            TARGET_OS="macos"
            TARGET_ARCH="arm64"
            QT_TARGET_ARCH="macos"
            TOOLCHAIN_FILE="$ROOT_DIR/cmake/toolchains/macos-osxcross.cmake"
            ;;
        *)
            die "unknown target '$TARGET' (expected linux-x86_64, windows-x86_64, windows-x86, macos-x86_64, or macos-arm64)"
            ;;
    esac

    TARGET_ROOT="$THIRD_PARTY_DIR/targets/$TARGET"
    QT_HOST_DIR="${QT_HOST_DIR:-$HAZOR_HOST_ROOT/qt/$HAZOR_QT_VERSION/gcc_64}"
    QT_TARGET_DIR="${QT_TARGET_DIR:-$TARGET_ROOT/qt/$HAZOR_QT_VERSION/$QT_TARGET_ARCH}"
    OPENCV_PREFIX="${OPENCV_PREFIX:-$TARGET_ROOT/opencv/$HAZOR_OPENCV_VERSION}"
    ZLIB_PREFIX="${ZLIB_PREFIX:-$TARGET_ROOT/zlib/$HAZOR_ZLIB_VERSION}"
    RAWENGINE_DEPS_DIR="${RAWENGINE_DEPS_DIR:-$TARGET_ROOT/rawengine}"
    REALESRGAN_DIR="${REALESRGAN_DIR:-$TARGET_ROOT/realesrgan/$HAZOR_REALESRGAN_VERSION}"
    REALESRGAN_MODELS_DIR="${REALESRGAN_MODELS_DIR:-$REALESRGAN_DIR/models}"
    ONNX_MODELS_DIR="${ONNX_MODELS_DIR:-$HAZOR_COMMON_ROOT/onnx/models}"

    case "$TARGET" in
        linux-x86_64)
            ONNXRUNTIME_DIR="${ONNXRUNTIME_DIR:-$TARGET_ROOT/onnxruntime/$HAZOR_ONNXRUNTIME_VERSION/$HAZOR_ORT_LINUX_PACKAGE}"
            ;;
        windows-x86_64)
            ONNXRUNTIME_DIR="${ONNXRUNTIME_DIR:-$TARGET_ROOT/onnxruntime/$HAZOR_ONNXRUNTIME_VERSION/$HAZOR_ORT_WINDOWS_PACKAGE}"
            ;;
        macos-arm64)
            ONNXRUNTIME_DIR="${ONNXRUNTIME_DIR:-$TARGET_ROOT/onnxruntime/$HAZOR_ONNXRUNTIME_VERSION/$HAZOR_ORT_MACOS_ARM64_PACKAGE}"
            ;;
        *)
            ONNXRUNTIME_DIR="${ONNXRUNTIME_DIR:-$TARGET_ROOT/onnxruntime/$HAZOR_ONNXRUNTIME_VERSION/unavailable}"
            ;;
    esac

    BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build/$TARGET-${CONFIG,,}}"
    DIST_DIR="${DIST_DIR:-$ROOT_DIR/dist/$TARGET}"
    ARTIFACT_DIR="${ARTIFACT_DIR:-$ROOT_DIR/artifacts/$TARGET}"
    export TARGET TARGET_OS TARGET_ARCH TARGET_ROOT QT_HOST_DIR QT_TARGET_DIR
    export OPENCV_PREFIX ZLIB_PREFIX RAWENGINE_DEPS_DIR ONNXRUNTIME_DIR
    export ONNX_MODELS_DIR REALESRGAN_DIR REALESRGAN_MODELS_DIR TOOLCHAIN_FILE
}

find_opencv_dir() {
    local result
    result="$(find "$OPENCV_PREFIX" -type f -name OpenCVConfig.cmake -printf '%h\n' 2>/dev/null | head -n 1)"
    [[ -n "$result" ]] || return 1
    printf '%s\n' "$result"
}

find_zlib_library() {
    local name
    for name in libz.a zlibstatic.lib libzlibstatic.a; do
        if [[ -f "$ZLIB_PREFIX/lib/$name" ]]; then
            printf '%s\n' "$ZLIB_PREFIX/lib/$name"
            return 0
        fi
    done
    return 1
}

write_provenance() {
    local prefix="$1"
    local component="$2"
    local version="$3"
    local source="$4"
    local sha256_value="${5:-not-applicable}"
    mkdir -p "$prefix"
    {
        printf 'component=%s\n' "$component"
        printf 'version=%s\n' "$version"
        printf 'target=%s\n' "$TARGET"
        printf 'source=%s\n' "$source"
        printf 'sha256=%s\n' "$sha256_value"
        printf 'prepared_at_utc=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)"
    } > "$prefix/.hazor-provenance"
}

fetch_checked() {
    local url="$1"
    local destination="$2"
    local expected_sha256="$3"
    require_command curl
    require_command sha256sum
    mkdir -p "$(dirname "$destination")"

    if [[ -f "$destination" && -n "$expected_sha256" ]]; then
        if printf '%s  %s\n' "$expected_sha256" "$destination" | sha256sum --check --status; then
            log "using cached $(basename "$destination")"
            return
        fi
        die "cached file failed checksum verification: $destination"
    elif [[ -f "$destination" ]]; then
        [[ "${DOWNLOAD_UNVERIFIED:-0}" == "1" ]] || die "no checksum is recorded for $(basename "$destination"); set DOWNLOAD_UNVERIFIED=1 to accept and record it explicitly"
        log "using explicitly accepted cached file $(basename "$destination")"
        return
    fi

    [[ -n "$expected_sha256" || "${DOWNLOAD_UNVERIFIED:-0}" == "1" ]] || die "refusing unverified download: $url (set DOWNLOAD_UNVERIFIED=1 only after reviewing the upstream asset)"
    local partial="$destination.part"
    curl -fL --retry 3 --output "$partial" "$url"
    if [[ -n "$expected_sha256" ]]; then
        printf '%s  %s\n' "$expected_sha256" "$partial" | sha256sum --check --status \
            || die "checksum mismatch for $url"
    else
        log "accepted checksum: $(sha256sum "$partial" | awk '{print $1}')  $(basename "$destination")"
    fi
    mv -f -- "$partial" "$destination"
}

checkout_pinned_source() {
    local url="$1"
    local commit="$2"
    local destination="$3"
    require_command git
    if [[ -d "$destination/.git" ]]; then
        local current
        current="$(git -C "$destination" rev-parse HEAD)"
        [[ "$current" == "$commit" ]] || die "source cache has unexpected commit ($current): $destination"
        return
    fi
    [[ ! -e "$destination" ]] || die "source cache exists but is not a git checkout: $destination"
    mkdir -p "$(dirname "$destination")"
    git init -q "$destination"
    git -C "$destination" remote add origin "$url"
    git -C "$destination" fetch --depth 1 origin "$commit"
    git -C "$destination" checkout -q --detach FETCH_HEAD
    [[ "$(git -C "$destination" rev-parse HEAD)" == "$commit" ]] || die "failed to check out pinned commit $commit"
}
