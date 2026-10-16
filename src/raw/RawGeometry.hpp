#pragma once

#include <optional>

#include <QTransform>

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

    // False when the PP3 that produced this render used fine (sub-degree)
    // rotation (RotateParams::degree != 0) or an active perspective
    // correction (PerspectiveParams::render) — neither is representable by
    // the fields above, so a reframeTransform() computed against a geometry
    // with this false can't be trusted (see reframeTransform() below).
    bool reframeExact = true;
};

// Derives the affine `M` that maps a point in `before`'s rendered-pixel
// space to the corresponding point in `after`'s rendered-pixel space —
// i.e. the transform a re-reveal's mask/node-transform compensation needs
// (Fase 4.4) so the composited result doesn't move when only coarse
// rotation/flip/crop/resize changed between the two renders.
//
// Returns std::nullopt when M is not derivable from these fields alone:
// either geometry has reframeExact == false (fine rotation/perspective —
// genuinely not representable here), or the two geometries don't agree on
// the underlying sensor frame size (shouldn't happen for renders of the
// same RawSource; treated as non-derivable rather than guessed). Callers
// must not substitute an identity transform in that case — see the
// "Geometria" verification in tmp/feat-raw-therapee-plan.md.
std::optional<QTransform> reframeTransform(const RawGeometry& before, const RawGeometry& after);
