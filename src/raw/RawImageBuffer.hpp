#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "raw/RawGeometry.hpp"

// RawSession::renderPreview() hands back RGB8 — it is backed by
// StagedImageProcessor's PreviewImageListener, which only ever produces
// display-ready 8-bit images (monitor profile already applied), same
// depth as the compositor's own Format_RGBA8888. RawSession::renderFull()
// hands back RGBFloat — rtengine::processImage()'s native result, with the
// output ICC profile's tone curve applied but *not* truncated to 8 bits;
// that truncation is RawDevelopService's job (Fase 4.2), at a single named
// point, not inside the engine.
enum class RawPixelFormat {
    RGB8,
    RGBFloat,
};

// Result of a RawSession render call. Never leaks an rtengine type.
struct RawImageBuffer {
    int width = 0;
    int height = 0;
    RawPixelFormat format = RawPixelFormat::RGBFloat;

    // True scene-linear light vs. an output-profile tone curve already
    // baked in. Always false today: rtengine::processImage() always encodes
    // whichever ICC profile outputIccProfile names, and detecting a linear
    // TRC in that profile is not implemented (documented limitation, see
    // docs/RAW-ENGINE.md). renderPreview() is always display-encoded (false).
    bool linear = false;

    // Name/path of the ICC profile baked into the pixels (ColorManagementParams::outputProfile).
    std::string outputIccProfile;

    RawGeometry geometry;

    // Interleaved RGB, row-major, no padding. Exactly one of the two is
    // populated, matching `format`.
    std::vector<std::uint8_t> pixelsU8;   // RGB8:     3 bytes/pixel, [0, 255]
    std::vector<float> pixelsFloat;       // RGBFloat: 3 floats/pixel, [0.0, 65535.0]

    bool isEmpty() const { return width <= 0 || height <= 0; }
};
