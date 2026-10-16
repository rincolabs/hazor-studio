#pragma once

#include <QFutureWatcher>
#include <QImage>
#include <QObject>
#include <QString>

#include <memory>
#include <unordered_map>

#include "raw/RawDevelopParams.hpp"
#include "raw/RawGeometry.hpp"
#include "raw/RawImageBuffer.hpp"
#include "raw/RawLayerData.hpp"

class LayerTreeNode;
class RawSession;

// Result of one completed re-reveal, handed back on the UI thread.
struct RawDevelopResult {
    bool ok = false;
    QString errorMessage;
    QImage image;          // Format_RGBA8888, ready for Layer::cpuImage.
    QSize developedSize;
    RawGeometry geometry;
};

// ─────────────────────────────────────────────────────────────────────────
// RawDevelopService — runs a RAW Layer's re-reveal (RawSession::renderFull()
// + the 16-bit/float -> 8-bit conversion) off the UI thread, same COW
// snapshot -> worker -> apply contract as StyleBakePool/AsyncProjectionBuilder
// (Fase 4.8). One entry per node that currently carries a RAW Layer, so a
// slow full-resolution render for one layer never blocks editing another.
//
// Unlike StyleBakePool this isn't polled every frame: a re-reveal is a
// discrete action (the user committed a PP3 change), so the caller
// dispatches once via requestDevelop() and reacts to developFinished().
//
// Caches the opened RawSession per node (and, for Embedded layers, the
// temp file its bytes were materialized to) so a second re-reveal of the
// same layer does not pay the decode cost of RawEngine::openFile() again.
// Not safe to call requestDevelop() for the same node twice before
// developFinished() fires for the first — mirrors RawSession's own "calls
// must be externally serialized" contract (RawSession.hpp).
// ─────────────────────────────────────────────────────────────────────────
class RawDevelopService : public QObject {
    Q_OBJECT
public:
    explicit RawDevelopService(QObject* parent = nullptr);
    ~RawDevelopService() override;

    // Dispatches an async renderFull() for `node`'s RAW layer. `source` is
    // the layer's immutable RawSource (shared, see RawLayerData.hpp);
    // `params` is the PP3 to render with. Returns false (no-op) when a job
    // for this node is already in flight.
    bool requestDevelop(LayerTreeNode* node,
                        std::shared_ptr<const RawSource> source,
                        RawStorageMode mode,
                        const RawDevelopParams& params);

    bool isBusy(const LayerTreeNode* node) const;

    // Drop the cached session/temp file for one node (layer deleted,
    // document closed, or the layer stopped being a RAW layer).
    void release(const LayerTreeNode* node);

    // The single named point where a RawImageBuffer's 16-bit/float pixels
    // become the compositor's 8-bit Format_RGBA8888 (Fase 4.2) — the
    // boundary between the RAW engine's own precision and Hazor's current
    // (8-bit) compositor, documented here rather than buried in the engine.
    // Public (not just requestDevelop()'s own worker task) so the initial
    // RAW file open (Fase 5, MainWindow::onOpenFile()) shares the same
    // conversion instead of duplicating it.
    static QImage convertToRgba8888(const RawImageBuffer& buffer, bool dither);

signals:
    // Always emitted on the UI thread, exactly once per requestDevelop()
    // that returned true.
    void developFinished(LayerTreeNode* node, RawDevelopResult result);

private:
    struct Task {
        // Non-null only when this task opened a fresh session (node had
        // none cached yet); onFinished() adopts it into the entry so the
        // next re-reveal of this node reuses it.
        std::unique_ptr<RawSession> openedSession;
        // Set alongside openedSession for Embedded layers: the path their
        // bytes were materialized to, so onFinished() can record it (and,
        // on release(), delete it) — see materializeEmbedded().
        QString openedTempPath;
        RawDevelopResult result;
        // Full-precision buffer (RGBFloat, [0, 65535]) RawSession::renderFull()
        // produced, retained for a future higher-depth export path per
        // Fase 4.2 — never written into Layer::cpuImage, never truncated
        // again, just cached alongside the session.
        RawImageBuffer fullBuffer;
    };

    struct Entry {
        ~Entry();
        std::unique_ptr<RawSession> session;
        QString tempPath; // Embedded mode only — deleted by ~Entry().
        RawImageBuffer lastFullBuffer;
        QFutureWatcher<std::shared_ptr<Task>> watcher;
        bool dispatched = false;
    };

    // Embedded mode only: writes `source.bytes` to a process-unique temp
    // file (extension taken from source.path, so format sniffing by
    // extension still works) the first time this node's layer is
    // developed, so RawSession::open() has a real path to decode from.
    // Called from the worker task, synchronously, with no signal/slot
    // connections on the QFile it uses — safe off the UI thread.
    static QString materializeEmbedded(const RawSource& source);

    Entry& entryFor(LayerTreeNode* node);
    void onFinished(LayerTreeNode* node);

    std::unordered_map<const LayerTreeNode*, std::unique_ptr<Entry>> m_entries;
};
