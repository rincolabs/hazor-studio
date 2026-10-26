#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <ostream>
#include <string>
#include <utility>
#include <variant>

// Error type plus a `?`-operator propagation idiom used pervasively across
// this library. See PROVENANCE.md for origin details.

namespace psd {

// Error produced while parsing, decoding, encoding or writing PSD/PSB data.
// Parsers never throw on malformed input; every failure is reported through
// this type.
class PsdError {
public:
    enum class Kind {
        UnexpectedEof,
        InvalidSignature,
        UnsupportedVersion,
        LimitExceeded,
        Invalid,
        Decompress,
        Unsupported,
    };

    static PsdError unexpectedEof(size_t offset, size_t needed);
    static PsdError invalidSignature(std::string expected, std::array<uint8_t, 4> found);
    static PsdError unsupportedVersion(uint16_t version);
    static PsdError limitExceeded(std::string reason);
    static PsdError invalid(std::string message);
    static PsdError decompress(std::string message);
    static PsdError unsupported(std::string message);

    Kind kind() const { return m_kind; }

    // Fields of UnexpectedEof.
    size_t offset() const { return m_offset; }
    size_t needed() const { return m_needed; }
    // Fields of InvalidSignature.
    const std::string& expectedSignature() const { return m_text; }
    std::array<uint8_t, 4> foundSignature() const { return m_found; }
    // Field of UnsupportedVersion.
    uint16_t unsupportedVersionValue() const { return m_version; }
    // Message text for LimitExceeded / Invalid / Decompress / Unsupported.
    const std::string& text() const { return m_text; }

    // Human-readable message, equivalent to the Rust #[error(...)] text.
    std::string message() const;

    bool operator==(const PsdError& other) const {
        return m_kind == other.m_kind && m_offset == other.m_offset && m_needed == other.m_needed && m_text == other.m_text &&
               m_found == other.m_found && m_version == other.m_version;
    }
    bool operator!=(const PsdError& other) const { return !(*this == other); }

private:
    Kind m_kind = Kind::Invalid;
    size_t m_offset = 0;
    size_t m_needed = 0;
    std::string m_text;
    std::array<uint8_t, 4> m_found{};
    uint16_t m_version = 0;
};

// Debug-style printers (the equivalent of Rust's #[derive(Debug)], needed for
// Boost.Test's assertion diagnostics).
std::ostream& operator<<(std::ostream& os, PsdError::Kind kind);
std::ostream& operator<<(std::ostream& os, const PsdError& error);

// Minimal Expected<T, E>: this project targets C++20, so std::expected is not
// available. Supports a Rust-Result-like shape (Result<T, PsdError>),
// including a void specialization for Result<()>.
template <typename T, typename E>
class Expected {
public:
    Expected(T value) : m_data(std::in_place_index<0>, std::move(value)) {}
    Expected(E error) : m_data(std::in_place_index<1>, std::move(error)) {}

    bool hasValue() const { return m_data.index() == 0; }
    explicit operator bool() const { return hasValue(); }

    T& value() & { return std::get<0>(m_data); }
    const T& value() const& { return std::get<0>(m_data); }
    T value() && { return std::get<0>(std::move(m_data)); }

    T& operator*() & { return value(); }
    const T& operator*() const& { return value(); }
    T* operator->() { return &value(); }
    const T* operator->() const { return &value(); }

    const E& error() const& { return std::get<1>(m_data); }
    E error() && { return std::get<1>(std::move(m_data)); }

    bool operator==(const Expected& other) const { return m_data == other.m_data; }
    bool operator!=(const Expected& other) const { return !(*this == other); }

private:
    std::variant<T, E> m_data;
};

// Void specialization for Result<void> (Rust's Result<()>).
template <typename E>
class Expected<void, E> {
public:
    Expected() : m_error(std::nullopt) {}
    Expected(E error) : m_error(std::move(error)) {}

    bool hasValue() const { return !m_error.has_value(); }
    explicit operator bool() const { return hasValue(); }

    void value() const {}

    const E& error() const& { return *m_error; }
    E error() && { return std::move(*m_error); }

    bool operator==(const Expected& other) const { return m_error == other.m_error; }
    bool operator!=(const Expected& other) const { return !(*this == other); }

private:
    std::optional<E> m_error;
};

// Convenience alias, equivalent to a Rust `pub type Result<T, E = PsdError>`.
template <typename T>
using Result = Expected<T, PsdError>;

} // namespace psd

// --- `?`-operator equivalents -------------------------------------------
//
// The original implementation this library is based on chains fallible calls
// pervasively with Rust's `?`. These macros are this library's mechanical
// stand-in: PSD_TRY for a bare
// Result<void> call, PSD_TRY_ASSIGN to bind a successful Result<T>'s value.
// Both early-return the error, implicitly converted to the enclosing
// function's own Result<...> via Expected's single-argument E constructor.
// Scoped to src/psd/ only; standard C++ (no statement-expression extension),
// so it is portable to MSVC.

#define PSD_CONCAT_IMPL_(a, b) a##b
#define PSD_CONCAT_(a, b) PSD_CONCAT_IMPL_(a, b)

#define PSD_TRY(expr)                                                 \
    do {                                                              \
        auto PSD_CONCAT_(psd_try_, __LINE__) = (expr);                \
        if (!(PSD_CONCAT_(psd_try_, __LINE__)))                       \
            return std::move(PSD_CONCAT_(psd_try_, __LINE__)).error(); \
    } while (0)

#define PSD_TRY_ASSIGN(decl, expr)                                     \
    auto PSD_CONCAT_(psd_try_, __LINE__) = (expr);                     \
    if (!(PSD_CONCAT_(psd_try_, __LINE__)))                            \
        return std::move(PSD_CONCAT_(psd_try_, __LINE__)).error();     \
    decl = std::move(PSD_CONCAT_(psd_try_, __LINE__)).value()
