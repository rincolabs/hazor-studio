#include "StyleBakePool.hpp"
#include "AsyncStyleBaker.hpp"

#include "core/LayerTreeNode.hpp"

#include <algorithm>

StyleBakePool::StyleBakePool() = default;
StyleBakePool::~StyleBakePool() = default;

StyleBakePool::Entry& StyleBakePool::entryFor(LayerTreeNode* node)
{
    auto [it, inserted] = m_entries.try_emplace(node);
    Entry& e = it->second;
    if (inserted) {
        e.baker = std::make_unique<AsyncStyleBaker>();
        QObject::connect(e.baker.get(), &AsyncStyleBaker::ready,
                         e.baker.get(), [this]() {
            // The worker that just finished may have been the last one. Report
            // it here too, not only from ensureFresh(): if nothing asks for
            // this node again (its texture turned out to be up to date) the
            // repaint below is the last event in the chain, and a busy state
            // left hanging would keep the host's loading dialog up forever.
            notifyBusyChanged();
            if (m_onReady) m_onReady();
        });
    }
    return e;
}

bool StyleBakePool::ensureFresh(LayerTreeNode* node, bool canDefer,
                                InterimBake* interim)
{
    // One evaluation per call, after any re-dispatch below, so a gesture's
    // back-to-back bakes read as one continuous busy state.
    const bool ready = ensureFreshImpl(node, canDefer, interim);
    notifyBusyChanged();
    return ready;
}

bool StyleBakePool::ensureFreshImpl(LayerTreeNode* node, bool canDefer,
                                    InterimBake* interim)
{
    if (!node)
        return true;

    // Nothing to bake: no styles at all, or the cache is already current. The
    // latter also happens with a worker still running — a CPU composite of the
    // LIVE document (export, the histogram panel, the magnetic lasso) bakes the
    // node inline — so drop the result that worker will deliver instead of
    // hoarding a full-resolution QImage until the node goes dirty again.
    if (node->effects.empty() || !node->styledBaseDirty()) {
        discardPending(node);
        return true;
    }

    Entry& e = entryFor(node);

    // 1) Pick up a finished bake and commit it into the node's own cache —
    //    exactly what computeStyledBaseImage() would have produced, so every
    //    other consumer (computeEffectedImage, thumbnails, the GL upload
    //    that follows in syncLayersToGpu) sees a normal, already-built cache.
    QImage resultImage;
    QRectF resultBounds;
    if (e.baker->takeResult(resultImage, resultBounds)) {
        node->commitStyledBaseBake(resultImage, resultBounds, e.dispatchedRevision);
        // commitStyledBaseBake stores the bake only when it still matches the
        // node's current params. When it doesn't — a slider move or a brush dab
        // landed while it was running — the node stays dirty and the result is
        // not the truth any more, but it is still newer than what the GPU holds:
        // hand it to the caller as a preview-only upload (see InterimBake).
        if (interim && node->styledBaseDirty() && !resultImage.isNull()) {
            interim->image  = resultImage;
            interim->bounds = resultBounds;
        }
    }

    if (!node->styledBaseDirty())
        return true;

    // 2) Nothing uploaded for this node yet, so there is no previous texture to
    //    keep showing and deferring would draw the layer unstyled for a frame.
    //    Let the caller's synchronous bake run, as it did before the async path.
    if (!canDefer)
        return true;

    // 3) Dispatch a fresh build if nothing is in flight already. shallowClone()
    //    is a COW snapshot — a Layer struct copy with the GPU handles zeroed,
    //    no pixel copy — so the whole bake (tile composite, mask pass, blur)
    //    runs on the worker over state the UI thread can no longer change.
    if (!e.baker->isBuilding()) {
        e.dispatchedRevision = node->styledBaseRevision();
        e.baker->request(node->shallowClone());
    }

    // 4) Not ready yet this frame — the caller uploads `interim` if it got one
    //    and otherwise keeps the previous texture.
    return false;
}

bool StyleBakePool::isBuilding(const LayerTreeNode* node) const
{
    auto it = m_entries.find(node);
    return it != m_entries.end() && it->second.baker
        && it->second.baker->isBuilding();
}

void StyleBakePool::notifyBusyChanged()
{
    bool busy = false;
    for (const auto& [node, entry] : m_entries) {
        if (entry.baker && entry.baker->isBuilding()) {
            busy = true;
            break;
        }
    }
    // Draining bakers are deliberately not counted: their node is gone, so
    // nobody is waiting for their result (retain() throws it away).
    if (busy == m_busy)
        return;
    m_busy = busy;
    if (m_onBusyChanged)
        m_onBusyChanged(busy);
}

void StyleBakePool::discardPending(const LayerTreeNode* node)
{
    auto it = m_entries.find(node);
    if (it == m_entries.end() || !it->second.baker)
        return;
    QImage image;
    QRectF bounds;
    it->second.baker->takeResult(image, bounds);
}

void StyleBakePool::release(const LayerTreeNode* node)
{
    auto it = m_entries.find(node);
    if (it == m_entries.end())
        return;
    // Same rule as retain(): never destroy a baker that is still running.
    // ~AsyncStyleBaker waits for its worker and this runs on the UI thread
    // (document teardown), so move it aside unkeyed and let the next retain()
    // reap it once idle.
    if (it->second.baker && it->second.baker->isBuilding())
        m_draining.push_back(std::move(it->second.baker));
    m_entries.erase(it);
    notifyBusyChanged();
}

void StyleBakePool::retain(const std::vector<LayerTreeNode*>& liveNodes)
{
    // Bakers moved aside by an earlier retain()/release() are destroyed once
    // idle (the destructor would otherwise wait on the worker, on the UI
    // thread).
    m_draining.erase(
        std::remove_if(m_draining.begin(), m_draining.end(),
                       [](const std::unique_ptr<AsyncStyleBaker>& b) {
                           return !b || !b->isBuilding();
                       }),
        m_draining.end());

    for (auto it = m_entries.begin(); it != m_entries.end(); ) {
        if (std::find(liveNodes.begin(), liveNodes.end(), it->first)
            != liveNodes.end()) {
            ++it;
            continue;
        }
        // Node is gone. Its result is worthless either way, but a running
        // baker must outlive the entry — unkeyed, so the stale result can
        // never be committed into a node that reuses the address.
        if (it->second.baker && it->second.baker->isBuilding())
            m_draining.push_back(std::move(it->second.baker));
        it = m_entries.erase(it);
    }
    notifyBusyChanged();
}
