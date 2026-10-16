#pragma once
// rtcompat/rtgui/version.h
//
// Stand-in for the generated rtgui/version.h (upstream produces it from
// version.h.in via CMake's git-describe substitution at RawTherapee's own
// build time). RTVERSION is purely a diagnostic string embedded in saved
// PP3s ("Version"/"AppVersion") and image metadata ("Exif.Image.Software"),
// never parsed back by rtengine — see PROVENANCE.md for the vendored
// upstream version this value documents.

#define RTVERSION "5.13-dev"
