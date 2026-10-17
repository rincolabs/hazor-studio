#pragma once

#include <array>
#include <memory>
#include <string>
#include <vector>

// Opaque, RAII wrapper around a PP3 develop profile (rtengine's ProcParams).
// Never leaks rtengine::procparams::ProcParams; everything outside src/raw/
// goes through the typed accessors below or the PP3 string/file round-trip.
//
// Accessor groups exist for every control Hazor Develop exposes (input
// profile, white balance, light, color, effects, curves, color mixer, color
// grading, detail, optics). Add more the same way — same pimpl, same
// pattern — if a future control needs one.
class RawDevelopParams {
public:
    // Maps to ColorManagementParams::inputProfile's four sentinel values.
    // Custom is read-only here: it is entered via setCustomInputProfile(),
    // never selected through setInputProfileMode().
    enum class InputProfileMode {
        CameraMatched,  // "(cameraICC)" — DCP, then ICC, then camera matrix (default)
        Embedded,       // "(embedded)"  — the profile embedded in the RAW file
        None,           // "(none)"      — no input color conversion
        CameraMatrix,   // "(camera)"    — force the camera's own color matrix
        Custom,         // an explicit DCP/ICC path set via setCustomInputProfile()
    };

    struct WhiteBalance {
        std::string method = "Camera";     // "Camera", "Auto", "Custom", ...
        int temperatureKelvin = 6504;
        double green = 1.0;                // WBParams::green (tint multiplier)
    };

    struct Exposure {
        bool autoExposure = false;
        double compensationEv = 0.0;
    };

    // The following groups deliberately use RawTherapee's own units and
    // defaults.  They are the UI-facing vocabulary for Hazor Develop; the
    // implementation maps them directly to ProcParams without exposing an
    // rtengine type to the rest of the application.
    struct Light {
        bool autoExposure = false;
        double exposureEv = 0.0;        // toneCurve.expcomp, -5..12 EV
        int contrast = 0;               // toneCurve.contrast, -100..100
        int highlights = 0;             // sh.highlights, 0..100
        int shadows = 0;                // sh.shadows, 0..100
        int whiteRecovery = 0;          // toneCurve.hlcompr, 0..500
        int blackPoint = 0;             // toneCurve.black, -16384..32768
    };

    struct Color {
        bool blackAndWhite = false;     // blackwhite.enabled
        int vibrance = 0;               // vibrance.pastels, -100..100
        int saturation = 0;             // toneCurve.saturation, -100..100
    };

    struct Effects {
        bool textureEnabled = false;    // sharpenMicro.enabled
        double texture = 20.0;          // sharpenMicro.amount, 0..100
        bool clarityEnabled = false;    // localContrast.enabled
        double clarity = 0.2;           // localContrast.amount, 0..1
        bool dehazeEnabled = false;
        int dehaze = 50;                // dehaze.strength, 0..100
        bool vignetteEnabled = false;
        double vignetteAmount = 0.0;    // pcvignette.strength, -6..6
        int vignetteFeather = 50;       // pcvignette.feather, 0..100
        int vignetteRoundness = 50;     // pcvignette.roundness, 0..100
    };

    struct Detail {
        bool sharpeningEnabled = false;
        int sharpeningAmount = 200;    // sharpening.amount, 1..1000
        double sharpeningRadius = 0.5; // sharpening.radius, .3..3
        double sharpeningDetail = 20;  // sharpening.contrast, 0..200
        bool denoiseEnabled = false;
        double luminanceNoise = 0.0;   // dirpyrDenoise.luma, 0..100
        double luminanceDetail = 0.0;  // dirpyrDenoise.Ldetail, 0..100
        double colorNoise = 15.0;      // dirpyrDenoise.chroma, 0..100
    };

    struct Optics {
        bool automaticLensProfile = false; // Lensfun auto match
        bool correctDistortion = true;
        bool correctVignetting = true;
        bool correctChromaticAberration = true;
        double distortion = 0.0;           // distortion.amount, -.5...5
        int lensVignetting = 0;            // vignetting.amount, -100..100
        double redChromaticAberration = 0.0;   // cacorrection.red, -0.005..0.005
        double blueChromaticAberration = 0.0;  // cacorrection.blue, -0.005..0.005
        bool defringeEnabled = false;
        double defringeRadius = 2.0;       // .5..5
        int defringeThreshold = 13;        // 0..100
    };

    // RawTherapee's Color Toning has independent RGB controls for the three
    // tonal regions. Hazor's circular grading controls compose those values;
    // keeping the three components explicit makes the PP3 mapping lossless.
    struct ColorGrading {
        bool enabled = false;
        std::array<double, 3> shadows{};
        std::array<double, 3> midtones{};
        std::array<double, 3> highlights{};
        int blending = 50;               // colorToning.strength, 0..100
        int balance = 0;                 // colorToning.balance, -100..100
    };

    struct ColorMixer {
        bool enabled = false;
        // Red, Orange, Yellow, Green, Aqua, Blue, Purple, Magenta.  Each
        // value is encoded in the corresponding rtengine FlatCurve.
        std::array<double, 8> hue{};
        std::array<double, 8> saturation{};
        std::array<double, 8> luminance{};
    };

    struct CurvePoint {
        double x = 0.0; // normalized 0..1
        double y = 0.0; // normalized 0..1
    };
    enum class CurveChannel { RGB, Red, Green, Blue };

    RawDevelopParams();
    RawDevelopParams(const RawDevelopParams& other);
    RawDevelopParams(RawDevelopParams&& other) noexcept;
    RawDevelopParams& operator=(const RawDevelopParams& other);
    RawDevelopParams& operator=(RawDevelopParams&& other) noexcept;
    ~RawDevelopParams();

    // rtengine's own hand-wired ProcParams defaults (white balance read from
    // the shot, no exposure/tone adjustments). Deliberately does *not* run
    // the ProfileStore dynamic-profile-rule subsystem ("Auto-Matched Curve",
    // ISO-based curve selection) — that is part of the Camera Raw UI, out of
    // scope for this API (see "Fora de escopo" in the plan).
    static RawDevelopParams cameraDefaults();

    // Falls back to cameraDefaults() if pp3 fails to parse (isValid() does
    // not distinguish that from a legitimately all-defaults PP3 — nothing
    // needs that distinction today).
    static RawDevelopParams fromPP3String(const std::string& pp3);
    std::string toPP3String() const;

    // Returns false (and leaves *out untouched) if the file could not be read/parsed.
    static bool loadPP3File(const std::string& path, RawDevelopParams& out);
    bool savePP3File(const std::string& path) const;

    // False only when HAVE_RAWENGINE is not compiled in, in which case every
    // operation above is an inert no-op (toPP3String/savePP3File return
    // empty/false, fromPP3String/cameraDefaults/loadPP3File just return/fill
    // an isValid() == false instance).
    bool isValid() const;

    InputProfileMode inputProfileMode() const;
    void setInputProfileMode(InputProfileMode mode);
    // Path (optionally "file:"-prefixed) to a user DCP or ICC profile; resolved
    // by rtengine's own DCPStore/ICCStore lookup chain, never reimplemented here.
    void setCustomInputProfile(const std::string& path);
    std::string customInputProfile() const; // non-empty only when inputProfileMode() == Custom

    WhiteBalance whiteBalance() const;
    void setWhiteBalance(const WhiteBalance& wb);

    Exposure exposure() const;
    void setExposure(const Exposure& exposure);

    Light light() const;
    void setLight(const Light& light);
    Color color() const;
    void setColor(const Color& color);
    Effects effects() const;
    void setEffects(const Effects& effects);
    Detail detail() const;
    void setDetail(const Detail& detail);
    Optics optics() const;
    void setOptics(const Optics& optics);
    ColorGrading colorGrading() const;
    void setColorGrading(const ColorGrading& grading);
    ColorMixer colorMixer() const;
    void setColorMixer(const ColorMixer& mixer);

    // Point-based PP3 curves (spline, NURBS and Catmull-Rom) retain their kind
    // while edited. Parametric curves cannot be represented by this point
    // editor and come back as identity rather than being reinterpreted.
    std::vector<CurvePoint> curve(CurveChannel channel) const;
    void setCurve(CurveChannel channel, const std::vector<CurvePoint>& points);
    bool needsAutoMatchedToneCurveResolution() const;
    void adoptResolvedToneCurve(const RawDevelopParams& resolved);

    struct Impl;
    Impl& impl() { return *m_impl; }
    const Impl& impl() const { return *m_impl; }

private:
    std::unique_ptr<Impl> m_impl;
};
