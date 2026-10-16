#pragma once

#include <QObject>
#include <QImage>
#include <QRectF>
#include <QFutureWatcher>
#include <memory>

class LayerTreeNode;

// ─────────────────────────────────────────────────────────────────────────
// AsyncStyleBaker — runs a node's styled-base bake (the tile composite, the
// mask pass and the OpenCV blur behind drop shadows, glows, strokes, ...) on
// a worker thread, so a Layer Styles live edit never blocks the UI thread the
// way a direct computeStyledBaseImage() call does. One instance per node that
// currently carries layer styles, pooled by StyleBakePool.
//
// Threading contract (mirrors AsyncProjectionBuilder):
//   • The caller hands over a COW snapshot of the node (LayerTreeNode::
//     shallowClone(): layer struct copy, no pixel copy, GPU handles zeroed).
//     The task owns that snapshot and touches nothing else — never the live
//     node, its caches or the Document — so it is safe to run while the UI
//     thread keeps mutating the live layer.
//   • ALL the bake work runs there. Assembling the pre-blur padded source on
//     the UI thread would not do: maskedBaseImage() alone is a full-res tile
//     composite plus a per-pixel mask pass, as costly as the blur itself on a
//     large layer.
//   • ready() is emitted on the UI thread when a result is available; the
//     caller commits it back into the live node's cache from there.
// ─────────────────────────────────────────────────────────────────────────
class AsyncStyleBaker : public QObject {
    Q_OBJECT
public:
    explicit AsyncStyleBaker(QObject* parent = nullptr);
    ~AsyncStyleBaker() override;

    // Dispatch a bake of `snapshot`. No-op while a bake is already running —
    // the caller re-requests a fresh snapshot once idle, same
    // discard-and-retry protocol as AsyncProjectionBuilder.
    void request(std::shared_ptr<LayerTreeNode> snapshot);

    // True from request() until the result has been delivered to the UI thread,
    // not merely while the worker runs: QFutureWatcher::isRunning() already goes
    // false when the task returns, which left a window where a caller saw an
    // idle baker with no result pending yet and re-dispatched — and setFuture()
    // drops the finished future's pending call-out, throwing a completed bake
    // away. Holding "building" until ready() closes that window.
    bool isBuilding() const { return m_dispatched; }

    // UI thread: take the latest finished result, if any. Returns false when
    // no new result is pending.
    bool takeResult(QImage& image, QRectF& bounds);

signals:
    void ready();

private slots:
    void onFinished();

private:
    struct Result {
        QImage image;
        QRectF bounds;
    };

    QFutureWatcher<std::shared_ptr<Result>> m_watcher;
    std::shared_ptr<Result>                 m_result;
    bool                                    m_hasResult = false;
    bool                                    m_dispatched = false;
};
