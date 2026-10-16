#include "raw/RawSession.hpp"

#include "raw/RawEngineAvailability.hpp"

#ifdef HAVE_RAWENGINE

#include <algorithm>
#include <cmath>
#include <condition_variable>
#include <mutex>

#include "raw/RawEngine.hpp"
#include "raw/internal/RawDevelopParamsImpl.hpp"
#include "rtengine/rtengine.h"

namespace {

std::string lowercaseExtension(const std::string& path)
{
    const auto dot = path.find_last_of('.');
    if (dot == std::string::npos) {
        return {};
    }
    std::string ext = path.substr(dot + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return std::tolower(c); });
    return ext;
}

RawMetadata extractMetadata(const rtengine::FramesMetaData& meta)
{
    RawMetadata out;
    out.cameraMake = meta.getMake();
    out.cameraModel = meta.getModel();
    out.lens = meta.getLens();
    out.isoSpeed = meta.getISOSpeed();
    out.fNumber = meta.getFNumber();
    out.focalLengthMm = meta.getFocalLen();
    out.shutterSpeedSeconds = meta.getShutterSpeed();
    out.exposureCompensationEv = meta.getExpComp();

    int w = 0, h = 0;
    meta.getDimensions(w, h);
    out.width = w;
    out.height = h;

    // Standard EXIF orientation strings — see imagedata.cc's `ormap` table,
    // which this mirrors exactly (values 1..8, "Unknown" for 0 and 9+).
    const std::string orientation = meta.getOrientation();
    if (orientation == "Rotate 90 CW") {
        out.orientationDegrees = 90;
    } else if (orientation == "Rotate 180") {
        out.orientationDegrees = 180;
    } else if (orientation == "Rotate 270 CW") {
        out.orientationDegrees = 270;
    } else if (orientation == "Mirror horizontal") {
        out.orientationMirrored = true;
    } else if (orientation == "Mirror vertical") {
        out.orientationDegrees = 180;
        out.orientationMirrored = true;
    } else if (orientation == "Mirror horizontal and rotate 90 CW") {
        out.orientationDegrees = 90;
        out.orientationMirrored = true;
    } else if (orientation == "Mirror horizontal and rotate 270 CW") {
        out.orientationDegrees = 270;
        out.orientationMirrored = true;
    }
    // "Horizontal (normal)" / "Unknown" / anything else: 0 degrees, not mirrored.

    return out;
}

RawGeometry geometryFromParams(const rtengine::procparams::ProcParams& pp, int fullWidth, int fullHeight)
{
    RawGeometry geo;
    geo.fullWidth = fullWidth;
    geo.fullHeight = fullHeight;
    geo.coarseRotationDegrees = pp.coarse.rotate;
    geo.mirrorHorizontal = pp.coarse.hflip;
    geo.mirrorVertical = pp.coarse.vflip;
    geo.cropEnabled = pp.crop.enabled;
    geo.cropX = pp.crop.x;
    geo.cropY = pp.crop.y;
    geo.cropWidth = pp.crop.w;
    geo.cropHeight = pp.crop.h;
    geo.resizeScale = pp.resize.enabled ? pp.resize.scale : 1.0;
    return geo;
}

// Bridges StagedImageProcessor's PreviewImageListener (background-thread,
// callback-driven) to the synchronous renderPreview() call below.
//
// previmg (the IImage8 handed to setImage()) is allocated once by the
// coordinator and reused in place across updates; imageReady() fires once
// per completed update. Per rtengine.h's own contract, when a listener is
// registered the coordinator expects *it* to delete previmg on delImage()
// (freeAll(), called when the StagedImageProcessor is destroyed) rather than
// deleting it itself.
class PreviewWaiter : public rtengine::PreviewImageListener {
public:
    void setImage(rtengine::IImage8* img, double /*scale*/, const rtengine::procparams::CropParams&,
                  const rtengine::procparams::CropGuideParams&) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_image = img;
    }

    void delImage(rtengine::IImage8* img) override { delete img; }

    void imageReady(const rtengine::procparams::CropParams&, const rtengine::procparams::CropGuideParams&) override
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ready = true;
        m_cv.notify_all();
    }

    // Must be called while still holding whatever serializes calls on this
    // RawSession, before beginUpdateParams()/endUpdateParams() kick off the
    // background update this call is about to wait for.
    void resetReady()
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_ready = false;
    }

    // Blocks until the in-flight update completes. There is no engine hook
    // to abort an update already in flight (ImProcCoordinator::stopProcessing()
    // itself just joins the worker thread), so this never times out early.
    bool waitForReady()
    {
        std::unique_lock<std::mutex> lock(m_mutex);
        m_cv.wait(lock, [this] { return m_ready; });
        return m_image != nullptr;
    }

    rtengine::IImage8* image() const { return m_image; }

private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    rtengine::IImage8* m_image = nullptr;
    bool m_ready = false;
};

class ProgressAdapter : public rtengine::ProgressListener {
public:
    explicit ProgressAdapter(RawProgressListener* target) : m_target(target) {}
    void setProgress(double p) override
    {
        if (m_target) {
            m_target->onProgress(p);
        }
    }
    void setProgressStr(const Glib::ustring&) override {}
    void setProgressState(bool) override {}
    void error(const Glib::ustring&) override {}

private:
    RawProgressListener* m_target;
};

} // namespace

struct RawSession::Impl {
    std::string path;
    RawMetadata metadata;

    // Owns one reference (see ~Impl); InitialImage::load() hands back refcount 1.
    rtengine::InitialImage* initialImage = nullptr;

    // Lazily created on the first renderPreview() call and kept for the
    // session's lifetime so repeated preview renders reuse the engine's own
    // caches instead of rebuilding the pipeline every time.
    std::unique_ptr<PreviewWaiter> previewWaiter;
    rtengine::StagedImageProcessor* sip = nullptr;

    ~Impl()
    {
        // StagedImageProcessor::destroy() calls previewWaiter->delImage() via
        // freeAll() and decreaseRef()s initialImage once (matching the
        // increaseRef() done when the SIP was created below) — must run
        // before our own decreaseRef() and before previewWaiter is freed.
        if (sip) {
            sip->setPreviewImageListener(nullptr);
            rtengine::StagedImageProcessor::destroy(sip);
        }
        if (initialImage) {
            initialImage->decreaseRef();
        }
    }
};

std::unique_ptr<RawSession> RawSession::open(const std::string& path, std::string* errorMessage)
{
    const bool isRaw = RawEngine::isSupportedExtension(lowercaseExtension(path));

    int errorCode = 0;
    rtengine::InitialImage* image = rtengine::InitialImage::load(path, isRaw, &errorCode);
    if (errorCode || !image) {
        if (errorMessage) {
            *errorMessage = "Could not decode RAW file: " + path;
        }
        return nullptr;
    }

    auto impl = std::make_unique<Impl>();
    impl->path = path;
    impl->initialImage = image;
    impl->metadata = extractMetadata(*image->getMetaData());

    return std::unique_ptr<RawSession>(new RawSession(std::move(impl)));
}

RawSession::RawSession(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
RawSession::~RawSession() = default;

const std::string& RawSession::filePath() const { return m_impl->path; }
const RawMetadata& RawSession::metadata() const { return m_impl->metadata; }

RawImageBuffer RawSession::renderPreview(int maxEdge, const RawDevelopParams& params, RawCancelToken cancelToken)
{
    if (maxEdge <= 0 || (cancelToken && cancelToken->load())) {
        return {};
    }

    if (!m_impl->sip) {
        // The SIP's destructor (~Impl) decreaseRef()s this once; balance it
        // here rather than relying on ProcessingJob's own increase/decrease
        // pairing, which is scoped to a single renderFull() call instead.
        m_impl->initialImage->increaseRef();
        m_impl->sip = rtengine::StagedImageProcessor::create(m_impl->initialImage);
        m_impl->previewWaiter = std::make_unique<PreviewWaiter>();
        m_impl->sip->setPreviewImageListener(m_impl->previewWaiter.get());
    }

    rtengine::StagedImageProcessor* sip = m_impl->sip;

    // getFullWidth()/getFullHeight() only report real values *after* the
    // first update has run (they default to 1x1 until then), so the scale
    // for this (possibly first) call is derived from the file's own
    // metadata dimensions instead — exact enough for an integer preview
    // divisor, and available immediately.
    const int longestEdge = std::max(m_impl->metadata.width, m_impl->metadata.height);
    const int scale = longestEdge > 0
        ? std::max(1, static_cast<int>(std::ceil(double(longestEdge) / double(maxEdge))))
        : 1;
    sip->setPreviewScale(scale);

    m_impl->previewWaiter->resetReady();

    rtengine::procparams::ProcParams* next = sip->beginUpdateParams();
    *next = params.impl().procParams;
    sip->endUpdateParams(rtengine::EvProfileChanged);

    if (!m_impl->previewWaiter->waitForReady()) {
        return {};
    }

    rtengine::IImage8* img = m_impl->previewWaiter->image();
    if (!img) {
        return {};
    }

    RawImageBuffer buffer;
    {
        MyMutex::MyLock lock(img->getMutex());
        buffer.width = img->getWidth();
        buffer.height = img->getHeight();
        buffer.format = RawPixelFormat::RGB8;
        const unsigned char* data = img->getData();
        buffer.pixelsU8.assign(data, data + std::size_t(buffer.width) * buffer.height * 3);
    }
    buffer.linear = false;
    buffer.outputIccProfile = params.impl().procParams.icm.outputProfile;
    buffer.geometry = geometryFromParams(params.impl().procParams, sip->getFullWidth(), sip->getFullHeight());
    return buffer;
}

RawImageBuffer RawSession::renderFull(const RawDevelopParams& params, RawProgressListener* progress, RawCancelToken cancelToken)
{
    if (cancelToken && cancelToken->load()) {
        return {};
    }

    // ProcessingJob::create() increases initialImage's reference count and
    // decreases it automatically once processImage() finishes (see
    // rtengine.h); no manual ref handling needed here.
    rtengine::ProcessingJob* job = rtengine::ProcessingJob::create(m_impl->initialImage, params.impl().procParams, /*fast=*/false);

    ProgressAdapter adapter(progress);
    int errorCode = 0;
    rtengine::IImagefloat* img = rtengine::processImage(job, errorCode, progress ? &adapter : nullptr, /*flush=*/false);
    if (errorCode || !img) {
        return {};
    }

    RawImageBuffer buffer;
    {
        MyMutex::MyLock lock(img->getMutex());
        buffer.width = img->getWidth();
        buffer.height = img->getHeight();
        buffer.format = RawPixelFormat::RGBFloat;
        buffer.pixelsFloat.resize(std::size_t(buffer.width) * buffer.height * 3);
        for (int y = 0; y < buffer.height; ++y) {
            float* row = &buffer.pixelsFloat[std::size_t(y) * buffer.width * 3];
            for (int x = 0; x < buffer.width; ++x) {
                row[x * 3 + 0] = img->r(y, x);
                row[x * 3 + 1] = img->g(y, x);
                row[x * 3 + 2] = img->b(y, x);
            }
        }
    }
    buffer.linear = false;
    buffer.outputIccProfile = params.impl().procParams.icm.outputProfile;
    buffer.geometry = geometryFromParams(params.impl().procParams, buffer.width, buffer.height);

    delete img;
    return buffer;
}

#else // !HAVE_RAWENGINE

struct RawSession::Impl {};

std::unique_ptr<RawSession> RawSession::open(const std::string&, std::string* errorMessage)
{
    if (errorMessage) {
        *errorMessage = "RAW support is not built into this copy of Hazor.";
    }
    return nullptr;
}

RawSession::RawSession(std::unique_ptr<Impl> impl) : m_impl(std::move(impl)) {}
RawSession::~RawSession() = default;

const std::string& RawSession::filePath() const
{
    static const std::string empty;
    return empty;
}

const RawMetadata& RawSession::metadata() const
{
    static const RawMetadata empty;
    return empty;
}

RawImageBuffer RawSession::renderPreview(int, const RawDevelopParams&, RawCancelToken) { return {}; }
RawImageBuffer RawSession::renderFull(const RawDevelopParams&, RawProgressListener*, RawCancelToken) { return {}; }

#endif
