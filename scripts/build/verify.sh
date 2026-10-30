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

# A packaged file may not send the target machine back to this checkout. What
# breaks there is a loader path, a symlink or a script/config that still names
# the local tree; a build path left inside a compiled binary's diagnostic
# strings cannot, so it is reported rather than fatal (a locally built OpenCV
# always carries its own install prefix for cv::samples::findFile, which Hazor
# never calls).
verify_no_local_paths() {
    [[ -d "$DIST_DIR" ]] || return 0
    # PE and Mach-O binaries get the same treatment as ELF. Without their mime
    # types here they fell through to the catch-all below, which greps the file
    # as if it were text and fails on any hit — so a Windows .exe was rejected
    # for the assert() strings libstdc++, winpthreads and the mingw CRT carry
    # from the toolchain build, exactly the "diagnostic strings only" case this
    # function is documented to report rather than reject.
    local path target mime failed=0
    while IFS= read -r -d '' path; do
        target="$(readlink "$path")"
        if [[ "$target" == "$ROOT_DIR"* ]]; then
            printf '[hazor-build] local path in a packaged symlink: %s -> %s\n' "$path" "$target" >&2
            failed=1
        fi
    done < <(find "$DIST_DIR" -type l -print0)

    while IFS= read -r -d '' path; do
        mime="$(file -b --mime-type "$path")"
        case "$mime" in
            application/x-sharedlib|application/x-executable|application/x-pie-executable|application/x-object|\
            application/vnd.microsoft.portable-executable|application/x-dosexec|\
            application/x-mach-binary)
                if readelf -d "$path" 2>/dev/null | grep -E 'RPATH|RUNPATH' | grep -qF "$ROOT_DIR"; then
                    printf '[hazor-build] local path in a runtime library path: %s\n' "$path" >&2
                    failed=1
                elif strings -a "$path" 2>/dev/null | grep -qF "$ROOT_DIR"; then
                    log "note: $(basename "$path") embeds the local build path in diagnostic strings only"
                fi
                ;;
            *)
                if grep -qF "$ROOT_DIR" "$path" 2>/dev/null; then
                    printf '[hazor-build] local path in a packaged file: %s\n' "$path" >&2
                    failed=1
                fi
                ;;
        esac
    done < <(find "$DIST_DIR" -type f -print0)

    [[ "$failed" -eq 0 ]] || die "packaged files reference the local source path: $ROOT_DIR"
}

appdir_needed_libraries() {
    readelf -d "$1" 2>/dev/null | sed -n 's|.*(NEEDED).*\[\(.*\)\]|\1|p'
}

# AppRun is the only thing that runs before the editor, and linuxdeploy writes
# it with one hardcoded `source` line per hook that existed when it ran — so a
# hook added afterwards is silently dead. package.sh replaces AppRun to source
# the directory instead; this is what keeps that from regressing, because
# nothing else in the package would look different if it did.
verify_appimage_apprun_hooks() {
    local appdir="$DIST_DIR/AppDir"
    local apprun="$appdir/AppRun"
    require_file "$apprun" "AppDir AppRun"
    bash -n "$apprun" || die "the AppDir's AppRun is not valid bash"
    grep -qF 'apprun-hooks/*.sh' "$apprun" \
        || die "AppRun does not source apprun-hooks/*.sh, so hooks written after linuxdeploy ran would never execute"
    local hook count=0
    while IFS= read -r -d '' hook; do
        # Sourced the way AppRun sources it — same `set -e`, same APPDIR — so a
        # hook whose last command fails is caught here instead of by a user
        # whose AppImage refuses to start.
        APPDIR="$appdir" bash -c 'set -e; source "$1"' hazor-verify "$hook" \
            || die "apprun-hook $(basename "$hook") fails when sourced; it would stop the AppImage from starting"
        count=$((count + 1))
    done < <(find "$appdir/apprun-hooks" -maxdepth 1 -name '*.sh' -print0)
    log "AppImage: $count apprun-hook(s) sourced by AppRun, each verified to source cleanly"
}

# The local Stable Diffusion module is the one part of the package whose GPU
# support is resolved on the user's machine rather than here, so the package has
# to carry every backend and none of the host's GPU stack.
verify_appimage_stable_diffusion() {
    local appdir="$DIST_DIR/AppDir"
    local binary="$appdir/usr/bin/Hazor"
    require_file "$binary" "packaged Hazor executable"
    local needed
    needed="$(appdir_needed_libraries "$binary")"
    if ! grep -q '^libggml' <<<"$needed"; then
        # Built without the module: then it must not have been staged either.
        find "$appdir/usr/bin" -maxdepth 1 -name 'libggml-*.so' -print -quit | grep -q . \
            && die "ggml backend modules were staged for a Hazor binary that does not link the Stable Diffusion module"
        return 0
    fi

    local modules
    modules="$(find "$appdir/usr/bin" -maxdepth 1 -name 'libggml-*.so' | wc -l)"
    [[ "$modules" -gt 0 ]] \
        || die "the Hazor binary links the Stable Diffusion module but no ggml backend module was staged into usr/bin"
    # ggml dlopen()s the modules out of the executable's directory, and they
    # resolve these two through the loader path: without them every backend
    # fails to load on every machine.
    find "$appdir/usr/lib" -maxdepth 1 -name 'libggml-base.so*' -print -quit | grep -q . \
        || die "AppImage staging is missing libggml-base"
    find "$appdir/usr/lib" -maxdepth 1 -name 'libgomp.so*' -print -quit | grep -q . \
        || die "AppImage staging is missing libgomp, which the ggml CPU backend modules link"
    # The baseline variant is the one that runs on any x86-64: every other CPU
    # module is gated on instruction sets a user's machine may not have.
    require_file "$appdir/usr/bin/libggml-cpu-x64.so" \
        "baseline x86-64 ggml CPU backend module (the fallback for older CPUs)"

    # Bundling any of these would override the user's own GPU stack with the
    # build machine's: the driver is matched to its kernel module and the Vulkan
    # loader is what enumerates the drivers actually installed there.
    local host_gpu_libs
    host_gpu_libs="$(find "$appdir/usr/lib" -maxdepth 1 \
        \( -name 'libcuda.so*' -o -name 'libnvidia-*.so*' -o -name 'libcudart.so*' \
           -o -name 'libcublas*.so*' -o -name 'libvulkan.so*' \) -printf '%f ' 2>/dev/null)"
    [[ -z "$host_gpu_libs" ]] \
        || die "the AppImage bundles host GPU libraries that must come from the user's system: $host_gpu_libs"

    # A CUDA-enabled package makes libcuda.so.1 a hard dependency of the
    # executable, so the stub and its hook are what keep the editor startable on
    # a machine with no NVIDIA driver at all.
    if grep -q '^libcuda\.so\.1$' <<<"$needed"; then
        require_file "$appdir/usr/lib/cuda-stubs/libcuda.so.1" \
            "CUDA driver stub (the executable links libcuda.so.1)"
        require_file "$appdir/apprun-hooks/cuda-stub.sh" "CUDA driver stub AppRun hook"
    fi
    log "AppImage: $modules ggml backend module(s) staged, no host GPU libraries bundled"
}

verify_package() {
    [[ "$PACKAGE_FORMAT" != "none" ]] || return
    require_command strings
    verify_no_local_paths
    case "$PACKAGE_FORMAT" in
        appimage)
            require_file "$(artifact_path linux-x86_64 AppImage)" AppImage
            require_file "$DIST_DIR/AppDir/usr/plugins/platformthemes/libqxdgdesktopportal.so" \
                "Qt xdgdesktopportal plugin"
            find "$DIST_DIR/AppDir/usr/lib" -maxdepth 1 -name 'libQt6DBus.so*' \
                -print -quit | grep -q . || die "AppImage staging is missing libQt6DBus"
            verify_appimage_apprun_hooks
            verify_appimage_stable_diffusion
            ;;
        flatpak)
            require_file "$(artifact_path linux-x86_64 flatpak)" "Flatpak bundle"
            ;;
        zip)
            require_file "$(artifact_path Windows-x64 zip)" "Windows ZIP"
            require_file "$DIST_DIR/platforms/qwindows.dll" "Qt Windows platform plugin"
            local dll dll_format
            while IFS= read -r -d '' dll; do
                dll_format="$(file -b "$dll")"
                [[ "$dll_format" == *"PE32+"* && "$dll_format" == *"x86-64"* ]] \
                    || die "Windows package contains a non-x86_64 DLL: $dll ($dll_format)"
            done < <(find "$DIST_DIR" -type f -name '*.dll' -print0)
            ;;
        bundle)
            require_file "$(artifact_path "macOS-$TARGET_ARCH" zip)" "macOS ZIP"
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
                    require_file "$(artifact_path linux-x86_64 AppImage)" AppImage
                    require_file "$(artifact_path linux-x86_64 flatpak)" "Flatpak bundle"
                    ;;
                windows)
                    require_file "$(artifact_path Windows-x64 zip)" "Windows ZIP"
                    ;;
                macos)
                    require_file "$(artifact_path "macOS-$TARGET_ARCH" zip)" "macOS ZIP"
                    ;;
            esac
            ;;
    esac
    log "package verification passed for $TARGET ($PACKAGE_FORMAT)"
}

verify_build
verify_package
