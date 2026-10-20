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

migrate_legacy_linux() {
    [[ "$TARGET" == "linux-x86_64" ]] || return 0
    link_directory "$ROOT_DIR/3rdparty/Qt/$HAZOR_QT_VERSION/gcc_64" "$QT_HOST_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/Qt/$HAZOR_QT_VERSION/gcc_64" "$QT_TARGET_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/onnx/runtime/$HAZOR_ORT_LINUX_PACKAGE" "$ONNXRUNTIME_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/realesrgan" "$REALESRGAN_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/rawengine" "$RAWENGINE_DEPS_DIR" || true
    link_directory "$ROOT_DIR/3rdparty/rawengine/zlib" "$ZLIB_PREFIX" || true

    if [[ -d "$ROOT_DIR/3rdparty/onnx/models" && ! -e "$ONNX_MODELS_DIR" ]]; then
        link_directory "$ROOT_DIR/3rdparty/onnx/models" "$ONNX_MODELS_DIR" || true
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
    "$aqt" version | grep -F "$HAZOR_AQT_VERSION" >/dev/null \
        || die "aqt version mismatch under $venv"
    printf '%s\n' "$aqt"
}

prepare_qt_host() {
    if [[ -x "$QT_HOST_DIR/bin/moc" && -f "$QT_HOST_DIR/lib/cmake/Qt6/Qt6Config.cmake" ]]; then
        return
    fi
    local aqt
    aqt="$(ensure_aqt)"
    mkdir -p "$HAZOR_HOST_ROOT/qt"
    "$aqt" install-qt linux desktop "$HAZOR_QT_VERSION" linux_gcc_64 \
        --outputdir "$HAZOR_HOST_ROOT/qt"
    require_file "$QT_HOST_DIR/lib/cmake/Qt6/Qt6Config.cmake" "host Qt CMake package"
    write_provenance "$QT_HOST_DIR" Qt "$HAZOR_QT_VERSION" \
        "aqt:$HAZOR_AQT_VERSION linux/desktop/linux_gcc_64"
}

prepare_qt_target() {
    if [[ "$TARGET" == "windows-x86" ]]; then
        die "windows-x86 is unavailable: Qt 6.8 supports Windows x86_64 only and publishes no 32-bit Qt 6.8.3 target package; the target is intentionally not substituted with x64"
    fi
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
            "$aqt" install-qt mac desktop "$HAZOR_QT_VERSION" clang_64 \
                --outputdir "$TARGET_ROOT/qt"
            ;;
    esac
    require_file "$QT_TARGET_DIR/lib/cmake/Qt6/Qt6Config.cmake" "target Qt CMake package"
    write_provenance "$QT_TARGET_DIR" Qt "$HAZOR_QT_VERSION" \
        "aqt:$HAZOR_AQT_VERSION target=$TARGET"
}

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

dependency_cmake_args() {
    local -n output_ref="$1"
    output_ref=(
        -G Ninja
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_INSTALL_PREFIX="__PREFIX__"
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
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
    HAZOR_TARGET_ROOT="$TARGET_ROOT" HAZOR_TARGET_ARCH="$TARGET_ARCH" \
        cmake -S "$source" -B "$build" "${args[@]}" \
            -DBUILD_LIST=core,imgproc,imgcodecs \
            -DBUILD_SHARED_LIBS=ON \
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
        *.zip) require_command unzip; unzip -q "$archive" -d "$parent" ;;
        *) die "unsupported dependency archive: $archive" ;;
    esac
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
            # The workflow package is MSVC-built. Its C API DLL is distributable,
            # but the supplied .lib is not used by MinGW until a GNU import-library
            # conversion is validated. The app therefore builds without ONNX on
            # this local cross target instead of mixing C++ ABIs silently.
            local archive="$HAZOR_DOWNLOAD_DIR/onnxruntime/$HAZOR_ORT_WINDOWS_PACKAGE.zip"
            fetch_checked "$HAZOR_ORT_WINDOWS_URL" "$archive" "$HAZOR_ORT_WINDOWS_SHA256"
            if [[ ! -f "$ONNXRUNTIME_DIR/lib/onnxruntime.dll" ]]; then
                extract_runtime_archive "$archive" "$(dirname "$ONNXRUNTIME_DIR")"
            fi
            write_provenance "$ONNXRUNTIME_DIR" "ONNX Runtime runtime-only" "$HAZOR_ONNXRUNTIME_VERSION" \
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
    [[ -f "$REALESRGAN_DIR/$executable" ]] && return
    local archive="$HAZOR_DOWNLOAD_DIR/realesrgan/$archive_name"
    local temp
    fetch_checked "$url" "$archive" "$sha"
    temp="$(mktemp -d)"
    unzip -q "$archive" -d "$temp"
    mkdir -p "$REALESRGAN_MODELS_DIR"
    local found
    found="$(find "$temp" -type f -name "$executable" | head -n 1)"
    [[ -n "$found" ]] || die "$executable was not found in $archive"
    cp -f "$found" "$REALESRGAN_DIR/$executable"
    chmod +x "$REALESRGAN_DIR/$executable"
    find "$temp" -type f \( -name '*.bin' -o -name '*.param' \) -exec cp -f {} "$REALESRGAN_MODELS_DIR/" \;
    find "$temp" -type f -name '*.dll' -exec cp -f {} "$REALESRGAN_DIR/" \;
    safe_remove_tree "$temp" /tmp
    write_provenance "$REALESRGAN_DIR" RealESRGAN "$HAZOR_REALESRGAN_VERSION" "$url" "$sha"
}

prepare_models() {
    require_command unzip
    local sam_dir="$ONNX_MODELS_DIR/segmenters/sam-mobile"
    local sam_archive="$HAZOR_DOWNLOAD_DIR/models/$HAZOR_SAM_ARCHIVE"
    if [[ ! -f "$sam_dir/model.json" ]]; then
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
        safe_remove_tree "$temp" /tmp
        write_provenance "$sam_dir" "Mobile SAM" "$HAZOR_SAM_VERSION" "$HAZOR_SAM_URL" "$HAZOR_SAM_SHA256"
    fi

    local birefnet_dir="$ONNX_MODELS_DIR/matting/BiRefNet_lite-ONNX"
    local model_cache="$HAZOR_DOWNLOAD_DIR/models/$HAZOR_BIREFNET_FILE"
    if [[ ! -f "$birefnet_dir/$HAZOR_BIREFNET_FILE" ]]; then
        fetch_checked "$HAZOR_BIREFNET_URL" "$model_cache" "$HAZOR_BIREFNET_SHA256"
        mkdir -p "$birefnet_dir"
        cp -f "$model_cache" "$birefnet_dir/$HAZOR_BIREFNET_FILE"
        printf '{\n  "schema_version": 1,\n  "id": "birefnet-matting-lite",\n  "type": "birefnet",\n  "backend": "onnxruntime",\n  "task": "matting",\n  "family": "birefnet",\n  "variant": "lite",\n  "display_name": "BiRefNet Matting Lite",\n  "description": "Lightweight BiRefNet model for foreground refinement and alpha mattes.",\n  "quality": "medium",\n  "default": true,\n  "files": {"model": "%s"},\n  "input": {"color_space": "rgb"},\n  "capabilities": ["alpha_matte", "refine_mask", "soft_edges", "foreground_extraction"],\n  "license": {"name": "MIT", "notice": "NOTICE.md"},\n  "source": {"name": "onnx-community/BiRefNet_lite-ONNX", "url": "https://huggingface.co/onnx-community/BiRefNet_lite-ONNX"}\n}\n' \
            "$HAZOR_BIREFNET_FILE" > "$birefnet_dir/model.json"
        printf '# BiRefNet Matting Lite\n\nVersion: %s\nSource: %s\nLicense: MIT\n' \
            "$HAZOR_BIREFNET_VERSION" "$HAZOR_BIREFNET_URL" > "$birefnet_dir/NOTICE.md"
        write_provenance "$birefnet_dir" "BiRefNet Lite" "$HAZOR_BIREFNET_VERSION" \
            "$HAZOR_BIREFNET_URL" "$HAZOR_BIREFNET_SHA256"
    fi
}

prepare_material_icons() {
    local marker="$ROOT_DIR/src/resources/shapes/.hazor-material-icons-$HAZOR_MATERIAL_ICONS_VERSION"
    [[ -f "$marker" ]] && return

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
    safe_remove_tree "$temp" /tmp
    printf 'version=%s\nsource=%s\nsha256=%s\n' \
        "$HAZOR_MATERIAL_ICONS_VERSION" "$HAZOR_MATERIAL_ICONS_URL" \
        "$HAZOR_MATERIAL_ICONS_SHA256" > "$marker"
    log "Material Design Icons $HAZOR_MATERIAL_ICONS_VERSION prepared"
}

prepare_appimage_tools() {
    [[ "$TARGET" == "linux-x86_64" ]] || return
    local tools="$HAZOR_HOST_ROOT/appimage"
    fetch_checked "$HAZOR_LINUXDEPLOY_URL" "$tools/linuxdeploy-x86_64.AppImage" "$HAZOR_LINUXDEPLOY_SHA256"
    fetch_checked "$HAZOR_LINUXDEPLOY_QT_URL" "$tools/linuxdeploy-plugin-qt-x86_64.AppImage" "$HAZOR_LINUXDEPLOY_QT_SHA256"
    fetch_checked "$HAZOR_APPIMAGETOOL_URL" "$tools/appimagetool-x86_64.AppImage" "$HAZOR_APPIMAGETOOL_SHA256"
    fetch_checked "$HAZOR_APPIMAGE_RUNTIME_URL" "$tools/runtime-x86_64" "$HAZOR_APPIMAGE_RUNTIME_SHA256"
    chmod +x "$tools"/*.AppImage
}

prepare_dependencies() {
    mkdir -p "$TARGET_ROOT" "$HAZOR_DOWNLOAD_DIR" "$HAZOR_COMMON_ROOT"
    migrate_legacy_linux
    if [[ "${MIGRATE_ONLY:-0}" == "1" ]]; then
        log "legacy dependency migration complete"
        return
    fi
    [[ "$TARGET" != "windows-x86" ]] \
        || die "windows-x86 is unavailable: Qt 6.8 supports Windows x86_64 only and publishes no 32-bit Qt 6.8.3 target package; the target is intentionally not substituted with x64"
    prepare_qt_host
    prepare_qt_target
    prepare_zlib
    prepare_opencv
    prepare_onnxruntime
    prepare_realesrgan
    prepare_models
    prepare_material_icons
    prepare_appimage_tools
    log "dependencies prepared for $TARGET"
}

prepare_dependencies
