#pragma once

#include <array>
#include <QLatin1String>
#include <QString>

#include "core/LayerId.hpp"

// Central animation type vocabulary. Everything downstream (tracks, model,
// evaluator, serialization, timeline UI) refers to these enums — there are NO
// free-form property strings spread through the codebase. The string forms used
// for serialization and the value/interpolation metadata are all derived from
// the Property enum, in this one place.
namespace anim {

// Integer frame is the primary time reference. Conversions to seconds go through
// the model's fps (see AnimationModel::fps).
using Frame = int;

// Every property the animation system can drive. Transform is animated by its
// decomposed components (never by raw matrix elements) so it composes with the
// existing transform pipeline in a later stage.
enum class Property {
    PositionX, PositionY,
    ScaleX, ScaleY,
    Rotation,
    SkewX, SkewY,
    PivotX, PivotY,
    Opacity,
    Visibility,
    BlendMode,
};

enum class ValueType { Float, Bool, Enum };

enum class Interpolation {
    Linear,   // position, scale, rotation, skew, pivot, opacity
    Hold,     // step: visibility, blend mode, discrete properties
    Bezier,   // reserved: structure is ready; the curve editor comes later
};

// All properties, for iteration by the UI/serialization. Keep in sync with the
// enum above.
inline const std::array<Property, 12>& allProperties() {
    static const std::array<Property, 12> kAll = {
        Property::PositionX, Property::PositionY,
        Property::ScaleX,    Property::ScaleY,
        Property::Rotation,
        Property::SkewX,     Property::SkewY,
        Property::PivotX,    Property::PivotY,
        Property::Opacity,
        Property::Visibility,
        Property::BlendMode,
    };
    return kAll;
}

// The value carried by a property's keyframes.
inline ValueType valueType(Property p) {
    switch (p) {
    case Property::Visibility: return ValueType::Bool;
    case Property::BlendMode:  return ValueType::Enum;
    default:                   return ValueType::Float;
    }
}

// The interpolation a brand-new track uses for this property. Discrete
// properties hold; continuous ones interpolate linearly by default.
inline Interpolation defaultInterpolation(Property p) {
    switch (p) {
    case Property::Visibility:
    case Property::BlendMode: return Interpolation::Hold;
    default:                  return Interpolation::Linear;
    }
}

// Canonical, stable string id for serialization (Etapa 10). Do not reuse these
// literals elsewhere — go through this function.
inline QLatin1String propertyId(Property p) {
    switch (p) {
    case Property::PositionX:  return QLatin1String("transform.position.x");
    case Property::PositionY:  return QLatin1String("transform.position.y");
    case Property::ScaleX:     return QLatin1String("transform.scale.x");
    case Property::ScaleY:     return QLatin1String("transform.scale.y");
    case Property::Rotation:   return QLatin1String("transform.rotation");
    case Property::SkewX:      return QLatin1String("transform.skew.x");
    case Property::SkewY:      return QLatin1String("transform.skew.y");
    case Property::PivotX:     return QLatin1String("transform.pivot.x");
    case Property::PivotY:     return QLatin1String("transform.pivot.y");
    case Property::Opacity:    return QLatin1String("opacity");
    case Property::Visibility: return QLatin1String("visibility");
    case Property::BlendMode:  return QLatin1String("blendMode");
    }
    return QLatin1String("");
}

// Parse a serialized id back to a Property. Returns false for unknown ids so a
// loader can ignore an unsupported track instead of losing the document.
inline bool propertyFromId(const QString& id, Property& out) {
    for (Property p : allProperties()) {
        if (id == propertyId(p)) { out = p; return true; }
    }
    return false;
}

} // namespace anim
