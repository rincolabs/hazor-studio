#include "raw/RawEngine.hpp"

#include "raw/RawEngineAvailability.hpp"
#include "raw/RawSession.hpp"

#include <algorithm>
#include <cctype>

#ifdef HAVE_RAWENGINE

#include "rtcompat/rtgui/options.hpp"
#include "rtengine/dcp.h"
#include "rtengine/iccstore.h"
#include "rtengine/rtapp.h"
#include "rtengine/rtengine.h"

namespace {

bool g_initialized = false;

// rtengine::Settings has no constructor of its own (see rtcompat/rtgui/options.hpp's
// `rtengine::Settings rtSettings{};` value-initialization, which zero/empty-inits
// every field not set here). This only needs to set the fields upstream's own
// Options::setDefaults() (rtgui/options.cc, not vendored — GUI-only) gives a
// non-zero/non-empty value; everything else is correctly 0/false/"" already.
void populateDefaultSettings(rtengine::Settings& s, const std::string& lensfunDbDir)
{
#if defined(_WIN32)
    s.iccDirectory = "C:\\WINDOWS\\System32\\spool\\drivers\\color";
#else
    s.iccDirectory = "/usr/share/color/icc";
#endif
    s.enableLibRaw = true;

    s.printerIntent = rtengine::RI_RELATIVE;
    s.printerBPC = true;
    s.monitorIntent = rtengine::RI_RELATIVE;
    s.monitorBPC = true;

    // Bundled ICC profile names (3rdparty/rawengine/rtdata/iccprofiles/), looked
    // up by ICCStore relative to RawEngine::initialize()'s resourceDir.
    s.adobe = "RTv2_Medium";
    s.prophoto = "RTv2_Large";
    s.widegamut = "RTv2_Wide";
    s.DCIP3 = "RTv2_DCIP3";
    s.srgb = "RTv4_sRGB";
    s.bruce = "RTv2_Bruce";
    s.beta = "RTv2_Beta";
    s.best = "RTv2_Best";
    s.rec2020 = "RTv2_Rec2020";
    s.ACESp0 = "RTv2_ACES-AP0";
    s.ACESp1 = "RTv2_ACES-AP1";

    s.gamutICC = true;
    s.gamutLch = true;
    s.amchroma = 40;
    s.amchromajz = 40;
    s.level123_cbdl = 30;
    s.cropsleep = 50;
    s.reduchigh = 0.85;
    s.reduclow = 0.85;
    s.detectshape = true;
    s.previewselection = 5;
    s.cbdlsensi = 1.0;
    s.fftwsigma = true;
    s.basecorlog = 0.12;
    s.itcwb_enable = true;
    s.itcwb_deltaspec = 0.075;
    s.itcwb_powponder = 0.15;
    s.edghi = 3.0;
    s.edglo = 0.5;
    s.limrad = 20.0;
    s.protectred = 60;
    s.protectredh = 0.3;
    s.denoiselabgamma = 2;
    s.nrauto = 10;
    s.nrautomax = 40;
    s.nrhigh = 0.45;
    s.nrwavlevel = 1;
    s.leveldnv = 2;
    s.rgbcurveslumamode_gamut = true;

    s.thumbnail_inspector_mode = rtengine::Settings::ThumbnailInspectorMode::JPEG;
    s.xmp_sidecar_style = rtengine::Settings::XmpSidecarStyle::STD;
    s.metadata_xmp_sync = rtengine::Settings::MetadataXmpSync::NONE;

    // lensfunDbDirectory stays empty so rtengine::init() first tries lensfun's
    // own search, which is what a native/system lensfun install wants. The
    // bundle directory is the fallback it takes when that search comes up
    // empty — the Windows case, where lensfun is linked statically and its
    // module-relative lookup hunts for a liblensfun.dll that is never built.
    // Empty when the platform ships no database, which init() also handles.
    s.lensfunDbBundleDirectory = Glib::ustring(lensfunDbDir);
}

} // namespace

void RawEngine::initialize(const std::string& resourceDir, const std::string& userDir,
                           const std::string& lensfunDbDir)
{
    if (g_initialized) {
        return;
    }

    Options::rtdir = Glib::ustring(userDir);

    Options& opts = App::get().mut_options();
    populateDefaultSettings(opts.rtSettings, lensfunDbDir);

    // rtengine::init() stores &opts.rtSettings in a process-lifetime static
    // (rtengine::settings); safe because App::get()'s singleton also lives
    // for the process's lifetime.
    rtengine::init(&opts.rtSettings, Glib::ustring(resourceDir), Glib::ustring(userDir), /*loadAll=*/true);
    g_initialized = true;
}

void RawEngine::shutdown()
{
    if (!g_initialized) {
        return;
    }
    rtengine::cleanup();
    g_initialized = false;
}

bool RawEngine::isAvailable() { return g_initialized; }

std::unique_ptr<RawSession> RawEngine::openFile(const std::string& path, std::string* errorMessage)
{
    if (!g_initialized) {
        if (errorMessage) {
            *errorMessage = "RAW support is not available (RawEngine::initialize() was not called).";
        }
        return nullptr;
    }
    return RawSession::open(path, errorMessage);
}

MatchedCameraProfile RawEngine::matchedCameraProfile(const RawMetadata& metadata)
{
    MatchedCameraProfile result;
    result.matchedCameraName = metadata.cameraName();

    if (!g_initialized) {
        return result;
    }

    // Mirrors RawImageSource::findInputProfile()'s "(cameraICC)" cascade —
    // DCP first, then ICC, then (implicitly) the camera matrix — without
    // reimplementing DCPStore/ICCStore's own lookup (incl. DCP aliasing).
    if (rtengine::DCPStore::getInstance()->getStdProfile(result.matchedCameraName)) {
        result.kind = MatchedProfileKind::Dcp;
    } else if (rtengine::ICCStore::getInstance()->getStdProfile(result.matchedCameraName)) {
        result.kind = MatchedProfileKind::Icc;
    } else {
        result.kind = MatchedProfileKind::CameraMatrix;
    }
    return result;
}

#else // !HAVE_RAWENGINE

void RawEngine::initialize(const std::string&, const std::string&, const std::string&) {}
void RawEngine::shutdown() {}
bool RawEngine::isAvailable() { return false; }

std::unique_ptr<RawSession> RawEngine::openFile(const std::string& path, std::string* errorMessage)
{
    return RawSession::open(path, errorMessage);
}

MatchedCameraProfile RawEngine::matchedCameraProfile(const RawMetadata& metadata)
{
    MatchedCameraProfile result;
    result.matchedCameraName = metadata.cameraName();
    return result;
}

#endif

// Extensions Hazor treats as RAW. Kept independent of
// rtcompat/rtgui/options.hpp's own (coincidentally identical) list, which
// governs flat-field/dark-frame directory scans, not file routing — update
// both if a new RAW extension is added.
namespace {
const std::vector<std::string>& rawExtensionList()
{
    static const std::vector<std::string> extensions = {
        "cr2", "cr3", "nef", "arw", "raf", "dng", "orf", "rw2", "pef", "srw",
        "3fr", "erf", "mrw", "dcr", "kdc", "mef", "mos", "nrw", "raw", "iiq", "fff",
    };
    return extensions;
}
} // namespace

const std::vector<std::string>& RawEngine::supportedExtensions() { return rawExtensionList(); }

bool RawEngine::isSupportedExtension(const std::string& lowercaseExtensionNoDot)
{
    const auto& extensions = rawExtensionList();
    return std::find(extensions.begin(), extensions.end(), lowercaseExtensionNoDot) != extensions.end();
}
