#pragma once

#include <QString>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

// Parser for "EngineData": the PostScript-like text engine document format
// stored inside a TySh layer block's "Txt " / "EngineData" raw bytes.
// Syntax: dictionaries `<< /Key value ... >>`, arrays `[ ... ]`, strings
// `( ... )` holding UTF-16BE with a FE FF byte-order mark and `\` escapes
// (the next raw byte, no octal), numbers, `true`/`false`, and `/Name`
// values.
//
// Independent of the src/psd/ library (which does not parse this format —
// see its PROVENANCE.md) and of Hazor's document model: it only builds a
// generic value tree.
namespace psdtext {

class EngineValue {
public:
    enum class Kind { Dict, Array, String, Int, Real, Bool, Name, Invalid };

    EngineValue() : m_kind(Kind::Invalid) {}

    static EngineValue dict(std::vector<std::pair<QString, EngineValue>> items);
    static EngineValue array(std::vector<EngineValue> items);
    static EngineValue string(QString s);
    static EngineValue integer(qint64 v);
    static EngineValue real(double v);
    static EngineValue boolean(bool v);
    static EngineValue name(QString s);

    Kind kind() const { return m_kind; }

    // Dict lookup; null (Kind::Invalid) EngineValue when absent or not a dict.
    const EngineValue& get(const QString& key) const;
    // Follows a chain of dict keys, stopping at the first miss.
    const EngineValue& path(std::initializer_list<const char*> keys) const;

    const std::vector<std::pair<QString, EngineValue>>& asDict() const { return m_dict; }
    const std::vector<EngineValue>& asArray() const { return m_array; }

    bool isValid() const { return m_kind != Kind::Invalid; }
    double asReal(double fallback = 0.0) const;
    qint64 asInt(qint64 fallback = 0) const;
    bool asBool(bool fallback = false) const;
    QString asString(const QString& fallback = QString()) const;

private:
    Kind m_kind;
    std::vector<std::pair<QString, EngineValue>> m_dict;
    std::vector<EngineValue> m_array;
    QString m_text; // String or Name payload.
    qint64 m_int = 0;
    double m_real = 0.0;
    bool m_bool = false;
};

// Parses an EngineData document (the top-level dictionary). Returns an
// invalid (Kind::Invalid) value on malformed input; never throws.
EngineValue parseEngineData(std::span<const uint8_t> data);

} // namespace psdtext
