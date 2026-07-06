#pragma once
#include <QColor>
#include <QPointF>
#include <QString>
#include <QImage>
#include <QSize>

#include "DynamicsConfig.hpp"

enum class BrushType {
    Round = 0,
    Square = 1,
};

enum class BrushTipSource {
    Circle = 0,
    Image = 1,
};

enum class BrushMode {
    Paint = 0,
    Erase = 1,
};

// How overlapping dabs within ONE stroke combine (the standard BuildUp/Wash
// split between accumulate-past-opacity and opacity-ceiling modes):
//   BuildUp — each dab is composited straight onto the layer; overlapping dabs in
//             a stroke keep accumulating past the opacity setting (airbrush feel).
//   Wash    — dabs accumulate on a per-stroke buffer with FLOW, and the buffer is
//             laid onto the layer capped at OPACITY, so a stroke never darkens
//             past its opacity (inking feel). At opacity 100% this is identical to
//             BuildUp (Porter-Duff "over" is associative), so 100%-opacity presets
//             are unaffected.
enum class BrushPaintMode {
    BuildUp = 0,  // default: preserves the historical accumulate-past-opacity feel
                  // (pencils, airbrush, textured tonal build-up rely on it)
    Wash = 1,     // opt-in: opacity ceiling per stroke (inking)
};

// How an image tip's pixels are turned into paint (brush application modes).
// Only image tips care; a procedural Circle/Square tip is
// always AlphaMask. Default is AlphaMask so every existing preset is unchanged.
enum class BrushApplication {
    AlphaMask = 0,  // tip = coverage; paint the brush colour (ALPHAMASK)
    ColorStamp,     // paint the tip's own RGBA colours (IMAGESTAMP)
    LightnessMap,   // paint the brush colour modulated by tip lightness (LIGHTNESSMAP)
};

enum class CloneSampleMode {
    CurrentLayer = 0,
    CurrentAndBelow,
    AllLayers,
};

struct CloneStampContext {
    QImage sourceImage;
    QPointF sourceOffset;
    QSize documentSize;

    // Healing Brush extension. When healing is true the dab transfers the
    // high-frequency detail (texture) of the source while adopting the
    // low-frequency tone/luminance of the destination (frequency separation),
    // instead of copying source pixels verbatim like the Clone Stamp. The same
    // stroke-start snapshot (sourceImage) provides both the source texture
    // (sampled at sourceOffset) and the destination tone (sampled at the dab
    // position), which keeps healing free of self-sampling on an empty layer.
    bool healing = false;
    float diffusion = 0.5f;  // 0 = preserve source detail, 1 = blend into destination

    bool isValid() const {
        return !sourceImage.isNull() && documentSize.isValid() && !documentSize.isEmpty();
    }
};

struct BrushSettings {
    float size = 20.0f;
    float hardness = 0.8f;
    float opacity = 1.0f;
    float flow = 1.0f;
    QColor color = Qt::black;
    BrushType type = BrushType::Round;
    BrushTipSource tipSource = BrushTipSource::Circle;
    QString tipImagePath;
    QString tipImageData;
    QImage tipImage;
    // Reference to a tip in the global library (BrushTipLibrary). tipImage/Path/Data
    // resolve the actual pixels; tipId is what the preset persists so tips stay
    // reusable resources rather than being trapped inside one brush (mirrors
    // textureConfig.textureId). Empty for the procedural Circle/Square tip.
    QString tipId;
    BrushMode mode = BrushMode::Paint;

    // For image tips: how the tip's pixels become paint. AlphaMask (the default)
    // keeps the historical behaviour of carimbando the brush colour through the
    // tip's coverage; ColorStamp paints the tip's own colours (brushes with
    // their own RGBA). Ignored for procedural Circle/Square tips.
    BrushApplication application = BrushApplication::AlphaMask;

    // Lightness remap for a LightnessMap image tip (brightness/contrast
    // adjustment). Neutral by default (brightness 0, contrast 0, midpoint 0.5) so
    // existing tips are unchanged; the importer fills these from the preset.
    float tipBrightness = 0.0f;   // -1..1
    float tipContrast = 0.0f;     // -1..1
    float tipMidpoint = 0.5f;     // 0..1

    // Procedural-tip (AutoBrush) knobs beyond `type`/`hardness`/`roundness`/`angle`.
    // Defaults reproduce the legacy round/square dab, so existing presets paint
    // identically; the importer maps <MaskGenerator> onto this.
    AutoBrushConfig autoBrush;

    // Static dab shape (Brush Tip Shape). These are the base values the dynamics
    // jitter then varies around; both the canvas and the preview read them
    // through DynamicsEvaluator, so the dab looks identical in both.
    float angle = 0.0f;      // dab rotation, radians
    float roundness = 1.0f;  // 0..1 (1 = circular; <1 flattens the dab)
    bool flipX = false;      // mirror the dab horizontally (matters for image tips)
    bool flipY = false;      // mirror the dab vertically

    ColorDynamics colorDynamics;
    TextureConfig textureConfig;
    DualBrushConfig dualBrushConfig;

    // Curve-driven dynamics: every per-stroke variation is a CurveOption driving
    // its axis through sensors + an editable response curve. Disabled options
    // are a no-op, so a static preset paints with the base values.
    CurveOption sizeOption;
    CurveOption opacityOption;
    CurveOption flowOption;
    CurveOption rotationOption;   // replaces the base angle (tip angle folded in)
    CurveOption ratioOption;      // scales roundness
    // Scatter offset per dab: maxValue is the strength (0..5, fraction of the dab
    // diameter), sensors modulate it, and `scatter` picks the axes.
    CurveOption scatterOption;
    ScatterConfig scatter;

    BrushBlendMode blendMode = BrushBlendMode::Normal;
    // BuildUp by default so existing brushes (pencils, airbrush, textured
    // build-up) keep their feel. Inking presets opt into Wash for an opacity
    // ceiling per stroke.
    BrushPaintMode paintMode = BrushPaintMode::BuildUp;
    SmoothingMode smoothingMode = SmoothingMode::Basic;
    float smoothingRadius = 10.0f;
    float spacing = 0.1f;
    // Auto-spacing: when on, the step is derived from the dab size
    // (relatively denser for large brushes, like air-brushing) instead of the
    // fixed `spacing` fraction. Off by default → existing presets are unchanged.
    bool useAutoSpacing = false;
    float autoSpacingCoeff = 1.0f;
    // Airbrush: keep emitting dabs at `airbrushRate` dabs/second while the
    // pointer is held down, even without movement.
    bool airbrush = false;
    float airbrushRate = 20.0f;
    bool wetEdges = false;
};

// "Use Pen Pressure" state: pressure is on when any of the Size/Opacity/Flow
// curve options has an active Pressure sensor.
inline bool brushPressureEnabled(const BrushSettings& s) {
    return s.sizeOption.hasActivePressureSensor()
        || s.opacityOption.hasActivePressureSensor()
        || s.flowOption.hasActivePressureSensor();
}

struct StrokeState {
    QPointF lastPoint;
    QPointF smoothPoints[3];
    int smoothCount = 0;
    float accumulatedDistance = 0.0f;
};

inline float brushFalloff(float d, float hardness, float aaWidth)
{
    if (d >= 1.0f) return 0.0f;
    if (hardness >= 1.0f) {
        if (d >= 1.0f) return 0.0f;
        if (d <= 1.0f - aaWidth) return 1.0f;
        float t = (d - (1.0f - aaWidth)) / aaWidth;
        return 1.0f - (3.0f - 2.0f * t) * t * t;
    }
    float edge = hardness;
    if (d <= edge) return 1.0f;
    float t = (d - edge) / (1.0f - edge);
    float base = 1.0f - t * t;
    if (d > 1.0f - aaWidth) {
        float aa_t = (d - (1.0f - aaWidth)) / aaWidth;
        base *= 1.0f - (3.0f - 2.0f * aa_t) * aa_t * aa_t;
    }
    return base;
}
