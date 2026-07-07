#pragma once

#include "core/Layer.hpp" // BlendMode
#include <QPainter>

// ─────────────────────────────────────────────────────────────────────────
// Single source of truth for blend-mode semantics, shared by the CPU
// compositor (DocumentCompositor) and the GPU compositor (GPUViewport /
// LayerCompositor).
//
// Before this header the mapping was duplicated in three places that could
// (and did) drift apart:
//   - DocumentCompositor::compositionModeForBlend  (BlendMode → QPainter mode)
//   - GPUViewport blendModeMap[]                    (BlendMode → shader id)
//   - GPUViewport::needsShaderBlend / applyFixedBlend
//
// The shader-id numbering matches the `bm_blend` mode ids in the shared GLSL
// blend library (renderer/BlendShaderLib.hpp).
//
// This header maps each BlendMode to a backend primitive; the actual blend
// FORMULAS live in exactly two mirrored files:
//   - renderer/BlendMath.hpp       C++  (CPU compositor / projection / export)
//   - renderer/BlendShaderLib.hpp  GLSL (live GPU preview)
// Keep the three files consistent when adding a mode.
// ─────────────────────────────────────────────────────────────────────────
namespace blend {

// GPU blend shader selector (`uBlendMode`). Returns -1 for Normal, which is
// drawn with fixed-function blending (GL_SRC_ALPHA / GL_ONE_MINUS_SRC_ALPHA)
// instead of the blend shader.
inline int shaderId(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal:     return -1;
    case BlendMode::Overlay:    return 0;
    case BlendMode::HardLight:  return 1;
    case BlendMode::SoftLight:  return 2;
    case BlendMode::ColorDodge: return 3;
    case BlendMode::ColorBurn:  return 4;
    case BlendMode::Difference: return 5;
    case BlendMode::Exclusion:  return 6;
    case BlendMode::Hue:        return 7;
    case BlendMode::Saturation: return 8;
    case BlendMode::Color:      return 9;
    case BlendMode::Luminosity: return 10;
    case BlendMode::Multiply:   return 11;
    case BlendMode::Screen:     return 12;
    case BlendMode::Darken:     return 13;
    case BlendMode::Lighten:    return 14;
    }
    return -1;
}

// True when the mode cannot be expressed by fixed-function GL blending and
// must go through the blend shader. Everything except Normal.
inline bool needsShaderBlend(BlendMode m) { return m != BlendMode::Normal; }

// Whether QPainter exposes this mode natively as a CompositionMode. The four
// non-separable HSL modes (Hue/Saturation/Color/Luminosity) have no QPainter
// equivalent and must be composited manually, per pixel, by the CPU path.
inline bool painterHasNativeMode(BlendMode m)
{
    switch (m) {
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity:
        return false;
    default:
        return true;
    }
}

// QPainter::CompositionMode for the CPU path. For modes without a native
// QPainter equivalent this returns SourceOver; callers must check
// painterHasNativeMode() first and composite those manually.
inline QPainter::CompositionMode painterMode(BlendMode m)
{
    switch (m) {
    case BlendMode::Normal:     return QPainter::CompositionMode_SourceOver;
    case BlendMode::Multiply:   return QPainter::CompositionMode_Multiply;
    case BlendMode::Screen:     return QPainter::CompositionMode_Screen;
    case BlendMode::Overlay:    return QPainter::CompositionMode_Overlay;
    case BlendMode::Darken:     return QPainter::CompositionMode_Darken;
    case BlendMode::Lighten:    return QPainter::CompositionMode_Lighten;
    case BlendMode::ColorDodge: return QPainter::CompositionMode_ColorDodge;
    case BlendMode::ColorBurn:  return QPainter::CompositionMode_ColorBurn;
    case BlendMode::HardLight:  return QPainter::CompositionMode_HardLight;
    case BlendMode::SoftLight:  return QPainter::CompositionMode_SoftLight;
    case BlendMode::Difference: return QPainter::CompositionMode_Difference;
    case BlendMode::Exclusion:  return QPainter::CompositionMode_Exclusion;
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity:
        return QPainter::CompositionMode_SourceOver; // composited manually
    }
    return QPainter::CompositionMode_SourceOver;
}

} // namespace blend
