#!/usr/bin/env bash
# Compiles and links tmp/raw_engine_selftest.cpp in isolation, against the
# already-built src/raw/*.cpp.o and librawengine.a from the last
# ./scripts/build.sh run — does NOT invoke CMake and does NOT touch the
# Hazor target or scripts/build.sh. Run ./scripts/build.sh at least once
# first (ENABLE_RAWENGINE=ON, the default) so build/ has those objects.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT/build}"

RAW_OBJS_DIR="$BUILD_DIR/CMakeFiles/Hazor.dir/src/raw"
for f in RawEngine.cpp.o RawSession.cpp.o RawDevelopParams.cpp.o RawGeometry.cpp.o; do
    [ -f "$RAW_OBJS_DIR/$f" ] || { echo "Missing $RAW_OBJS_DIR/$f — run ./scripts/build.sh first." >&2; exit 1; }
done

# Same CXX_DEFINES/CXX_INCLUDES/CXX_FLAGS CMake used for the Hazor target
# (build/CMakeFiles/Hazor.dir/flags.make) — not re-derived by hand.
FLAGS_MAKE="$BUILD_DIR/CMakeFiles/Hazor.dir/flags.make"
CXX_DEFINES="$(sed -n 's/^CXX_DEFINES = //p' "$FLAGS_MAKE")"
CXX_INCLUDES="$(sed -n 's/^CXX_INCLUDES = //p' "$FLAGS_MAKE")"
CXX_FLAGS="$(sed -n 's/^CXX_FLAGS = //p' "$FLAGS_MAKE")"
eval "DEFINES=($CXX_DEFINES)"
eval "INCLUDES=($CXX_INCLUDES)"
eval "FLAGS=($CXX_FLAGS)"

echo "Compiling tmp/raw_engine_selftest.cpp..."
c++ "${FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}" \
    -c "$ROOT/tmp/raw_engine_selftest.cpp" -o "$ROOT/tmp/raw_engine_selftest.o"

echo "Linking tmp/raw_engine_selftest..."
RAWENGINE_DEPS="${RAWENGINE_DEPS_DIR:-$ROOT/3rdparty/rawengine}"
QT_LIB="$ROOT/3rdparty/Qt/6.8.3/gcc_64/lib"
(cd "$BUILD_DIR" && c++ -O3 -DNDEBUG \
    "$ROOT/tmp/raw_engine_selftest.o" \
    "$RAW_OBJS_DIR/RawEngine.cpp.o" \
    "$RAW_OBJS_DIR/RawSession.cpp.o" \
    "$RAW_OBJS_DIR/RawDevelopParams.cpp.o" \
    "$RAW_OBJS_DIR/RawGeometry.cpp.o" \
    -o "$ROOT/tmp/raw_engine_selftest" \
    -Wl,-rpath,"$QT_LIB:$RAWENGINE_DEPS/exiv2/lib" \
    "$QT_LIB/libQt6Concurrent.so.6.8.3" \
    "$QT_LIB/libQt6Gui.so.6.8.3" \
    "$QT_LIB/libQt6Core.so.6.8.3" \
    -ldl \
    src/rawengine/librawengine.a \
    src/rawengine/external/librawengine_libraw.a \
    src/rawengine/external/librawengine_cjson.a \
    src/rawengine/external/librawengine_jpeg_ijg.a \
    src/rawengine/external/librawengine_klt.a \
    "$RAWENGINE_DEPS/lcms2/lib/liblcms2.a" \
    "$RAWENGINE_DEPS/fftw3f/lib/libfftw3f.a" \
    "$RAWENGINE_DEPS/fftw3f/lib/libfftw3f_threads.a" \
    "$RAWENGINE_DEPS/exiv2/lib/libexiv2.so.0.28.9" \
    "$RAWENGINE_DEPS/expat/lib/libexpat.a" \
    "$RAWENGINE_DEPS/libpng/lib/libpng.a" \
    "$RAWENGINE_DEPS/libtiff/lib/libtiff.a" \
    "$RAWENGINE_DEPS/libjpeg-turbo/lib/libjpeg.a" \
    "$RAWENGINE_DEPS/zlib/lib/libz.a" \
    "$RAWENGINE_DEPS/lensfun/lib/liblensfun.a" \
    -lglib-2.0 \
    /usr/lib/gcc/x86_64-linux-gnu/11/libgomp.so \
    /usr/lib/x86_64-linux-gnu/libpthread.a)

echo "Built tmp/raw_engine_selftest"
