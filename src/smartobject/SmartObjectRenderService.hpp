#pragma once

#include <QFutureWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QSize>

#include <cstdint>
#include <memory>

#include "color/ColorProfile.hpp"
#include "smartobject/SmartObjectData.hpp"

class Document;

// What one render job is FOR. Deliberately not "which instance": a Smart
// Object's cpuImage depends only on the referenced document and the profile it
// has to land in — the instance's transform, mask, opacity, blend and effects
// are applied by the compositor, downstream. So every instance of the same
// document under the same parent profile shares ONE render (and one QImage,
// which Qt's implicit sharing makes free to hand out) instead of paying for N
// identical composites.
struct SmartObjectRenderKey {
    SmartObjectDocId docId;
    // Empty for an Embedded reference (the document lives in this container).
    // For a Linked one it is the payload's stored path, which is what makes
    // "the same file linked from two instances" one job instead of two — and
    // keeps an Embedded docId from ever colliding with a Linked one that
    // happens to carry the same id (a linked payload stores the TARGET file's
    // docId, which is meaningless in this container).
    QString linkedPath;
    // ColorProfile::fingerprint() of the destination (parent) document. A
    // cheap stable discriminator, not colorimetric equality — parents with
    // different profiles correctly get separate jobs.
    quint64 targetProfile = 0;

    bool operator==(const SmartObjectRenderKey& other) const
    {
        return docId == other.docId
            && linkedPath == other.linkedPath
            && targetProfile == other.targetProfile;
    }
};

inline size_t qHash(const SmartObjectRenderKey& key, size_t seed = 0)
{
    return qHashMulti(seed, key.docId, key.linkedPath, key.targetProfile);
}

struct SmartObjectRenderResult {
    SmartObjectRenderKey key;
    // Referenced document's compositionGeneration captured at SCHEDULING time,
    // not at completion: that is what makes "is this result already superseded?"
    // answerable at commit (see ImageController::commitSmartObjectRender).
    std::uint64_t jobGeneration = 0;
    bool ok = false;
    QImage image;        // Format_RGBA8888, in the destination profile
    QSize sourceSize;    // referenced document's canvas at render time
    double sourceDpi = 300.0;
    // Does committing this result make the documents it lands in unsaved?
    // True for a render caused by an edit: the cached render is part of the
    // .hzs, so changing it changes the file. False for the validation pass
    // after a load, which regenerates what was already on disk — marking a
    // project modified just for opening it would be a lie.
    bool marksDirty = true;
};

// ─────────────────────────────────────────────────────────────────────────
// SmartObjectRenderService — composites a referenced document off the UI
// thread and hands the pixels back, per (docId, destination profile).
//
// Same COW-snapshot → worker → commit contract as StyleBakePool,
// AsyncProjectionBuilder and RawDevelopService:
//
//   1. request() takes a LayerTreeNode::shallowClone() snapshot of the
//      referenced document ON THE UI THREAD (cheap: no pixel copies) and
//      records the document's generation as the job's identity.
//   2. The worker runs DocumentCompositor::composite() over the snapshot —
//      the CPU compositor is the visual source of truth (AGENTS.md invariant
//      1), so the instance can never disagree with an export.
//   3. renderFinished() fires on the UI thread; ImageController distributes
//      the one QImage to every live instance of the key.
//
// NO POINTER TO A NODE IS EVER STORED. A job's identity is (key,
// jobGeneration), and the instances are found by walking the live tree at
// commit time. Keying pending work by LayerTreeNode* — which is what
// m_pendingRawDevelops does — is only narrowly safe for RAW (it needs the
// layer deleted mid-render AND a reallocation at the same address); with a
// nested cascade re-entering the same keys, that window is wide open.
//
// One job per key at a time. A request that arrives while a job is in flight
// does not queue a second composite: it stashes a fresh snapshot and
// re-dispatches once the first commits, so a burst of edits collapses into
// one extra render rather than N.
// ─────────────────────────────────────────────────────────────────────────
class SmartObjectRenderService : public QObject {
    Q_OBJECT
public:
    explicit SmartObjectRenderService(QObject* parent = nullptr);
    ~SmartObjectRenderService() override;

    // Snapshots `source` and dispatches (or stashes) a render for `key`.
    // `source` must be the live referenced document and this must be the UI
    // thread. Returns false when the document cannot be rendered at all
    // (empty canvas).
    bool request(const SmartObjectRenderKey& key,
                 const Document& source,
                 const ColorProfile& targetProfile,
                 bool marksDirty);

    bool isBusy(const SmartObjectRenderKey& key) const;

signals:
    // UI thread, once per dispatched job that was not released meanwhile.
    void renderFinished(SmartObjectRenderResult result);

private:
    struct Job {
        std::shared_ptr<Document> snapshot;
        ColorProfile sourceProfile;
        ColorProfile targetProfile;
        std::uint64_t generation = 0;
        double sourceDpi = 300.0;
        bool marksDirty = true;
    };

    struct Entry {
        QFutureWatcher<std::shared_ptr<SmartObjectRenderResult>> watcher;
        bool connected = false;    // the watcher's finished() is wired once
        bool dispatched = false;
        // Generation the in-flight job is rendering. A second request for that
        // same generation has nothing new to produce and is dropped.
        std::uint64_t inFlightGeneration = 0;
        // Snapshot taken while a job was in flight; dispatched on completion.
        std::unique_ptr<Job> queued;
    };

    // Thread-safe snapshot of a document for off-thread compositing. Same
    // technique as ProjectionCache::makeSnapshot(): shallowClone() COW-shares
    // every pixel buffer, so this is safe to build on the UI thread and safe
    // for the worker to read while the UI thread keeps editing.
    static std::shared_ptr<Document> makeSnapshot(const Document& doc);
    static std::shared_ptr<SmartObjectRenderResult> runJob(SmartObjectRenderKey key,
                                                           std::shared_ptr<Job> job);

    void dispatch(const SmartObjectRenderKey& key, std::unique_ptr<Job> job);
    void onFinished(const SmartObjectRenderKey& key);

    // shared_ptr, not unique_ptr: QHash requires a copyable value type.
    QHash<SmartObjectRenderKey, std::shared_ptr<Entry>> m_entries;
};
