#pragma once

// Gate macro for the native RAW engine (tmp/feat-raw-therapee-plan.md).
// Defined by the root CMakeLists.txt as HAVE_RAWENGINE=1 when
// ENABLE_RAWENGINE is ON and every RawEngine dependency was found (same
// optional-dependency pattern as HAVE_ONNXRUNTIME, invariant 10).
//
// Code outside src/raw/ must never check HAVE_RAWENGINE directly: call
// RawEngine::isAvailable() instead (see RawEngine.hpp), which also covers
// the "compiled in but initialize() was never called or failed" case. Every
// src/raw/*.cpp compiles in both configurations; when HAVE_RAWENGINE is not
// defined, rtengine headers are not even on the include path, so no raw/*.cpp
// may include anything under "rtengine/" or "rtcompat/" outside an
// `#ifdef HAVE_RAWENGINE` block.
