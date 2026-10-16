#pragma once

#include <memory>
#include <string>

// Opaque, RAII wrapper around a PP3 develop profile (rtengine's ProcParams).
// Never leaks rtengine::procparams::ProcParams; everything outside src/raw/
// goes through the typed accessors below or the PP3 string/file round-trip.
//
// Only the accessor groups an actual caller needs exist so far (input
// profile, white balance, exposure). Add more as the Camera Raw UI (out of
// scope here, see tmp/feat-raw-therapee-plan.md) needs them — same pimpl,
// same pattern.
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

    struct Impl;
    Impl& impl() { return *m_impl; }
    const Impl& impl() const { return *m_impl; }

private:
    std::unique_ptr<Impl> m_impl;
};
