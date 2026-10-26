#include "PsdEngineData.hpp"

namespace psdtext {

namespace {
const EngineValue kInvalid;
}

EngineValue EngineValue::dict(std::vector<std::pair<QString, EngineValue>> items)
{
    EngineValue v;
    v.m_kind = Kind::Dict;
    v.m_dict = std::move(items);
    return v;
}

EngineValue EngineValue::array(std::vector<EngineValue> items)
{
    EngineValue v;
    v.m_kind = Kind::Array;
    v.m_array = std::move(items);
    return v;
}

EngineValue EngineValue::string(QString s)
{
    EngineValue v;
    v.m_kind = Kind::String;
    v.m_text = std::move(s);
    return v;
}

EngineValue EngineValue::integer(qint64 n)
{
    EngineValue v;
    v.m_kind = Kind::Int;
    v.m_int = n;
    return v;
}

EngineValue EngineValue::real(double d)
{
    EngineValue v;
    v.m_kind = Kind::Real;
    v.m_real = d;
    return v;
}

EngineValue EngineValue::boolean(bool b)
{
    EngineValue v;
    v.m_kind = Kind::Bool;
    v.m_bool = b;
    return v;
}

EngineValue EngineValue::name(QString s)
{
    EngineValue v;
    v.m_kind = Kind::Name;
    v.m_text = std::move(s);
    return v;
}

const EngineValue& EngineValue::get(const QString& key) const
{
    if (m_kind != Kind::Dict)
        return kInvalid;
    for (const auto& [k, v] : m_dict) {
        if (k == key)
            return v;
    }
    return kInvalid;
}

const EngineValue& EngineValue::path(std::initializer_list<const char*> keys) const
{
    const EngineValue* cur = this;
    for (const char* k : keys) {
        cur = &cur->get(QString::fromLatin1(k));
        if (!cur->isValid())
            return kInvalid;
    }
    return *cur;
}

double EngineValue::asReal(double fallback) const
{
    switch (m_kind) {
    case Kind::Int: return static_cast<double>(m_int);
    case Kind::Real: return m_real;
    default: return fallback;
    }
}

qint64 EngineValue::asInt(qint64 fallback) const
{
    switch (m_kind) {
    case Kind::Int: return m_int;
    case Kind::Real: return static_cast<qint64>(m_real);
    default: return fallback;
    }
}

bool EngineValue::asBool(bool fallback) const
{
    switch (m_kind) {
    case Kind::Bool: return m_bool;
    case Kind::Int: return m_int != 0;
    default: return fallback;
    }
}

QString EngineValue::asString(const QString& fallback) const
{
    if (m_kind == Kind::String || m_kind == Kind::Name)
        return m_text;
    return fallback;
}

namespace {

// A UTF-16BE string (with its FE FF mark already stripped by the caller) as
// a QString; raw ASCII bytes (no mark) decode byte-for-byte, matching how
// short ASCII-only literals are stored.
QString decodeEngineString(const std::vector<uint8_t>& raw)
{
    if (raw.size() >= 2 && raw[0] == 0xFE && raw[1] == 0xFF) {
        std::vector<char16_t> units;
        units.reserve((raw.size() - 2) / 2);
        for (size_t i = 2; i + 1 < raw.size(); i += 2)
            units.push_back(static_cast<char16_t>((raw[i] << 8) | raw[i + 1]));
        return QString::fromUtf16(units.data(), static_cast<qsizetype>(units.size()));
    }
    QString out;
    out.reserve(static_cast<int>(raw.size()));
    for (uint8_t b : raw)
        out.append(QChar(static_cast<ushort>(b)));
    return out;
}

constexpr int kMaxDepth = 64;

class Parser {
public:
    explicit Parser(std::span<const uint8_t> b) : m_b(b) {}

    EngineValue parseTop() { return value(0); }

private:
    std::span<const uint8_t> m_b;
    size_t m_i = 0;
    bool m_error = false;

    uint8_t peek(size_t off = 0) const
    {
        const size_t at = m_i + off;
        return at < m_b.size() ? m_b[at] : 0;
    }
    bool atEnd() const { return m_i >= m_b.size(); }

    void skipWs()
    {
        while (!atEnd() && (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n' || peek() == 0))
            ++m_i;
    }

    static bool isDelimiter(uint8_t c)
    {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n'
            || c == '[' || c == ']' || c == '<' || c == '>' || c == '(' || c == '/';
    }

    QString readName()
    {
        ++m_i; // '/'
        const size_t start = m_i;
        while (!atEnd() && !isDelimiter(peek()))
            ++m_i;
        QByteArray bytes(reinterpret_cast<const char*>(m_b.data() + start),
                          static_cast<int>(m_i - start));
        return QString::fromUtf8(bytes);
    }

    EngineValue value(int depth)
    {
        if (depth > kMaxDepth || m_error) {
            m_error = true;
            return EngineValue();
        }
        skipWs();
        if (atEnd()) {
            m_error = true;
            return EngineValue();
        }
        const uint8_t c = peek();
        if (c == '<' && peek(1) == '<') {
            m_i += 2;
            std::vector<std::pair<QString, EngineValue>> items;
            for (;;) {
                skipWs();
                if (atEnd()) { m_error = true; return EngineValue(); }
                if (peek() == '>' && peek(1) == '>') {
                    m_i += 2;
                    return EngineValue::dict(std::move(items));
                }
                if (peek() != '/') { m_error = true; return EngineValue(); }
                QString key = readName();
                EngineValue v = value(depth + 1);
                if (m_error) return EngineValue();
                items.emplace_back(std::move(key), std::move(v));
            }
        }
        if (c == '[') {
            ++m_i;
            std::vector<EngineValue> items;
            for (;;) {
                skipWs();
                if (atEnd()) { m_error = true; return EngineValue(); }
                if (peek() == ']') {
                    ++m_i;
                    return EngineValue::array(std::move(items));
                }
                items.push_back(value(depth + 1));
                if (m_error) return EngineValue();
            }
        }
        if (c == '(') {
            ++m_i;
            std::vector<uint8_t> raw;
            for (;;) {
                if (atEnd()) { m_error = true; return EngineValue(); }
                const uint8_t ch = peek();
                if (ch == '\\') {
                    if (m_i + 1 < m_b.size())
                        raw.push_back(m_b[m_i + 1]);
                    m_i += 2;
                } else if (ch == ')') {
                    ++m_i;
                    break;
                } else {
                    raw.push_back(ch);
                    ++m_i;
                }
            }
            return EngineValue::string(decodeEngineString(raw));
        }
        if (c == '/')
            return EngineValue::name(readName());

        const size_t start = m_i;
        while (!atEnd() && !isDelimiter(peek()))
            ++m_i;
        if (m_i == start) {
            m_error = true;
            return EngineValue();
        }
        const QByteArray tok(reinterpret_cast<const char*>(m_b.data() + start),
                              static_cast<int>(m_i - start));
        if (tok == "true") return EngineValue::boolean(true);
        if (tok == "false") return EngineValue::boolean(false);
        bool ok = false;
        if (!tok.contains('.') && !tok.contains('e') && !tok.contains('E')) {
            const qint64 iv = tok.toLongLong(&ok);
            if (ok) return EngineValue::integer(iv);
        }
        const double dv = tok.toDouble(&ok);
        if (ok) return EngineValue::real(dv);
        m_error = true;
        return EngineValue();
    }
};

} // namespace

EngineValue parseEngineData(std::span<const uint8_t> data)
{
    Parser p(data);
    EngineValue v = p.parseTop();
    return v;
}

} // namespace psdtext
