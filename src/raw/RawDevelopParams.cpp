#include "raw/RawDevelopParams.hpp"

#include "raw/RawEngineAvailability.hpp"

#ifdef HAVE_RAWENGINE

#include <cstdio>
#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <random>
#include <sstream>

#include "rtengine/diagonalcurvetypes.h"
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

constexpr double kFlatControlPoints = 1.0;

// The HSV Equalizer's curves are indexed by normalized hue: improcfun.cc's
// rgbProc() does `h /= 6.f` before hCurve->getVal(h), so a control point only
// affects the hue it actually sits on. Camera Raw's eight bands are specific
// hues (0/30/60/120/180/240/280/320 degrees), not eight equal slices of the
// circle, so these positions — not i/7 — are what makes the labelled band and
// the affected colour the same thing.
//
// Magenta deliberately stops at 320/360 instead of reaching 1.0: FlatCurve is
// periodic and replicates the first control point at x[0] + 1.0
// (flatcurves.cc), so a point at exactly 1.0 would collide with the Red band's
// own wrap-around copy and the two would fight over the same position.
constexpr std::array<double, 8> kMixerBandHues{
    0.0,          // Red       0°
    1.0 / 12.0,   // Orange   30°
    1.0 / 6.0,    // Yellow   60°
    1.0 / 3.0,    // Green   120°
    0.5,          // Aqua    180°
    2.0 / 3.0,    // Blue    240°
    7.0 / 9.0,    // Purple  280°
    8.0 / 9.0,    // Magenta 320°
};

// Shortest distance between two normalized hues, the hue circle's wrap
// included — so a control point at 0.98 is near the Red band (0.0), not far
// from it.
double hueDistance(double a, double b)
{
    // Clamped first: these x values come from a PP3 and are only *expected*
    // to be in [0, 1]. Without the clamp an out-of-range point would make
    // 1 - delta negative and win every comparison.
    const double delta = std::abs(std::clamp(a, 0.0, 1.0) - std::clamp(b, 0.0, 1.0));
    return std::min(delta, 1.0 - delta);
}

int editableDiagonalCurveKind(int kind)
{
    return kind == DCT_Spline || kind == DCT_NURBS || kind == DCT_CatumullRom
        ? kind : DCT_Spline;
}

int editableDiagonalCurveKind(const std::vector<double>& curve)
{
    return curve.empty() ? DCT_Spline
                         : editableDiagonalCurveKind(static_cast<int>(curve.front()));
}

std::vector<RawDevelopParams::CurvePoint> decodeDiagonalCurve(const std::vector<double>& curve)
{
    std::vector<RawDevelopParams::CurvePoint> points;
    const int kind = editableDiagonalCurveKind(curve);
    const bool isPointCurve = !curve.empty()
        && kind == static_cast<int>(curve.front());
    if (curve.size() >= 5 && isPointCurve) {
        for (std::size_t i = 1; i + 1 < curve.size(); i += 2)
            points.push_back({std::clamp(curve[i], 0.0, 1.0), std::clamp(curve[i + 1], 0.0, 1.0)});
    }
    if (points.size() < 2)
        return {{0.0, 0.0}, {1.0, 1.0}};
    return points;
}

std::vector<double> encodeDiagonalCurve(std::vector<RawDevelopParams::CurvePoint> points,
                                        int kind = DCT_Spline)
{
    for (auto& point : points) {
        point.x = std::clamp(point.x, 0.0, 1.0);
        point.y = std::clamp(point.y, 0.0, 1.0);
    }
    std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.x < b.x; });
    if (points.empty() || points.front().x > 0.0)
        points.insert(points.begin(), {0.0, 0.0});
    if (points.back().x < 1.0)
        points.push_back({1.0, 1.0});

    std::vector<double> curve;
    curve.reserve(1 + points.size() * 2);
    curve.push_back(editableDiagonalCurveKind(kind));
    for (const auto& point : points) {
        curve.push_back(point.x);
        curve.push_back(point.y);
    }
    return curve;
}

// Reads each band from the control point closest to that band's own hue, not
// from the point at the matching *index*: a curve written by RawTherapee's own
// HSV Equalizer has however many points the user placed, at arbitrary hues, so
// indexing would report them under the wrong band names. Known lossiness: an
// eight-band UI cannot represent an arbitrary curve, so re-encoding one after
// an edit snaps it to kMixerBandHues (see setColorMixer()).
std::array<double, 8> decodeMixerCurve(const std::vector<double>& curve)
{
    std::array<double, 8> values{};
    if (curve.size() < 5 || static_cast<int>(curve.front()) != static_cast<int>(kFlatControlPoints))
        return values;

    const std::size_t count = (curve.size() - 1) / 4;
    if (!count)
        return values;
    for (std::size_t band = 0; band < values.size(); ++band) {
        std::size_t nearest = 0;
        double nearestDistance = 2.0;
        for (std::size_t i = 0; i < count; ++i) {
            const double distance = hueDistance(curve[1 + i * 4], kMixerBandHues[band]);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = i;
            }
        }
        values[band] = std::clamp((curve[1 + nearest * 4 + 1] - 0.5) * 200.0, -100.0, 100.0);
    }
    return values;
}

std::vector<double> encodeMixerCurve(const std::array<double, 8>& values)
{
    std::vector<double> curve;
    curve.reserve(1 + values.size() * 4);
    curve.push_back(kFlatControlPoints);
    for (std::size_t i = 0; i < values.size(); ++i) {
        curve.push_back(kMixerBandHues[i]);
        curve.push_back(std::clamp(0.5 + values[i] / 200.0, 0.0, 1.0));
        curve.push_back(0.0);
        curve.push_back(0.0);
    }
    return curve;
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

bool RawDevelopParams::presetWhiteBalance(const std::string& method, WhiteBalance& out)
{
    // The temperatures live in the engine's own table; they are not
    // duplicated here. Entries with no fixed temperature ("Camera", the two
    // automatic methods, "Custom") have nothing to report.
    for (const auto& entry : rtengine::procparams::WBParams::getWbEntries()) {
        if (entry.ppLabel != method) {
            continue;
        }
        if (entry.temperature <= 0) {
            return false;
        }
        out.method = method;
        out.temperatureKelvin = entry.temperature;
        out.green = entry.green;
        return true;
    }
    return false;
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

RawDevelopParams::Light RawDevelopParams::light() const
{
    const auto& tone = m_impl->procParams.toneCurve;
    const auto& sh = m_impl->procParams.sh;
    return {
        tone.autoexp, tone.expcomp, tone.contrast, sh.highlights, sh.shadows,
        tone.hlcompr, tone.black
    };
}

void RawDevelopParams::setLight(const Light& light)
{
    auto& tone = m_impl->procParams.toneCurve;
    auto& sh = m_impl->procParams.sh;
    tone.autoexp = light.autoExposure;
    tone.expcomp = std::clamp(light.exposureEv, -5.0, 12.0);
    tone.contrast = std::clamp(light.contrast, -100, 100);
    sh.highlights = std::clamp(light.highlights, 0, 100);
    sh.shadows = std::clamp(light.shadows, 0, 100);
    sh.enabled = sh.highlights != 0 || sh.shadows != 0;
    tone.hlcompr = std::clamp(light.whiteRecovery, 0, 500);
    tone.black = std::clamp(light.blackPoint, -16384, 32768);
}

RawDevelopParams::Color RawDevelopParams::color() const
{
    return {m_impl->procParams.blackwhite.enabled, m_impl->procParams.vibrance.pastels,
            m_impl->procParams.toneCurve.saturation};
}

void RawDevelopParams::setColor(const Color& color)
{
    m_impl->procParams.blackwhite.enabled = color.blackAndWhite;
    auto& vibrance = m_impl->procParams.vibrance;
    vibrance.pastels = std::clamp(color.vibrance, -100, 100);
    vibrance.enabled = vibrance.pastels != 0;
    m_impl->procParams.toneCurve.saturation = std::clamp(color.saturation, -100, 100);
}

RawDevelopParams::Effects RawDevelopParams::effects() const
{
    const auto& micro = m_impl->procParams.sharpenMicro;
    const auto& contrast = m_impl->procParams.localContrast;
    const auto& dehaze = m_impl->procParams.dehaze;
    const auto& vignette = m_impl->procParams.pcvignette;
    return {micro.enabled, micro.amount, contrast.enabled, contrast.amount,
            dehaze.enabled, dehaze.strength, vignette.enabled, vignette.strength,
            vignette.feather, vignette.roundness};
}

void RawDevelopParams::setEffects(const Effects& effects)
{
    auto& micro = m_impl->procParams.sharpenMicro;
    micro.enabled = effects.textureEnabled;
    micro.amount = std::clamp(effects.texture, 0.0, 100.0);

    auto& contrast = m_impl->procParams.localContrast;
    contrast.enabled = effects.clarityEnabled;
    contrast.amount = std::clamp(effects.clarity, 0.0, 1.0);

    auto& dehaze = m_impl->procParams.dehaze;
    dehaze.enabled = effects.dehazeEnabled;
    dehaze.strength = std::clamp(effects.dehaze, 0, 100);

    auto& vignette = m_impl->procParams.pcvignette;
    vignette.enabled = effects.vignetteEnabled;
    vignette.strength = std::clamp(effects.vignetteAmount, -6.0, 6.0);
    vignette.feather = std::clamp(effects.vignetteFeather, 0, 100);
    vignette.roundness = std::clamp(effects.vignetteRoundness, 0, 100);
}

RawDevelopParams::Detail RawDevelopParams::detail() const
{
    const auto& sharpening = m_impl->procParams.sharpening;
    const auto& denoise = m_impl->procParams.dirpyrDenoise;
    return {sharpening.enabled, sharpening.amount, sharpening.radius, sharpening.contrast,
            denoise.enabled, denoise.luma, denoise.Ldetail, denoise.chroma};
}

void RawDevelopParams::setDetail(const Detail& detail)
{
    auto& sharpening = m_impl->procParams.sharpening;
    sharpening.enabled = detail.sharpeningEnabled;
    sharpening.amount = std::clamp(detail.sharpeningAmount, 1, 1000);
    sharpening.radius = std::clamp(detail.sharpeningRadius, 0.3, 3.0);
    sharpening.contrast = std::clamp(detail.sharpeningDetail, 0.0, 200.0);

    auto& denoise = m_impl->procParams.dirpyrDenoise;
    denoise.enabled = detail.denoiseEnabled;
    denoise.perform = detail.denoiseEnabled;
    denoise.luma = std::clamp(detail.luminanceNoise, 0.0, 100.0);
    denoise.Ldetail = std::clamp(detail.luminanceDetail, 0.0, 100.0);
    denoise.chroma = std::clamp(detail.colorNoise, 0.0, 100.0);
}

RawDevelopParams::Optics RawDevelopParams::optics() const
{
    const auto& lens = m_impl->procParams.lensProf;
    const auto& distortion = m_impl->procParams.distortion;
    const auto& vignette = m_impl->procParams.vignetting;
    const auto& ca = m_impl->procParams.cacorrection;
    const auto& defringe = m_impl->procParams.defringe;
    return {lens.lcMode == rtengine::procparams::LensProfParams::LcMode::LENSFUNAUTOMATCH,
            lens.useDist, lens.useVign, lens.useCA, distortion.amount, vignette.amount,
            ca.red, ca.blue, defringe.enabled, defringe.radius, defringe.threshold};
}

void RawDevelopParams::setOptics(const Optics& optics)
{
    auto& lens = m_impl->procParams.lensProf;
    lens.lcMode = optics.automaticLensProfile
        ? rtengine::procparams::LensProfParams::LcMode::LENSFUNAUTOMATCH
        : rtengine::procparams::LensProfParams::LcMode::NONE;
    lens.useDist = optics.correctDistortion;
    lens.useVign = optics.correctVignetting;
    lens.useCA = optics.correctChromaticAberration;

    m_impl->procParams.distortion.amount = std::clamp(optics.distortion, -0.5, 0.5);
    m_impl->procParams.vignetting.amount = std::clamp(optics.lensVignetting, -100, 100);
    m_impl->procParams.cacorrection.red = std::clamp(optics.redChromaticAberration, -0.005, 0.005);
    m_impl->procParams.cacorrection.blue = std::clamp(optics.blueChromaticAberration, -0.005, 0.005);
    auto& defringe = m_impl->procParams.defringe;
    defringe.enabled = optics.defringeEnabled;
    defringe.radius = std::clamp(optics.defringeRadius, 0.5, 5.0);
    defringe.threshold = std::clamp(optics.defringeThreshold, 0, 100);
}

RawDevelopParams::ColorGrading RawDevelopParams::colorGrading() const
{
    const auto& toning = m_impl->procParams.colorToning;
    return {toning.enabled,
            {toning.redlow, toning.greenlow, toning.bluelow},
            {toning.redmed, toning.greenmed, toning.bluemed},
            {toning.redhigh, toning.greenhigh, toning.bluehigh},
            toning.strength, toning.balance};
}

void RawDevelopParams::setColorGrading(const ColorGrading& grading)
{
    auto& toning = m_impl->procParams.colorToning;
    toning.enabled = grading.enabled;
    // Splitco is the engine mode that consumes the independent low/mid/high
    // RGB mixer values below. RGBSliders instead consumes the separate
    // two-colour shadowsColSat/hlColSat controls and ignores these wheels.
    toning.method = "Splitco";
    toning.redlow = std::clamp(grading.shadows[0], -100.0, 100.0);
    toning.greenlow = std::clamp(grading.shadows[1], -100.0, 100.0);
    toning.bluelow = std::clamp(grading.shadows[2], -100.0, 100.0);
    toning.redmed = std::clamp(grading.midtones[0], -100.0, 100.0);
    toning.greenmed = std::clamp(grading.midtones[1], -100.0, 100.0);
    toning.bluemed = std::clamp(grading.midtones[2], -100.0, 100.0);
    toning.redhigh = std::clamp(grading.highlights[0], -100.0, 100.0);
    toning.greenhigh = std::clamp(grading.highlights[1], -100.0, 100.0);
    toning.bluehigh = std::clamp(grading.highlights[2], -100.0, 100.0);
    toning.strength = std::clamp(grading.strength, 0, 100);
    toning.balance = std::clamp(grading.balance, -100, 100);
}

RawDevelopParams::ColorMixer RawDevelopParams::colorMixer() const
{
    const auto& mixer = m_impl->procParams.hsvequalizer;
    return {mixer.enabled, decodeMixerCurve(mixer.hcurve), decodeMixerCurve(mixer.scurve),
            decodeMixerCurve(mixer.vcurve)};
}

void RawDevelopParams::setColorMixer(const ColorMixer& mixer)
{
    auto& target = m_impl->procParams.hsvequalizer;
    target.enabled = mixer.enabled;
    target.hcurve = encodeMixerCurve(mixer.hue);
    target.scurve = encodeMixerCurve(mixer.saturation);
    target.vcurve = encodeMixerCurve(mixer.luminance);
}

std::vector<RawDevelopParams::CurvePoint> RawDevelopParams::curve(CurveChannel channel) const
{
    const auto& params = m_impl->procParams;
    switch (channel) {
    case CurveChannel::RGB: return decodeDiagonalCurve(params.toneCurve.curve);
    case CurveChannel::Red: return decodeDiagonalCurve(params.rgbCurves.rcurve);
    case CurveChannel::Green: return decodeDiagonalCurve(params.rgbCurves.gcurve);
    case CurveChannel::Blue: return decodeDiagonalCurve(params.rgbCurves.bcurve);
    }
    return {};
}

void RawDevelopParams::setCurve(CurveChannel channel, const std::vector<CurvePoint>& points)
{
    auto& params = m_impl->procParams;
    switch (channel) {
    case CurveChannel::RGB:
        params.toneCurve.curve = encodeDiagonalCurve(
            points, editableDiagonalCurveKind(params.toneCurve.curve));
        // Auto-Matched Curve presets regenerate toneCurve.curve during every
        // preview while histogram matching remains enabled. A manual edit is
        // the same boundary RawTherapee uses to leave that automatic mode;
        // otherwise the freshly written points are immediately overwritten.
        params.toneCurve.histmatching = false;
        params.toneCurve.fromHistMatching = false;
        break;
    case CurveChannel::Red:
        params.rgbCurves.rcurve = encodeDiagonalCurve(
            points, editableDiagonalCurveKind(params.rgbCurves.rcurve));
        params.rgbCurves.enabled = true;
        break;
    case CurveChannel::Green:
        params.rgbCurves.gcurve = encodeDiagonalCurve(
            points, editableDiagonalCurveKind(params.rgbCurves.gcurve));
        params.rgbCurves.enabled = true;
        break;
    case CurveChannel::Blue:
        params.rgbCurves.bcurve = encodeDiagonalCurve(
            points, editableDiagonalCurveKind(params.rgbCurves.bcurve));
        params.rgbCurves.enabled = true;
        break;
    }
}

bool RawDevelopParams::needsResolvedToneCurve() const
{
    const auto& tone = m_impl->procParams.toneCurve;
    // Two engine-side resolutions land in the same struct: histogram matching
    // generates `curve` on the first render, and auto-exposure recomputes
    // expcomp/contrast/black/hlcompr on every render (ImProcCoordinator's
    // M_AUTOEXP block). Either way the sliders have to show what the engine
    // decided, not the placeholder the profile carried in.
    return tone.autoexp || (tone.histmatching && !tone.fromHistMatching);
}

bool RawDevelopParams::adoptResolvedToneCurve(const RawDevelopParams& resolved)
{
    if (m_impl->procParams.toneCurve == resolved.m_impl->procParams.toneCurve)
        return false;
    m_impl->procParams.toneCurve = resolved.m_impl->procParams.toneCurve;
    return true;
}

bool RawDevelopParams::adoptResolvedWhiteBalance(const RawDevelopParams& resolved)
{
    auto& wb = m_impl->procParams.wb;
    const auto& source = resolved.m_impl->procParams.wb;
    // ImProcCoordinator writes the temperature/green it actually used back
    // into ProcParams on every render (improccoordinator.cc, right after the
    // method cascade), whatever the method was. That is the only way to learn
    // the shot's own white balance: with method "Camera" the engine takes
    // ImageSource::getWB() and ignores these two fields entirely, so leaving
    // ProcParams' hand-wired 6504 K on the sliders would both misreport the
    // shot and make the first Temperature drag jump the image.  Only the two
    // resolved values are adopted — never `method`, which stays the user's.
    if (wb.temperature == source.temperature && wb.green == source.green)
        return false;
    wb.temperature = source.temperature;
    wb.green = source.green;
    return true;
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
bool RawDevelopParams::presetWhiteBalance(const std::string&, WhiteBalance&) { return false; }
RawDevelopParams::Exposure RawDevelopParams::exposure() const { return {}; }
void RawDevelopParams::setExposure(const Exposure&) {}
RawDevelopParams::Light RawDevelopParams::light() const { return {}; }
void RawDevelopParams::setLight(const Light&) {}
RawDevelopParams::Color RawDevelopParams::color() const { return {}; }
void RawDevelopParams::setColor(const Color&) {}
RawDevelopParams::Effects RawDevelopParams::effects() const { return {}; }
void RawDevelopParams::setEffects(const Effects&) {}
RawDevelopParams::Detail RawDevelopParams::detail() const { return {}; }
void RawDevelopParams::setDetail(const Detail&) {}
RawDevelopParams::Optics RawDevelopParams::optics() const { return {}; }
void RawDevelopParams::setOptics(const Optics&) {}
RawDevelopParams::ColorGrading RawDevelopParams::colorGrading() const { return {}; }
void RawDevelopParams::setColorGrading(const ColorGrading&) {}
RawDevelopParams::ColorMixer RawDevelopParams::colorMixer() const { return {}; }
void RawDevelopParams::setColorMixer(const ColorMixer&) {}
std::vector<RawDevelopParams::CurvePoint> RawDevelopParams::curve(CurveChannel) const { return {}; }
void RawDevelopParams::setCurve(CurveChannel, const std::vector<CurvePoint>&) {}
bool RawDevelopParams::needsResolvedToneCurve() const { return false; }
bool RawDevelopParams::adoptResolvedToneCurve(const RawDevelopParams&) { return false; }
bool RawDevelopParams::adoptResolvedWhiteBalance(const RawDevelopParams&) { return false; }

#endif
