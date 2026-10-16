#pragma once
// rtcompat/glib/ustring.hpp
//
// Stand-in for <glibmm/ustring.h>, included by vendored rtengine sources via
// `#include "rtcompat/glib/ustring.hpp"` wherever they used to say
// `#include <glibmm/ustring.h>`.
//
// rtengine's own ~900 call sites (grep-verified) use Glib::ustring as a
// generic string: filenames, PP3 keys/values, camera and lens model names,
// translated-label keys. None of it relies on glibmm's UTF-8-character-aware
// semantics (character-counted length/indexing, locale-aware case folding).
// So `ustring` is simply `std::string` with the handful of extra members
// rtengine actually calls grafted on: casefold/lowercase/uppercase (ASCII
// fold only — documented limitation, see below), raw(), validate() (real
// UTF-8 validation), casefold_collate_key() and the static compose()
// formatter. Everything else (substr, find, operator+, comparisons, …)
// comes from the std::string base for free.
//
// ASCII-fold limitation: lowercase()/uppercase()/casefold() only fold
// US-ASCII letters. Real glibmm folds full Unicode. This is safe here
// because the only places that compare folded ustrings are filename
// extension checks and camera/lens model name matching (dcp.cc, lcp.cc,
// profilestore.cc, iccstore.cc, dfmanager.cc, ffmanager.cc) — all ASCII in
// practice for the camera databases this engine ships with.
#include <cfloat>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <utility>

// Real <glibmm/ustring.h> transitively includes <glib.h>, which is where
// MIN/MAX/ABS/CLAMP and G_DIR_SEPARATOR[_S] actually come from — rtengine
// itself never includes <glib.h> or <glib/gmacros.h> directly for these.
// Since every rtengine translation unit includes Glib::ustring (directly
// or transitively) already, this is the one rtcompat header guaranteed to
// be seen everywhere that transitive leak was relied upon.
#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif
#ifndef MAX
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#endif
#ifndef ABS
#define ABS(a) (((a) < 0) ? -(a) : (a))
#endif
#ifndef CLAMP
#define CLAMP(x, low, high) (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#endif

#if defined(_WIN32)
#define G_DIR_SEPARATOR '\\'
#define G_DIR_SEPARATOR_S "\\"
#else
#define G_DIR_SEPARATOR '/'
#define G_DIR_SEPARATOR_S "/"
#endif

// Likewise: real glib/gtypes.h's basic type aliases, transitively visible
// everywhere via <glib.h>. rtthumbnail.cc (guint32-sized width/height on
// its .rtti thumbnail cache format) and ashift_dt.c (the perspective-
// correction algorithm, pulled into perspectivecorrection.cc as
// upstream-adapted darktable C source — gboolean/gpointer/TRUE/FALSE)
// rely on this leak too.
using gchar = char;
using guchar = unsigned char;
using gshort = short;
using gushort = unsigned short;
using gint = int;
using guint = unsigned int;
using glong = long;
using gulong = unsigned long;
using gint8 = std::int8_t;
using guint8 = std::uint8_t;
using gint16 = std::int16_t;
using guint16 = std::uint16_t;
using gint32 = std::int32_t;
using guint32 = std::uint32_t;
using gint64 = std::int64_t;
using guint64 = std::uint64_t;
using gfloat = float;
using gdouble = double;
using gboolean = gint;
using gpointer = void*;
using gconstpointer = const void*;
using gunichar = guint32;
using gunichar2 = guint16;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

namespace Glib {

class ustring : public std::string {
public:
    using std::string::string;

    ustring() = default;
    ustring(const std::string& s) : std::string(s) {}
    ustring(std::string&& s) : std::string(std::move(s)) {}

    // Shadows std::string::substr(), which returns plain std::string —
    // rtengine chains .substr(...).casefold()/.lowercase() in several
    // places (dcp.cc, lcp.cc, profilestore.cc, utils.cc), which would
    // otherwise fail to compile (std::string has no casefold()).
    ustring substr(size_type pos = 0, size_type count = npos) const
    {
        return ustring(std::string::substr(pos, count));
    }

    ustring casefold() const;
    ustring lowercase() const;
    ustring uppercase() const;

    // Real glibmm returns the underlying byte sequence; since we store
    // UTF-8 bytes in a plain std::string already, this is just a copy.
    std::string raw() const { return *this; }

    // Real UTF-8 well-formedness check (not stubbed: imagedata.cc uses it
    // to decide whether an EXIF string field is usable as-is).
    bool validate() const;

    // Locale-independent, ASCII-fold-based collate key. Deliberately not
    // delegating to a locale-aware primitive — dcp.cc relies on this being
    // stable across locales for automatic camera/DCP-profile matching.
    std::string casefold_collate_key() const { return casefold().raw(); }

    // Real glibmm's plain (non-casefolded) collate_key() is for locale-aware
    // *sort order*. rtlensfun.cc's one call site only ever uses it as an
    // opaque, stable lookup key in a std::set (membership, not ordering),
    // so the identity transform preserves correctness without the locale
    // dependency.
    std::string collate_key() const { return raw(); }

    // Minimal variadic stand-in for Glib::ustring::compose(): replaces
    // %1, %2, … placeholders in `fmt` with `args...` streamed through
    // operator<<. rtengine only ever calls this with a single substitution
    // (progress-bar strings), but it is implemented generally.
    template <typename... Args>
    static ustring compose(const std::string& fmt, Args&&... args)
    {
        ustring result(fmt);
        composeInto(result, 1, std::forward<Args>(args)...);
        return result;
    }

private:
    static void composeInto(ustring&, int) {}

    template <typename T, typename... Rest>
    static void composeInto(ustring& result, int index, T&& value, Rest&&... rest)
    {
        std::ostringstream os;
        os << value;
        const std::string placeholder = "%" + std::to_string(index);
        const std::string::size_type pos = result.find(placeholder);
        if (pos != std::string::npos) {
            result.replace(pos, placeholder.size(), os.str());
        }
        composeInto(result, index + 1, std::forward<Rest>(rest)...);
    }
};

// Shadows std::operator+(const basic_string&, const basic_string&), which
// returns plain std::string — overload resolution prefers this exact
// (ustring, ustring) match over that one. rtlensfun.cc chains
// (a + b + c).collate_key(), which needs the result to still be a ustring.
inline ustring operator+(const ustring& lhs, const ustring& rhs)
{
    return ustring(static_cast<const std::string&>(lhs) + static_cast<const std::string&>(rhs));
}

// Rvalue overloads so chains like (a + b) + c don't leave the (ustring,
// ustring) overload above competing on equal footing with std::string's
// own (basic_string&&, ...) overloads — which GCC resolves anyway (as a
// permitted extension) but warns about as ISO-ambiguous.
inline ustring operator+(ustring&& lhs, const ustring& rhs)
{
    lhs.append(rhs);
    return std::move(lhs);
}

inline ustring operator+(const ustring& lhs, ustring&& rhs)
{
    rhs.insert(0, lhs);
    return std::move(rhs);
}

inline ustring operator+(ustring&& lhs, ustring&& rhs)
{
    lhs.append(rhs);
    return std::move(lhs);
}

} // namespace Glib
