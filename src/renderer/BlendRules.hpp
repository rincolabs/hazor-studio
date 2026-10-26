#pragma once

#include "core/Layer.hpp" // BlendMode
#include <QPainter>
#include <array>

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
    case BlendMode::LinearBurn:   return 15;
    case BlendMode::LinearDodge:  return 16;
    case BlendMode::VividLight:   return 17;
    case BlendMode::LinearLight:  return 18;
    case BlendMode::PinLight:     return 19;
    case BlendMode::HardMix:      return 20;
    case BlendMode::DarkerColor:  return 21;
    case BlendMode::LighterColor: return 22;
    case BlendMode::Subtract:     return 23;
    case BlendMode::Divide:       return 24;
    // Dissolve carries no colour formula: bm_coverage() reads the id and
    // quantizes the source coverage instead (bm_blend returns the source).
    case BlendMode::Dissolve:     return 25;
    }
    return -1;
}

// True when the mode cannot be expressed by fixed-function GL blending and
// must go through the blend shader. Everything except Normal.
inline bool needsShaderBlend(BlendMode m) { return m != BlendMode::Normal; }

// Whether QPainter exposes this mode natively as a CompositionMode. The
// non-separable HSL modes, the light/arithmetic modes and Dissolve
// have no QPainter equivalent and must be composited manually, per pixel, by
// the CPU path (DocumentCompositor::compositeStage).
inline bool painterHasNativeMode(BlendMode m)
{
    switch (m) {
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity:
    case BlendMode::LinearBurn:
    case BlendMode::LinearDodge:
    case BlendMode::VividLight:
    case BlendMode::LinearLight:
    case BlendMode::PinLight:
    case BlendMode::HardMix:
    case BlendMode::DarkerColor:
    case BlendMode::LighterColor:
    case BlendMode::Subtract:
    case BlendMode::Divide:
    case BlendMode::Dissolve:
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
    default:
        // Composited manually — see painterHasNativeMode().
        return QPainter::CompositionMode_SourceOver;
    }
}

// ── Picker order ────────────────────────────────────────────────────────
// Display order and labels for every blend-mode combo, shared so the layer
// panel and the tool options cannot drift apart. BlendMode values are
// appended as modes are added (they are persisted), so the enum order is not
// a usable display order; this table groups the modes by what they do —
// darkening, lightening, contrast, inversion, component — with a null label
// marking a separator between groups. The order matches what users of other
// layered editors expect to find in this menu.
struct PickerEntry {
    BlendMode mode;
    const char* label; // nullptr → group separator
};

inline const std::array<PickerEntry, 32>& pickerEntries()
{
    static const std::array<PickerEntry, 32> kEntries{{
        {BlendMode::Normal,       "Normal"},
        {BlendMode::Dissolve,     "Dissolve"},
        {BlendMode::Normal,       nullptr},
        {BlendMode::Darken,       "Darken"},
        {BlendMode::Multiply,     "Multiply"},
        {BlendMode::ColorBurn,    "Color Burn"},
        {BlendMode::LinearBurn,   "Linear Burn"},
        {BlendMode::DarkerColor,  "Darker Color"},
        {BlendMode::Normal,       nullptr},
        {BlendMode::Lighten,      "Lighten"},
        {BlendMode::Screen,       "Screen"},
        {BlendMode::ColorDodge,   "Color Dodge"},
        {BlendMode::LinearDodge,  "Linear Dodge (Add)"},
        {BlendMode::LighterColor, "Lighter Color"},
        {BlendMode::Normal,       nullptr},
        {BlendMode::Overlay,      "Overlay"},
        {BlendMode::SoftLight,    "Soft Light"},
        {BlendMode::HardLight,    "Hard Light"},
        {BlendMode::VividLight,   "Vivid Light"},
        {BlendMode::LinearLight,  "Linear Light"},
        {BlendMode::PinLight,     "Pin Light"},
        {BlendMode::HardMix,      "Hard Mix"},
        {BlendMode::Normal,       nullptr},
        {BlendMode::Difference,   "Difference"},
        {BlendMode::Exclusion,    "Exclusion"},
        {BlendMode::Subtract,     "Subtract"},
        {BlendMode::Divide,       "Divide"},
        {BlendMode::Normal,       nullptr},
        {BlendMode::Hue,          "Hue"},
        {BlendMode::Saturation,   "Saturation"},
        {BlendMode::Color,        "Color"},
        {BlendMode::Luminosity,   "Luminosity"},
    }};
    return kEntries;
}

} // namespace blend
