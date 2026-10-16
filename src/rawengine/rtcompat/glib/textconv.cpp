#include "rtcompat/glib/textconv.hpp"

#include <cstdlib>
#include <cstring>
#include <stdexcept>

namespace rtcompat {

namespace {

std::uint32_t decodeOneUtf8(const unsigned char* bytes, std::size_t len, std::size_t& index)
{
    const unsigned char lead = bytes[index];
    std::uint32_t codepoint;
    std::size_t extra;

    if (lead < 0x80) {
        codepoint = lead;
        extra = 0;
    } else if ((lead & 0xE0) == 0xC0) {
        codepoint = lead & 0x1F;
        extra = 1;
    } else if ((lead & 0xF0) == 0xE0) {
        codepoint = lead & 0x0F;
        extra = 2;
    } else if ((lead & 0xF8) == 0xF0) {
        codepoint = lead & 0x07;
        extra = 3;
    } else {
        throw std::runtime_error("rtcompat: invalid UTF-8 lead byte");
    }

    if (index + extra >= len) {
        throw std::runtime_error("rtcompat: truncated UTF-8 sequence");
    }

    for (std::size_t k = 1; k <= extra; ++k) {
        const unsigned char cont = bytes[index + k];
        if ((cont & 0xC0) != 0x80) {
            throw std::runtime_error("rtcompat: invalid UTF-8 continuation byte");
        }
        codepoint = (codepoint << 6) | (cont & 0x3F);
    }

    index += extra + 1;
    return codepoint;
}

} // namespace

int encodeUtf8(std::uint32_t cp, char* out)
{
    if (cp <= 0x7F) {
        out[0] = static_cast<char>(cp);
        return 1;
    }
    if (cp <= 0x7FF) {
        out[0] = static_cast<char>(0xC0 | (cp >> 6));
        out[1] = static_cast<char>(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp <= 0xFFFF) {
        out[0] = static_cast<char>(0xE0 | (cp >> 12));
        out[1] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out[2] = static_cast<char>(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = static_cast<char>(0xF0 | (cp >> 18));
    out[1] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
    out[2] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
    out[3] = static_cast<char>(0x80 | (cp & 0x3F));
    return 4;
}

std::u16string utf8ToUtf16(const std::string& utf8)
{
    std::u16string result;
    result.reserve(utf8.size());

    const auto* bytes = reinterpret_cast<const unsigned char*>(utf8.data());
    const std::size_t len = utf8.size();
    std::size_t i = 0;

    while (i < len) {
        std::uint32_t cp = decodeOneUtf8(bytes, len, i);
        if (cp <= 0xFFFF) {
            result.push_back(static_cast<char16_t>(cp));
        } else {
            cp -= 0x10000;
            result.push_back(static_cast<char16_t>(0xD800 + (cp >> 10)));
            result.push_back(static_cast<char16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }

    return result;
}

std::string utf16ToUtf8(const std::u16string& utf16)
{
    std::string result;
    result.reserve(utf16.size());

    const std::size_t len = utf16.size();
    std::size_t i = 0;

    while (i < len) {
        std::uint32_t cp = utf16[i];
        ++i;

        if (cp >= 0xD800 && cp <= 0xDBFF && i < len) {
            const std::uint32_t low = utf16[i];
            if (low >= 0xDC00 && low <= 0xDFFF) {
                cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                ++i;
            }
        }

        char encoded[4];
        const int n = encodeUtf8(cp, encoded);
        result.append(encoded, n);
    }

    return result;
}

} // namespace rtcompat

gunichar2* g_utf8_to_utf16(const gchar* str, long len, long* /*items_read*/, long* items_written, void* /*error*/)
{
    if (!str) {
        return nullptr;
    }

    const std::string input = (len < 0) ? std::string(str) : std::string(str, str + len);
    std::u16string utf16;
    try {
        utf16 = rtcompat::utf8ToUtf16(input);
    } catch (const std::runtime_error&) {
        return nullptr;
    }

    auto* buf = static_cast<gunichar2*>(std::malloc((utf16.size() + 1) * sizeof(gunichar2)));
    if (!buf) {
        return nullptr;
    }
    std::memcpy(buf, utf16.data(), utf16.size() * sizeof(gunichar2));
    buf[utf16.size()] = 0;

    if (items_written) {
        *items_written = static_cast<long>(utf16.size());
    }
    return buf;
}

gchar* g_utf16_to_utf8(const gunichar2* str, long len, long* /*items_read*/, long* items_written, void* /*error*/)
{
    if (!str) {
        return nullptr;
    }

    std::u16string input;
    if (len < 0) {
        while (str[input.size()] != 0) {
            input.push_back(static_cast<char16_t>(str[input.size()]));
        }
    } else {
        input.assign(reinterpret_cast<const char16_t*>(str), static_cast<std::size_t>(len));
    }

    const std::string utf8 = rtcompat::utf16ToUtf8(input);

    auto* buf = static_cast<gchar*>(std::malloc(utf8.size() + 1));
    if (!buf) {
        return nullptr;
    }
    std::memcpy(buf, utf8.data(), utf8.size());
    buf[utf8.size()] = '\0';

    if (items_written) {
        *items_written = static_cast<long>(utf8.size());
    }
    return buf;
}

gint g_unichar_to_utf8(gunichar c, gchar* outbuf)
{
    char encoded[4];
    const int n = rtcompat::encodeUtf8(c, encoded);
    if (outbuf) {
        std::memcpy(outbuf, encoded, n);
    }
    return n;
}

void g_free(void* ptr)
{
    std::free(ptr);
}

namespace Glib {

std::string filename_to_utf8(const std::string& opsysString)
{
    // POSIX filenames are treated as UTF-8 bytes already.
    return opsysString;
}

std::string filename_from_utf8(const std::string& utf8String)
{
    return utf8String;
}

std::string locale_to_utf8(const std::string& opsysString)
{
    return opsysString;
}

std::string convert_with_fallback(
    const std::string& str,
    const std::string& /*toCodeset*/,
    const std::string& /*fromCodeset*/,
    const std::string& /*fallback*/
)
{
    return str;
}

} // namespace Glib
