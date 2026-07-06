#pragma once

#include <QPointF>
#include <cmath>

struct BrushInputState {
    QPointF imagePos;
    float pressure = 1.0f;
    float tiltX = 0.0f;
    float tiltY = 0.0f;
    float rotation = 0.0f;
    float tangentialPressure = 1.0f;
    float velocity = 0.0f;
    float direction = 0.0f;
    // Dab sequence number within the stroke (drives the Fade sensor).
    int dabIndex = 0;

    // Stroke accumulators the orchestrator (Layer C) fills per dab, so the
    // Distance/Time sensors read real values instead of 0. Raw units (px /
    // seconds); each sensor normalizes them by its own length setting. Default 0
    // keeps the preview and any non-stroke caller neutral.
    float strokeDistance = 0.0f;  // accumulated path length so far (px)
    float strokeTime = 0.0f;      // elapsed time since stroke start (seconds)

    // One random value fixed for the whole stroke (per-stroke fuzzy source).
    // The orchestrator sets it once at stroke start; every dab carries the same value.
    float strokeRandom = 0.5f;
};
