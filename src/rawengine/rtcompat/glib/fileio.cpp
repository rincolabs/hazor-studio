#include "rtcompat/glib/fileio.hpp"

#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <system_error>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>

#include "rtcompat/glib/textconv.hpp"
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#if defined(_WIN32)

FILE* g_fopen(const char* filename, const char* mode)
{
    const std::u16string wname = rtcompat::utf8ToUtf16(filename);
    const std::u16string wmode = rtcompat::utf8ToUtf16(mode);
    return _wfopen(reinterpret_cast<const wchar_t*>(wname.c_str()), reinterpret_cast<const wchar_t*>(wmode.c_str()));
}

int g_open(const char* filename, int flags, int mode)
{
    const std::u16string wname = rtcompat::utf8ToUtf16(filename);
    return _wopen(reinterpret_cast<const wchar_t*>(wname.c_str()), flags, mode);
}

int g_stat(const char* filename, GStatBuf* buf)
{
    const std::u16string wname = rtcompat::utf8ToUtf16(filename);
    return _wstat64(reinterpret_cast<const wchar_t*>(wname.c_str()), buf);
}

int g_remove(const char* filename)
{
    const std::u16string wname = rtcompat::utf8ToUtf16(filename);
    return _wremove(reinterpret_cast<const wchar_t*>(wname.c_str()));
}

#else

FILE* g_fopen(const char* filename, const char* mode)
{
    return std::fopen(filename, mode);
}

int g_open(const char* filename, int flags, int mode)
{
    return ::open(filename, flags, mode);
}

int g_stat(const char* filename, GStatBuf* buf)
{
    return ::stat(filename, buf);
}

int g_remove(const char* filename)
{
    return std::remove(filename);
}

#endif

char* g_file_read_link(const char* filename, void* /*error*/)
{
    std::error_code ec;
    const std::filesystem::path target = std::filesystem::read_symlink(filename, ec);
    if (ec) {
        return nullptr;
    }

    const std::string targetUtf8 = target.string();
    auto* buf = static_cast<char*>(std::malloc(targetUtf8.size() + 1));
    if (!buf) {
        return nullptr;
    }
    std::memcpy(buf, targetUtf8.data(), targetUtf8.size());
    buf[targetUtf8.size()] = '\0';
    return buf;
}
