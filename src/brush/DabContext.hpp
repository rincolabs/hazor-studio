#pragma once

#include <cstdint>

// ── Layer A/B/C shared per-dab types ──────────────────────────────────────
//
// These describe ONE dab after the stroke orchestrator (Layer C) has sampled
// the input and the dynamics (Layer B) have resolved the geometry. They carry
// no engine state and no Qt types, so the mask producers (Layer A) and the
// dynamics evaluator can share them without pulling in BrushSettings.

// Geometry of a single dab. scaleX/scaleY are multipliers on the base radius
// (roundness = scaleY/scaleX); rotation is in radians. flipX/flipY
// mirror the tip (matters for image tips and asymmetric mask generators).
struct DabShape {
    float scaleX = 1.0f;
    float scaleY = 1.0f;
    float rotation = 0.0f;   // radians
    bool flipX = false;
    bool flipY = false;
};

// Per-dab input snapshot read by the Layer-B sensors and handed to the Layer-A
// brush (for pipe-frame selection and per-dab procedural variation). All driving
// values are normalized to the 0..1 (or -1..1) ranges the sensors expect, except
// the stroke accumulators (raw px / seconds / dab count), which each sensor maps
// through its own configurable length. A mouse stroke arrives as pressure 1.0 /
// everything else neutral, so dynamics collapse to a no-op.
struct DabContext {
    float pressure = 1.0f;       // 0..1
    float tiltX = 0.0f;          // -1..1
    float tiltY = 0.0f;          // -1..1
    float tiltElevation = 1.0f;  // 0..1 (1 = pen perpendicular to the tablet)
    float tiltDirection = 0.5f;  // 0..1 (tilt azimuth atan2(-tiltX,tiltY), 0.5 = neutral)
    float speed = 0.0f;          // 0..1 (velocity through a soft cap)
    float drawingAngle = 0.5f;   // 0..1 (0.5 + strokeAngle/2π; 0.5 = stroke pointing right)
    float rotation = 0.0f;       // 0..1 (barrel rotation, 0..2pi wrapped)

    // Raw stroke accumulators; the Fade/Distance/Time sensors normalize them by
    // their own length setting.
    float strokeDistance = 0.0f; // px travelled since stroke start
    float strokeTime = 0.0f;     // seconds since stroke start
    int dabIndex = 0;            // dab sequence number within the stroke

    // Deterministic per-dab RNG for the Fuzzy sensor and per-dab procedural
    // noise. Mutable so a sensor can advance it from a const-evaluated
    // CurveOption; seeded per dab so seeded strokes stay reproducible.
    mutable uint32_t rngState = 0x9e3779b9u;

    // One random value held for the WHOLE stroke: every dab in a stroke reads
    // the same value, so a per-stroke rotation/scatter is steady along the
    // stroke instead of jittering each dab. Default 0.5 (neutral).
    float strokeRandom = 0.5f;

    float nextRandom() const {
        rngState = rngState * 1103515245u + 12345u;
        return static_cast<float>(rngState % 10000u) / 10000.0f;
    }
};
