#!/usr/bin/env bash
set -euo pipefail

# Called by scripts/build.sh after common.sh has configured TARGET.

link_directory() {
    local source="$1"
    local destination="$2"
    [[ -d "$source" ]] || return 1
    mkdir -p "$(dirname "$destination")"
    if [[ -L "$destination" ]]; then
        [[ "$(real_path "$destination")" == "$(real_path "$source")" ]] \
            || die "existing link points at a different dependency: $destination"
    elif [[ -e "$destination" ]]; then
        return 0
    else
        ln -s "$(real_path "$source")" "$destination"
        log "migrated without copying: $destination -> $source"
    fi
}

# Platform-neutral payloads (ONNX models, ncnn weights) are reused by every
# target, so a legacy copy is adopted even when preparing a cross target: the
# alternative is downloading model files that are already on disk.
migrate_shared_assets() {
    link_directory "$ROOT_DIR/3rdparty/onnx/models" "$ONNX_MODELS_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/realesrgan/models" "$REALESRGAN_MODELS_DIR" || true
}

migrate_legacy_linux() {
    [[ "$TARGET" == "linux-x86_64" ]] || return 0
    link_directory "$ROOT_DIR/3rdparty/Qt/$HAZOR_QT_VERSION/gcc_64" "$QT_HOST_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/Qt/$HAZOR_QT_VERSION/gcc_64" "$QT_TARGET_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/onnx/runtime/$HAZOR_ORT_LINUX_PACKAGE" "$ONNXRUNTIME_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/realesrgan" "$REALESRGAN_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/rawengine" "$RAWENGINE_DEPS_DIR" || true
    # RawEngine's vendored zlib is deliberately NOT adopted as the target zlib:
    # it is a static build without -fPIC, which links into the Hazor executable
    # but makes ld reject OpenCV's shared libraries ("relocation R_X86_64_PC32
    # ... can not be used when making a shared object"). The target zlib is
    # built from the same pinned commit with position-independent code instead.
}

# AppPaths::thirdPartyDir() resolves 3rdparty/onnx/models relative to the
# executable or the source tree, so a build run from build/ reads the legacy
# path. Point it at the shared tree when no legacy install exists, so nobody
# has to fetch a second copy of the models to run a development build.
publish_shared_assets_for_dev_runs() {
    local legacy_models="$ROOT_DIR/3rdparty/onnx/models"
    if [[ ! -e "$legacy_models" && -d "$ONNX_MODELS_DIR" ]]; then
        link_directory "$ONNX_MODELS_DIR" "$legacy_models" || true
    fi
    # Same for the upscaler, whose executable and weights the backend looks up
    # under 3rdparty/realesrgan/.
    if [[ "$TARGET" == "linux-x86_64" && ! -e "$ROOT_DIR/3rdparty/realesrgan" ]]; then
        link_directory "$REALESRGAN_DIR" "$ROOT_DIR/3rdparty/realesrgan" || true
    fi
}

ensure_aqt() {
    local venv="$HAZOR_HOST_ROOT/aqt/$HAZOR_AQT_VERSION"
    local aqt="$venv/bin/aqt"
    if [[ ! -x "$aqt" ]]; then
        require_command python3
        python3 -m venv "$venv"
        # ensure_aqt is used through command substitution. Keep installer output
        # on stderr so stdout contains exactly the executable path.
        "$venv/bin/python" -m pip install --disable-pip-version-check \
            "aqtinstall==$HAZOR_AQT_VERSION" >&2
    fi
    # aqtinstall reports its banner through the logging module, i.e. on stderr.
    "$aqt" version 2>&1 | grep -F "$HAZOR_AQT_VERSION" >/dev/null \
        || die "aqt version mismatch under $venv"
    printf '%s\n' "$aqt"
}

# Qt 6 ships the code generators in libexec/ on Linux; bin/ only has the
# user-facing tools. Probing bin/moc made a complete host install look missing
# and triggered a redundant ~GB aqt download.
host_qt_tool() {
    local name="$1"
    local candidate
    for candidate in "$QT_HOST_DIR/libexec/$name" "$QT_HOST_DIR/bin/$name"; do
        if [[ -x "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

prepare_qt_host() {
    if host_qt_tool moc >/dev/null && [[ -f "$QT_HOST_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        return
    fi
    local aqt
    aqt="$(ensure_aqt)"
    mkdir -p "$HAZOR_HOST_ROOT/qt"
    "$aqt" install-qt linux desktop "$HAZOR_QT_VERSION" linux_gcc_64 \
        --outputdir "$HAZOR_HOST_ROOT/qt"
    require_file "$QT_HOST_DIR/lib/cmake/Qt6/Qt6Config.cmake" "host Qt CMake package"
    host_qt_tool moc >/dev/null || die "host Qt has no moc under libexec/ or bin/: $QT_HOST_DIR"
    write_provenance "$QT_HOST_DIR" Qt "$HAZOR_QT_VERSION" \
        "aqt:$HAZOR_AQT_VERSION linux/desktop/linux_gcc_64"
}

# The official macOS package is a universal x86_64 + arm64 build: both macOS
# targets link the same install instead of fetching those archives per
# architecture. Builds stay separate; only the download is shared.
prepare_qt_macos_shared() {
    local aqt="$1"
    if [[ -f "$QT_SHARED_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        log "reusing shared macOS Qt install: $QT_SHARED_DIR"
        return
    fi
    "$aqt" install-qt mac desktop "$HAZOR_QT_VERSION" clang_64 \
        --outputdir "$HAZOR_COMMON_ROOT/qt"
    require_file "$QT_SHARED_DIR/lib/cmake/Qt6/Qt6Config.cmake" "shared macOS Qt CMake package"
    write_provenance "$QT_SHARED_DIR" Qt "$HAZOR_QT_VERSION" \
        "aqt:$HAZOR_AQT_VERSION mac/desktop/clang_64 (universal; shared by macos-x86_64 and macos-arm64)"
}

prepare_qt_target() {
    if [[ -f "$QT_TARGET_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        return
    fi
    prepare_qt_host
    local aqt
    aqt="$(ensure_aqt)"
    case "$TARGET" in
        linux-x86_64)
            # Host and target share the same ABI on the native Linux target.
            link_directory "$QT_HOST_DIR" "$QT_TARGET_DIR"
            ;;
        windows-x86_64)
            "$aqt" install-qt windows desktop "$HAZOR_QT_VERSION" win64_mingw \
                --outputdir "$TARGET_ROOT/qt"
            ;;
        macos-*)
            prepare_qt_macos_shared "$aqt"
            link_directory "$QT_SHARED_DIR" "$QT_TARGET_DIR"
            require_file "$QT_TARGET_DIR/lib/cmake/Qt6/Qt6Config.cmake" "target Qt CMake package"
            # The provenance of the shared install is written once, by the
            # install itself, and must not be rewritten per target.
            return
            ;;
    esac
    require_file "$QT_TARGET_DIR/lib/cmake/Qt6/Qt6Config.cmake" "target Qt CMake package"
    write_provenance "$QT_TARGET_DIR" Qt "$HAZOR_QT_VERSION" \
        "aqt:$HAZOR_AQT_VERSION target=$TARGET"
}


# ── MinGW-w64 cross toolchain ────────────────────────────────────────────────
# Built from the pinned sources above because no usable GCC 13 MinGW exists for
# this host: Ubuntu 22.04 packages GCC 10, whose libstdc++ ABI cannot consume
# Qt's win64_mingw libraries, and the Ubuntu 24.04 packages need glibc 2.38.
# The stages are the standard cross-toolchain bootstrap, each one skipped when
# its output is already installed, so an interrupted run resumes.
mingw_toolchain_requirements() {
    local missing=() tool header
    for tool in make flex bison makeinfo; do
        command -v "$tool" >/dev/null 2>&1 || missing+=("$tool")
    done
    # Probe GCC's arithmetic libraries by compiling, the way its own configure
    # does: on Debian/Ubuntu these headers live in the multiarch include dir,
    # so looking them up by path finds nothing.
    for header in gmp.h mpfr.h mpc.h; do
        printf '#include <%s>\nint main(void){return 0;}\n' "$header" \
            | cc -x c -fsyntax-only - >/dev/null 2>&1 || missing+=("$header")
    done
    if (( ${#missing[@]} > 0 )); then
        die "building the MinGW-w64 toolchain needs ${missing[*]}; on Debian/Ubuntu: sudo apt install flex bison texinfo libgmp-dev libmpfr-dev libmpc-dev"
    fi
}

# libstdc++, libwinpthreads and the mingw-w64 CRT bake __FILE__ into their
# assert() strings, and every Windows binary links them — so without this the
# build machine's directory layout ends up inside each shipped .exe/.dll, which
# verify_no_local_paths() reports and no amount of stripping removes (the
# strings live in .rdata, not in debug sections). The *_FOR_TARGET variants are
# the ones that reach the runtime libraries; plain CFLAGS would only affect the
# build-host tools.
#
# -g -O2 is not decoration: autoconf substitutes that pair only when CFLAGS is
# unset in the environment, so assigning CFLAGS here to the prefix map alone
# silently drops the optimisation level and builds the CRT and winpthreads at
# -O0.
mingw_target_cflags() {
    printf '%s' "-g -O2 -ffile-prefix-map=$ROOT_DIR=."
}

mingw_configure_and_make() {
    local stage="$1" build="$2" configure="$3"
    shift 3
    local map
    map="$(mingw_target_cflags)"
    mkdir -p "$build"
    log "MinGW toolchain: configuring $stage"
    ( cd "$build" && PATH="$MINGW_TOOLCHAIN_DIR/bin:$PATH" \
        CFLAGS_FOR_TARGET="$map" CXXFLAGS_FOR_TARGET="$map" \
        CFLAGS="$map" CXXFLAGS="$map" \
        "$configure" "$@" )
    log "MinGW toolchain: building $stage"
    ( cd "$build" && PATH="$MINGW_TOOLCHAIN_DIR/bin:$PATH" make -j"$JOBS" )
    ( cd "$build" && PATH="$MINGW_TOOLCHAIN_DIR/bin:$PATH" make install )
}

prepare_mingw_toolchain() {
    [[ "$TARGET" == "windows-x86_64" ]] || return 0
    local tc="$MINGW_TOOLCHAIN_DIR"
    local triple="$HAZOR_MINGW_TRIPLE"
    if [[ -f "$tc/$triple/lib/libstdc++.a" && -x "$tc/bin/$triple-g++" ]]; then
        log "reusing the provisioned MinGW-w64 toolchain: $tc"
        return
    fi
    mingw_toolchain_requirements

    local dl="$HAZOR_DOWNLOAD_DIR/toolchains"
    fetch_checked "$HAZOR_MINGW_BINUTILS_URL" "$dl/$HAZOR_MINGW_BINUTILS_ARCHIVE" "$HAZOR_MINGW_BINUTILS_SHA256"
    fetch_checked "$HAZOR_MINGW_GCC_URL" "$dl/$HAZOR_MINGW_GCC_ARCHIVE" "$HAZOR_MINGW_GCC_SHA256"
    fetch_checked "$HAZOR_MINGW_W64_URL" "$dl/$HAZOR_MINGW_W64_ARCHIVE" "$HAZOR_MINGW_W64_SHA256"

    local build="$THIRD_PARTY_DIR/build-deps/$TARGET/toolchain"
    local src="$build/src"
    local binutils_src="$src/binutils-$HAZOR_MINGW_BINUTILS_VERSION"
    local gcc_src="$src/gcc-$HAZOR_MINGW_GCC_VERSION"
    local mingw_src="$src/mingw-w64-v$HAZOR_MINGW_W64_VERSION"
    mkdir -p "$src" "$tc"
    [[ -d "$binutils_src" ]] || tar -xf "$dl/$HAZOR_MINGW_BINUTILS_ARCHIVE" -C "$src"
    [[ -d "$gcc_src" ]] || tar -xf "$dl/$HAZOR_MINGW_GCC_ARCHIVE" -C "$src"
    [[ -d "$mingw_src" ]] || tar -xf "$dl/$HAZOR_MINGW_W64_ARCHIVE" -C "$src"

    # mingw-w64's configure checks expect <sysroot>/mingw to reach the target
    # tree when the sysroot is the toolchain prefix.
    ln -sfn "$triple" "$tc/mingw"

    if [[ ! -x "$tc/bin/$triple-ld" ]]; then
        mingw_configure_and_make "binutils $HAZOR_MINGW_BINUTILS_VERSION" \
            "$build/binutils" "$binutils_src/configure" \
            --target="$triple" --prefix="$tc" --with-sysroot="$tc" \
            --disable-multilib --disable-nls --disable-werror
    fi

    if [[ ! -f "$tc/$triple/include/windows.h" ]]; then
        mingw_configure_and_make "mingw-w64 headers $HAZOR_MINGW_W64_VERSION" \
            "$build/headers" "$mingw_src/mingw-w64-headers/configure" \
            --host="$triple" --prefix="$tc/$triple" \
            --with-default-msvcrt="$HAZOR_MINGW_CRT"
    fi

    # Stage 1 builds only the compiler: libstdc++ needs the CRT and winpthreads,
    # which do not exist yet.
    #
    # This stage cannot go through mingw_configure_and_make(): it runs configure
    # and make separately so that stage 1 can stop at all-gcc. The target flags
    # therefore have to be repeated here, and they matter more here than
    # anywhere else — libstdc++ is built by stage 2 below, and it is the runtime
    # library that carries the most __FILE__ assert strings into every binary.
    if [[ ! -x "$tc/bin/$triple-gcc" ]]; then
        mkdir -p "$build/gcc"
        log "MinGW toolchain: configuring gcc $HAZOR_MINGW_GCC_VERSION"
        ( cd "$build/gcc" && PATH="$tc/bin:$PATH" \
            CFLAGS_FOR_TARGET="$(mingw_target_cflags)" \
            CXXFLAGS_FOR_TARGET="$(mingw_target_cflags)" \
            "$gcc_src/configure" \
            --target="$triple" --prefix="$tc" --with-sysroot="$tc" \
            --enable-languages=c,c++ --enable-threads=posix \
            --disable-sjlj-exceptions --enable-fully-dynamic-string \
            --enable-libstdcxx-time=yes --enable-lto \
            --disable-multilib --disable-nls --disable-werror )
        log "MinGW toolchain: building gcc stage 1 (compiler only)"
        ( cd "$build/gcc" && PATH="$tc/bin:$PATH" make -j"$JOBS" all-gcc )
        ( cd "$build/gcc" && PATH="$tc/bin:$PATH" make install-gcc )
    fi

    if [[ ! -f "$tc/$triple/lib/libmsvcrt.a" ]]; then
        mingw_configure_and_make "mingw-w64 CRT" \
            "$build/crt" "$mingw_src/mingw-w64-crt/configure" \
            --host="$triple" --prefix="$tc/$triple" \
            --with-default-msvcrt="$HAZOR_MINGW_CRT" \
            --disable-lib32 --enable-lib64
    fi

    if [[ ! -f "$tc/$triple/lib/libwinpthread.a" ]]; then
        mingw_configure_and_make "winpthreads" \
            "$build/winpthreads" "$mingw_src/mingw-w64-libraries/winpthreads/configure" \
            --host="$triple" --prefix="$tc/$triple"
    fi

    # Repeating the flags on the make line is what makes a resumed run correct:
    # configure bakes them into the generated Makefile, so a build directory
    # left behind by an earlier run without them would otherwise rebuild
    # libstdc++ at the stock "-g -O2" and re-embed the local paths.
    log "MinGW toolchain: building gcc stage 2 (libgcc, libstdc++)"
    ( cd "$build/gcc" && PATH="$tc/bin:$PATH" make -j"$JOBS" \
        CFLAGS_FOR_TARGET="$(mingw_target_cflags)" \
        CXXFLAGS_FOR_TARGET="$(mingw_target_cflags)" )
    ( cd "$build/gcc" && PATH="$tc/bin:$PATH" make install )
    require_file "$tc/$triple/lib/libstdc++.a" "cross libstdc++"
    write_provenance "$tc" "MinGW-w64 cross toolchain" \
        "gcc $HAZOR_MINGW_GCC_VERSION / binutils $HAZOR_MINGW_BINUTILS_VERSION / mingw-w64 $HAZOR_MINGW_W64_VERSION" \
        "$HAZOR_MINGW_GCC_URL (posix threads, SEH, $HAZOR_MINGW_CRT)" \
        "$HAZOR_MINGW_GCC_SHA256"
    log "MinGW-w64 toolchain ready: $tc"
}

dependency_cmake_args() {
    local -n output_ref="$1"
    output_ref=(
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="__PREFIX__"
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        # Distributable libraries must not carry this machine's paths: assert
        # macros bake __FILE__ into the binary, and the artifact verification
        # rejects any reference to the local source tree.
        "-DCMAKE_C_FLAGS=-ffile-prefix-map=$ROOT_DIR=."
        "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$ROOT_DIR=."
    )
    if [[ "$TARGET" != "linux-x86_64" ]]; then
        output_ref+=("-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE")
    fi
}

prepare_zlib() {
    if find_zlib_library >/dev/null 2>&1 && [[ -f "$ZLIB_PREFIX/include/zlib.h" ]]; then
        return
    fi
    validate_cross_toolchain
    local source="$HAZOR_DOWNLOAD_DIR/sources/zlib-$HAZOR_ZLIB_COMMIT"
    local build="$THIRD_PARTY_DIR/build-deps/$TARGET/zlib-$HAZOR_ZLIB_VERSION"
    checkout_pinned_source "$HAZOR_ZLIB_GIT_URL" "$HAZOR_ZLIB_COMMIT" "$source"
    mkdir -p "$build"
    local -a args
    dependency_cmake_args args
    args[3]="-DCMAKE_INSTALL_PREFIX=$ZLIB_PREFIX"
    HAZOR_TARGET_ROOT="$TARGET_ROOT" HAZOR_TARGET_ARCH="$TARGET_ARCH" \
        cmake -S "$source" -B "$build" "${args[@]}" \
            -DZLIB_BUILD_SHARED=OFF \
            -DZLIB_BUILD_EXAMPLES=OFF
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
    write_provenance "$ZLIB_PREFIX" zlib "$HAZOR_ZLIB_VERSION" \
        "$HAZOR_ZLIB_GIT_URL@$HAZOR_ZLIB_COMMIT"
}

prepare_opencv() {
    if find_opencv_dir >/dev/null 2>&1; then
        return
    fi
    validate_cross_toolchain
    local source="$HAZOR_DOWNLOAD_DIR/sources/opencv-$HAZOR_OPENCV_COMMIT"
    local build="$THIRD_PARTY_DIR/build-deps/$TARGET/opencv-$HAZOR_OPENCV_VERSION"
    checkout_pinned_source "$HAZOR_OPENCV_GIT_URL" "$HAZOR_OPENCV_COMMIT" "$source"
    mkdir -p "$build"
    local -a args
    dependency_cmake_args args
    args[3]="-DCMAKE_INSTALL_PREFIX=$OPENCV_PREFIX"
    # cv::getBuildInformation() embeds the whole configuration dump — compiler
    # command line included — into libopencv_core, which puts this machine's
    # paths in a distributable library. Hazor never calls it, the pinned
    # provenance lives in .hazor-provenance, and OPENCV_SKIP_STATUS_FINALIZATION
    # below leaves this file as the single source of that string.
    #
    # It has to exist before configuring, not after: with status finalization
    # skipped OpenCV never generates it, and modules/core/CMakeLists.txt lists
    # it as a source, so the generate step fails outright without it.
    mkdir -p "$build/modules/core"
    printf '"Hazor Studio local build of OpenCV %s (commit %s)\\n"\n' \
        "$HAZOR_OPENCV_VERSION" "$HAZOR_OPENCV_COMMIT" \
        > "$build/modules/core/version_string.inc"
    HAZOR_TARGET_ROOT="$TARGET_ROOT" HAZOR_TARGET_ARCH="$TARGET_ARCH" \
        cmake -S "$source" -B "$build" "${args[@]}" \
            -DBUILD_LIST=core,imgproc,imgcodecs \
            -DBUILD_SHARED_LIBS=ON \
            -DCMAKE_SKIP_INSTALL_RPATH=ON \
            -DOPENCV_SKIP_STATUS_FINALIZATION=ON \
            -DBUILD_JPEG=ON \
            -DBUILD_PNG=ON \
            -DBUILD_TIFF=ON \
            -DBUILD_WEBP=ON \
            -DBUILD_ZLIB=OFF \
            -DBUILD_TESTS=OFF \
            -DBUILD_PERF_TESTS=OFF \
            -DBUILD_EXAMPLES=OFF \
            -DBUILD_opencv_apps=OFF \
            -DBUILD_JAVA=OFF \
            -DBUILD_opencv_python2=OFF \
            -DBUILD_opencv_python3=OFF \
            -DWITH_OPENEXR=OFF \
            -DWITH_JASPER=OFF \
            -DWITH_OPENJPEG=OFF \
            -DWITH_FFMPEG=OFF \
            -DWITH_GSTREAMER=OFF \
            -DWITH_GTK=OFF \
            -DWITH_QT=OFF \
            -DWITH_1394=OFF \
            -DWITH_V4L=OFF \
            "-DZLIB_ROOT=$ZLIB_PREFIX" \
            "-DZLIB_INCLUDE_DIR=$ZLIB_PREFIX/include" \
            "-DZLIB_LIBRARY=$(find_zlib_library)"
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
    write_provenance "$OPENCV_PREFIX" OpenCV "$HAZOR_OPENCV_VERSION" \
        "$HAZOR_OPENCV_GIT_URL@$HAZOR_OPENCV_COMMIT"
}

extract_runtime_archive() {
    local archive="$1"
    local parent="$2"
    mkdir -p "$parent"
    case "$archive" in
        *.tgz|*.tar.gz) tar -xzf "$archive" -C "$parent" ;;
        *.tar.xz) tar -xJf "$archive" -C "$parent" ;;
        *.tar.bz2) tar -xjf "$archive" -C "$parent" ;;
        *.zip) require_command unzip; unzip -q "$archive" -d "$parent" ;;
        *) die "unsupported dependency archive: $archive" ;;
    esac
}

# Download, verify and unpack a release tarball into a stable directory name,
# independent of whatever top-level directory the archive happens to carry.
#
# The cache file is named after the component rather than after the URL: GitHub
# tag tarballs are served as bare "3.2.0.tar.gz" or "v0.3.4.tar.gz", which says
# nothing about what is inside and shares one directory with every other
# component's downloads.
extract_pinned_source() {
    local url="$1"
    local sha256_value="$2"
    local source_dir="$3"
    local archive="$HAZOR_DOWNLOAD_DIR/rawengine/$(basename "$source_dir").${url##*.tar.}"
    archive="${archive%.*}.tar.${url##*.tar.}"
    [[ -d "$source_dir" ]] && return 0
    fetch_checked "$url" "$archive" "$sha256_value"
    local parent temp entries
    parent="$(dirname "$source_dir")"
    mkdir -p "$parent"
    temp="$(mktemp -d -p "$parent")"
    extract_runtime_archive "$archive" "$temp"
    mapfile -t entries < <(find "$temp" -mindepth 1 -maxdepth 1)
    [[ "${#entries[@]}" -eq 1 && -d "${entries[0]}" ]] \
        || die "expected exactly one top-level directory in $(basename "$archive")"
    mv -- "${entries[0]}" "$source_dir"
    rmdir -- "$temp"
}

prepare_onnxruntime() {
    case "$TARGET" in
        linux-x86_64)
            local archive="$HAZOR_DOWNLOAD_DIR/onnxruntime/$HAZOR_ORT_LINUX_PACKAGE.tgz"
            fetch_checked "$HAZOR_ORT_LINUX_URL" "$archive" "$HAZOR_ORT_LINUX_SHA256"
            if [[ ! -f "$ONNXRUNTIME_DIR/lib/libonnxruntime.so" ]]; then
                extract_runtime_archive "$archive" "$(dirname "$ONNXRUNTIME_DIR")"
            fi
            require_file "$ONNXRUNTIME_DIR/lib/libonnxruntime.so" "ONNX Runtime Linux library"
            write_provenance "$ONNXRUNTIME_DIR" "ONNX Runtime" "$HAZOR_ONNXRUNTIME_VERSION" \
                "$HAZOR_ORT_LINUX_URL" "$HAZOR_ORT_LINUX_SHA256"
            ;;
        windows-x86_64)
            # The release is MSVC-built and ships an MSVC import library the
            # MinGW linker cannot read. That costs nothing in ABI terms:
            # libonnxruntime exports four plain C entry points (OrtGetApiBase
            # and the three OrtSessionOptionsAppendExecutionProvider_* helpers),
            # and the whole Ort:: surface in onnxruntime_cxx_api.h is
            # header-only inline code dispatching through the OrtApi
            # function-pointer table — no C++ symbol crosses the boundary. A
            # GNU import library generated from the DLL's export table below
            # binds exactly those four symbols, so this target gets the same
            # AI feature set as Linux.
            local archive="$HAZOR_DOWNLOAD_DIR/onnxruntime/$HAZOR_ORT_WINDOWS_PACKAGE.zip"
            fetch_checked "$HAZOR_ORT_WINDOWS_URL" "$archive" "$HAZOR_ORT_WINDOWS_SHA256"
            if [[ ! -f "$ONNXRUNTIME_DIR/lib/onnxruntime.dll" ]]; then
                extract_runtime_archive "$archive" "$(dirname "$ONNXRUNTIME_DIR")"
            fi
            require_file "$ONNXRUNTIME_DIR/lib/onnxruntime.dll" "ONNX Runtime Windows DLL"
            if [[ ! -f "$ONNXRUNTIME_DIR/lib/libonnxruntime.dll.a" ]]; then
                generate_mingw_import_library \
                    "$ONNXRUNTIME_DIR/lib/onnxruntime.dll" \
                    "$ONNXRUNTIME_DIR/lib/libonnxruntime.dll.a"
                log "generated MinGW import library for onnxruntime.dll"
            fi
            write_provenance "$ONNXRUNTIME_DIR" "ONNX Runtime" "$HAZOR_ONNXRUNTIME_VERSION" \
                "$HAZOR_ORT_WINDOWS_URL" "$HAZOR_ORT_WINDOWS_SHA256"
            ;;
        macos-arm64)
            local archive="$HAZOR_DOWNLOAD_DIR/onnxruntime/$HAZOR_ORT_MACOS_ARM64_PACKAGE.tgz"
            fetch_checked "$HAZOR_ORT_MACOS_ARM64_URL" "$archive" "$HAZOR_ORT_MACOS_ARM64_SHA256"
            if [[ ! -d "$ONNXRUNTIME_DIR" ]]; then
                extract_runtime_archive "$archive" "$(dirname "$ONNXRUNTIME_DIR")"
            fi
            write_provenance "$ONNXRUNTIME_DIR" "ONNX Runtime" "$HAZOR_ONNXRUNTIME_VERSION" \
                "$HAZOR_ORT_MACOS_ARM64_URL" "$HAZOR_ORT_MACOS_ARM64_SHA256"
            ;;
        *)
            log "ONNX Runtime $TARGET: no compatible workflow artifact; AI inference remains disabled"
            ;;
    esac
}

# ── RawEngine dependencies ───────────────────────────────────────────────────
# src/rawengine/ needs nine libraries. Each one is installed into
# $RAWENGINE_DEPS_DIR/<name>/{include,lib} — the layout CMakeLists.txt's
# _rawengine_add_imported_lib() and the exiv2 find_package() read — so the app
# build consumes them with nothing but -DRAWENGINE_DEPS_DIR.
#
# linux-x86_64 adopts the pre-existing 3rdparty/rawengine/ tree through
# migrate_legacy_linux() instead of rebuilding it, so in practice this
# provisions cross targets. RAW development is a shipped feature, not an
# optional extra, so a cross target that cannot build these is a failure
# rather than a quietly reduced build.

rawengine_source_dir() {
    printf '%s/sources/%s\n' "$HAZOR_DOWNLOAD_DIR" "$1"
}

rawengine_installed() {
    local name="$1" header="$2" library="$3"
    [[ -f "$RAWENGINE_DEPS_DIR/$name/include/$header" \
        && -f "$RAWENGINE_DEPS_DIR/$name/lib/$library" ]]
}

# zlib's own CMake appends an "s" to the static library name on Windows, so the
# cross target installs libzs.a where every other platform installs libz.a.
# libpng, libtiff and exiv2 are all handed the path explicitly, so they each
# need the real one.
rawengine_zlib_library() {
    local candidate
    for candidate in libz.a libzs.a; do
        if [[ -f "$RAWENGINE_DEPS_DIR/zlib/lib/$candidate" ]]; then
            printf '%s\n' "$RAWENGINE_DEPS_DIR/zlib/lib/$candidate"
            return 0
        fi
    done
    return 1
}

# CMAKE_INSTALL_LIBDIR=lib is not cosmetic: GNUInstallDirs resolves to lib64 on
# some hosts, and _rawengine_add_imported_lib() only ever looks in <prefix>/lib.
rawengine_cmake_build() {
    local name="$1"
    local source="$2"
    shift 2
    local prefix="$RAWENGINE_DEPS_DIR/$name"
    local build="$THIRD_PARTY_DIR/build-deps/$TARGET/rawengine-$name"
    mkdir -p "$build"
    HAZOR_TARGET_ROOT="$TARGET_ROOT" HAZOR_TARGET_ARCH="$TARGET_ARCH" \
        cmake -S "$source" -B "$build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE" \
            "-DCMAKE_INSTALL_PREFIX=$prefix" \
            -DCMAKE_INSTALL_LIBDIR=lib \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            "-DCMAKE_C_FLAGS=-ffile-prefix-map=$ROOT_DIR=." \
            "-DCMAKE_CXX_FLAGS=-ffile-prefix-map=$ROOT_DIR=." \
            "$@"
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
}

rawengine_autotools_build() {
    local name="$1"
    local source="$2"
    shift 2
    local prefix="$RAWENGINE_DEPS_DIR/$name"
    local build="$THIRD_PARTY_DIR/build-deps/$TARGET/rawengine-$name"
    mkdir -p "$build"
    local -a host_args=()
    target_is_windows && host_args=("--host=$HAZOR_MINGW_TRIPLE")
    (
        cd "$build"
        CFLAGS="-ffile-prefix-map=$ROOT_DIR=." \
        CXXFLAGS="-ffile-prefix-map=$ROOT_DIR=." \
            "$source/configure" "${host_args[@]}" \
                "--prefix=$prefix" "--libdir=$prefix/lib" "$@"
        make -j"$JOBS"
        make install
    )
}

prepare_rawengine_zlib() {
    rawengine_zlib_library >/dev/null && [[ -f "$RAWENGINE_DEPS_DIR/zlib/include/zlib.h" ]] && return
    local source
    source="$(rawengine_source_dir "zlib-$HAZOR_ZLIB_COMMIT")"
    checkout_pinned_source "$HAZOR_ZLIB_GIT_URL" "$HAZOR_ZLIB_COMMIT" "$source"
    rawengine_cmake_build zlib "$source" \
        -DZLIB_BUILD_SHARED=OFF -DZLIB_BUILD_EXAMPLES=OFF
    write_provenance "$RAWENGINE_DEPS_DIR/zlib" zlib "$HAZOR_ZLIB_VERSION" \
        "$HAZOR_ZLIB_GIT_URL@$HAZOR_ZLIB_COMMIT"
}

prepare_rawengine_libjpeg_turbo() {
    rawengine_installed libjpeg-turbo jpeglib.h libjpeg.a && return
    local source
    source="$(rawengine_source_dir "libjpeg-turbo-$HAZOR_RAWENGINE_JPEG_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_JPEG_URL" "$HAZOR_RAWENGINE_JPEG_SHA256" "$source"
    # WITH_SIMD=OFF matches the native tree: the SIMD paths need nasm, which is
    # not a build dependency this project takes on.
    #
    # WITH_TOOLS=OFF (which also turns WITH_TESTS off) because RawEngine links
    # the library and nothing else: cjpeg/djpeg and the regression programs are
    # never shipped, and bmpsizetest calls fmemopen, which is POSIX and absent
    # from the MSVCRT runtime, so building them fails the Windows cross target
    # at link time over code no target uses.
    rawengine_cmake_build libjpeg-turbo "$source" \
        -DENABLE_SHARED=OFF -DWITH_SIMD=OFF -DWITH_TURBOJPEG=OFF -DWITH_JPEG8=ON \
        -DWITH_TOOLS=OFF -DWITH_TESTS=OFF
    write_provenance "$RAWENGINE_DEPS_DIR/libjpeg-turbo" libjpeg-turbo \
        "$HAZOR_RAWENGINE_JPEG_VERSION" "$HAZOR_RAWENGINE_JPEG_URL" "$HAZOR_RAWENGINE_JPEG_SHA256"
}

prepare_rawengine_libpng() {
    rawengine_installed libpng png.h libpng.a && return
    local source
    source="$(rawengine_source_dir "libpng-$HAZOR_RAWENGINE_PNG_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_PNG_URL" "$HAZOR_RAWENGINE_PNG_SHA256" "$source"
    rawengine_cmake_build libpng "$source" \
        "-DZLIB_ROOT=$RAWENGINE_DEPS_DIR/zlib" \
        "-DZLIB_INCLUDE_DIR=$RAWENGINE_DEPS_DIR/zlib/include" \
        "-DZLIB_LIBRARY=$(rawengine_zlib_library)" \
        -DPNG_SHARED=OFF -DPNG_TESTS=OFF -DPNG_TOOLS=OFF
    write_provenance "$RAWENGINE_DEPS_DIR/libpng" libpng \
        "$HAZOR_RAWENGINE_PNG_VERSION" "$HAZOR_RAWENGINE_PNG_URL" "$HAZOR_RAWENGINE_PNG_SHA256"
}

prepare_rawengine_libtiff() {
    rawengine_installed libtiff tiff.h libtiff.a && return
    local source
    source="$(rawengine_source_dir "tiff-$HAZOR_RAWENGINE_TIFF_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_TIFF_URL" "$HAZOR_RAWENGINE_TIFF_SHA256" "$source"
    # rtengine reads TIFF with JPEG and DEFLATE only; every other codec would
    # be one more library to pin for no gain.
    rawengine_cmake_build libtiff "$source" \
        "-DZLIB_ROOT=$RAWENGINE_DEPS_DIR/zlib" \
        "-DZLIB_INCLUDE_DIR=$RAWENGINE_DEPS_DIR/zlib/include" \
        "-DZLIB_LIBRARY=$(rawengine_zlib_library)" \
        "-DJPEG_INCLUDE_DIR=$RAWENGINE_DEPS_DIR/libjpeg-turbo/include" \
        "-DJPEG_LIBRARY=$RAWENGINE_DEPS_DIR/libjpeg-turbo/lib/libjpeg.a" \
        -DBUILD_SHARED_LIBS=OFF -Dtiff-tools=OFF -Dtiff-tests=OFF \
        -Dtiff-docs=OFF -Dtiff-contrib=OFF \
        -Djbig=OFF -Dlzma=OFF -Dzstd=OFF -Dwebp=OFF -Dlibdeflate=OFF
    write_provenance "$RAWENGINE_DEPS_DIR/libtiff" libtiff \
        "$HAZOR_RAWENGINE_TIFF_VERSION" "$HAZOR_RAWENGINE_TIFF_URL" "$HAZOR_RAWENGINE_TIFF_SHA256"
}

prepare_rawengine_expat() {
    rawengine_installed expat expat.h libexpat.a && return
    local source
    source="$(rawengine_source_dir "expat-$HAZOR_RAWENGINE_EXPAT_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_EXPAT_URL" "$HAZOR_RAWENGINE_EXPAT_SHA256" "$source"
    rawengine_cmake_build expat "$source" \
        -DEXPAT_SHARED_LIBS=OFF -DEXPAT_BUILD_TESTS=OFF \
        -DEXPAT_BUILD_EXAMPLES=OFF -DEXPAT_BUILD_TOOLS=OFF -DEXPAT_BUILD_DOCS=OFF
    write_provenance "$RAWENGINE_DEPS_DIR/expat" expat \
        "$HAZOR_RAWENGINE_EXPAT_VERSION" "$HAZOR_RAWENGINE_EXPAT_URL" "$HAZOR_RAWENGINE_EXPAT_SHA256"
}

prepare_rawengine_lcms2() {
    rawengine_installed lcms2 lcms2.h liblcms2.a && return
    local source
    source="$(rawengine_source_dir "lcms2-$HAZOR_RAWENGINE_LCMS2_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_LCMS2_URL" "$HAZOR_RAWENGINE_LCMS2_SHA256" "$source"
    rawengine_autotools_build lcms2 "$source" \
        --disable-shared --enable-static --with-pic
    write_provenance "$RAWENGINE_DEPS_DIR/lcms2" lcms2 \
        "$HAZOR_RAWENGINE_LCMS2_VERSION" "$HAZOR_RAWENGINE_LCMS2_URL" "$HAZOR_RAWENGINE_LCMS2_SHA256"
}

prepare_rawengine_fftw3f() {
    rawengine_installed fftw3f fftw3.h libfftw3f.a && return
    local source
    source="$(rawengine_source_dir "fftw-$HAZOR_RAWENGINE_FFTW_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_FFTW_URL" "$HAZOR_RAWENGINE_FFTW_SHA256" "$source"
    # CMakeLists.txt requires libfftw3f_threads alongside libfftw3f.
    rawengine_autotools_build fftw3f "$source" \
        --enable-float --enable-threads --disable-shared --enable-static --with-pic
    require_file "$RAWENGINE_DEPS_DIR/fftw3f/lib/libfftw3f_threads.a" "fftw3f threads library"
    write_provenance "$RAWENGINE_DEPS_DIR/fftw3f" fftw3f \
        "$HAZOR_RAWENGINE_FFTW_VERSION" "$HAZOR_RAWENGINE_FFTW_URL" "$HAZOR_RAWENGINE_FFTW_SHA256"
}

prepare_rawengine_exiv2() {
    [[ -f "$RAWENGINE_DEPS_DIR/exiv2/lib/cmake/exiv2/exiv2Config.cmake" ]] && return
    local source
    source="$(rawengine_source_dir "exiv2-$HAZOR_RAWENGINE_EXIV2_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_EXIV2_URL" "$HAZOR_RAWENGINE_EXIV2_SHA256" "$source"
    # Shared, matching the prebuilt release the native tree uses: a static
    # exiv2 makes its exiv2Config.cmake re-resolve zlib and expat as link
    # dependencies, which would pull the host's copies in beside the vendored
    # ones. brotli and inih default ON in 0.28 and would each be another
    # pinned dependency, for BMFF and ini support rtengine's metadata reader
    # does not use.
    rawengine_cmake_build exiv2 "$source" \
        -DBUILD_SHARED_LIBS=ON \
        -DEXIV2_ENABLE_BROTLI=OFF -DEXIV2_ENABLE_INIH=OFF \
        -DEXIV2_ENABLE_XMP=ON -DEXIV2_ENABLE_PNG=ON \
        -DEXIV2_BUILD_SAMPLES=OFF -DEXIV2_BUILD_EXIV2_COMMAND=OFF \
        -DEXIV2_BUILD_UNIT_TESTS=OFF \
        "-DZLIB_ROOT=$RAWENGINE_DEPS_DIR/zlib" \
        "-DZLIB_INCLUDE_DIR=$RAWENGINE_DEPS_DIR/zlib/include" \
        "-DZLIB_LIBRARY=$(rawengine_zlib_library)" \
        "-DEXPAT_INCLUDE_DIR=$RAWENGINE_DEPS_DIR/expat/include" \
        "-DEXPAT_LIBRARY=$RAWENGINE_DEPS_DIR/expat/lib/libexpat.a"
    write_provenance "$RAWENGINE_DEPS_DIR/exiv2" exiv2 \
        "$HAZOR_RAWENGINE_EXIV2_VERSION" "$HAZOR_RAWENGINE_EXIV2_URL" "$HAZOR_RAWENGINE_EXIV2_SHA256"
}

# ── glib-2.0 for lensfun, Windows only ───────────────────────────────────────
# lensfun links glib-2.0 unconditionally. Native targets resolve it from the
# platform — Qt 6 links it too, so a second private copy in one process would
# be the defect rather than an isolation win — but Windows has no platform
# glib, so the cross target builds a minimal one. NLS is disabled so libintl
# does not become a fourth dependency; lensfun never asks glib to translate.

ensure_meson() {
    local venv="$HAZOR_HOST_ROOT/meson/$HAZOR_MESON_VERSION"
    local meson="$venv/bin/meson"
    if [[ ! -x "$meson" ]]; then
        require_command python3
        python3 -m venv "$venv"
        # Used through command substitution: installer output goes to stderr so
        # stdout carries exactly the executable path.
        "$venv/bin/python" -m pip install --disable-pip-version-check \
            "meson==$HAZOR_MESON_VERSION" >&2
    fi
    printf '%s\n' "$meson"
}

write_meson_cross_file() {
    local path="$1"
    mkdir -p "$(dirname "$path")"
    cat > "$path" <<EOF
[binaries]
c = '$HAZOR_MINGW_TRIPLE-gcc'
cpp = '$HAZOR_MINGW_TRIPLE-g++'
ar = '$HAZOR_MINGW_TRIPLE-ar'
strip = '$HAZOR_MINGW_TRIPLE-strip'
windres = '$HAZOR_MINGW_TRIPLE-windres'
pkg-config = 'pkg-config'

[host_machine]
system = 'windows'
cpu_family = 'x86_64'
cpu = 'x86_64'
endian = 'little'

[properties]
pkg_config_libdir = '$RAWENGINE_DEPS_DIR/glib/lib/pkgconfig'
EOF
}

prepare_rawengine_libffi() {
    [[ -f "$RAWENGINE_DEPS_DIR/glib/lib/libffi.a" ]] && return
    local source
    source="$(rawengine_source_dir "libffi-$HAZOR_RAWENGINE_LIBFFI_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_LIBFFI_URL" "$HAZOR_RAWENGINE_LIBFFI_SHA256" "$source"
    # libffi, pcre2 and glib share one prefix: glib's pkg-config files have to
    # find their own dependencies, and a single prefix is what meson's
    # pkg_config_libdir above points at.
    rawengine_autotools_build glib "$source" \
        --disable-shared --enable-static --with-pic --disable-docs
}

prepare_rawengine_pcre2() {
    [[ -f "$RAWENGINE_DEPS_DIR/glib/lib/libpcre2-8.a" ]] && return
    local source
    source="$(rawengine_source_dir "pcre2-$HAZOR_RAWENGINE_PCRE2_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_PCRE2_URL" "$HAZOR_RAWENGINE_PCRE2_SHA256" "$source"
    local prefix="$RAWENGINE_DEPS_DIR/glib"
    local build="$THIRD_PARTY_DIR/build-deps/$TARGET/rawengine-pcre2"
    mkdir -p "$build"
    HAZOR_TARGET_ROOT="$TARGET_ROOT" HAZOR_TARGET_ARCH="$TARGET_ARCH" \
        cmake -S "$source" -B "$build" -G Ninja \
            -DCMAKE_BUILD_TYPE=Release \
            "-DCMAKE_TOOLCHAIN_FILE=$TOOLCHAIN_FILE" \
            "-DCMAKE_INSTALL_PREFIX=$prefix" \
            -DCMAKE_INSTALL_LIBDIR=lib \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DBUILD_SHARED_LIBS=OFF \
            -DPCRE2_BUILD_PCRE2GREP=OFF -DPCRE2_BUILD_TESTS=OFF
    cmake --build "$build" --parallel "$JOBS"
    cmake --install "$build"
}

prepare_rawengine_glib() {
    [[ -f "$RAWENGINE_DEPS_DIR/glib/lib/libglib-2.0.a" ]] && return
    target_is_windows || return 0
    prepare_rawengine_libffi
    prepare_rawengine_pcre2
    local source meson cross_file
    source="$(rawengine_source_dir "glib-$HAZOR_RAWENGINE_GLIB_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_GLIB_URL" "$HAZOR_RAWENGINE_GLIB_SHA256" "$source"
    meson="$(ensure_meson)"
    cross_file="$THIRD_PARTY_DIR/build-deps/$TARGET/meson-mingw-cross.ini"
    write_meson_cross_file "$cross_file"
    local prefix="$RAWENGINE_DEPS_DIR/glib"
    local build="$THIRD_PARTY_DIR/build-deps/$TARGET/rawengine-glib"
    # glib compiles its install prefix into the library: GLIB_LOCALE_DIR (and
    # GLIB_LOCALSTATEDIR/GLIB_RUNSTATEDIR) are derived from it in meson.build
    # and baked into config.h as string literals, so -ffile-prefix-map cannot
    # touch them and every binary linking libglib-2.0.a ends up carrying this
    # machine's directory layout. Building against a fictitious prefix and
    # staging the install through DESTDIR keeps those literals free of it.
    #
    # The value is otherwise inert on Windows: glib_get_locale_dir() in
    # glib/ggettext.c reads back only the last two components of
    # GLIB_LOCALE_DIR ("/share/locale") and resolves the rest from the loaded
    # module at runtime. It does need those two components, though — the
    # lookup walks backwards over two slashes and would run off the start of a
    # shorter string, so the prefix cannot simply be made relative or empty.
    local neutral_prefix="/hazor/rawengine/glib"
    local stage="$build/stage"
    rm -rf -- "$build"
    PKG_CONFIG_LIBDIR="$prefix/lib/pkgconfig" \
        "$meson" setup "$build" "$source" \
            --cross-file "$cross_file" \
            --prefix "$neutral_prefix" --libdir lib \
            --buildtype release --default-library static \
            --localedir share/locale \
            -Dnls=disabled -Dtests=false -Dglib_debug=disabled \
            -Dintrospection=disabled -Dman-pages=disabled -Ddocumentation=false
    "$meson" compile -C "$build" -j "$JOBS"
    DESTDIR="$stage" "$meson" install -C "$build"

    # Merge rather than replace: prepare_rawengine_libffi and
    # prepare_rawengine_pcre2 above share this one prefix (glib's pkg-config
    # files have to find them) and their artifacts must survive.
    require_dir "$stage$neutral_prefix/lib" "staged glib install"
    mkdir -p "$prefix"
    cp -a "$stage$neutral_prefix/." "$prefix/"

    # The staged .pc files describe the fictitious prefix. The top-level
    # CMakeLists finds glib with pkg_check_modules() and links
    # GLIB2_LINK_LIBRARIES, the absolute paths pkg-config resolves from these
    # files, so they have to name the real tree.
    grep -rl -- "$neutral_prefix" "$prefix/lib/pkgconfig" 2>/dev/null \
        | xargs -r sed -i -- "s|$neutral_prefix|$prefix|g"
    if grep -rq -- "$neutral_prefix" "$prefix/lib/pkgconfig" 2>/dev/null; then
        die "glib pkg-config files still reference $neutral_prefix"
    fi
    write_provenance "$prefix" glib "$HAZOR_RAWENGINE_GLIB_VERSION" \
        "$HAZOR_RAWENGINE_GLIB_URL" "$HAZOR_RAWENGINE_GLIB_SHA256"
}

prepare_rawengine_lensfun() {
    rawengine_installed lensfun lensfun/lensfun.h liblensfun.a && return
    prepare_rawengine_glib
    local source
    source="$(rawengine_source_dir "lensfun-$HAZOR_RAWENGINE_LENSFUN_VERSION")"
    extract_pinned_source "$HAZOR_RAWENGINE_LENSFUN_URL" "$HAZOR_RAWENGINE_LENSFUN_SHA256" "$source"
    local -a glib_args=()
    if target_is_windows; then
        glib_args=(
            "-DGLIB2_INCLUDE_DIRS=$RAWENGINE_DEPS_DIR/glib/include/glib-2.0;$RAWENGINE_DEPS_DIR/glib/lib/glib-2.0/include"
            "-DGLIB2_LIBRARIES=$RAWENGINE_DEPS_DIR/glib/lib/libglib-2.0.a"
        )
    fi
    # lensfun 0.3.4 predates CMake 3.5; 3.31 accepts it under an explicit
    # policy floor, 4.x will not.
    # apps/CMakeLists.txt ignores INSTALL_PYTHON_MODULE and installs the helper
    # module whenever it finds python3, by shelling out to `setup.py install`
    # against the target prefix — which is a Windows prefix here, and fails.
    # Blanking the FIND_PROGRAM cache entry is what actually skips that branch.
    #
    # On Windows lensfun defaults CMAKE_INSTALL_DATAROOTDIR to $ALLUSERSPROFILE,
    # which is empty when cross-compiling from Linux: the camera database would
    # install to an absolute "/lensfun/version_1". "share" is what it uses on
    # every other platform and keeps the data inside the target prefix.
    #
    # PLATFORM_WINDOWS is declared by config.h.in.cmake but never set by
    # lensfun's own CMakeLists, so a MinGW cross build silently takes the *nix
    # branch and bakes CMAKE_INSTALL_FULL_DATAROOTDIR — this machine's prefix —
    # into CONF_DATADIR, which then ships inside Hazor.exe and only resolves
    # here. Setting it picks the relative "share/lensfun" the Windows branch
    # uses instead. Safe for the ABI: BUILD_STATIC defines CONF_LENSFUN_STATIC,
    # which short-circuits lensfun.h's dllimport/dllexport decoration before it
    # ever consults PLATFORM_WINDOWS. The relative path is not what finds the
    # database at runtime either — that is lensfunDbBundleDirectory, set from
    # AppPaths::lensfunDatabaseDir() — because lensfun's module-relative lookup
    # searches for a liblensfun.dll that a static build never produces.
    local -a platform_args=()
    target_is_windows && platform_args=(-DPLATFORM_WINDOWS=ON)
    rawengine_cmake_build lensfun "$source" \
        -DCMAKE_INSTALL_DATAROOTDIR=share \
        "${platform_args[@]}" \
        -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
        -DBUILD_STATIC=ON -DBUILD_TESTS=OFF -DBUILD_DOC=OFF \
        -DINSTALL_PYTHON_MODULE=OFF -DBUILD_LENSTOOL=OFF \
        -DPYTHON=PYTHON-NOTFOUND \
        "${glib_args[@]}"
    write_provenance "$RAWENGINE_DEPS_DIR/lensfun" lensfun \
        "$HAZOR_RAWENGINE_LENSFUN_VERSION" "$HAZOR_RAWENGINE_LENSFUN_URL" "$HAZOR_RAWENGINE_LENSFUN_SHA256"
}

# rtdata (camera matrices, DCP/ICC profiles, processing profiles) is plain data
# read at runtime, identical on every platform, and it is vendored alongside
# the engine source rather than published as a release of its own. Cross
# targets therefore link the native tree's copy instead of refetching it.
link_shared_rawengine_rtdata() {
    [[ -e "$RAWENGINE_DEPS_DIR/rtdata" ]] && return 0
    link_directory "$ROOT_DIR/3rdparty/rawengine/rtdata" "$RAWENGINE_DEPS_DIR/rtdata" || true
}

prepare_rawengine() {
    # The native tree was adopted wholesale by migrate_legacy_linux().
    if [[ -L "$RAWENGINE_DEPS_DIR" ]]; then
        log "RawEngine dependencies: reusing the adopted native tree"
        return
    fi
    target_is_macos && { log "RawEngine dependencies: macOS provisioning not implemented"; return; }
    validate_cross_toolchain
    mkdir -p "$RAWENGINE_DEPS_DIR"
    prepare_rawengine_zlib
    prepare_rawengine_libjpeg_turbo
    prepare_rawengine_libpng
    prepare_rawengine_libtiff
    prepare_rawengine_expat
    prepare_rawengine_lcms2
    prepare_rawengine_fftw3f
    prepare_rawengine_exiv2
    prepare_rawengine_lensfun
    link_shared_rawengine_rtdata
    log "RawEngine dependencies ready under $RAWENGINE_DEPS_DIR"
}

realesrgan_weights_present() {
    [[ -d "$REALESRGAN_MODELS_DIR" ]] || return 1
    find "$REALESRGAN_MODELS_DIR/" -maxdepth 1 -type f -name '*.bin' -print -quit | grep -q .
}

# RealEsrganProcessBackend expects models/ next to the executable, and so does
# the package layout. The weights themselves stay shared between targets; the
# per-target prefix only points at them.
link_shared_realesrgan_weights() {
    [[ ! -e "$REALESRGAN_DIR/models" ]] || return 0
    link_directory "$REALESRGAN_MODELS_DIR" "$REALESRGAN_DIR/models" || true
}

prepare_realesrgan() {
    local url sha archive_name executable
    case "$TARGET_OS" in
        linux)
            url="$HAZOR_REALESRGAN_LINUX_URL"
            sha="$HAZOR_REALESRGAN_LINUX_SHA256"
            archive_name="$HAZOR_REALESRGAN_LINUX_ARCHIVE"
            executable="realesrgan-ncnn-vulkan"
            ;;
        windows)
            url="$HAZOR_REALESRGAN_WINDOWS_URL"
            sha="$HAZOR_REALESRGAN_WINDOWS_SHA256"
            archive_name="$HAZOR_REALESRGAN_WINDOWS_ARCHIVE"
            executable="realesrgan-ncnn-vulkan.exe"
            ;;
        macos)
            url="$HAZOR_REALESRGAN_MACOS_URL"
            sha="$HAZOR_REALESRGAN_MACOS_SHA256"
            archive_name="$HAZOR_REALESRGAN_MACOS_ARCHIVE"
            executable="realesrgan-ncnn-vulkan"
            ;;
    esac
    if [[ ! -f "$REALESRGAN_DIR/$executable" ]] || ! realesrgan_weights_present; then
        local archive="$HAZOR_DOWNLOAD_DIR/realesrgan/$archive_name"
        local temp weight found
        fetch_checked "$url" "$archive" "$sha"
        temp="$(mktemp -d)"
        unzip -q "$archive" -d "$temp"
        mkdir -p "$REALESRGAN_DIR" "$REALESRGAN_MODELS_DIR"
        found="$(find "$temp" -type f -name "$executable" | head -n 1)"
        [[ -n "$found" ]] || die "$executable was not found in $archive"
        cp -f "$found" "$REALESRGAN_DIR/$executable"
        chmod +x "$REALESRGAN_DIR/$executable"
        # Weights are shared: copy only what the shared directory is missing, so
        # a second target reuses the files the first one extracted.
        while IFS= read -r -d '' weight; do
            [[ -f "$REALESRGAN_MODELS_DIR/$(basename "$weight")" ]] \
                || cp -f "$weight" "$REALESRGAN_MODELS_DIR/"
        done < <(find "$temp" -type f \( -name '*.bin' -o -name '*.param' \) -print0)
        find "$temp" -type f -name '*.dll' -exec cp -f {} "$REALESRGAN_DIR/" \;
        safe_remove_tree "$temp" "${TMPDIR:-/tmp}"
        write_provenance "$REALESRGAN_DIR" RealESRGAN "$HAZOR_REALESRGAN_VERSION" "$url" "$sha"
    fi
    link_shared_realesrgan_weights
}

# AiModelRegistry discovers bundled models by scanning for model.json under the
# whole models tree, so a model that is already installed under any folder name
# must not be fetched again: that would both re-download the weights and show
# the model twice in the registry.
installed_model_dir() {
    local file_name="$1"
    local found
    [[ -d "$ONNX_MODELS_DIR" ]] || return 1
    found="$(find "$ONNX_MODELS_DIR/" -type f -name "$file_name" -print -quit)"
    [[ -n "$found" ]] || return 1
    dirname "$found"
}

prepare_models() {
    require_command unzip
    local sam_dir="$ONNX_MODELS_DIR/segmenters/sam-mobile"
    local sam_archive="$HAZOR_DOWNLOAD_DIR/models/$HAZOR_SAM_ARCHIVE"
    local installed
    if installed="$(installed_model_dir mobile_sam.encoder.onnx)"; then
        log "reusing bundled Mobile SAM already installed in $installed"
    elif [[ ! -f "$sam_dir/model.json" ]]; then
        fetch_checked "$HAZOR_SAM_URL" "$sam_archive" "$HAZOR_SAM_SHA256"
        local temp encoder decoder
        temp="$(mktemp -d)"
        unzip -q "$sam_archive" -d "$temp"
        encoder="$(find "$temp" -type f -name '*encoder*.onnx' | head -n 1)"
        decoder="$(find "$temp" -type f -name '*decoder*.onnx' | head -n 1)"
        [[ -n "$encoder" && -n "$decoder" ]] || die "SAM archive does not contain encoder and decoder ONNX files"
        mkdir -p "$sam_dir"
        cp -f "$encoder" "$sam_dir/$(basename "$encoder")"
        cp -f "$decoder" "$sam_dir/$(basename "$decoder")"
        find "$temp" -type f \( -iname 'LICENSE*' -o -iname 'NOTICE*' -o -iname 'README*' \) \
            -exec cp -f {} "$sam_dir/" \;
        printf '{\n  "schema_version": 1,\n  "id": "mobile-sam",\n  "type": "segment_anything",\n  "backend": "onnxruntime",\n  "task": "segmentation",\n  "family": "sam",\n  "variant": "mobile_sam",\n  "display_name": "Segment Anything Mobile",\n  "description": "Lightweight Segment Anything model used for object selection by point prompt or bounding box.",\n  "quality": "fast",\n  "default": true,\n  "files": {"encoder": "%s", "decoder": "%s"},\n  "input": {"input_size": 1024, "max_width": 1024, "max_height": 682, "color_space": "rgb"},\n  "capabilities": ["point_prompt", "box_prompt", "object_selection", "selection_mask"],\n  "license": {"name": "Apache-2.0", "file": "LICENSE", "notice": "NOTICE.md"},\n  "source": {"name": "vietanhdev/segment-anything-onnx-models", "url": "https://huggingface.co/vietanhdev/segment-anything-onnx-models"}\n}\n' \
            "$(basename "$encoder")" "$(basename "$decoder")" > "$sam_dir/model.json"
        if [[ ! -f "$sam_dir/NOTICE.md" ]]; then
            printf '# Segment Anything Mobile\n\nVersion: %s\nSource: %s\nLicense: Apache-2.0\n' \
                "$HAZOR_SAM_VERSION" "$HAZOR_SAM_URL" > "$sam_dir/NOTICE.md"
        fi
        safe_remove_tree "$temp" "${TMPDIR:-/tmp}"
        write_provenance "$sam_dir" "Mobile SAM" "$HAZOR_SAM_VERSION" "$HAZOR_SAM_URL" "$HAZOR_SAM_SHA256"
    fi

    local birefnet_dir="$ONNX_MODELS_DIR/matting/BiRefNet_lite-ONNX"
    local model_cache="$HAZOR_DOWNLOAD_DIR/models/$HAZOR_BIREFNET_CACHE_FILE"
    if [[ ! -f "$birefnet_dir/$HAZOR_BIREFNET_FILE" ]]; then
        # Adopt a blob cached under the previous generic name rather than
        # downloading the same revision again.
        if [[ ! -f "$model_cache" && -f "$HAZOR_DOWNLOAD_DIR/models/$HAZOR_BIREFNET_FILE" ]]; then
            mv -f "$HAZOR_DOWNLOAD_DIR/models/$HAZOR_BIREFNET_FILE" "$model_cache"
        fi
        fetch_checked "$HAZOR_BIREFNET_URL" "$model_cache" "$HAZOR_BIREFNET_SHA256"
        mkdir -p "$birefnet_dir"
        # Hard link so the cached archive and the installed model are one file
        # on disk; fall back to a copy across filesystems.
        cp -f --link "$model_cache" "$birefnet_dir/$HAZOR_BIREFNET_FILE" \
            || cp -f "$model_cache" "$birefnet_dir/$HAZOR_BIREFNET_FILE"
        printf '{\n  "schema_version": 1,\n  "id": "birefnet-matting-lite",\n  "type": "birefnet",\n  "backend": "onnxruntime",\n  "task": "matting",\n  "family": "birefnet",\n  "variant": "lite",\n  "display_name": "BiRefNet Matting Lite",\n  "description": "Lightweight BiRefNet model for foreground refinement and alpha mattes.",\n  "quality": "medium",\n  "default": true,\n  "files": {"model": "%s"},\n  "input": {"color_space": "rgb"},\n  "capabilities": ["alpha_matte", "refine_mask", "soft_edges", "foreground_extraction"],\n  "license": {"name": "MIT", "notice": "NOTICE.md"},\n  "source": {"name": "onnx-community/BiRefNet_lite-ONNX", "url": "https://huggingface.co/onnx-community/BiRefNet_lite-ONNX"}\n}\n' \
            "$HAZOR_BIREFNET_FILE" > "$birefnet_dir/model.json"
        printf '# BiRefNet Matting Lite\n\nVersion: %s\nSource: %s\nLicense: MIT\n' \
            "$HAZOR_BIREFNET_VERSION" "$HAZOR_BIREFNET_URL" > "$birefnet_dir/NOTICE.md"
        write_provenance "$birefnet_dir" "BiRefNet Lite" "$HAZOR_BIREFNET_VERSION" \
            "$HAZOR_BIREFNET_URL" "$HAZOR_BIREFNET_SHA256"
    fi
}

# CMake globs src/resources/shapes/**.svg into the Hazor resources and
# ShapeIconLibrary walks the whole :/shapes tree, so an icon set that predates
# this script must not be shadowed by a second one: the two would be compiled
# together and every shared icon would show up twice in the shape library.
foreign_shape_svgs_present() {
    local base="$ROOT_DIR/src/resources/shapes"
    [[ -d "$base" ]] || return 1
    find "$base" -mindepth 1 -name '*.svg' -not -path "$base/material/*" -print -quit | grep -q .
}

prepare_material_icons() {
    local marker="$ROOT_DIR/src/resources/shapes/.hazor-material-icons-$HAZOR_MATERIAL_ICONS_VERSION"
    [[ -f "$marker" ]] && return

    if foreign_shape_svgs_present; then
        log "shape SVGs that this script did not install are already present under src/resources/shapes"
        log "keeping them: Material Design Icons $HAZOR_MATERIAL_ICONS_VERSION was NOT installed, because both sets would be compiled into the same resource tree"
        log "move those folders out of src/resources/shapes to switch to the pinned set used by the workflows"
        return
    fi

    require_command unzip
    local archive="$HAZOR_DOWNLOAD_DIR/material-icons/$HAZOR_MATERIAL_ICONS_ARCHIVE"
    local destination="$ROOT_DIR/src/resources/shapes/material"
    local temp root production category
    fetch_checked "$HAZOR_MATERIAL_ICONS_URL" "$archive" "$HAZOR_MATERIAL_ICONS_SHA256"
    temp="$(mktemp -d)"
    unzip -q "$archive" -d "$temp"
    root="$(find "$temp" -mindepth 1 -maxdepth 1 -type d -name 'material-design-icons-*' -print -quit)"
    [[ -n "$root" ]] || die "material icon archive has no material-design-icons-* root"

    if [[ -e "$destination" ]]; then
        safe_remove_tree "$destination" "$ROOT_DIR"
    fi
    mkdir -p "$destination"
    while IFS= read -r -d '' production; do
        category="$(basename "$(dirname "$(dirname "$production")")")"
        mkdir -p "$destination/$category"
        find "$production" -maxdepth 1 -type f -name '*.svg' \
            -exec cp -f {} "$destination/$category/" \;
    done < <(find "$root" -type d -path '*/svg/production' -print0)
    find "$destination" -type f -name '*.svg' -print -quit | grep -q . \
        || die "material icon archive did not yield production SVG files"
    # Apache-2.0 license / notice files live at the archive root; the reference
    # workflow copies them next to the icons and packaging looks for them there.
    find "$root" -maxdepth 1 -type f \
        \( -iname 'LICENSE*' -o -iname 'NOTICE*' -o -iname 'README*' \) \
        -exec cp -f {} "$destination/" \;
    safe_remove_tree "$temp" "${TMPDIR:-/tmp}"
    printf 'version=%s\nsource=%s\nsha256=%s\n' \
        "$HAZOR_MATERIAL_ICONS_VERSION" "$HAZOR_MATERIAL_ICONS_URL" \
        "$HAZOR_MATERIAL_ICONS_SHA256" > "$marker"
    log "Material Design Icons $HAZOR_MATERIAL_ICONS_VERSION prepared"
}

prepare_appimage_tools() {
    # "return 0", not a bare "return": a bare one propagates the failed test's
    # status, and under `set -e` that aborts the whole prepare run on every
    # non-Linux target right at the end of a successful provisioning.
    [[ "$TARGET" == "linux-x86_64" ]] || return 0
    local tools="$HAZOR_HOST_ROOT/appimage"
    fetch_checked "$HAZOR_LINUXDEPLOY_URL" "$tools/linuxdeploy-x86_64.AppImage" "$HAZOR_LINUXDEPLOY_SHA256"
    fetch_checked "$HAZOR_LINUXDEPLOY_QT_URL" "$tools/linuxdeploy-plugin-qt-x86_64.AppImage" "$HAZOR_LINUXDEPLOY_QT_SHA256"
    fetch_checked "$HAZOR_APPIMAGETOOL_URL" "$tools/appimagetool-x86_64.AppImage" "$HAZOR_APPIMAGETOOL_SHA256"
    fetch_checked "$HAZOR_APPIMAGE_RUNTIME_URL" "$tools/runtime-x86_64" "$HAZOR_APPIMAGE_RUNTIME_SHA256"
    chmod +x "$tools"/*.AppImage
}

prepare_dependencies() {
    mkdir -p "$TARGET_ROOT" "$HAZOR_DOWNLOAD_DIR" "$HAZOR_COMMON_ROOT"
    migrate_shared_assets
    migrate_legacy_linux
    if [[ "${MIGRATE_ONLY:-0}" == "1" ]]; then
        log "legacy dependency migration complete"
        return
    fi
    # Fail before the first download: an unusable cross toolchain makes the
    # target Qt, OpenCV and zlib useless, and the target Qt alone is ~1 GB.
    prepare_mingw_toolchain
    validate_cross_toolchain
    prepare_qt_host
    prepare_qt_target
    prepare_zlib
    prepare_opencv
    prepare_onnxruntime
    prepare_rawengine
    prepare_realesrgan
    prepare_models
    publish_shared_assets_for_dev_runs
    prepare_material_icons
    prepare_appimage_tools
    log "dependencies prepared for $TARGET"
}

prepare_dependencies
