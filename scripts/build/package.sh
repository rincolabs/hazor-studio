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

rawengine_was_built() {
    [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || return 1
    [[ "$(sed -n 's|^ENABLE_RAWENGINE:[^=]*=||p' "$BUILD_DIR/CMakeCache.txt" | tail -n 1)" == "ON" ]]
}

stable_diffusion_was_built() {
    [[ -f "$BUILD_DIR/CMakeCache.txt" ]] || return 1
    [[ "$(sed -n 's|^ENABLE_STABLE_DIFFUSION:[^=]*=||p' "$BUILD_DIR/CMakeCache.txt" | tail -n 1)" == "ON" ]] \
        || return 1
    # The option alone records only what was *asked* for: with no prepared
    # package CMake warns and builds Hazor without the module. The executable's
    # own DT_NEEDED list is the authoritative answer, and the difference is 230 MB
    # of backend modules staged for a binary that cannot load any of them.
    local binary
    binary="$(find_hazor_binary)"
    [[ -f "$binary" ]] || return 1
    patchelf --print-needed "$binary" 2>/dev/null | grep -q '^libggml'
}

# The ggml backend modules the local Stable Diffusion module runs on, plus the
# CUDA driver stub that keeps the executable startable without an NVIDIA driver.
#
# linuxdeploy cannot find any of this on its own: the modules are dlopen'd by
# filename, never linked, so they appear in no DT_NEEDED list. ggml looks for
# them in the directory holding the executable (ggml-backend-reg.cpp,
# get_executable_path()), which in an AppDir is usr/bin — real copies here, not
# the symlinks the build directory gets.
stage_stable_diffusion_appimage() {
    local appdir="$1"
    # A package built without the module must not ship its backends.
    stable_diffusion_was_built || return 0
    [[ -d "$STABLE_DIFFUSION_BACKEND_DIR" ]] || return 0

    # Every module dlopen'd out of usr/bin resolves libggml-base.so.0 (the
    # device registry) and, for the CPU variants, libgomp.so.1 through the
    # loader path, not through an rpath of their own — the installed modules
    # carry none. Both come from linuxdeploy's recursive deployment of the
    # executable's own dependencies, so staging the modules without them
    # produces a package where every single backend fails to load and the panel
    # reports "no device" on every machine. Checked rather than assumed.
    find "$appdir/usr/lib" -maxdepth 1 -name 'libggml-base.so*' -print -quit | grep -q . \
        || die "libggml-base was not bundled into usr/lib; the staged ggml backend modules could not be loaded"
    find "$appdir/usr/lib" -maxdepth 1 -name 'libgomp.so*' -print -quit | grep -q . \
        || die "libgomp was not bundled into usr/lib; the ggml CPU backend modules could not be loaded"

    local module count=0
    mkdir -p "$appdir/usr/bin"
    while IFS= read -r -d '' module; do
        # -L: ggml's scan considers regular files only, so whatever the prefix
        # holds has to arrive here as bytes under the unversioned name. The
        # installed modules are plain files today; a prefix assembled any other
        # way would otherwise ship a link into the build machine's 3rdparty.
        cp -aL "$module" "$appdir/usr/bin/"
        count=$((count + 1))
    done < <(find "$STABLE_DIFFUSION_BACKEND_DIR" -maxdepth 1 -name 'libggml-*.so' -print0)
    [[ "$count" -gt 0 ]] || die "no ggml backend modules found in $STABLE_DIFFUSION_BACKEND_DIR"

    # linuxdeploy resolves the executable's DT_NEEDED libcuda.so.1 — SD_CUDA
    # links the driver API — and happily bundles this machine's NVIDIA driver
    # library, which its excludelist does not cover. Shipping a driver
    # component is worse than shipping nothing: it is matched to the kernel
    # module on the build machine and would be preferred over the user's own.
    # Removed here the same way libonnxruntime is, along with the rest of the
    # GPU stack that has to come from the user's system: the CUDA runtime and
    # cuBLAS (hundreds of megabytes, and version-matched to the driver) and the
    # Vulkan loader, which is the component that enumerates the host's own
    # installed drivers. A backend module whose libraries are missing simply
    # fails to dlopen and the next one is used.
    find "$appdir/usr/lib" -maxdepth 1 \
        \( -name 'libcuda.so*' -o -name 'libnvidia-*.so*' \
           -o -name 'libcudart.so*' -o -name 'libcublas*.so*' \
           -o -name 'libvulkan.so*' \) -delete 2>/dev/null || true

    # With the driver library gone, a machine that has no NVIDIA driver has
    # nothing to satisfy that same DT_NEEDED, and the editor would not start at
    # all — losing the whole application over an optional backend. The CUDA
    # toolkit's own stub satisfies the loader and makes every call through it
    # fail cleanly, so it is injected only when the host really has no driver;
    # where one exists, the host's is used and CUDA works for real.
    local stub="$STABLE_DIFFUSION_PREFIX/cuda-stubs/libcuda.so.1"
    if [[ -f "$stub" ]]; then
        mkdir -p "$appdir/usr/lib/cuda-stubs" "$appdir/apprun-hooks"
        cp -aL "$stub" "$appdir/usr/lib/cuda-stubs/"
        cat > "$appdir/apprun-hooks/cuda-stub.sh" <<'HOOK'
#!/bin/sh
# Only when the host has no real libcuda.so.1: the stub must never shadow a
# working driver. ldconfig is tried first (it knows whatever the distribution
# configured), with the usual driver locations as the fallback for systems
# where it is absent from PATH or does not cover them.
hazor_has_libcuda() {
    for ldc in ldconfig /sbin/ldconfig /usr/sbin/ldconfig; do
        if command -v "$ldc" >/dev/null 2>&1; then
            "$ldc" -p 2>/dev/null | grep -q 'libcuda\.so\.1' && return 0
        fi
    done
    # The loader searches LD_LIBRARY_PATH before its cache, so a driver (or a
    # toolkit install) reachable only that way still wins over the stub and
    # must be found here too.
    hazor_ifs="$IFS"
    IFS=:
    for dir in $LD_LIBRARY_PATH; do
        [ -n "$dir" ] && [ -e "$dir/libcuda.so.1" ] && { IFS="$hazor_ifs"; return 0; }
    done
    IFS="$hazor_ifs"
    # Distributions that keep the driver out of the cache, or out of PATH's
    # reach of ldconfig: Debian's alternatives directory, NixOS's driver
    # bind-mount, the lib64 split.
    for dir in /lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /usr/lib64 /usr/lib \
               /usr/lib/x86_64-linux-gnu/nvidia/current /usr/lib64/nvidia \
               /usr/lib/nvidia /run/opengl-driver/lib; do
        [ -e "$dir/libcuda.so.1" ] && return 0
    done
    return 1
}
if ! hazor_has_libcuda; then
    export LD_LIBRARY_PATH="$APPDIR/usr/lib/cuda-stubs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi
HOOK
    fi
    log "AppImage: staged $count ggml backend module(s) into usr/bin"
}

# linuxdeploy writes its AppRun with one hardcoded `source` line per hook that
# existed at the moment it ran — it does not scan the directory. Every hook this
# script adds afterwards was therefore written into the AppDir and then never
# executed: the xdgdesktopportal platform theme has silently never applied in a
# released AppImage, and the CUDA stub above would not have either, which with
# libcuda.so.1 as a DT_NEEDED of the executable means the *editor* fails to
# start on any machine without an NVIDIA driver.
#
# The replacement sources the directory: linuxdeploy's own plugin hooks first,
# then this project's, so ours still win where both set the same variable
# (QT_QPA_PLATFORMTHEME, which linuxdeploy-plugin-qt points at gtk2 on
# GNOME/XFCE). APPDIR is exported because the hooks use it and linuxdeploy's
# AppRun never set it — the AppImage runtime does, but a directly executed
# AppDir does not. `set -e` and the exec of AppRun.wrapped are kept verbatim
# from linuxdeploy's own template.
finalise_apprun_hooks() {
    local appdir="$1"
    require_file "$appdir/AppRun" "AppRun generated by linuxdeploy"
    [[ -e "$appdir/AppRun.wrapped" ]] \
        || die "AppRun.wrapped is missing; linuxdeploy did not generate a hook-capable AppRun"
    cat > "$appdir/AppRun" <<'APPRUN'
#! /usr/bin/env bash

# generated by linuxdeploy, replaced by scripts/build/package.sh so that every
# hook in apprun-hooks/ runs, not only the ones linuxdeploy knew about.

# make sure errors in sourced scripts will cause this script to stop
set -e

this_dir="$(readlink -f "$(dirname "$0")")"
export APPDIR="${APPDIR:-$this_dir}"

for hook in "$this_dir"/apprun-hooks/linuxdeploy-plugin-*.sh; do
    if [ -r "$hook" ]; then
        source "$hook"
    fi
done
for hook in "$this_dir"/apprun-hooks/*.sh; do
    case "${hook##*/}" in
        linuxdeploy-plugin-*) continue ;;
    esac
    if [ -r "$hook" ]; then
        source "$hook"
    fi
done

exec "$this_dir"/AppRun.wrapped "$@"
APPRUN
    chmod +x "$appdir/AppRun"
    local hooks
    hooks="$(find "$appdir/apprun-hooks" -maxdepth 1 -name '*.sh' -printf '%f ' 2>/dev/null)"
    log "AppImage: AppRun sources every apprun-hook (${hooks:-none})"
}

# $2 ("on"/"off", optional) overrides the CMake-cache probe for packages whose
# RAW engine state is decided by a packaging manifest rather than by this
# build directory — the Flatpak builds its own copy inside the sandbox.
copy_runtime_assets() {
    local thirdparty_out="$1"
    local rawengine_state="${2:-}"
    if [[ -z "$rawengine_state" ]]; then
        if rawengine_was_built; then rawengine_state=on; else rawengine_state=off; fi
    fi
    mkdir -p "$thirdparty_out/onnx/models" "$thirdparty_out/realesrgan/models"
    # -L: shared and migrated dependency trees are reached through symlinks, so
    # the package must receive the real files instead of links into 3rdparty.
    if [[ -d "$ONNX_MODELS_DIR" ]]; then
        cp -aL "$ONNX_MODELS_DIR/." "$thirdparty_out/onnx/models/"
    fi
    if [[ -d "$REALESRGAN_DIR" ]]; then
        cp -aL "$REALESRGAN_DIR/." "$thirdparty_out/realesrgan/"
    fi
    # The ncnn weights are shared between targets and live outside the
    # per-target RealESRGAN prefix.
    if [[ -d "$REALESRGAN_MODELS_DIR" ]]; then
        cp -aL "$REALESRGAN_MODELS_DIR/." "$thirdparty_out/realesrgan/models/"
    fi
    # rtdata is only readable by the RAW engine, so a build that left the engine
    # out must not ship it: the CMake cache is the authoritative record of what
    # was actually built.
    if [[ "$rawengine_state" == "on" && -d "$RAWENGINE_DEPS_DIR/rtdata" ]]; then
        mkdir -p "$thirdparty_out/rawengine"
        cp -aL "$RAWENGINE_DEPS_DIR/rtdata" "$thirdparty_out/rawengine/"
    fi
}

write_runtime_notices() {
    local destination="$1"
    mkdir -p "$destination"
    printf '# Qt\n\nVersion: %s\nSource: https://www.qt.io\nLicense: LGPL-3.0 (Qt Open Source)\n' \
        "$HAZOR_QT_VERSION" > "$destination/QT-NOTICE.md"
    if [[ -d "$ONNXRUNTIME_DIR/lib" ]]; then
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
    for notice in LICENSE LICENSE.txt NOTICE NOTICE.md; do
        if [[ -f "$ONNXRUNTIME_DIR/$notice" ]]; then
            cp -f "$ONNXRUNTIME_DIR/$notice" "$destination/ONNXRUNTIME-$notice"
        fi
    done
    if [[ -f "$ONNX_MODELS_DIR/segmenters/sam-mobile/NOTICE.md" ]]; then
        cp -f "$ONNX_MODELS_DIR/segmenters/sam-mobile/NOTICE.md" \
            "$destination/SAM-MOBILE-NOTICE.md"
    fi
    if [[ -f "$ROOT_DIR/src/resources/shapes/LICENSES/material-icons.txt" ]]; then
        cp -f "$ROOT_DIR/src/resources/shapes/LICENSES/material-icons.txt" \
            "$destination/MATERIAL-ICONS-LICENSE.txt"
    elif [[ -f "$ROOT_DIR/src/resources/shapes/material/LICENSE" ]]; then
        # Fallback for a checkout where only the prepared archive copy exists.
        cp -f "$ROOT_DIR/src/resources/shapes/material/LICENSE" \
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
    # stable-diffusion.cpp's prefix is on this list so linuxdeploy can resolve
    # the executable's DT_NEEDED libggml.so / libggml-base.so and bundle them
    # into usr/lib; the backend modules it cannot see are staged separately.
    export LD_LIBRARY_PATH="$QT_TARGET_DIR/lib:$OPENCV_PREFIX/lib:$ONNXRUNTIME_DIR/lib:$STABLE_DIFFUSION_PREFIX/lib:${LD_LIBRARY_PATH:-}"
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

    stage_stable_diffusion_appimage "$appdir"
    # After the last hook is written, never before it.
    finalise_apprun_hooks "$appdir"
    copy_runtime_assets "$appdir/3rdparty"
    write_runtime_notices "$appdir/usr/share/licenses/hazor-studio"
    if [[ -d "$ONNXRUNTIME_DIR" ]]; then
        mkdir -p "$appdir/3rdparty/onnx/runtime"
        # -L: a migrated legacy install is a symlink, and the rpath above must
        # resolve inside the AppImage, not back into this machine's 3rdparty.
        cp -aL "$ONNXRUNTIME_DIR" "$appdir/3rdparty/onnx/runtime/"
    fi
    local packaged_binary="$appdir/usr/bin/Hazor"
    require_file "$packaged_binary" "packaged Hazor executable"
    patchelf --set-rpath \
        '$ORIGIN/../lib:$ORIGIN/../../3rdparty/onnx/runtime/'"$HAZOR_ORT_LINUX_PACKAGE"'/lib' \
        "$packaged_binary"
    find "$appdir/usr/lib" -maxdepth 1 -name 'libonnxruntime*.so*' -delete 2>/dev/null || true

    output="$(artifact_path linux-x86_64 AppImage)"
    if [[ -f "$output" ]]; then
        rm -f -- "$output"
    fi
    ARCH=x86_64 "$tools/appimagetool-x86_64.AppImage" \
        --runtime-file "$tools/runtime-x86_64" "$appdir" "$output"
    chmod +x "$output"
    log "AppImage created: $output"
}

# The sandbox compiles Qt-, OpenCV- and RawEngine-linked code itself, but the
# platform-neutral payloads are the very same bytes the AppImage ships, and the
# ONNX Runtime release is a manylinux build that the KDE runtime can load as
# is. Staging them out of the prepared host tree keeps one definition of the
# runtime layout — copy_runtime_assets() — instead of a second one written in
# YAML that could drift from it.
stage_flatpak_assets() {
    local stage="$ROOT_DIR/build/flatpak-assets"
    require_dir "$ONNXRUNTIME_DIR" "prepared ONNX Runtime"
    require_dir "$RAWENGINE_DEPS_DIR/rtdata" "prepared RawEngine rtdata"
    # Re-staging identical bytes still rewrites the directory flatpak-builder
    # hashes, which invalidates this module and every module after it — an
    # hour of recompiling OpenCV and Hazor for no change at all. The marker
    # records what was staged so a genuine dependency change still restages.
    local marker="$stage/.hazor-staged"
    local signature="$HAZOR_ORT_LINUX_PACKAGE $HAZOR_REALESRGAN_VERSION $HAZOR_SAM_VERSION $HAZOR_BIREFNET_VERSION"
    if [[ -f "$marker" && "$(cat "$marker")" == "$signature" ]]; then
        log "reusing staged Flatpak runtime assets: $stage"
        return
    fi
    reset_package_directory "$stage"
    mkdir -p "$stage/onnxruntime" "$stage/runtime/3rdparty"
    # Resolve the prefix itself (a migrated legacy install is a symlink) but
    # keep the libonnxruntime.so -> .so.1 -> .so.1.26.0 chain intact, so /app/lib
    # gets one 26 MB library and two links rather than three copies.
    cp -a "$(real_path "$ONNXRUNTIME_DIR")/." "$stage/onnxruntime/"
    # ENABLE_RAWENGINE is ON in the manifest unconditionally, so rtdata always
    # belongs in the bundle regardless of what this host's build dir says.
    copy_runtime_assets "$stage/runtime/3rdparty" on
    write_runtime_notices "$stage/runtime/licenses"
    printf '%s' "$signature" > "$marker"
    log "staged Flatpak runtime assets: $stage"
}

# flatpak-builder mounts each module's prefix through rofiles-fuse to catch
# writes outside it. That needs /dev/fuse, which containers, CI runners and
# sandboxed shells routinely withhold — and when it is missing the failure
# surfaces several modules in as "Build directory ... not initialized", which
# says nothing about FUSE. Probe once and tell flatpak-builder up front.
flatpak_rofiles_fuse_works() {
    command -v rofiles-fuse >/dev/null 2>&1 || return 1
    local source mount status=1
    source="$(mktemp -d)"
    mount="$(mktemp -d)"
    if rofiles-fuse "$source" "$mount" >/dev/null 2>&1; then
        status=0
        fusermount3 -u "$mount" >/dev/null 2>&1 || fusermount -u "$mount" >/dev/null 2>&1 || true
    fi
    rmdir "$mount" "$source" 2>/dev/null || true
    return "$status"
}

package_flatpak() {
    [[ "$TARGET" == "linux-x86_64" ]] || die "Flatpak packaging is available only for linux-x86_64"
    require_command flatpak-builder
    require_command flatpak
    local build_root="$ROOT_DIR/build/flatpak-x86_64"
    local repo="$DIST_DIR/flatpak-repo"
    local output
    output="$(artifact_path linux-x86_64 flatpak)"
    # Without an explicit state dir, flatpak-builder caches its git mirrors and
    # downloads in the caller's working directory, so the sandbox sources are
    # re-fetched whenever the script runs from somewhere else.
    local state_dir="$ROOT_DIR/build/flatpak-state"
    # The app branch, which is not the runtime version: passing the runtime's
    # "6.8" here asked build-bundle for a ref flatpak-builder never wrote (it
    # defaults to "master"), and the whole build failed at the very last step.
    # One variable drives both commands so they cannot disagree again.
    local branch="stable"
    if [[ -e "$build_root" ]]; then
        safe_remove_tree "$build_root" "$ROOT_DIR"
    fi
    reset_package_directory "$DIST_DIR"
    mkdir -p "$ARTIFACT_DIR" "$state_dir"
    stage_flatpak_assets
    local -a builder_args=(
        --force-clean --state-dir "$state_dir" --repo="$repo"
        --default-branch="$branch"
    )
    if ! flatpak_rofiles_fuse_works; then
        log "rofiles-fuse cannot mount here; building without it"
        builder_args+=(--disable-rofiles-fuse)
    fi
    flatpak-builder "${builder_args[@]}" \
        "$build_root" "$ROOT_DIR/packaging/flatpak/com.rincolabs.HazorStudio.yml"
    flatpak build-bundle "$repo" "$output" com.rincolabs.HazorStudio "$branch"
    log "Flatpak bundle created: $output"
}

copy_mingw_runtime() {
    local destination="$1"
    local compiler="$HAZOR_MINGW_TRIPLE-g++"
    local runtime path sysroot
    sysroot="$("$compiler" -print-sysroot 2>/dev/null || true)"
    for runtime in libstdc++-6.dll libgcc_s_seh-1.dll libwinpthread-1.dll; do
        # -print-file-name searches GCC's library directories and returns the
        # name unchanged when it finds nothing. libstdc++ and libgcc do live
        # there, but the winpthreads DLL is installed under the target's bin/,
        # so that one needs the sysroot sweep below.
        path="$("$compiler" -print-file-name="$runtime")"
        if [[ ! -f "$path" && -n "$sysroot" ]]; then
            path="$(find "$sysroot" -name "$runtime" -print -quit 2>/dev/null || true)"
        fi
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
    find "$OPENCV_PREFIX" -type f -name 'libopencv*.dll' -exec cp -f {} "$DIST_DIR/" \;
    find "$DIST_DIR" -maxdepth 1 -name 'libopencv_core*.dll' -print -quit | grep -q . \
        || die "OpenCV DLLs not found under $OPENCV_PREFIX"
    copy_mingw_runtime "$DIST_DIR"

    # Windows resolves a DLL from the executable's own directory, so the AI
    # runtime and the one shared RawEngine dependency go beside Hazor.exe
    # rather than under 3rdparty/. The CUDA and TensorRT provider DLLs ship
    # too: they are runtime execution providers selected in Settings, never a
    # separate build (AGENTS.md invariant 10).
    if [[ -d "$ONNXRUNTIME_DIR/lib" ]]; then
        find "$ONNXRUNTIME_DIR/lib" -maxdepth 1 -type f -name 'onnxruntime*.dll' \
            -exec cp -f {} "$DIST_DIR/" \;
        require_file "$DIST_DIR/onnxruntime.dll" "packaged ONNX Runtime DLL"
    fi
    if rawengine_was_built; then
        find "$RAWENGINE_DEPS_DIR/exiv2/bin" "$RAWENGINE_DEPS_DIR/exiv2/lib" \
            -maxdepth 1 -type f -name 'libexiv2*.dll' -exec cp -f {} "$DIST_DIR/" \; 2>/dev/null || true
        find "$DIST_DIR" -maxdepth 1 -name 'libexiv2*.dll' -print -quit | grep -q . \
            || die "RawEngine was built but its exiv2 DLL was not found under $RAWENGINE_DEPS_DIR/exiv2"
        # Lens Corrections needs lensfun's camera/lens database at runtime.
        # This layout is load-bearing: AppPaths::lensfunDatabaseDir() looks for
        # share/lensfun/version_1 beside the executable and hands it to
        # rtengine as lensfunDbBundleDirectory. Do not rely on lensfun's own
        # module-relative search to find it — that path hunts for a
        # liblensfun.dll, and Hazor links lensfun statically.
        require_dir "$RAWENGINE_DEPS_DIR/lensfun/share/lensfun" "lensfun database"
        mkdir -p "$DIST_DIR/share"
        cp -aL "$RAWENGINE_DEPS_DIR/lensfun/share/lensfun" "$DIST_DIR/share/"
    fi

    copy_runtime_assets "$DIST_DIR/3rdparty"
    write_runtime_notices "$DIST_DIR/licenses"
    require_file "$DIST_DIR/platforms/qwindows.dll" "Qt Windows platform plugin"

    # Debug sections carry the build machine's directory layout — not from
    # Hazor's own sources (-ffile-prefix-map already covers those) but from
    # the libgcc and mingw-w64 CRT objects every PE links, which were compiled
    # when prepare.sh built the cross toolchain. A distribution build has no
    # use for them either way.
    local binary
    while IFS= read -r -d '' binary; do
        "$HAZOR_MINGW_TRIPLE-strip" --strip-all "$binary" 2>/dev/null || true
    done < <(find "$DIST_DIR" -maxdepth 2 -type f \( -name '*.exe' -o -name '*.dll' \) -print0)

    local output
    output="$(artifact_path Windows-x64 zip)"
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
    local output
    output="$(artifact_path "macOS-$TARGET_ARCH" zip)"
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
