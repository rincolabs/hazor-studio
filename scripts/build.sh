#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" >/dev/null 2>&1 && pwd)"
ROOT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"

BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build}"
THIRD_PARTY_DIR="${THIRD_PARTY_DIR:-$ROOT_DIR/3rdparty}"

QT_DIR="${QT_DIR:-$THIRD_PARTY_DIR/Qt/6.8.3/gcc_64}"
ONNXRUNTIME_DIR="${ONNXRUNTIME_DIR:-$THIRD_PARTY_DIR/onnx/runtime/onnxruntime-linux-x64-gpu-1.26.0}"
REALESRGAN_DIR="${REALESRGAN_DIR:-$THIRD_PARTY_DIR/realesrgan}"
REALESRGAN_MODELS_DIR="${REALESRGAN_MODELS_DIR:-$THIRD_PARTY_DIR/realesrgan/models}"

require_dir() {
    local path="$1"
    local name="$2"

    if [[ ! -d "$path" ]]; then
        echo "Missing $name directory:"
        echo "  $path"
        exit 1
    fi
}

require_dir "$QT_DIR" "Qt"
require_dir "$ONNXRUNTIME_DIR" "ONNX Runtime"
require_dir "$REALESRGAN_DIR" "RealESRGAN"
require_dir "$REALESRGAN_MODELS_DIR" "RealESRGAN models"

echo "Project root: $ROOT_DIR"
echo "Build dir: $BUILD_DIR"
echo "Third-party dir: $THIRD_PARTY_DIR"
echo "Qt: $QT_DIR"
echo "ONNX Runtime: $ONNXRUNTIME_DIR"
echo "RealESRGAN: $REALESRGAN_DIR"
echo "RealESRGAN models: $REALESRGAN_MODELS_DIR"

mkdir -p "$BUILD_DIR"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="$QT_DIR" \
  -DONNXRUNTIME_ROOT="$ONNXRUNTIME_DIR" \
  -DONNXRUNTIME_INCLUDE_DIR="$ONNXRUNTIME_DIR/include" \
  -DONNXRUNTIME_LIBRARY="$ONNXRUNTIME_DIR/lib/libonnxruntime.so"

cmake --build "$BUILD_DIR" -j"$(nproc)" --target Hazor
