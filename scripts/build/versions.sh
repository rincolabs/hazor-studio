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
readonly HAZOR_REALESRGAN_VERSION="20220424"
readonly HAZOR_SAM_VERSION="20230629"
readonly HAZOR_BIREFNET_VERSION="20240101"
readonly HAZOR_MATERIAL_ICONS_VERSION="3.0.1"
readonly HAZOR_OSXCROSS_COMMIT="27d21e4977c9751d01199c7a226a6faf494c3dd9"

readonly HAZOR_OPENCV_GIT_URL="https://github.com/opencv/opencv.git"
readonly HAZOR_ZLIB_GIT_URL="https://github.com/madler/zlib.git"
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
