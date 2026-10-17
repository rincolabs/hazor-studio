#pragma once

#include <atomic>
#include <memory>
#include <string>

#include "raw/RawDevelopParams.hpp"
#include "raw/RawImageBuffer.hpp"
#include "raw/RawMetadata.hpp"

// Cooperative cancellation flag shared between a caller and whatever thread
// drives a render call. Set the bool to request cancellation. Rendering is a
// single blocking rtengine call with no interrupt hook of its own, so this is
// only checked *before* a render starts, not during one already in flight —
// see the comment on renderFull()/renderPreview() below. RawDevelopService
// (Fase 4) is what actually moves these calls off the UI thread.
using RawCancelToken = std::shared_ptr<std::atomic_bool>;

// Coarse invalidation groups for interactive preview updates. They map to
// rtengine's own refresh actions inside RawSession.cpp, keeping rtengine types
// out of Hazor's public API while allowing its staged processor to reuse work.
enum class RawPreviewUpdate : unsigned int {
    None = 0,
    Exposure = 1u << 0,
    WhiteBalance = 1u << 1,
    ShadowsHighlights = 1u << 2,
    Color = 1u << 3,
    Full = 0xffffffffu,
};

constexpr RawPreviewUpdate operator|(RawPreviewUpdate lhs, RawPreviewUpdate rhs)
{
    return static_cast<RawPreviewUpdate>(
        static_cast<unsigned int>(lhs) | static_cast<unsigned int>(rhs));
}

constexpr RawPreviewUpdate& operator|=(RawPreviewUpdate& lhs, RawPreviewUpdate rhs)
{
    lhs = lhs | rhs;
    return lhs;
}

// A 1:1 slice of the image, rendered through the engine's *detail* pipeline
// (dcrop.cc) rather than the scaled preview one. It is the only path that
// applies sharpening, noise reduction, texture (micro-contrast) and
// defringe: ImProcCoordinator never runs those four at any scale — they
// live in the detail pipeline behind a `skip == 1` guard — which is what
// rtengine.h's own PreviewImageListener contract means by "if the scale of
// the preview image is >1, no sharpening, no denoising ... is applied".
//
// Requesting a region costs one full-quality demosaic: the engine keys its
// `highDetailNeeded` flag off any registered region with skip 1, so those
// stages simply cannot be seen on the fast demosaic. The result is latched
// for as long as a region stays requested, and dropping the region puts the
// next render back on the fast path — so ask for one only while something
// that needs it is actually on screen.
struct RawDetailRegion {
    // Rectangle in full-resolution output coordinates — the space
    // RawGeometry::fullWidth/fullHeight describe, not the preview buffer's.
    // renderPreview() treats it as in/out: it pins a negative origin, lets
    // the engine clamp the far edges, and writes back the rectangle the
    // engine actually delivered.
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool isEmpty() const { return width <= 0 || height <= 0; }
};

class RawProgressListener {
public:
    virtual ~RawProgressListener() = default;
    virtual void onProgress(double fraction) = 0; // 0..1
};

// One open RAW file. Created by RawEngine::openFile(); owns the decoded
// rtengine InitialImage (and, lazily, a StagedImageProcessor for repeated
// preview renders) for its lifetime. Never copyable — there is exactly one
// native decode behind a RawSession.
//
// Not internally thread-safe: calls on a given instance (including
// renderPreview()/renderFull() themselves) must be externally serialized —
// the underlying rtengine ImageSource is not built to be driven by two
// pipelines at once. RawDevelopService (Fase 4) is the intended place that
// serializes calls and moves them off the UI thread.
class RawSession {
public:
    // Returns nullptr on failure (file missing, unsupported, decode error);
    // when errorMessage is non-null it is set to a human-readable reason.
    // RawEngine::openFile() is the public entry point; it just forwards here.
    static std::unique_ptr<RawSession> open(const std::string& path, std::string* errorMessage = nullptr);

    ~RawSession();
    RawSession(const RawSession&) = delete;
    RawSession& operator=(const RawSession&) = delete;
    RawSession(RawSession&&) = delete;
    RawSession& operator=(RawSession&&) = delete;

    const std::string& filePath() const;
    const RawMetadata& metadata() const;

    // Interactive-quality render, scaled so neither dimension exceeds
    // maxEdge. Backed by StagedImageProcessor, which skips sharpening,
    // denoising, texture and defringe and uses faster transform algorithms
    // above 1:1 scale (see rtengine.h's own PreviewImageListener docs) —
    // expected, not a defect. Pass a detailRegion to get those four stages
    // for one 1:1 slice of the frame.
    // Always RawPixelFormat::RGB8 (see RawImageBuffer.hpp). cancelToken is
    // only checked before starting; once rtengine's background update thread
    // is running there is no way to interrupt it, only to wait for it.
    // Returns an empty RawImageBuffer (isEmpty()) if cancelled before
    // starting or if the engine isn't available. When resolvedParams is
    // provided, it receives the processor's effective parameters after the
    // render, including an image-specific auto-matched tone curve.
    //
    // detailRegion/detailOut are how the stages the scaled preview skips
    // become visible; see RawDetailRegion. They ride along with this call
    // instead of having one of their own because a region is rendered by the
    // same engine update (and reads the curves and white balance that update
    // is what computes), so asking separately would run the pipeline twice.
    // detailOut is RawPixelFormat::RGB8 like the return value, and is left
    // empty when the engine delivered nothing for the region.
    RawImageBuffer renderPreview(int maxEdge, const RawDevelopParams& params,
                                 RawPreviewUpdate updates = RawPreviewUpdate::Full,
                                 RawCancelToken cancelToken = nullptr,
                                 RawDevelopParams* resolvedParams = nullptr,
                                 RawDetailRegion* detailRegion = nullptr,
                                 RawImageBuffer* detailOut = nullptr);

    // Full-quality, full-resolution render via ProcessingJob::create() +
    // rtengine::processImage(). Always RawPixelFormat::RGBFloat. Same
    // cancellation caveat as renderPreview(): checked only before the
    // (synchronous, potentially slow) call starts.
    RawImageBuffer renderFull(const RawDevelopParams& params, RawProgressListener* progress = nullptr, RawCancelToken cancelToken = nullptr);

    struct Impl;

private:
    explicit RawSession(std::unique_ptr<Impl> impl);

    std::unique_ptr<Impl> m_impl;
};
