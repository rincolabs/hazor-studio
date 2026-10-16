#include "raw/RawDevelopParams.hpp"

#include "raw/RawEngineAvailability.hpp"

#ifdef HAVE_RAWENGINE

#include <cstdio>
#include <filesystem>
#include <random>
#include <sstream>

#include "rtcompat/glib/fileio.hpp"
#include "raw/internal/RawDevelopParamsImpl.hpp"

namespace {

std::string tempDirUtf8()
{
    const auto path = std::filesystem::temp_directory_path();
#if defined(_WIN32)
    // Known limitation: if the Windows temp directory itself contains
    // non-ASCII characters (e.g. an accented username), this still goes
    // through u8string()'s own conversion, which is correct — it is the
    // subsequent g_fopen() call below that actually needs the UTF-8 byte
    // sequence this produces.
    const auto u8 = path.u8string();
    return std::string(u8.begin(), u8.end());
#else
    return path.string();
#endif
}

std::string makeTempPp3Path()
{
    static std::mt19937_64 rng{std::random_device{}()};
    std::ostringstream name;
    name << tempDirUtf8() << '/' << "hazor-raw-" << std::hex << rng() << ".pp3";
    return name.str();
}

// ProcParams::save()/load() only ever write/read a PP3 *file* (they build a
// Glib::KeyFile internally and serialize straight to disk); there is no
// in-memory string API upstream. Rather than reimplementing PP3 codec on top
// of the Glib::KeyFile shim (duplicating ~900 lines of save()/load() field
// mappings), fromPP3String()/toPP3String() round-trip through a scratch file
// using the engine's own save()/load(), which is the same code path every
// on-disk .pp3 in this engine already goes through.
bool writeStringToFile(const std::string& path, const std::string& content)
{
    FILE* f = g_fopen(path.c_str(), "wb");
    if (!f) {
        return false;
    }
    const bool ok = content.empty() || std::fwrite(content.data(), 1, content.size(), f) == content.size();
    std::fclose(f);
    return ok;
}

bool readFileToString(const std::string& path, std::string& out)
{
    FILE* f = g_fopen(path.c_str(), "rb");
    if (!f) {
        return false;
    }
    std::string content;
    char buffer[4096];
    std::size_t n;
    while ((n = std::fread(buffer, 1, sizeof(buffer), f)) > 0) {
        content.append(buffer, n);
    }
    const bool ok = std::feof(f) != 0;
    std::fclose(f);
    if (ok) {
        out = std::move(content);
    }
    return ok;
}

} // namespace

RawDevelopParams::RawDevelopParams() : m_impl(std::make_unique<Impl>()) {}
RawDevelopParams::RawDevelopParams(const RawDevelopParams& other) : m_impl(std::make_unique<Impl>(*other.m_impl)) {}
RawDevelopParams::RawDevelopParams(RawDevelopParams&& other) noexcept = default;
RawDevelopParams& RawDevelopParams::operator=(RawDevelopParams&& other) noexcept = default;
RawDevelopParams::~RawDevelopParams() = default;

RawDevelopParams& RawDevelopParams::operator=(const RawDevelopParams& other)
{
    if (this != &other) {
        m_impl = std::make_unique<Impl>(*other.m_impl);
    }
    return *this;
}

RawDevelopParams RawDevelopParams::cameraDefaults()
{
    // ProcParams's own constructor already sets rtengine's hand-wired
    // defaults (white balance "Camera" i.e. read from the shot, no exposure
    // or tone adjustments) — exactly what we want here. Deliberately not the
    // ProfileStore dynamic-profile-rule subsystem; see the header comment.
    return RawDevelopParams();
}

bool RawDevelopParams::isValid() const
{
    return true;
}

RawDevelopParams RawDevelopParams::fromPP3String(const std::string& pp3)
{
    const std::string path = makeTempPp3Path();
    if (writeStringToFile(path, pp3)) {
        RawDevelopParams loaded;
        const bool ok = loaded.m_impl->procParams.load(path) == 0;
        g_remove(path.c_str());
        if (ok) {
            return loaded;
        }
    }
    return cameraDefaults();
}

std::string RawDevelopParams::toPP3String() const
{
    const std::string path = makeTempPp3Path();
    std::string result;
    if (m_impl->procParams.save(path) == 0) {
        readFileToString(path, result);
    }
    g_remove(path.c_str());
    return result;
}

bool RawDevelopParams::loadPP3File(const std::string& path, RawDevelopParams& out)
{
    RawDevelopParams params;
    if (params.m_impl->procParams.load(path) != 0) {
        return false;
    }
    out = std::move(params);
    return true;
}

bool RawDevelopParams::savePP3File(const std::string& path) const
{
    return m_impl->procParams.save(path) == 0;
}

RawDevelopParams::InputProfileMode RawDevelopParams::inputProfileMode() const
{
    const std::string& profile = m_impl->procParams.icm.inputProfile;
    if (profile == "(cameraICC)") return InputProfileMode::CameraMatched;
    if (profile == "(embedded)") return InputProfileMode::Embedded;
    if (profile == "(none)") return InputProfileMode::None;
    if (profile == "(camera)") return InputProfileMode::CameraMatrix;
    return InputProfileMode::Custom;
}

void RawDevelopParams::setInputProfileMode(InputProfileMode mode)
{
    switch (mode) {
    case InputProfileMode::CameraMatched: m_impl->procParams.icm.inputProfile = "(cameraICC)"; return;
    case InputProfileMode::Embedded: m_impl->procParams.icm.inputProfile = "(embedded)"; return;
    case InputProfileMode::None: m_impl->procParams.icm.inputProfile = "(none)"; return;
    case InputProfileMode::CameraMatrix: m_impl->procParams.icm.inputProfile = "(camera)"; return;
    case InputProfileMode::Custom: return; // use setCustomInputProfile() instead
    }
}

void RawDevelopParams::setCustomInputProfile(const std::string& path)
{
    // Resolved by RawImageSource::findInputProfile() at render time: DCP if
    // DCPStore::isValidDCPFileName() accepts it, else ICC — not reimplemented here.
    m_impl->procParams.icm.inputProfile = path;
}

std::string RawDevelopParams::customInputProfile() const
{
    return inputProfileMode() == InputProfileMode::Custom ? m_impl->procParams.icm.inputProfile : std::string();
}

RawDevelopParams::WhiteBalance RawDevelopParams::whiteBalance() const
{
    const auto& wb = m_impl->procParams.wb;
    WhiteBalance out;
    out.method = wb.method;
    out.temperatureKelvin = wb.temperature;
    out.green = wb.green;
    return out;
}

void RawDevelopParams::setWhiteBalance(const WhiteBalance& wb)
{
    auto& target = m_impl->procParams.wb;
    target.method = wb.method;
    target.temperature = wb.temperatureKelvin;
    target.green = wb.green;
}

RawDevelopParams::Exposure RawDevelopParams::exposure() const
{
    const auto& tc = m_impl->procParams.toneCurve;
    Exposure out;
    out.autoExposure = tc.autoexp;
    out.compensationEv = tc.expcomp;
    return out;
}

void RawDevelopParams::setExposure(const Exposure& exposure)
{
    auto& target = m_impl->procParams.toneCurve;
    target.autoexp = exposure.autoExposure;
    target.expcomp = exposure.compensationEv;
}

#else // !HAVE_RAWENGINE

// Inert stand-in so RawDevelopParams compiles (and anything that merely
// stores one, e.g. a future RawDevelopState, keeps working) in builds
// without the engine. RawEngine::isAvailable() is false in this
// configuration, so no caller should actually reach these.
struct RawDevelopParams::Impl {};

RawDevelopParams::RawDevelopParams() : m_impl(std::make_unique<Impl>()) {}
RawDevelopParams::RawDevelopParams(const RawDevelopParams&) : m_impl(std::make_unique<Impl>()) {}
RawDevelopParams::RawDevelopParams(RawDevelopParams&&) noexcept = default;
RawDevelopParams& RawDevelopParams::operator=(RawDevelopParams&&) noexcept = default;
RawDevelopParams::~RawDevelopParams() = default;

RawDevelopParams& RawDevelopParams::operator=(const RawDevelopParams&) { return *this; }

RawDevelopParams RawDevelopParams::cameraDefaults() { return RawDevelopParams(); }
bool RawDevelopParams::isValid() const { return false; }
RawDevelopParams RawDevelopParams::fromPP3String(const std::string&) { return RawDevelopParams(); }
std::string RawDevelopParams::toPP3String() const { return std::string(); }
bool RawDevelopParams::loadPP3File(const std::string&, RawDevelopParams&) { return false; }
bool RawDevelopParams::savePP3File(const std::string&) const { return false; }

RawDevelopParams::InputProfileMode RawDevelopParams::inputProfileMode() const { return InputProfileMode::CameraMatched; }
void RawDevelopParams::setInputProfileMode(InputProfileMode) {}
void RawDevelopParams::setCustomInputProfile(const std::string&) {}
std::string RawDevelopParams::customInputProfile() const { return std::string(); }

RawDevelopParams::WhiteBalance RawDevelopParams::whiteBalance() const { return {}; }
void RawDevelopParams::setWhiteBalance(const WhiteBalance&) {}
RawDevelopParams::Exposure RawDevelopParams::exposure() const { return {}; }
void RawDevelopParams::setExposure(const Exposure&) {}

#endif
