#!/usr/bin/env bash
set -euo pipefail

find_hazor_binary_for_verification() {
    if target_is_macos; then
        find "$BUILD_DIR" -type f -path '*/Hazor.app/Contents/MacOS/Hazor' -print -quit
    elif target_is_windows; then
        find "$BUILD_DIR" -type f -name Hazor.exe -print -quit
    else
        find "$BUILD_DIR" -type f -name Hazor -perm -u+x -print -quit
    fi
}

cache_value() {
    local key="$1"
    sed -n "s|^${key}:[^=]*=||p" "$BUILD_DIR/CMakeCache.txt" | tail -n 1
}

verify_cache_path() {
    local key="$1"
    local value
    value="$(cache_value "$key")"
    [[ -n "$value" ]] || die "CMake cache does not contain $key"
    [[ "$value" == "$TARGET_ROOT"/* ]] \
        || die "$key escaped target prefix: $value (expected under $TARGET_ROOT)"
    log "$key: $value"
}

verify_build() {
    require_command file
    require_file "$BUILD_DIR/CMakeCache.txt" "CMake cache"
    local binary format
    binary="$(find_hazor_binary_for_verification)"
    [[ -f "$binary" ]] || die "Hazor build output was not found under $BUILD_DIR"
    format="$(file -b "$binary")"
    log "binary: $binary"
    log "format: $format"
    verify_cache_path Qt6_DIR
    verify_cache_path OpenCV_DIR
    verify_cache_path ZLIB_INCLUDE_DIR

    case "$TARGET" in
        linux-x86_64)
            [[ "$format" == *"ELF 64-bit"* && "$format" == *"x86-64"* ]] \
                || die "expected an x86_64 ELF binary"
            require_command readelf
            readelf -h "$binary" | grep -F 'Machine:'
            ;;
        windows-x86_64)
            [[ "$format" == *"PE32+"* && "$format" == *"x86-64"* ]] \
                || die "expected a Windows x86_64 PE32+ binary"
            require_command x86_64-w64-mingw32-objdump
            x86_64-w64-mingw32-objdump -f "$binary" | grep -F 'pei-x86-64'
            ;;
        windows-x86)
            [[ "$format" == *"PE32 executable"* && "$format" != *"PE32+"* ]] \
                || die "expected a Windows x86 PE32 binary"
            ;;
        macos-x86_64)
            [[ "$format" == *"Mach-O 64-bit"* && "$format" == *"x86_64"* ]] \
                || die "expected a macOS x86_64 Mach-O binary"
            ;;
        macos-arm64)
            [[ "$format" == *"Mach-O 64-bit"* && "$format" == *"arm64"* ]] \
                || die "expected a macOS arm64 Mach-O binary"
            ;;
    esac
    log "build verification passed for $TARGET"
}

verify_package() {
    [[ "$PACKAGE_FORMAT" != "none" ]] || return
    if [[ -d "$DIST_DIR" ]] && rg -a -F "$ROOT_DIR" "$DIST_DIR" >/dev/null 2>&1; then
        die "packaged files contain the local source path: $ROOT_DIR"
    fi
    case "$PACKAGE_FORMAT" in
        appimage)
            require_file "$ARTIFACT_DIR/HazorStudio-linux-x86_64.AppImage" AppImage
            require_file "$DIST_DIR/AppDir/usr/plugins/platformthemes/libqxdgdesktopportal.so" \
                "Qt xdgdesktopportal plugin"
            find "$DIST_DIR/AppDir/usr/lib" -maxdepth 1 -name 'libQt6DBus.so*' \
                -print -quit | grep -q . || die "AppImage staging is missing libQt6DBus"
            ;;
        flatpak)
            require_file "$ARTIFACT_DIR/HazorStudio-linux-x86_64.flatpak" "Flatpak bundle"
            ;;
        zip)
            require_file "$ARTIFACT_DIR/HazorStudio-Windows-x64.zip" "Windows ZIP"
            require_file "$DIST_DIR/platforms/qwindows.dll" "Qt Windows platform plugin"
            local dll dll_format
            while IFS= read -r -d '' dll; do
                dll_format="$(file -b "$dll")"
                [[ "$dll_format" == *"PE32+"* && "$dll_format" == *"x86-64"* ]] \
                    || die "Windows package contains a non-x86_64 DLL: $dll ($dll_format)"
            done < <(find "$DIST_DIR" -type f -name '*.dll' -print0)
            ;;
        bundle)
            require_file "$ARTIFACT_DIR/HazorStudio-macOS-$TARGET_ARCH.zip" "macOS ZIP"
            local app_binary="$DIST_DIR/Hazor.app/Contents/MacOS/Hazor"
            local app_format dependency macho otool
            require_file "$app_binary" "packaged macOS executable"
            app_format="$(file -b "$app_binary")"
            [[ "$app_format" == *"$TARGET_ARCH"* ]] \
                || die "packaged macOS executable is not $TARGET_ARCH: $app_format"
            require_file "$DIST_DIR/Hazor.app/Contents/PlugIns/platforms/libqcocoa.dylib" \
                "packaged Qt Cocoa platform plugin"
            otool="$(find_osxcross_tool otool)"
            while IFS= read -r -d '' macho; do
                file -b "$macho" | grep -Fq 'Mach-O' || continue
                while IFS= read -r dependency; do
                    case "$dependency" in
                        /*)
                            [[ "$dependency" == /System/Library/* || "$dependency" == /usr/lib/* ]] \
                                || die "macOS package contains a non-system absolute dependency: $dependency ($macho)"
                            ;;
                    esac
                done < <("$otool" -L "$macho" | tail -n +2 | awk '{print $1}')
            done < <(find "$DIST_DIR/Hazor.app/Contents" -type f -print0)
            ;;
        all)
            case "$TARGET_OS" in
                linux)
                    require_file "$ARTIFACT_DIR/HazorStudio-linux-x86_64.AppImage" AppImage
                    require_file "$ARTIFACT_DIR/HazorStudio-linux-x86_64.flatpak" "Flatpak bundle"
                    ;;
                windows)
                    require_file "$ARTIFACT_DIR/HazorStudio-Windows-x64.zip" "Windows ZIP"
                    ;;
                macos)
                    require_file "$ARTIFACT_DIR/HazorStudio-macOS-$TARGET_ARCH.zip" "macOS ZIP"
                    ;;
            esac
            ;;
    esac
    log "package verification passed for $TARGET ($PACKAGE_FORMAT)"
}

verify_build
verify_package
