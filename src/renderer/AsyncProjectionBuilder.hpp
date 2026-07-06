#pragma once

#include <QObject>
#include <QImage>
#include <QFutureWatcher>
#include <cstdint>
#include <memory>

class Document;

// ─────────────────────────────────────────────────────────────────────────
// AsyncProjectionBuilder — runs DocumentCompositor::composite on a worker
// thread so finalizing a live edit (adjustment commit, brush stroke, transform,
// …) never blocks the UI thread with a full-document CPU recomposite.
//
// Threading contract:
//   • The caller (ProjectionCache, on the UI thread) hands over a *snapshot*
//     Document built with LayerTreeNode::shallowClone() — pixel buffers are
//     COW-shared, tile containers are owned by the snapshot, GPU handles are
//     zeroed. The worker only ever reads that snapshot, so it can run while the
//     UI thread keeps mutating the live document.
//   • Each request carries the live document pointer + its compositionGeneration
//     as an identity token. takeResult() returns them so the caller can discard
//     a result that no longer matches the current state (revision discard).
//   • ready() is emitted on the UI thread when a result is available; the caller
//     uploads it to GL from its own (GL-current) context, never the worker.
// ─────────────────────────────────────────────────────────────────────────
class AsyncProjectionBuilder : public QObject {
    Q_OBJECT
public:
    explicit AsyncProjectionBuilder(QObject* parent = nullptr);
    ~AsyncProjectionBuilder() override;

    // Dispatch a composite of `snapshot` (owned by the worker until done),
    // tagged with the live `docToken` + `generation`. No-op while a job is
    // already running — the caller re-requests the latest state once idle.
    void request(std::shared_ptr<Document> snapshot,
                 const void* docToken, uint64_t generation);

    bool isBuilding() const { return m_watcher.isRunning(); }

    // UI thread: take the latest finished result, if any. Returns false when no
    // new result is pending. `docToken`/`generation` identify what it was built
    // from so the caller can drop it if the live document moved on.
    bool takeResult(const void*& docToken, uint64_t& generation, QImage& image);

signals:
    void ready();

private slots:
    void onFinished();

private:
    struct Result {
        QImage      image;
        const void* docToken  = nullptr;
        uint64_t    generation = 0;
    };

    QFutureWatcher<std::shared_ptr<Result>> m_watcher;
    std::shared_ptr<Result>                 m_result;
    bool                                    m_hasResult = false;
};
