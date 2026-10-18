#pragma once

#include <QHash>
#include <QString>
#include <memory>
#include <vector>

#include "smartobject/SmartObjectData.hpp"

class DocumentContainer;

// ─────────────────────────────────────────────────────────────────────────
// ContainerRegistry — one DocumentContainer per physical .hzs, app-wide.
//
// The registry is the ONLY owner of containers (it holds shared_ptr). Tabs
// borrow from it; Linked Smart Object references resolve through it.
//
// There is deliberately no separate "linked document cache". Such a cache
// *would be* this registry, and keeping both is precisely the bug: B.hzs open
// in a tab and B.hzs reached through a link from A.hzs would be two Documents
// for one file, with diverging state and two save paths racing for the same
// bytes. With one registry:
//
//   • one container per file ⇒ one dirty flag, one save path;
//   • editing B.hzs in its own tab invalidates the instances in A.hzs through
//     the same generation mechanism — a feature, not a workaround;
//   • "Edit Contents" on a Linked instance just opens B.hzs as a normal tab
//     (reactivating it when it is already open);
//   • closing B.hzs's tab does not discard the container while a link still
//     references it (refcount);
//   • cycle detection is reachability over (canonical path, docId) pairs HERE.
//     DocumentContainer::canReference() only covers the in-file half; the
//     cross-file case A.hzs → B.hzs → A.hzs belongs to the registry.
// ─────────────────────────────────────────────────────────────────────────
class ContainerRegistry {
public:
    static ContainerRegistry& instance();

    // The container for `path` if it is already open, else null. `path` need
    // not be canonical.
    std::shared_ptr<DocumentContainer> find(const QString& path) const;

    // Loads `path` (or returns the already-open container for it). Null on
    // failure, with the reason in *error.
    std::shared_ptr<DocumentContainer> open(const QString& path, QString* error = nullptr);

    // Registers an unsaved container, or re-keys one that was just saved to a
    // new path (Save As). Also sets the container's own path.
    void registerContainer(const std::shared_ptr<DocumentContainer>& container,
                           const QString& path);

    // Drops the registry's reference. The container itself survives as long as
    // a tab or a Linked reference still holds it — which is the point: a file
    // whose tab was closed stays alive while something links to it.
    void forget(const QString& path);

    // Canonical absolute path used as the registry key. Falls back to the
    // absolute path when the file does not exist yet (Save As target).
    static QString canonicalKey(const QString& path);

    // Would a Linked reference from (fromPath, fromDocId) to (toPath, toDocId)
    // close a cycle across files? Walks both embedded references (inside each
    // container) and linked ones (across containers in this registry), over
    // (path, docId) pairs. A link whose target is not open yet cannot close a
    // cycle through a document we cannot see, so an unresolvable hop ends that
    // branch rather than failing the whole check.
    bool wouldCreateCycle(const QString& fromPath, const SmartObjectDocId& fromDocId,
                          const QString& toPath, const SmartObjectDocId& toDocId) const;

private:
    ContainerRegistry() = default;

    QHash<QString, std::shared_ptr<DocumentContainer>> m_containers;
};
