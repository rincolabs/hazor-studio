#include "rtcompat/glib/ustring.hpp"

#include <cctype>
#include <cstdint>

namespace Glib {

namespace {

char asciiToLower(char c)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

char asciiToUpper(char c)
{
    return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
}

} // namespace

ustring ustring::casefold() const
{
    return lowercase();
}

ustring ustring::lowercase() const
{
    std::string result(*this);
    for (char& c : result) {
        c = asciiToLower(c);
    }
    return ustring(std::move(result));
}

ustring ustring::uppercase() const
{
    std::string result(*this);
    for (char& c : result) {
        c = asciiToUpper(c);
    }
    return ustring(std::move(result));
}

bool ustring::validate() const
{
    const auto* bytes = reinterpret_cast<const unsigned char*>(data());
    const std::size_t len = size();
    std::size_t i = 0;

    while (i < len) {
        const unsigned char lead = bytes[i];

        if (lead < 0x80) {
            ++i;
            continue;
        }

        std::size_t continuationCount;
        unsigned char minLead;

        if ((lead & 0xE0) == 0xC0) {
            continuationCount = 1;
            minLead = 0xC2; // 0xC0/0xC1 would encode an overlong 1-byte value.
        } else if ((lead & 0xF0) == 0xE0) {
            continuationCount = 2;
            minLead = 0xE0;
        } else if ((lead & 0xF8) == 0xF0) {
            continuationCount = 3;
            minLead = 0xF0;
        } else {
            return false;
        }

        if (lead < minLead || lead > 0xF4) {
            return false;
        }

        if (i + continuationCount >= len) {
            return false;
        }

        for (std::size_t k = 1; k <= continuationCount; ++k) {
            const unsigned char cont = bytes[i + k];
            if ((cont & 0xC0) != 0x80) {
                return false;
            }
        }

        // Reject UTF-16 surrogate code points encoded in UTF-8.
        if (continuationCount == 2) {
            const uint32_t codepoint =
                (static_cast<uint32_t>(lead & 0x0F) << 12) |
                (static_cast<uint32_t>(bytes[i + 1] & 0x3F) << 6) |
                static_cast<uint32_t>(bytes[i + 2] & 0x3F);
            if (codepoint >= 0xD800 && codepoint <= 0xDFFF) {
                return false;
            }
        }
        if (continuationCount == 3) {
            const uint32_t codepoint =
                (static_cast<uint32_t>(lead & 0x07) << 18) |
                (static_cast<uint32_t>(bytes[i + 1] & 0x3F) << 12) |
                (static_cast<uint32_t>(bytes[i + 2] & 0x3F) << 6) |
                static_cast<uint32_t>(bytes[i + 3] & 0x3F);
            if (codepoint > 0x10FFFF) {
                return false;
            }
        }

        i += continuationCount + 1;
    }

    return true;
}

} // namespace Glib
