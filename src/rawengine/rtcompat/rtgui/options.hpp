#pragma once
// rtcompat/rtgui/options.hpp
//
// Stand-in for rtgui/options.h: a global `options` object rtengine reads
// settings/performance knobs from. Real Options has ~550 lines backing
// RawTherapee's full preferences dialog; rtengine itself only ever reads
// the handful of fields below (grep-verified across the vendored tree —
// see tmp/feat-raw-therapee-plan.md Fase 2 item 4), so that is all this
// reimplements. RawEngine::initialize() (Fase 3) is expected to populate
// `options.rtdir` and friends from Hazor's own AppPaths; until then these
// just carry RawTherapee's own upstream defaults (options.cc).
#include <algorithm>
#include <set>
#include <string>
#include <vector>

#include "rtcompat/glib/ustring.hpp"
#include "rtcompat/rtgui/addsetids.h"
#include "rtengine/rtapp.h"
#include "rtengine/settings.h"

#ifdef _WIN32
#define DEFPROFILE_RAW "${G}\\Auto-Matched Curve - ISO Low"
#else
#define DEFPROFILE_RAW "${G}/Auto-Matched Curve - ISO Low"
#endif
#define DEFPROFILE_IMG "Neutral"
#define DEFPROFILE_INTERNAL "Neutral"
#define DEFPROFILE_DYNAMIC "Dynamic"

enum prevdemo_t { PD_Sidecar = 1, PD_Fast = 0 };

class Options {
public:
    Options()
    {
        baBehav.assign(ADDSET_PARAM_NUM, 0);
        // A conservative default covering the formats RawEngine targets
        // (see docs/RAW-ENGINE.md); ffmanager/dfmanager (flat-field / dark-
        // frame directory scans) are the only two rtengine call sites that
        // consult this.
        for (const char* ext : {"cr2", "cr3", "nef", "arw", "raf", "dng", "orf", "rw2", "pef", "srw", "3fr", "erf", "mrw", "dcr", "kdc", "mef", "mos", "nrw", "raw", "iiq", "fff"}) {
            parsedExtensions.insert(Glib::ustring(ext));
        }
    }

    bool measure = false;
    std::size_t chunkSizeAMAZE = 2;
    std::size_t chunkSizeCA = 2;
    std::size_t chunkSizeRCD = 2;
    std::size_t chunkSizeRGB = 2;
    std::size_t chunkSizeXT = 2;
    int clutCacheSize = 1;
    bool fbShowHidden = false;
    bool useBundledProfiles = true;
    Glib::ustring defProfRaw = DEFPROFILE_RAW;
    Glib::ustring defProfImg = DEFPROFILE_IMG;
    rtengine::Settings rtSettings;
    std::vector<int> baBehav;

    // Fields added by Hazor's App::options() refactor of the vendored tree
    // (see rtapp.h) on top of what upstream's rtgui/options.h carries;
    // defaults mirror upstream's Options::setDefaults() (options.cc).
    int complexity = 2;
    int spotmet = 0;
    prevdemo_t prevdemo = PD_Sidecar;
    int rgbDenoiseThreadLimit = 0;
    bool serializeTiffRead = true;
    Glib::ustring clutsDir = "./cluts";

    // Set by RawEngine::initialize() from Hazor's AppPaths; empty (current
    // directory) until then.
    static Glib::ustring rtdir;

    Glib::ustring userProfilePath;
    Glib::ustring globalProfilePath;

    Glib::ustring getUserProfilePath() const { return userProfilePath; }
    Glib::ustring getGlobalProfilePath() const { return globalProfilePath; }

    bool is_extention_enabled(const Glib::ustring& ext) const
    {
        return parsedExtensions.find(ext.lowercase()) != parsedExtensions.end();
    }

    bool is_defProfRawMissing() const { return defProfRawMissing_; }
    bool is_defProfImgMissing() const { return defProfImgMissing_; }
    void setDefProfRawMissing(bool value) { defProfRawMissing_ = value; }
    void setDefProfImgMissing(bool value) { defProfImgMissing_ = value; }

private:
    std::set<Glib::ustring> parsedExtensions;
    bool defProfRawMissing_ = false;
    bool defProfImgMissing_ = false;
};

extern Options options;
