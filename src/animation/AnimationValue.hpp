#pragma once

#include <variant>

#include "AnimationTypes.hpp"

namespace anim {

// A single animated value. Float covers every transform component plus opacity;
// Bool is visibility; Enum (stored as int) is blend mode. Interpolation math
// (Etapa 5) operates on these; here it is just a typed, copyable holder.
class AnimationValue {
public:
    AnimationValue() : m_v(0.0f) {}
    explicit AnimationValue(float f) : m_v(f) {}
    explicit AnimationValue(bool b) : m_v(b) {}

    // Enum values (e.g. BlendMode) are stored as int. Named factory so it is not
    // confused with the bool/float constructors.
    static AnimationValue fromEnum(int e) {
        AnimationValue v;
        v.m_v = e;
        return v;
    }

    bool isFloat() const { return std::holds_alternative<float>(m_v); }
    bool isBool()  const { return std::holds_alternative<bool>(m_v); }
    bool isEnum()  const { return std::holds_alternative<int>(m_v); }

    ValueType type() const {
        if (isBool()) return ValueType::Bool;
        if (isEnum()) return ValueType::Enum;
        return ValueType::Float;
    }
    bool matches(ValueType t) const { return type() == t; }

    // Accessors with sane cross-type fallbacks (a malformed loaded value never
    // crashes the evaluator; it degrades gracefully).
    float asFloat() const {
        if (isFloat()) return std::get<float>(m_v);
        if (isBool())  return std::get<bool>(m_v) ? 1.0f : 0.0f;
        return static_cast<float>(std::get<int>(m_v));
    }
    bool asBool() const {
        if (isBool())  return std::get<bool>(m_v);
        if (isFloat()) return std::get<float>(m_v) != 0.0f;
        return std::get<int>(m_v) != 0;
    }
    int asEnum() const {
        if (isEnum())  return std::get<int>(m_v);
        if (isBool())  return std::get<bool>(m_v) ? 1 : 0;
        return static_cast<int>(std::get<float>(m_v));
    }

    bool operator==(const AnimationValue& o) const { return m_v == o.m_v; }
    bool operator!=(const AnimationValue& o) const { return !(*this == o); }

private:
    std::variant<float, bool, int> m_v;
};

} // namespace anim
