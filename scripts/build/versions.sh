#!/usr/bin/env bash

# Single source of truth for local dependency provisioning. Keep versions aligned
# with the existing GitHub workflows unless a target has a documented exception.
readonly HAZOR_QT_VERSION="6.8.3"
readonly HAZOR_AQT_VERSION="3.3.0"
readonly HAZOR_OPENCV_VERSION="4.5.4"
readonly HAZOR_OPENCV_COMMIT="4223495e6cd67011f86b8ecd9be1fa105018f3b1"
readonly HAZOR_ZLIB_VERSION="1.3.2"
readonly HAZOR_ZLIB_COMMIT="da607da739fa6047df13e66a2af6b8bec7c2a498"
readonly HAZOR_ONNXRUNTIME_VERSION="1.26.0"
# leejet/stable-diffusion.cpp has no release versioning of its own (pinned by
# commit only, like HAZOR_OSXCROSS_COMMIT below); the short hash doubles as
# the version component of its install path under 3rdparty/targets/.
readonly HAZOR_SD_CPP_COMMIT="687915518efd69548965d24818f0d3bbbb903718"
readonly HAZOR_SD_CPP_GGML_COMMIT="a5915eb0e47adef21c3f25b889a008925ea838c2"
readonly HAZOR_SD_CPP_VERSION="${HAZOR_SD_CPP_COMMIT:0:12}"
# ggml-vulkan's build-time dependencies, vendored rather than required from the
# distribution. Both are header-only, install in seconds, and are pinned as the
# matched pair of one Vulkan SDK release — mixing versions between them is what
# produces "unknown SPIR-V capability" errors out of glslc.
#
# They are vendored because the floor is unreachable on supported hosts:
# ggml-vulkan needs VK_HEADER_VERSION >= 272 (VK_EXT_layer_settings) and Ubuntu
# 22.04 ships 204, while SPIRV-Headers is not in the KDE Flatpak SDK at all.
# glslc is the one Vulkan build dependency still taken from the system: it is a
# compiler (shaderc + glslang + SPIRV-Tools), far too heavy to build here.
readonly HAZOR_VULKAN_SDK_VERSION="1.4.321.0"
readonly HAZOR_VULKAN_HEADERS_COMMIT="2cd90f9d20df57eac214c148f3aed885372ddcfe"
readonly HAZOR_SPIRV_HEADERS_COMMIT="2a611a970fdbc41ac2e3e328802aed9985352dca"
readonly HAZOR_REALESRGAN_VERSION="20220424"
readonly HAZOR_SAM_VERSION="20230629"
readonly HAZOR_BIREFNET_VERSION="20240101"
readonly HAZOR_MATERIAL_ICONS_VERSION="3.0.1"
readonly HAZOR_OSXCROSS_COMMIT="27d21e4977c9751d01199c7a226a6faf494c3dd9"

readonly HAZOR_OPENCV_GIT_URL="https://github.com/opencv/opencv.git"
readonly HAZOR_ZLIB_GIT_URL="https://github.com/madler/zlib.git"
readonly HAZOR_SD_CPP_GIT_URL="https://github.com/leejet/stable-diffusion.cpp.git"
readonly HAZOR_SD_CPP_GGML_GIT_URL="https://github.com/leejet/ggml.git"
readonly HAZOR_VULKAN_HEADERS_GIT_URL="https://github.com/KhronosGroup/Vulkan-Headers.git"
readonly HAZOR_SPIRV_HEADERS_GIT_URL="https://github.com/KhronosGroup/SPIRV-Headers.git"
readonly HAZOR_OSXCROSS_GIT_URL="https://github.com/tpoechtrager/osxcross.git"

readonly HAZOR_ORT_LINUX_PACKAGE="onnxruntime-linux-x64-gpu-${HAZOR_ONNXRUNTIME_VERSION}"
readonly HAZOR_ORT_LINUX_URL="https://github.com/microsoft/onnxruntime/releases/download/v${HAZOR_ONNXRUNTIME_VERSION}/${HAZOR_ORT_LINUX_PACKAGE}.tgz"
readonly HAZOR_ORT_LINUX_SHA256="cb7df7ee2ca0f962c7ce7c839aeae36223d146a91fb4646d62fb0046f297479f"

readonly HAZOR_ORT_WINDOWS_PACKAGE="onnxruntime-win-x64-gpu-${HAZOR_ONNXRUNTIME_VERSION}"
readonly HAZOR_ORT_WINDOWS_URL="https://github.com/microsoft/onnxruntime/releases/download/v${HAZOR_ONNXRUNTIME_VERSION}/${HAZOR_ORT_WINDOWS_PACKAGE}.zip"
readonly HAZOR_ORT_WINDOWS_SHA256="1133b1bcb0fb6f82b1c5b470b7cc15f9080a58b27dbc7b579a1fd63125ec2a15"

readonly HAZOR_ORT_MACOS_ARM64_PACKAGE="onnxruntime-osx-arm64-${HAZOR_ONNXRUNTIME_VERSION}"
readonly HAZOR_ORT_MACOS_ARM64_URL="https://github.com/microsoft/onnxruntime/releases/download/v${HAZOR_ONNXRUNTIME_VERSION}/${HAZOR_ORT_MACOS_ARM64_PACKAGE}.tgz"
readonly HAZOR_ORT_MACOS_ARM64_SHA256="7a1280bbb1701ea514f71828765237e7896e0f2e1cd332f1f70dbd5c3e33aca3"

readonly HAZOR_REALESRGAN_LINUX_ARCHIVE="realesrgan-ncnn-vulkan-${HAZOR_REALESRGAN_VERSION}-ubuntu.zip"
readonly HAZOR_REALESRGAN_LINUX_URL="https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/${HAZOR_REALESRGAN_LINUX_ARCHIVE}"
readonly HAZOR_REALESRGAN_LINUX_SHA256="e5aa6eb131234b87c0c51f82b89390f5e3e642b7b70f2b9bbe95b6a285a40c96"

readonly HAZOR_REALESRGAN_WINDOWS_ARCHIVE="realesrgan-ncnn-vulkan-${HAZOR_REALESRGAN_VERSION}-windows.zip"
readonly HAZOR_REALESRGAN_WINDOWS_URL="https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/${HAZOR_REALESRGAN_WINDOWS_ARCHIVE}"
readonly HAZOR_REALESRGAN_WINDOWS_SHA256="abc02804e17982a3be33675e4d471e91ea374e65b70167abc09e31acb412802d"

readonly HAZOR_REALESRGAN_MACOS_ARCHIVE="realesrgan-ncnn-vulkan-${HAZOR_REALESRGAN_VERSION}-macos.zip"
readonly HAZOR_REALESRGAN_MACOS_URL="https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/${HAZOR_REALESRGAN_MACOS_ARCHIVE}"
readonly HAZOR_REALESRGAN_MACOS_SHA256="e0ad05580abfeb25f8d8fb55aaf7bedf552c375b5b4d9bd3c8d59764d2cc333a"

# Use the immutable Hugging Face revision that introduced the archive, not main.
readonly HAZOR_SAM_ARCHIVE="mobile_sam_${HAZOR_SAM_VERSION}.zip"
readonly HAZOR_SAM_URL="https://huggingface.co/vietanhdev/segment-anything-onnx-models/resolve/de48c3935ffc614d4f5120222d17367ad27d0306/${HAZOR_SAM_ARCHIVE}"
readonly HAZOR_SAM_SHA256="41aff2660b7531becfee21fb257c49933ddc892c554507bdb775bf504d443942"

readonly HAZOR_BIREFNET_FILE="model_fp16.onnx"
readonly HAZOR_BIREFNET_REVISION="f82954f197e4671c1934c01d7dd85b9687c011b9"
readonly HAZOR_BIREFNET_URL="https://huggingface.co/onnx-community/BiRefNet_lite-ONNX/resolve/${HAZOR_BIREFNET_REVISION}/onnx/${HAZOR_BIREFNET_FILE}"
readonly HAZOR_BIREFNET_SHA256="d39b897ceb16ae654c1731f3dba0cf9b368d9cae74b5a57459b455cc8bfec402"
# The download cache is shared by every component and several Hugging Face repos
# publish a file literally named model_fp16.onnx, so cache it under a name that
# cannot collide with another model's blob.
readonly HAZOR_BIREFNET_CACHE_FILE="birefnet-lite-${HAZOR_BIREFNET_REVISION:0:12}-${HAZOR_BIREFNET_FILE}"

# Linux-hosted MinGW-w64 cross toolchain, built from source because Ubuntu
# 22.04 only packages GCC 10 — whose libstdc++ ABI cannot consume Qt's
# win64_mingw libraries — and the 24.04 packages need a newer glibc than this
# host provides. The configuration mirrors the toolchain Qt published for its
# own Windows binaries (download.qt.io/development_releases/prebuilt/mingw_64
# ships MinGW-w64-x86_64-13.1.0-release-posix-seh-msvcrt-rt_v11-rev1):
# GCC 13.1.0, posix threads, SEH exceptions, MSVCRT runtime, mingw-w64 rt v11.
readonly HAZOR_MINGW_GCC_VERSION="13.1.0"
readonly HAZOR_MINGW_BINUTILS_VERSION="2.41"
readonly HAZOR_MINGW_W64_VERSION="11.0.1"
readonly HAZOR_MINGW_CRT="msvcrt"
readonly HAZOR_MINGW_TRIPLE="x86_64-w64-mingw32"

readonly HAZOR_MINGW_BINUTILS_ARCHIVE="binutils-${HAZOR_MINGW_BINUTILS_VERSION}.tar.xz"
readonly HAZOR_MINGW_BINUTILS_URL="https://ftp.gnu.org/gnu/binutils/${HAZOR_MINGW_BINUTILS_ARCHIVE}"
readonly HAZOR_MINGW_BINUTILS_SHA256="ae9a5789e23459e59606e6714723f2d3ffc31c03174191ef0d015bdf06007450"

readonly HAZOR_MINGW_GCC_ARCHIVE="gcc-${HAZOR_MINGW_GCC_VERSION}.tar.xz"
readonly HAZOR_MINGW_GCC_URL="https://ftp.gnu.org/gnu/gcc/gcc-${HAZOR_MINGW_GCC_VERSION}/${HAZOR_MINGW_GCC_ARCHIVE}"
# GPG-verified against the GCC release signature (Jakub Jelinek, 6C35B99309B5FA62).
readonly HAZOR_MINGW_GCC_SHA256="61d684f0aa5e76ac6585ad8898a2427aade8979ed5e7f85492286c4dfc13ee86"

readonly HAZOR_MINGW_W64_ARCHIVE="mingw-w64-v${HAZOR_MINGW_W64_VERSION}.tar.bz2"
readonly HAZOR_MINGW_W64_URL="https://downloads.sourceforge.net/project/mingw-w64/mingw-w64/mingw-w64-release/${HAZOR_MINGW_W64_ARCHIVE}"
readonly HAZOR_MINGW_W64_SHA256="3f66bce069ee8bed7439a1a13da7cb91a5e67ea6170f21317ac7f5794625ee10"

# ── RawEngine dependencies ───────────────────────────────────────────────────
# The nine libraries src/rawengine/ needs, pinned to the versions docs/BUILD.md
# records for the native Linux tree. Cross targets build every one of them from
# these sources; the pre-existing 3rdparty/rawengine/ tree is adopted as-is on
# linux-x86_64 (migrate_legacy_linux) rather than rebuilt.
#
# Versions deliberately do not track Hazor's own zlib/OpenCV: RawEngine's
# constraint is an exact, reproducible version per dependency.
readonly HAZOR_RAWENGINE_JPEG_VERSION="3.2.0"
readonly HAZOR_RAWENGINE_JPEG_URL="https://github.com/libjpeg-turbo/libjpeg-turbo/archive/refs/tags/${HAZOR_RAWENGINE_JPEG_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_JPEG_SHA256="980dd81f425082aa6d7c9e47fef27554ce7a9ffc8e2f6e863b97d263c5c50858"

readonly HAZOR_RAWENGINE_PNG_VERSION="1.6.59"
readonly HAZOR_RAWENGINE_PNG_URL="https://github.com/pnggroup/libpng/archive/refs/tags/v${HAZOR_RAWENGINE_PNG_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_PNG_SHA256="2540302a1844ad2b2b501977abecfa850f265f97b78f065a712ab4074a89f5b5"

readonly HAZOR_RAWENGINE_TIFF_VERSION="4.7.2"
readonly HAZOR_RAWENGINE_TIFF_URL="https://download.osgeo.org/libtiff/tiff-${HAZOR_RAWENGINE_TIFF_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_TIFF_SHA256="672bd7d10aee4606171afb864f3570b83340f6a33e2c186dc0512f7145ffdf6a"

readonly HAZOR_RAWENGINE_EXPAT_VERSION="2.9.0"
readonly HAZOR_RAWENGINE_EXPAT_URL="https://github.com/libexpat/libexpat/releases/download/R_2_9_0/expat-${HAZOR_RAWENGINE_EXPAT_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_EXPAT_SHA256="16afbb9cefead2aa278105cf27d9f597bde7fbf3dbb85015857ca7ca6a4e89ba"

readonly HAZOR_RAWENGINE_LCMS2_VERSION="2.16"
readonly HAZOR_RAWENGINE_LCMS2_URL="https://github.com/mm2/Little-CMS/releases/download/lcms${HAZOR_RAWENGINE_LCMS2_VERSION}/lcms2-${HAZOR_RAWENGINE_LCMS2_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_LCMS2_SHA256="d873d34ad8b9b4cea010631f1a6228d2087475e4dc5e763eb81acc23d9d45a51"

readonly HAZOR_RAWENGINE_FFTW_VERSION="3.3.10"
readonly HAZOR_RAWENGINE_FFTW_URL="https://www.fftw.org/fftw-${HAZOR_RAWENGINE_FFTW_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_FFTW_SHA256="56c932549852cddcfafdab3820b0200c7742675be92179e59e6215b340e26467"

# Built from source on cross targets. The native Linux tree uses the upstream
# prebuilt Linux release instead, which is why that one is not reproduced here.
readonly HAZOR_RAWENGINE_EXIV2_VERSION="0.28.9"
readonly HAZOR_RAWENGINE_EXIV2_URL="https://github.com/Exiv2/exiv2/archive/refs/tags/v${HAZOR_RAWENGINE_EXIV2_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_EXIV2_SHA256="700b76b97695b2fab4ef8c79619c68ae57d09e0c130724791cafbd39e0eb4aef"

readonly HAZOR_RAWENGINE_LENSFUN_VERSION="0.3.4"
readonly HAZOR_RAWENGINE_LENSFUN_URL="https://github.com/lensfun/lensfun/archive/refs/tags/v${HAZOR_RAWENGINE_LENSFUN_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_LENSFUN_SHA256="dafb39c08ef24a0e2abd00d05d7341b1bf1f0c38bfcd5a4c69cf5f0ecb6db112"

# glib-2.0 is lensfun's only external dependency. Native builds take it from
# the platform (Qt 6 links it too, so a second private copy in one process
# would be the defect rather than an isolation win), but Windows has no
# platform glib, so the cross target builds a minimal one — with its own two
# dependencies, and NLS disabled so libintl is not a third.
readonly HAZOR_RAWENGINE_LIBFFI_VERSION="3.4.6"
readonly HAZOR_RAWENGINE_LIBFFI_URL="https://github.com/libffi/libffi/releases/download/v${HAZOR_RAWENGINE_LIBFFI_VERSION}/libffi-${HAZOR_RAWENGINE_LIBFFI_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_LIBFFI_SHA256="b0dea9df23c863a7a50e825440f3ebffabd65df1497108e5d437747843895a4e"

readonly HAZOR_RAWENGINE_PCRE2_VERSION="10.44"
readonly HAZOR_RAWENGINE_PCRE2_URL="https://github.com/PCRE2Project/pcre2/releases/download/pcre2-${HAZOR_RAWENGINE_PCRE2_VERSION}/pcre2-${HAZOR_RAWENGINE_PCRE2_VERSION}.tar.gz"
readonly HAZOR_RAWENGINE_PCRE2_SHA256="86b9cb0aa3bcb7994faa88018292bc704cdbb708e785f7c74352ff6ea7d3175b"

readonly HAZOR_RAWENGINE_GLIB_VERSION="2.82.5"
readonly HAZOR_RAWENGINE_GLIB_URL="https://download.gnome.org/sources/glib/2.82/glib-${HAZOR_RAWENGINE_GLIB_VERSION}.tar.xz"
readonly HAZOR_RAWENGINE_GLIB_SHA256="05c2031f9bdf6b5aba7a06ca84f0b4aced28b19bf1b50c6ab25cc675277cbc3f"

readonly HAZOR_MESON_VERSION="1.6.1"

readonly HAZOR_MATERIAL_ICONS_ARCHIVE="material-design-icons-${HAZOR_MATERIAL_ICONS_VERSION}.zip"
readonly HAZOR_MATERIAL_ICONS_URL="https://github.com/google/material-design-icons/releases/download/${HAZOR_MATERIAL_ICONS_VERSION}/${HAZOR_MATERIAL_ICONS_ARCHIVE}"
readonly HAZOR_MATERIAL_ICONS_SHA256="722e3b09121b82a3746f3da2ecd3a2db8d7d24153b8433324315695a45f06a90"

# Pinned replacements for the workflows' floating AppImage tool URLs.
readonly HAZOR_LINUXDEPLOY_VERSION="1-alpha-20251107-1"
readonly HAZOR_LINUXDEPLOY_URL="https://github.com/linuxdeploy/linuxdeploy/releases/download/${HAZOR_LINUXDEPLOY_VERSION}/linuxdeploy-x86_64.AppImage"
readonly HAZOR_LINUXDEPLOY_SHA256="c20cd71e3a4e3b80c3483cef793cda3f4e990aca14014d23c544ca3ce1270b4d"
readonly HAZOR_LINUXDEPLOY_QT_VERSION="1-alpha-20240109-1"
readonly HAZOR_LINUXDEPLOY_QT_URL="https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/${HAZOR_LINUXDEPLOY_QT_VERSION}/linuxdeploy-plugin-qt-x86_64.AppImage"
readonly HAZOR_LINUXDEPLOY_QT_SHA256="f53349093d333a6558c560844c1a0f64a3b6bd077bf02740af3ad3dbb8827433"
readonly HAZOR_APPIMAGETOOL_VERSION="1.9.1"
readonly HAZOR_APPIMAGETOOL_URL="https://github.com/AppImage/appimagetool/releases/download/${HAZOR_APPIMAGETOOL_VERSION}/appimagetool-x86_64.AppImage"
readonly HAZOR_APPIMAGETOOL_SHA256="ed4ce84f0d9caff66f50bcca6ff6f35aae54ce8135408b3fa33abfc3cb384eb0"
readonly HAZOR_APPIMAGE_RUNTIME_URL="https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-x86_64"
readonly HAZOR_APPIMAGE_RUNTIME_SHA256="617aa77314d57c8b8e4fae9c3f0b35af7b30d5529cb69e98f64f92510ed98a46"
