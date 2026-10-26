#include "smartobject/SmartObjectRenderService.hpp"

#include "color/ColorManagementService.hpp"
#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"
#include "io/ImageCodec.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"

#include <QtConcurrent/QtConcurrent>

namespace {

// Re-tags `image` from `source` into `destination` through the normal colour
// pipeline — the same path an externally imported image takes
// (ColorManagementService). D8: the referenced document keeps its OWN profile
// (it is a real document), and only the render handed to the instance is
// converted into the parent's space, so an AdobeRGB Smart Object placed in an
// sRGB project does not shift colour on the canvas.
QImage convertToProfile(const QImage& image,
                        const ColorProfile& source,
                        const ColorProfile& destination)
{
    if (image.isNull() || !source.isValid() || !destination.isValid())
        return image;
    if (source.equivalentTo(destination))
        return image;

    DocumentImage documentImage = convertQImageToDocumentImageValue(image, QString(), QString());
    documentImage.colorProfile = source;
    documentImage.iccProfile = source.iccBytes();

    const DocumentImage converted = ColorManagementService::instance().convertPixels(
        documentImage, source, destination,
        ColorManagementService::instance().defaultConversionOptions());
    QImage out = convertDocumentImageToQImage(converted);
    return out.isNull() ? image : out.convertToFormat(QImage::Format_RGBA8888);
}

} // namespace

SmartObjectRenderService::SmartObjectRenderService(QObject* parent)
    : QObject(parent)
{}

SmartObjectRenderService::~SmartObjectRenderService() = default;

std::shared_ptr<Document> SmartObjectRenderService::makeSnapshot(const Document& doc)
{
    auto snap = std::make_shared<Document>();
    snap->size = doc.size;
    snap->resolutionDpi = doc.resolutionDpi;
    snap->colorMode = doc.colorMode;
    snap->bitDepth = doc.bitDepth;
    snap->compositionGeneration = doc.compositionGeneration;
    snap->setColorProfile(doc.colorProfile());
    snap->setProfileSource(doc.profileSource());
    snap->roots.reserve(doc.roots.size());
    for (const auto& root : doc.roots)
        snap->roots.push_back(root->shallowClone());
    return snap;
}

bool SmartObjectRenderService::request(const SmartObjectRenderKey& key,
                                       const Document& source,
                                       const ColorProfile& targetProfile,
                                       bool marksDirty)
{
    if (key.docId.isEmpty() || source.size.isEmpty())
        return false;

    auto job = std::make_unique<Job>();
    job->snapshot = makeSnapshot(source);
    job->sourceProfile = source.colorProfile();
    job->targetProfile = targetProfile;
    // Captured now, not at completion — see SmartObjectRenderResult.
    job->generation = source.compositionGeneration;
    job->sourceDpi = source.resolutionDpi;
    job->marksDirty = marksDirty;

    auto it = m_entries.find(key);
    if (it != m_entries.end() && (*it)->dispatched) {
        // The in-flight job is already rendering this exact state, so there is
        // nothing new to produce — drop the request instead of queueing a
        // duplicate. This is what keeps a periodic refresh from piling a second
        // (and differently flagged) job behind one that already covers it.
        if ((*it)->inFlightGeneration == job->generation && !(*it)->queued)
            return true;
        // Otherwise the source moved on: replace whatever was stashed with the
        // newer snapshot. Collapsing a burst of edits into one follow-up render
        // is the point — queueing each would render states nobody will see.
        (*it)->queued = std::move(job);
        return true;
    }

    dispatch(key, std::move(job));
    return true;
}

void SmartObjectRenderService::dispatch(const SmartObjectRenderKey& key,
                                        std::unique_ptr<Job> job)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        it = m_entries.insert(key, std::make_shared<Entry>());
    Entry* entry = it->get();

    if (!entry->connected) {
        QObject::connect(&entry->watcher,
                         &QFutureWatcher<std::shared_ptr<SmartObjectRenderResult>>::finished,
                         this, [this, key]() { onFinished(key); });
        entry->connected = true;
    }
    entry->dispatched = true;
    entry->inFlightGeneration = job->generation;
    entry->queued.reset();

    std::shared_ptr<Job> shared = std::move(job);
    entry->watcher.setFuture(QtConcurrent::run([key, shared]() {
        return runJob(key, shared);
    }));
}

std::shared_ptr<SmartObjectRenderResult>
SmartObjectRenderService::runJob(SmartObjectRenderKey key, std::shared_ptr<Job> job)
{
    auto result = std::make_shared<SmartObjectRenderResult>();
    result->key = std::move(key);
    result->jobGeneration = job->generation;
    result->sourceSize = job->snapshot->size;
    result->sourceDpi = job->sourceDpi;
    result->marksDirty = job->marksDirty;

    RenderContext ctx;
    ctx.document = job->snapshot.get();
    ctx.outputSize = job->snapshot->size;
    ctx.documentRect = QRectF(QPointF(0, 0), job->snapshot->size);
    // Flatten, not Canvas: this is a committed composite of a whole document,
    // the same class of work as Merge/Flatten, and the CPU compositor is the
    // visual source of truth for it (AGENTS.md invariant 1). Routing the
    // instance through the same function an export uses is what guarantees the
    // live frame and the exported file agree.
    ctx.targetType = RenderTargetType::Flatten;
    ctx.highQuality = true;

    QImage composite = DocumentCompositor::composite(job->snapshot.get(), ctx);
    if (composite.isNull())
        return result;

    result->image = convertToProfile(composite.convertToFormat(QImage::Format_RGBA8888),
                                     job->sourceProfile, job->targetProfile);
    result->ok = !result->image.isNull();
    return result;
}

void SmartObjectRenderService::onFinished(const SmartObjectRenderKey& key)
{
    auto it = m_entries.find(key);
    if (it == m_entries.end())
        return;
    Entry* entry = it->get();
    entry->dispatched = false;

    // Read the result BEFORE re-dispatching: dispatch() installs a new future
    // on the same watcher, after which result() would answer for the wrong job.
    std::shared_ptr<SmartObjectRenderResult> result = entry->watcher.result();

    // A snapshot stashed while this job ran goes out now, so the cascade and
    // the user's next edit both get a fresh render without ever having two
    // composites of the same document running at once.
    if (entry->queued) {
        auto queued = std::move(entry->queued);
        dispatch(key, std::move(queued));
    }
    // `entry`/`it` are not touched past this point — dispatch() may have
    // rehashed the table.

    if (result)
        emit renderFinished(*result);
}

bool SmartObjectRenderService::isBusy(const SmartObjectRenderKey& key) const
{
    auto it = m_entries.constFind(key);
    return it != m_entries.constEnd() && (*it)->dispatched;
}
