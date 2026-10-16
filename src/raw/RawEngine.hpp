#pragma once

#include <memory>
#include <string>
#include <vector>

#include "raw/RawMetadata.hpp"

class RawSession;

// How RawEngine::matchedCameraProfile() resolved the "(cameraICC)" input
// profile cascade for a given camera (RawImageSource::findInputProfile(),
// not reimplemented — see RawDevelopParams::InputProfileMode).
enum class MatchedProfileKind {
    Dcp,            // DCPStore::getStdProfile() matched (highest quality)
    Icc,            // no DCP; ICCStore::getStdProfile() matched
    CameraMatrix,   // neither matched; the camera's own color matrix is used
};

struct MatchedCameraProfile {
    MatchedProfileKind kind = MatchedProfileKind::CameraMatrix;
    // The camera name string ("Make Model") the lookup was keyed on —
    // useful to show in the UI (e.g. when it differs from the raw EXIF
    // string because of DCPStore's alias table).
    std::string matchedCameraName;
};

// Process-wide entry point for the native RAW engine. init/shutdown +
// availability + the one static lookup (matchedCameraProfile) that doesn't
// belong to a single open file. Everything that *is* about one open file is
// RawSession (see openFile()).
class RawEngine {
public:
    RawEngine() = delete;

    // resourceDir must contain the bundled runtime resources rtengine reads
    // at startup: iccprofiles/, profiles/, dcpprofiles/, camconst.json,
    // cammatrices.json, dcraw.json (3rdparty/rawengine/rtdata/, resolved via
    // AppPaths — see docs/BUILD.md). userDir is Hazor's own per-user RAW data
    // directory (e.g. userDir/dcpprofiles for user-dropped camera profiles).
    // Safe to call when HAVE_RAWENGINE is not compiled in: isAvailable()
    // simply stays false. Not thread-safe; call once during startup.
    static void initialize(const std::string& resourceDir, const std::string& userDir);
    static void shutdown();

    // True only after a successful initialize() (not merely HAVE_RAWENGINE
    // compiled in) — mirrors AiRuntimeManager::isRuntimeAvailable().
    static bool isAvailable();

    // Extensions Hazor treats as RAW (lowercase, no leading dot). Valid
    // regardless of isAvailable(), so the Open dialog / file routing (Fase 5)
    // can still recognize a RAW file and report "RAW support not built into
    // this copy of Hazor" instead of trying to decode it as a regular image.
    static const std::vector<std::string>& supportedExtensions();
    static bool isSupportedExtension(const std::string& lowercaseExtensionNoDot);

    // Returns nullptr on failure (file missing, unsupported, decode error);
    // when errorMessage is non-null it is set to a human-readable reason.
    static std::unique_ptr<RawSession> openFile(const std::string& path, std::string* errorMessage = nullptr);

    static MatchedCameraProfile matchedCameraProfile(const RawMetadata& metadata);
};
