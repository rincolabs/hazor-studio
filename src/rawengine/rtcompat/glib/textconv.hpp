#pragma once
// rtcompat/glib/textconv.hpp
//
// Stand-in for the handful of GLib C text-conversion functions rtengine
// calls directly (`#include <glib/gstdio.h>` / `<glibmm/convert.h>` sites),
// plus the UTF-8 <-> UTF-16 helper used internally by rtcompat/glib/fileio
// for wide-char filesystem calls on Windows.
//
// std::codecvt is deprecated since C++17, so this is a small conversion
// routine of our own rather than a wrapper around it (same reasoning as
// upstream avoiding Glib::ustring::casefold_collate_key()'s locale
// dependence — see ustring.hpp).
#include <cstdint>
#include <string>

#include "rtcompat/glib/ustring.hpp"

namespace rtcompat {

// Both throw std::runtime_error on malformed input.
std::u16string utf8ToUtf16(const std::string& utf8);
std::string utf16ToUtf8(const std::u16string& utf16);

// Encodes a single Unicode code point as UTF-8 into `out` (must have room
// for 4 bytes); returns the number of bytes written. Shared implementation
// behind g_unichar_to_utf8() and utf16ToUtf8().
int encodeUtf8(std::uint32_t cp, char* out);

} // namespace rtcompat

// --- GLib C API (glib/gstdio.h-adjacent conversion functions) ----------
//
// gchar/gint/gunichar/gunichar2 come from rtcompat/glib/ustring.hpp
// (included above), which defines the full glib basic-type alias set.

// Signature matches glib's g_utf8_to_utf16(); `items_read`/`items_written`
// are accepted for source compatibility but, matching every call site in
// the vendored tree (all pass NULL), are left unpopulated.
gunichar2* g_utf8_to_utf16(const gchar* str, long len, long* items_read, long* items_written, void* error);
gchar* g_utf16_to_utf8(const gunichar2* str, long len, long* items_read, long* items_written, void* error);
// Encodes a single Unicode code point as UTF-8 into outbuf (must have room
// for at least 4 bytes) and returns the number of bytes written.
gint g_unichar_to_utf8(gunichar c, gchar* outbuf);
void g_free(void* ptr);
using GFreeFunc = void (*)(void*);

// --- glibmm/convert.h -----------------------------------------------------

namespace Glib {

class Error; // see rtcompat/glib/misc.hpp

// Filenames are treated as UTF-8 bytes already on POSIX (the only platform
// this engine currently builds and runs on); the Windows branch in
// rtcompat/glib/pathutils.cpp exercises the wide-char path instead.
std::string filename_to_utf8(const std::string& opsysString);
std::string filename_from_utf8(const std::string& utf8String);
std::string locale_to_utf8(const std::string& opsysString);
std::string convert_with_fallback(
    const std::string& str,
    const std::string& toCodeset,
    const std::string& fromCodeset,
    const std::string& fallback
);

} // namespace Glib
