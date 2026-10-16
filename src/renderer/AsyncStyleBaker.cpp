#include "AsyncStyleBaker.hpp"

#include "core/LayerTreeNode.hpp"

#include <QtConcurrent/QtConcurrent>

AsyncStyleBaker::AsyncStyleBaker(QObject* parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFutureWatcher<std::shared_ptr<Result>>::finished,
            this, &AsyncStyleBaker::onFinished);
}

AsyncStyleBaker::~AsyncStyleBaker()
{
    // The running task owns its snapshot and captures neither `this` nor the
    // live node, so it is safe to let it finish. Wait anyway so the worker
    // isn't left running past the pool that owns it (teardown only — the pool
    // never destroys a baker while it is building).
    if (m_watcher.isRunning())
        m_watcher.waitForFinished();
}

void AsyncStyleBaker::request(std::shared_ptr<LayerTreeNode> snapshot)
{
    if (!snapshot || m_dispatched)
        return; // already busy — caller re-snapshots the latest state when idle

    m_dispatched = true;
    auto future = QtConcurrent::run(
        [snapshot = std::move(snapshot)]() -> std::shared_ptr<Result> {
            auto r = std::make_shared<Result>();
            r->image  = snapshot->computeStyledBaseImage();
            r->bounds = snapshot->styledBaseImageBounds();
            return r;
        });
    m_watcher.setFuture(future);
}

void AsyncStyleBaker::onFinished()
{
    m_result     = m_watcher.result();
    m_hasResult  = (m_result != nullptr);
    m_dispatched = false;
    emit ready();
}

bool AsyncStyleBaker::takeResult(QImage& image, QRectF& bounds)
{
    if (!m_hasResult || !m_result)
        return false;
    image  = m_result->image;
    bounds = m_result->bounds;
    m_hasResult = false;
    m_result.reset();
    return true;
}
