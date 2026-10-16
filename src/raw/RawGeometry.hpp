#pragma once

// Develop-space geometry of a rendered RawImageBuffer: how the pixels it
// carries relate to the sensor frame. The RAW Layer (Fase 4) uses this to
// map masks/transforms across a re-reveal; Fase 3 only records it.
struct RawGeometry {
    // Dimensions of the rendered buffer at full (1:1) scale, i.e. before any
    // further resize applied by the layer/compositor. For renderPreview()
    // this is the *preview's* own full-scale size (StagedImageProcessor's
    // getFullWidth/Height), not the downscaled preview buffer itself.
    int fullWidth = 0;
    int fullHeight = 0;

    // ProcParams::coarse — rotation/flip applied on top of the RAW file's
    // own embedded orientation (see RawMetadata::orientationDegrees).
    int coarseRotationDegrees = 0;     // one of 0, 90, 180, 270 (clockwise)
    bool mirrorHorizontal = false;
    bool mirrorVertical = false;

    // ProcParams::crop, in post-rotation pixel coordinates.
    bool cropEnabled = false;
    int cropX = 0;
    int cropY = 0;
    int cropWidth = 0;
    int cropHeight = 0;

    // ProcParams::resize. 1.0 when resize is disabled.
    double resizeScale = 1.0;
};
