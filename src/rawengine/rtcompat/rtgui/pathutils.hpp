#pragma once
// rtcompat/rtgui/pathutils.hpp
//
// Stand-in for rtgui/pathutils.h. Only removeExtension() is actually
// called from within rtengine (metadata.cc); getExtension()/fname_to_utf8()
// are carried along for fidelity with upstream's small, self-contained
// pathutils.cc, ported inline here since it has no glibmm dependency
// beyond Glib::ustring/Glib::path_get_basename.
#include <string>

#include "rtcompat/glib/fileutils.hpp"
#include "rtcompat/glib/textconv.hpp"
#include "rtcompat/glib/ustring.hpp"

inline Glib::ustring removeExtension(const Glib::ustring& filename)
{
    const Glib::ustring bname = Glib::path_get_basename(filename);
    const std::size_t lastdot = bname.find_last_of('.');
    const std::size_t lastwhitespace = bname.find_last_of(" \t\f\v\n\r");

    if (lastdot != bname.npos && (lastwhitespace == bname.npos || lastdot > lastwhitespace)) {
        return filename.substr(0, filename.size() - (bname.size() - lastdot));
    }
    return filename;
}

inline Glib::ustring getExtension(const Glib::ustring& filename)
{
    const Glib::ustring bname = Glib::path_get_basename(filename);
    const std::size_t lastdot = bname.find_last_of('.');
    const std::size_t lastwhitespace = bname.find_last_of(" \t\f\v\n\r");

    if (lastdot != bname.npos && (lastwhitespace == bname.npos || lastdot > lastwhitespace)) {
        return filename.substr(filename.size() - (bname.size() - lastdot) + 1, filename.npos);
    }
    return "";
}

inline Glib::ustring fname_to_utf8(const std::string& fname)
{
#ifdef _WIN32
    try {
        return Glib::locale_to_utf8(fname);
    } catch (const Glib::Error&) {
        return Glib::convert_with_fallback(fname, "UTF-8", "ISO-8859-1", "?");
    }
#else
    return Glib::filename_to_utf8(fname);
#endif
}
