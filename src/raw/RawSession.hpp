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
    // denoising and uses faster transform algorithms above 1:1 scale (see
    // rtengine.h's own PreviewImageListener docs) — expected, not a defect.
    // Always RawPixelFormat::RGB8 (see RawImageBuffer.hpp). cancelToken is
    // only checked before starting; once rtengine's background update thread
    // is running there is no way to interrupt it, only to wait for it.
    // Returns an empty RawImageBuffer (isEmpty()) if cancelled before
    // starting or if the engine isn't available.
    RawImageBuffer renderPreview(int maxEdge, const RawDevelopParams& params, RawCancelToken cancelToken = nullptr);

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
