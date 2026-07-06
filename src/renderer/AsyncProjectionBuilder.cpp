#include "AsyncProjectionBuilder.hpp"

#include "DocumentCompositor.hpp"
#include "RenderContext.hpp"
#include "core/Document.hpp"

#include <QtConcurrent/QtConcurrent>

AsyncProjectionBuilder::AsyncProjectionBuilder(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<std::shared_ptr<Result>>::finished,
            this, &AsyncProjectionBuilder::onFinished);
}

AsyncProjectionBuilder::~AsyncProjectionBuilder()
{
    // The running task captures only the snapshot (by value) — never `this` —
    // so it is safe to let it finish. Wait so the worker isn't left compositing
    // into a freed snapshot mid-teardown.
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
}

void AsyncProjectionBuilder::request(std::shared_ptr<Document> snapshot,
                                     const void* docToken, uint64_t generation)
{
    if (!snapshot || m_watcher.isRunning())
        return; // already busy — caller re-requests the latest state when idle

    // Capture only value types (shared_ptr keeps the snapshot alive; the raw
    // docToken is an opaque identity tag, never dereferenced on the worker).
    auto future = QtConcurrent::run(
        [snapshot, docToken, generation]() -> std::shared_ptr<Result> {
            auto r = std::make_shared<Result>();
            r->docToken   = docToken;
            r->generation = generation;
            RenderContext ctx;
            ctx.document    = snapshot.get();
            ctx.outputSize  = snapshot->size;
            ctx.targetType  = RenderTargetType::Canvas;
            ctx.highQuality = true;
            r->image = DocumentCompositor::composite(snapshot.get(), ctx);
            return r;
        });
    m_watcher.setFuture(future);
}

void AsyncProjectionBuilder::onFinished()
{
    m_result    = m_watcher.result();
    m_hasResult = (m_result != nullptr);
    emit ready();
}

bool AsyncProjectionBuilder::takeResult(const void*& docToken,
                                        uint64_t& generation, QImage& image)
{
    if (!m_hasResult || !m_result)
        return false;
    docToken   = m_result->docToken;
    generation = m_result->generation;
    image      = m_result->image;
    m_hasResult = false;
    m_result.reset();
    return true;
}
