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
            die "unknown target '$TARGET' (expected linux-x86_64, windows-x86_64, macos-x86_64, or macos-arm64)"
            ;;
    esac

    TARGET_ROOT="$THIRD_PARTY_DIR/targets/$TARGET"
    QT_HOST_DIR="${QT_HOST_DIR:-$HAZOR_HOST_ROOT/qt/$HAZOR_QT_VERSION/gcc_64}"
    QT_TARGET_DIR="${QT_TARGET_DIR:-$TARGET_ROOT/qt/$HAZOR_QT_VERSION/$QT_TARGET_ARCH}"
    # Qt publishes one universal (x86_64 + arm64) macOS package, so the two
    # macOS targets resolve a single shared install through a per-target link
    # instead of downloading the same archives twice.
    QT_SHARED_DIR=""
    if target_is_macos; then
        QT_SHARED_DIR="$HAZOR_COMMON_ROOT/qt/$HAZOR_QT_VERSION/$QT_TARGET_ARCH"
    fi
    OPENCV_PREFIX="${OPENCV_PREFIX:-$TARGET_ROOT/opencv/$HAZOR_OPENCV_VERSION}"
    ZLIB_PREFIX="${ZLIB_PREFIX:-$TARGET_ROOT/zlib/$HAZOR_ZLIB_VERSION}"
    RAWENGINE_DEPS_DIR="${RAWENGINE_DEPS_DIR:-$TARGET_ROOT/rawengine}"
    REALESRGAN_DIR="${REALESRGAN_DIR:-$TARGET_ROOT/realesrgan/$HAZOR_REALESRGAN_VERSION}"
    # Only the ncnn process executable (and its Windows DLLs) is platform
    # specific; the .param/.bin weights are plain data shipped identically in
    # every per-OS archive of the release, so one shared copy serves all targets.
    REALESRGAN_MODELS_DIR="${REALESRGAN_MODELS_DIR:-$HAZOR_COMMON_ROOT/realesrgan/$HAZOR_REALESRGAN_VERSION/models}"
    ONNX_MODELS_DIR="${ONNX_MODELS_DIR:-$HAZOR_COMMON_ROOT/onnx/models}"
    # A provisioned cross toolchain has to win over an older MinGW installed on
    # the host, so it goes in front of PATH as soon as it exists.
    MINGW_TOOLCHAIN_DIR="${MINGW_TOOLCHAIN_DIR:-$THIRD_PARTY_DIR/toolchains/mingw-w64-$HAZOR_MINGW_TRIPLE-gcc-$HAZOR_MINGW_GCC_VERSION-$HAZOR_MINGW_CRT}"
    if [[ "$TARGET" == "windows-x86_64" && -d "$MINGW_TOOLCHAIN_DIR/bin" ]]; then
        PATH="$MINGW_TOOLCHAIN_DIR/bin:$PATH"
        export PATH
    fi

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
    export QT_SHARED_DIR
    export OPENCV_PREFIX ZLIB_PREFIX RAWENGINE_DEPS_DIR ONNXRUNTIME_DIR
    export ONNX_MODELS_DIR REALESRGAN_DIR REALESRGAN_MODELS_DIR TOOLCHAIN_FILE
    export MINGW_TOOLCHAIN_DIR
}

# Needed by scripts/build.sh on every build, not only when dependencies are
# prepared, so it lives here rather than in prepare.sh.
validate_cross_toolchain() {
    case "$TARGET" in
        linux-x86_64)
            require_command cc
            require_command c++
            ;;
        windows-x86_64)
            require_command x86_64-w64-mingw32-gcc
            require_command x86_64-w64-mingw32-g++
            local mingw_major
            mingw_major="$(x86_64-w64-mingw32-g++ -dumpfullversion | cut -d. -f1)"
            [[ "$mingw_major" == "13" ]] || die "Qt $HAZOR_QT_VERSION win64_mingw was built with MinGW-w64 GCC 13.1; found GCC $(x86_64-w64-mingw32-g++ -dumpfullversion). Install a target-compatible GCC 13 toolchain and put it first in PATH"
            ;;
        macos-*)
            [[ -n "${OSXCROSS_TARGET_DIR:-}" ]] \
                || die "OSXCROSS_TARGET_DIR is required and must point to an OSXCross installation built from a legally obtained macOS SDK"
            require_dir "$OSXCROSS_TARGET_DIR/bin" "OSXCross bin"
            require_file "$OSXCROSS_TARGET_DIR/toolchain.cmake" "OSXCross CMake toolchain"
            ;;
    esac
}

find_opencv_dir() {
    local result
    result="$(find "$OPENCV_PREFIX" -type f -name OpenCVConfig.cmake -printf '%h\n' 2>/dev/null | head -n 1)"
    [[ -n "$result" ]] || return 1
    printf '%s\n' "$result"
}

find_zlib_library() {
    local name
    # libzs.a is what zlib's own CMake installs on Windows: it appends an "s"
    # to the static library name there.
    for name in libz.a libzs.a zlibstatic.lib libzlibstatic.a; do
        if [[ -f "$ZLIB_PREFIX/lib/$name" ]]; then
            printf '%s\n' "$ZLIB_PREFIX/lib/$name"
            return 0
        fi
    done
    return 1
}

# Build a GNU import library for a DLL that only ships an MSVC .lib.
#
# This is a link-format problem, not an ABI one. The MinGW linker cannot read
# MSVC's import libraries, but it can consume a .def built from the DLL's own
# export table, and on x86-64 there is a single calling convention and no name
# decoration, so the names in that table are the names the compiler emits.
generate_mingw_import_library() {
    local dll="$1"
    local output="$2"
    local objdump="$HAZOR_MINGW_TRIPLE-objdump"
    local dlltool="$HAZOR_MINGW_TRIPLE-dlltool"
    local definition="${output%.dll.a}.def"
    require_command "$objdump"
    require_command "$dlltool"
    require_file "$dll" "DLL to generate an import library from"
    mkdir -p "$(dirname "$output")"
    {
        printf 'LIBRARY %s\n' "$(basename "$dll")"
        printf 'EXPORTS\n'
        "$objdump" -p "$dll" | sed -n \
            '/^\[Ordinal\/Name Pointer\] Table$/,/^$/ s/^[[:space:]]*\[[[:space:]]*[0-9]\{1,\}\][[:space:]]\{1,\}//p'
    } > "$definition"
    [[ "$(wc -l < "$definition")" -gt 2 ]] \
        || die "no export table found in $(basename "$dll"); cannot build an import library"
    "$dlltool" --machine i386:x86-64 \
        --input-def "$definition" \
        --dllname "$(basename "$dll")" \
        --output-lib "$output"
    require_file "$output" "generated import library"
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
