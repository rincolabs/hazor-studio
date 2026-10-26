#include "Error.hpp"

#include <sstream>

namespace psd {

PsdError PsdError::unexpectedEof(size_t offset, size_t needed) {
    PsdError e;
    e.m_kind = Kind::UnexpectedEof;
    e.m_offset = offset;
    e.m_needed = needed;
    return e;
}

PsdError PsdError::invalidSignature(std::string expected, std::array<uint8_t, 4> found) {
    PsdError e;
    e.m_kind = Kind::InvalidSignature;
    e.m_text = std::move(expected);
    e.m_found = found;
    return e;
}

PsdError PsdError::unsupportedVersion(uint16_t version) {
    PsdError e;
    e.m_kind = Kind::UnsupportedVersion;
    e.m_version = version;
    return e;
}

PsdError PsdError::limitExceeded(std::string reason) {
    PsdError e;
    e.m_kind = Kind::LimitExceeded;
    e.m_text = std::move(reason);
    return e;
}

PsdError PsdError::invalid(std::string message) {
    PsdError e;
    e.m_kind = Kind::Invalid;
    e.m_text = std::move(message);
    return e;
}

PsdError PsdError::decompress(std::string message) {
    PsdError e;
    e.m_kind = Kind::Decompress;
    e.m_text = std::move(message);
    return e;
}

PsdError PsdError::unsupported(std::string message) {
    PsdError e;
    e.m_kind = Kind::Unsupported;
    e.m_text = std::move(message);
    return e;
}

std::string PsdError::message() const {
    std::ostringstream os;
    switch (m_kind) {
    case Kind::UnexpectedEof:
        os << "unexpected end of data at offset " << m_offset << " (needed " << m_needed << " more bytes)";
        break;
    case Kind::InvalidSignature: {
        os << "invalid signature [";
        for (size_t i = 0; i < m_found.size(); ++i) {
            if (i) os << ", ";
            os << static_cast<int>(m_found[i]);
        }
        os << "], expected " << m_text;
        break;
    }
    case Kind::UnsupportedVersion:
        os << "unsupported file version " << m_version;
        break;
    case Kind::LimitExceeded:
        os << "limit exceeded: " << m_text;
        break;
    case Kind::Invalid:
        os << "invalid data: " << m_text;
        break;
    case Kind::Decompress:
        os << "decompression failed: " << m_text;
        break;
    case Kind::Unsupported:
        os << "unsupported: " << m_text;
        break;
    }
    return os.str();
}

std::ostream& operator<<(std::ostream& os, PsdError::Kind kind) {
    switch (kind) {
    case PsdError::Kind::UnexpectedEof: return os << "UnexpectedEof";
    case PsdError::Kind::InvalidSignature: return os << "InvalidSignature";
    case PsdError::Kind::UnsupportedVersion: return os << "UnsupportedVersion";
    case PsdError::Kind::LimitExceeded: return os << "LimitExceeded";
    case PsdError::Kind::Invalid: return os << "Invalid";
    case PsdError::Kind::Decompress: return os << "Decompress";
    case PsdError::Kind::Unsupported: return os << "Unsupported";
    }
    return os << "?";
}

std::ostream& operator<<(std::ostream& os, const PsdError& error) { return os << error.kind() << "(" << error.message() << ")"; }

} // namespace psd
