#include "raw/RawPreviewCapabilities.hpp"

#include "raw/RawDevelopParams.hpp"

#include <QCoreApplication>
#include <QObject>
#include <QStringList>

namespace rawpreview {
namespace {

// The engine's 1:1 slice is rendered at exactly 1:1 and then drawn at the
// view's zoom, so anything other than 100% either drops the four stages
// (below) or magnifies them (above). The tolerance absorbs the rounding of a
// zoom that was derived from a fit scale.
constexpr double kUnityTolerance = 0.005;

QString joinStages(const std::vector<Stage>& stages)
{
    QStringList names;
    for (std::size_t i = 0; i < stages.size(); ++i) {
        const QString name = stageName(stages[i]);
        names << (i == 0 ? name : name.toLower());
    }
    if (names.size() <= 1)
        return names.value(0);
    const QString last = names.takeLast();
    return QObject::tr("%1 and %2").arg(names.join(QStringLiteral(", ")), last);
}

} // namespace

std::vector<Stage> activeStages(const RawDevelopParams& params)
{
    std::vector<Stage> stages;
    if (params.detail().sharpeningEnabled)
        stages.push_back(Stage::Sharpening);
    if (params.detail().denoiseEnabled)
        stages.push_back(Stage::NoiseReduction);
    if (params.effects().textureEnabled)
        stages.push_back(Stage::Texture);
    if (params.optics().defringeEnabled)
        stages.push_back(Stage::Defringe);
    return stages;
}

QString stageName(Stage stage)
{
    switch (stage) {
        case Stage::Sharpening:     return QObject::tr("Sharpening");
        case Stage::NoiseReduction: return QObject::tr("Noise reduction");
        case Stage::Texture:        return QObject::tr("Texture");
        case Stage::Defringe:       return QObject::tr("Defringe");
    }
    return QString();
}

Status status(const State& state)
{
    if (state.applying || state.processing)
        return Status::Processing;
    if (!state.engineAvailable || state.showingBefore)
        return Status::Ready;
    // The slice is the engine's own full-quality output for the visible
    // region; everything else on screen came from the scaled pipeline.
    return state.detailSliceVisible ? Status::FinalQuality : Status::Interactive;
}

QString statusLabel(Status status)
{
    switch (status) {
        case Status::Ready:        return QObject::tr("Ready");
        case Status::Processing:   return QObject::tr("Processing");
        case Status::Interactive:  return QObject::tr("Interactive Preview");
        case Status::FinalQuality: return QObject::tr("Final Quality");
    }
    return QString();
}

QString statusText(const State& state)
{
    if (state.applying)
        return QObject::tr("Applying full-quality RAW development…");
    if (state.processing)
        return QObject::tr("Processing…");
    return statusLabel(status(state));
}

Notice notice(const State& state)
{
    if (!state.errorMessage.isEmpty())
        return {Severity::Error, state.errorMessage, false};
    if (!state.engineAvailable)
        return {Severity::Error, QObject::tr("RAW preview is unavailable in this build."), false};
    // The before image is the layer as it already stands — a finished
    // develop, with no preview limitation to report.
    if (state.showingBefore || state.activeStages.empty())
        return {};

    const QString stages = joinStages(state.activeStages);
    if (state.zoom < 1.0 - kUnityTolerance) {
        return {Severity::Unavailable,
                QObject::tr("%1 preview requires 100% zoom.").arg(stages), true};
    }
    if (state.zoom > 1.0 + kUnityTolerance) {
        // Rendered, but magnified from the 1:1 slice, so what is on screen is
        // an interpolation of the stage rather than the stage itself.
        return {Severity::Approximate,
                QObject::tr("%1 is best evaluated at 100%.").arg(stages), true};
    }
    if (!state.detailSliceVisible) {
        return {Severity::Approximate,
                QObject::tr("Interactive preview — final rendering may differ."), false};
    }
    return {};
}

} // namespace rawpreview
