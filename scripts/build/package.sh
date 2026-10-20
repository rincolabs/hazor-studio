#!/usr/bin/env bash
set -euo pipefail

find_hazor_binary() {
    local name="Hazor"
    target_is_windows && name="Hazor.exe"
    if target_is_macos && [[ -f "$BUILD_DIR/Hazor.app/Contents/MacOS/Hazor" ]]; then
        printf '%s\n' "$BUILD_DIR/Hazor.app/Contents/MacOS/Hazor"
        return
    fi
    if [[ -f "$BUILD_DIR/$name" ]]; then
        printf '%s\n' "$BUILD_DIR/$name"
        return
    fi
    find "$BUILD_DIR" -type f -name "$name" -print -quit
}

reset_package_directory() {
    local path="$1"
    if [[ -e "$path" ]]; then
        safe_remove_tree "$path" "$ROOT_DIR"
    fi
    mkdir -p "$path"
}

copy_runtime_assets() {
    local thirdparty_out="$1"
    mkdir -p "$thirdparty_out/onnx/models" "$thirdparty_out/realesrgan/models"
    if [[ -d "$ONNX_MODELS_DIR" ]]; then
        cp -a "$ONNX_MODELS_DIR/." "$thirdparty_out/onnx/models/"
    fi
    if [[ -d "$REALESRGAN_DIR" ]]; then
        cp -a "$REALESRGAN_DIR/." "$thirdparty_out/realesrgan/"
    fi
    if [[ -d "$RAWENGINE_DEPS_DIR/rtdata" ]]; then
        mkdir -p "$thirdparty_out/rawengine"
        cp -a "$RAWENGINE_DEPS_DIR/rtdata" "$thirdparty_out/rawengine/"
    fi
}

write_runtime_notices() {
    local destination="$1"
    mkdir -p "$destination"
    printf '# Qt\n\nVersion: %s\nSource: https://www.qt.io\nLicense: LGPL-3.0 (Qt Open Source)\n' \
        "$HAZOR_QT_VERSION" > "$destination/QT-NOTICE.md"
    if [[ "$TARGET" != "windows-x86_64" && -d "$ONNXRUNTIME_DIR/lib" ]]; then
        printf '# ONNX Runtime\n\nVersion: %s\nSource: https://github.com/microsoft/onnxruntime\nLicense: MIT\n' \
            "$HAZOR_ONNXRUNTIME_VERSION" > "$destination/ONNXRUNTIME-NOTICE.md"
    fi
    printf '# OpenCV\n\nVersion: %s\nSource: https://opencv.org\nLicense: Apache-2.0\n' \
        "$HAZOR_OPENCV_VERSION" > "$destination/OPENCV-NOTICE.md"
    printf '# Real-ESRGAN ncnn Vulkan\n\nVersion: %s\nSource: https://github.com/xinntao/Real-ESRGAN\nLicense: BSD-3-Clause\n' \
        "$HAZOR_REALESRGAN_VERSION" > "$destination/REALESRGAN-NOTICE.md"
    printf '# Material Design Icons\n\nVersion: %s\nSource: https://github.com/google/material-design-icons\nLicense: Apache-2.0\n' \
        "$HAZOR_MATERIAL_ICONS_VERSION" > "$destination/MATERIAL-ICONS-NOTICE.md"
    printf '# BiRefNet Lite\n\nVersion: %s\nSource: https://huggingface.co/onnx-community/BiRefNet_lite-ONNX\nLicense: MIT\n' \
        "$HAZOR_BIREFNET_VERSION" > "$destination/BIREFNET-LITE-NOTICE.md"

    local notice
    if [[ "$TARGET" != "windows-x86_64" ]]; then
        for notice in LICENSE LICENSE.txt NOTICE NOTICE.md; do
            if [[ -f "$ONNXRUNTIME_DIR/$notice" ]]; then
                cp -f "$ONNXRUNTIME_DIR/$notice" "$destination/ONNXRUNTIME-$notice"
            fi
        done
    fi
    if [[ -f "$ONNX_MODELS_DIR/segmenters/sam-mobile/NOTICE.md" ]]; then
        cp -f "$ONNX_MODELS_DIR/segmenters/sam-mobile/NOTICE.md" \
            "$destination/SAM-MOBILE-NOTICE.md"
    fi
    if [[ -f "$ROOT_DIR/src/resources/shapes/LICENSES/material-icons.txt" ]]; then
        cp -f "$ROOT_DIR/src/resources/shapes/LICENSES/material-icons.txt" \
            "$destination/MATERIAL-ICONS-LICENSE.txt"
    fi
}

package_appimage() {
    [[ "$TARGET" == "linux-x86_64" ]] || die "AppImage packaging is available only for linux-x86_64"
    require_command patchelf
    local binary tools appdir meta output
    binary="$(find_hazor_binary)"
    [[ -f "$binary" ]] || die "Hazor binary not found under $BUILD_DIR"
    tools="$HAZOR_HOST_ROOT/appimage"
    require_file "$tools/linuxdeploy-x86_64.AppImage" linuxdeploy
    require_file "$tools/linuxdeploy-plugin-qt-x86_64.AppImage" linuxdeploy-plugin-qt
    require_file "$tools/appimagetool-x86_64.AppImage" appimagetool
    require_file "$tools/runtime-x86_64" "AppImage runtime"
    appdir="$DIST_DIR/AppDir"
    meta="$DIST_DIR/meta"
    reset_package_directory "$DIST_DIR"
    mkdir -p "$appdir" "$meta" "$ARTIFACT_DIR"
    cp -f "$ROOT_DIR/packaging/linux/com.rincolabs.HazorStudio.desktop" "$meta/"
    cp -f "$ROOT_DIR/src/resources/icons/app-icon.png" "$meta/com.rincolabs.HazorStudio.png"
    require_file "$QT_TARGET_DIR/plugins/platformthemes/libqxdgdesktopportal.so" \
        "Qt xdgdesktopportal platform theme plugin"
    mkdir -p "$appdir/usr/plugins/platformthemes"
    cp -f "$QT_TARGET_DIR/plugins/platformthemes/libqxdgdesktopportal.so" \
        "$appdir/usr/plugins/platformthemes/"

    export APPIMAGE_EXTRACT_AND_RUN=1
    export QMAKE="$QT_TARGET_DIR/bin/qmake"
    export EXTRA_QT_PLUGINS="svg"
    export LD_LIBRARY_PATH="$QT_TARGET_DIR/lib:$OPENCV_PREFIX/lib:$ONNXRUNTIME_DIR/lib:${LD_LIBRARY_PATH:-}"
    "$tools/linuxdeploy-x86_64.AppImage" \
        --appdir "$appdir" \
        --executable "$binary" \
        --desktop-file "$meta/com.rincolabs.HazorStudio.desktop" \
        --icon-file "$meta/com.rincolabs.HazorStudio.png" \
        --plugin qt

    rm -f "$appdir/usr/plugins/platformthemes/libqgtk3.so" 2>/dev/null || true
    mkdir -p "$appdir/apprun-hooks"
    printf '%s\n' '#!/bin/sh' 'export QT_QPA_PLATFORMTHEME=xdgdesktopportal' \
        > "$appdir/apprun-hooks/qt-platformtheme.sh"
    require_file "$appdir/usr/plugins/platformthemes/libqxdgdesktopportal.so" \
        "packaged Qt xdgdesktopportal plugin"
    find "$appdir/usr/lib" -maxdepth 1 -name 'libQt6DBus.so*' -print -quit | grep -q . \
        || die "libQt6DBus was not bundled for the xdgdesktopportal plugin"

    copy_runtime_assets "$appdir/3rdparty"
    write_runtime_notices "$appdir/usr/share/licenses/hazor-studio"
    if [[ -d "$ONNXRUNTIME_DIR" ]]; then
        mkdir -p "$appdir/3rdparty/onnx/runtime"
        cp -a "$ONNXRUNTIME_DIR" "$appdir/3rdparty/onnx/runtime/"
    fi
    local packaged_binary="$appdir/usr/bin/Hazor"
    require_file "$packaged_binary" "packaged Hazor executable"
    patchelf --set-rpath \
        '$ORIGIN/../lib:$ORIGIN/../../3rdparty/onnx/runtime/'"$HAZOR_ORT_LINUX_PACKAGE"'/lib' \
        "$packaged_binary"
    find "$appdir/usr/lib" -maxdepth 1 -name 'libonnxruntime*.so*' -delete 2>/dev/null || true

    output="$ARTIFACT_DIR/HazorStudio-linux-x86_64.AppImage"
    if [[ -f "$output" ]]; then
        rm -f -- "$output"
    fi
    ARCH=x86_64 "$tools/appimagetool-x86_64.AppImage" \
        --runtime-file "$tools/runtime-x86_64" "$appdir" "$output"
    chmod +x "$output"
    log "AppImage created: $output"
}

package_flatpak() {
    [[ "$TARGET" == "linux-x86_64" ]] || die "Flatpak packaging is available only for linux-x86_64"
    require_command flatpak-builder
    require_command flatpak
    local build_root="$ROOT_DIR/build/flatpak-x86_64"
    local repo="$DIST_DIR/flatpak-repo"
    local output="$ARTIFACT_DIR/HazorStudio-linux-x86_64.flatpak"
    if [[ -e "$build_root" ]]; then
        safe_remove_tree "$build_root" "$ROOT_DIR"
    fi
    reset_package_directory "$DIST_DIR"
    mkdir -p "$ARTIFACT_DIR"
    flatpak-builder --force-clean --repo="$repo" \
        "$build_root" "$ROOT_DIR/packaging/flatpak/com.rincolabs.HazorStudio.yml"
    flatpak build-bundle "$repo" "$output" com.rincolabs.HazorStudio 6.8
    log "Flatpak bundle created: $output"
}

copy_mingw_runtime() {
    local destination="$1"
    local compiler="x86_64-w64-mingw32-g++"
    local runtime path
    for runtime in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
        path="$($compiler -print-file-name="$runtime")"
        [[ -f "$path" ]] || die "MinGW runtime not found: $runtime"
        cp -f "$path" "$destination/"
    done
}

package_windows_zip() {
    [[ "$TARGET" == "windows-x86_64" ]] || die "Windows ZIP packaging currently supports windows-x86_64 only"
    require_command zip
    local binary
    binary="$(find_hazor_binary)"
    [[ -f "$binary" ]] || die "Hazor.exe not found under $BUILD_DIR"
    reset_package_directory "$DIST_DIR"
    mkdir -p "$ARTIFACT_DIR"
    cp -f "$binary" "$DIST_DIR/Hazor.exe"

    find "$QT_TARGET_DIR/bin" -maxdepth 1 -type f -name 'Qt6*.dll' -exec cp -f {} "$DIST_DIR/" \;
    local plugin
    for plugin in platforms imageformats iconengines styles; do
        if [[ -d "$QT_TARGET_DIR/plugins/$plugin" ]]; then
            mkdir -p "$DIST_DIR/$plugin"
            find "$QT_TARGET_DIR/plugins/$plugin" -maxdepth 1 -type f -name '*.dll' \
                -exec cp -f {} "$DIST_DIR/$plugin/" \;
        fi
    done
    find "$OPENCV_PREFIX" -type f -name 'opencv*.dll' -exec cp -f {} "$DIST_DIR/" \;
    copy_mingw_runtime "$DIST_DIR"
    copy_runtime_assets "$DIST_DIR/3rdparty"
    write_runtime_notices "$DIST_DIR/licenses"
    require_file "$DIST_DIR/platforms/qwindows.dll" "Qt Windows platform plugin"

    local output="$ARTIFACT_DIR/HazorStudio-Windows-x64.zip"
    if [[ -f "$output" ]]; then
        rm -f -- "$output"
    fi
    (cd "$DIST_DIR" && zip -q -r "$output" .)
    log "Windows ZIP created: $output"
}

find_osxcross_tool() {
    local name="$1"
    local tool=""
    if [[ -x "$OSXCROSS_TARGET_DIR/bin/xcrun" ]]; then
        tool="$(PATH="$OSXCROSS_TARGET_DIR/bin:$PATH" \
            "$OSXCROSS_TARGET_DIR/bin/xcrun" -f "$name" 2>/dev/null || true)"
    fi
    if [[ -z "$tool" || ! -x "$tool" ]]; then
        tool="$(find "$OSXCROSS_TARGET_DIR/bin" -maxdepth 1 \
            \( -type f -o -type l \) -name "*-$name" -print -quit)"
    fi
    [[ -n "$tool" && -x "$tool" ]] || die "OSXCross tool not found: $name"
    printf '%s\n' "$tool"
}

copy_macos_framework() {
    local name="$1"
    local source="$QT_TARGET_DIR/lib/$name.framework"
    [[ -d "$source" ]] || return
    cp -a "$source" "$MACOS_FRAMEWORKS_DIR/"
}

copy_macos_plugins() {
    local plugin
    for plugin in platforms imageformats iconengines styles networkinformation tls; do
        if [[ -d "$QT_TARGET_DIR/plugins/$plugin" ]]; then
            mkdir -p "$MACOS_PLUGINS_DIR/$plugin"
            cp -a "$QT_TARGET_DIR/plugins/$plugin/." "$MACOS_PLUGINS_DIR/$plugin/"
        fi
    done
    require_file "$MACOS_PLUGINS_DIR/platforms/libqcocoa.dylib" \
        "Qt Cocoa platform plugin"
}

patch_macos_binary() {
    local binary="$1"
    local install_name_tool="$2"
    local otool="$3"
    local dependency suffix framework replacement
    while IFS= read -r dependency; do
        [[ -n "$dependency" ]] || continue
        replacement=""
        if [[ "$dependency" == *'.framework/'* ]]; then
            framework="${dependency%%.framework/*}.framework"
            framework="$(basename "$framework")"
            suffix="${dependency#*.framework/}"
            if [[ -d "$MACOS_FRAMEWORKS_DIR/$framework" ]]; then
                replacement="@rpath/$framework/$suffix"
            fi
        elif [[ -f "$MACOS_FRAMEWORKS_DIR/$(basename "$dependency")" ]]; then
            replacement="@rpath/$(basename "$dependency")"
        fi
        if [[ -n "$replacement" && "$replacement" != "$dependency" ]]; then
            "$install_name_tool" -change "$dependency" "$replacement" "$binary"
        fi
    done < <("$otool" -L "$binary" | tail -n +2 | awk '{print $1}')
}

deploy_macos_bundle() {
    local bundle="$1"
    local install_name_tool otool macho framework_name
    install_name_tool="$(find_osxcross_tool install_name_tool)"
    otool="$(find_osxcross_tool otool)"
    MACOS_FRAMEWORKS_DIR="$bundle/Contents/Frameworks"
    MACOS_PLUGINS_DIR="$bundle/Contents/PlugIns"
    export MACOS_FRAMEWORKS_DIR MACOS_PLUGINS_DIR
    mkdir -p "$MACOS_FRAMEWORKS_DIR" "$MACOS_PLUGINS_DIR"

    for framework_name in QtCore QtGui QtWidgets QtOpenGL QtOpenGLWidgets \
        QtNetwork QtConcurrent QtDBus QtSvg; do
        copy_macos_framework "$framework_name"
    done
    copy_macos_plugins

    find "$OPENCV_PREFIX/lib" -maxdepth 1 \
        \( -type f -o -type l \) -name 'libopencv*.dylib*' \
        -exec cp -a {} "$MACOS_FRAMEWORKS_DIR/" \;
    if [[ -d "$ONNXRUNTIME_DIR/lib" ]]; then
        find "$ONNXRUNTIME_DIR/lib" -maxdepth 1 \
            \( -type f -o -type l \) -name '*.dylib*' \
            -exec cp -a {} "$MACOS_FRAMEWORKS_DIR/" \;
    fi

    local executable="$bundle/Contents/MacOS/Hazor"
    require_file "$executable" "macOS bundle executable"
    if ! "$otool" -l "$executable" | grep -Fq '@executable_path/../Frameworks'; then
        "$install_name_tool" -add_rpath '@executable_path/../Frameworks' "$executable"
    fi

    while IFS= read -r -d '' macho; do
        if file -b "$macho" | grep -Fq 'Mach-O'; then
            patch_macos_binary "$macho" "$install_name_tool" "$otool"
            case "$macho" in
                *.framework/Versions/*/*)
                    framework_name="${macho%%.framework/*}.framework"
                    framework_name="$(basename "$framework_name" .framework)"
                    "$install_name_tool" -id \
                        "@rpath/$framework_name.framework/${macho#*.framework/}" "$macho"
                    ;;
                *.dylib|*.dylib.*)
                    "$install_name_tool" -id "@rpath/$(basename "$macho")" "$macho"
                    ;;
            esac
        fi
    done < <(find "$bundle/Contents/MacOS" "$MACOS_FRAMEWORKS_DIR" \
        "$MACOS_PLUGINS_DIR" -type f -print0)
}

package_macos() {
    target_is_macos || die "macOS packaging requires a macos-* target"
    local bundle="$BUILD_DIR/Hazor.app"
    require_dir "$bundle" "cross-compiled Hazor.app"
    require_command file
    require_command zip
    validate_cross_toolchain
    reset_package_directory "$DIST_DIR"
    cp -a "$bundle" "$DIST_DIR/Hazor.app"
    deploy_macos_bundle "$DIST_DIR/Hazor.app"
    copy_runtime_assets "$DIST_DIR/Hazor.app/Contents/Resources/3rdparty"
    write_runtime_notices "$DIST_DIR/Hazor.app/Contents/Resources/licenses"
    mkdir -p "$ARTIFACT_DIR"
    local output="$ARTIFACT_DIR/HazorStudio-macOS-$TARGET_ARCH.zip"
    [[ ! -f "$output" ]] || rm -f -- "$output"
    (cd "$DIST_DIR" && zip -q -r -y "$output" Hazor.app)
    log "unsigned macOS $TARGET_ARCH bundle created locally: $output"
    log "codesign, notarization and runtime validation on macOS were not performed"
}

package_target() {
    case "$PACKAGE_FORMAT" in
        appimage) package_appimage ;;
        flatpak) package_flatpak ;;
        zip) package_windows_zip ;;
        bundle) package_macos ;;
        all)
            case "$TARGET_OS" in
                linux) package_appimage; package_flatpak ;;
                windows) package_windows_zip ;;
                macos) package_macos ;;
            esac
            ;;
        none) return ;;
        *) die "unknown package format '$PACKAGE_FORMAT'" ;;
    esac
}

package_target
