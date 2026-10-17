#pragma once

#include <QString>
#include <vector>

class RawDevelopParams;

// What Hazor Develop's preview can and cannot show, in one place.
//
// The limits are the *engine's*, not the UI's: ImProcCoordinator runs
// sharpening, noise reduction, texture (micro-contrast) and defringe only in
// the detail pipeline behind a `skip == 1` guard — see RawDetailRegion in
// RawSession.hpp. So those four controls, and only those four, cannot be seen
// below 1:1, and the rule is identical for all of them. Centralizing it here
// keeps every control from inventing its own warning, and keeps the wording
// tied to what the engine actually does rather than to a guess.
namespace rawpreview {

// The four stages that exist only in the engine's 1:1 detail pipeline.
enum class Stage { Sharpening, NoiseReduction, Texture, Defringe };

enum class Severity {
    None,
    // The engine renders the stage at this zoom, but judging it needs 1:1.
    Approximate,
    // The engine cannot render the stage at this zoom at all.
    Unavailable,
    // A source or render failure; the message is the engine's own.
    Error,
};

enum class Status { Ready, Processing, Interactive, FinalQuality };

// Everything the status line and the alerts are derived from. The window
// fills this in once per refresh; nothing below reads any other state.
struct State {
    bool engineAvailable = true;
    bool processing = false;    // a preview render is running or queued
    bool applying = false;      // the final-quality develop is in flight
    bool showingBefore = false; // the viewport shows the layer as it stands
    // Real zoom of what is on screen, Fit included (1.0 == 1:1).
    double zoom = 1.0;
    // True when the engine delivered a 1:1 slice for the visible region.
    bool detailSliceVisible = false;
    std::vector<Stage> activeStages;
    QString errorMessage;
};

struct Notice {
    Severity severity = Severity::None;
    QString message;
    // True when going to 1:1 is what resolves the notice, so the status bar
    // can offer it as an action.
    bool offerFullZoom = false;

    bool isEmpty() const { return severity == Severity::None || message.isEmpty(); }
};

// The detail-pipeline stages the PP3 currently has enabled.
std::vector<Stage> activeStages(const RawDevelopParams& params);
QString stageName(Stage stage);

Status status(const State& state);
// Short label for the status chip ("Ready", "Interactive Preview", …).
QString statusLabel(Status status);
// What the status line reads, including the two busy cases.
QString statusText(const State& state);

Notice notice(const State& state);

} // namespace rawpreview
