#pragma once
// rtcompat/rtgui/multilangmgr.hpp
//
// Stand-in for rtgui/multilangmgr.h: M(key) normally looks up a translated
// UI string. RawEngine has no UI of its own (the Camera Raw dialog is a
// later Hazor step, see tmp/feat-raw-therapee-plan.md), and the ~70
// M(...) call sites in rtengine are all either progress-bar strings
// (ignored — RawSession::renderPreview/renderFull take their own
// cancelToken/progress callback, not this) or default-profile labels that
// only matter once that UI exists. So M() is the identity function: it
// returns the translation key itself rather than a translated string.
#include <string>

#include "rtcompat/glib/ustring.hpp"

inline Glib::ustring M(const std::string& key)
{
    return Glib::ustring(key);
}
