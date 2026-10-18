#pragma once

#include <QHash>
#include <QString>
#include <memory>
#include <utility>
#include <vector>

#include "smartobject/SmartObjectData.hpp"

class Document;
class LayerTreeNode;

// ─────────────────────────────────────────────────────────────────────────
// DocumentContainer — every document that lives in one .hzs file.
//
// One container per physical file (enforced by ContainerRegistry, which is the
// only owner of containers). Storage is FLAT: a hash from docId to document,
// plus the id of the root. Nesting is expressed only by the Smart Object
// instances inside the documents, so a document referenced by three instances
// at three different depths is still stored exactly once.
//
//   Project.hzs
//   └── DocumentContainer
//       ├── rootId   = "a1b2…"
//       └── documents
//           ├── a1b2…  (root)       → SmartObject(embedded) → c3d4…
//           ├── c3d4…               → SmartObject(linked)   → Other.hzs
//           └── e5f6…               (shared by 3 instances)
//
// Lifetime is by refcount (shared_ptr): an embedded document outlives both the
// tab that edits it and the instance that referenced it, so an undo in the
// parent cannot invalidate a child tab, and closing a child tab cannot destroy
// a document the file still has to save.
//
// The reverse index (docId → instances) is REBUILT on demand by walking the
// documents, never maintained incrementally. It costs O(nodes) and only runs on
// structural edits, whereas an incremental index would have to stay in sync
// with undo, redo, reorder, paste, delete and load — six paths where going out
// of sync fails silently.
//
// Reachability lives here for references *within* one file. The cross-file case
// (A.hzs → B.hzs → A.hzs) is ContainerRegistry's, not this class's: a Linked
// reference is a path, and only the registry knows which path maps to which
// container.
// ─────────────────────────────────────────────────────────────────────────
class DocumentContainer {
public:
    // Takes `root` as the container's root document and gives it a fresh id.
    explicit DocumentContainer(std::shared_ptr<Document> root);
    // Load path: the root keeps the id it was persisted with.
    DocumentContainer(std::shared_ptr<Document> root, SmartObjectDocId rootId);

    const SmartObjectDocId& rootId() const { return m_rootId; }
    std::shared_ptr<Document> rootDocument() const { return document(m_rootId); }

    std::shared_ptr<Document> document(const SmartObjectDocId& id) const;
    bool contains(const SmartObjectDocId& id) const;
    std::vector<SmartObjectDocId> documentIds() const;
    int documentCount() const { return m_documents.size(); }

    // Adds a document under a newly generated id and returns it.
    SmartObjectDocId addDocument(std::shared_ptr<Document> doc);
    // Adds a document under a caller-chosen id (load, cross-container import
    // with remapping). Returns false when the id is already taken or empty.
    bool addDocumentWithId(const SmartObjectDocId& id, std::shared_ptr<Document> doc);
    // Swaps the document stored under `id`, returning the previous one (null
    // when `id` was unknown). The id is deliberately preserved: that is what
    // makes "Replace Contents" reach EVERY instance at once, which is the
    // point of the command, instead of re-pointing only the layer it ran on.
    // The caller owns the returned document — the undo command holds it.
    std::shared_ptr<Document> replaceDocument(const SmartObjectDocId& id,
                                              std::shared_ptr<Document> doc);

    // Every Smart Object instance that displays `id`, across every document in
    // the container. Embedded references only — a Linked instance names a file,
    // not a docId in here.
    std::vector<LayerTreeNode*> instancesOf(const SmartObjectDocId& id) const;

    // May `from` display `to` without creating a cycle? False for from == to
    // and whenever `to` can already reach `from`. Covers the in-file half only
    // (see the class comment).
    // It is also the cycle guard the render cascade reuses at RESOLVE time: an
    // edge whose target can already reach its host is refused, so a malformed
    // file that somehow contains a loop renders nothing instead of cascading
    // forever.
    bool canReference(const SmartObjectDocId& from, const SmartObjectDocId& to) const;

    // Documents the root can reach, root included. This — not the hash's key
    // set — is the set that gets persisted, which is what stops a .hzs from
    // accumulating documents no instance references any more.
    std::vector<SmartObjectDocId> reachableFromRoot() const;

    // ── Isolated edit sessions ──────────────────────────────────────────
    // A content tab does NOT edit the stored document: it edits a COPY, and
    // Ctrl+S in that tab commits the copy back here. A Smart Object's contents
    // behave like a document of their own — until you save them, the instances
    // (and the project) are untouched, so closing the tab really can discard
    // the work and the main document really is unmodified until then.
    //
    // The session document is deliberately NOT part of documentIds() or
    // reachableFromRoot(): it is scratch state, never persisted, and a save
    // that picked it up would write content the user has not committed. It is
    // registered here rather than only in the tab because the render cascade
    // has to find it — a nested Smart Object INSIDE the copy still has to
    // re-render when its own content is committed.
    void beginEditSession(const SmartObjectDocId& id, std::shared_ptr<Document> working);
    std::shared_ptr<Document> editSession(const SmartObjectDocId& id) const;
    void endEditSession(const SmartObjectDocId& id);
    // (docId, working document) for every open session, in no particular order.
    std::vector<std::pair<SmartObjectDocId, std::shared_ptr<Document>>> editSessions() const;

    // The .hzs this container was loaded from / last saved to. Base for the
    // relative paths of Linked references. Empty for an unsaved container.
    const QString& path() const { return m_path; }
    void setPath(const QString& path) { m_path = path; }

    static SmartObjectDocId generateDocId();

private:
    // Embedded docIds `doc` references directly, in tree order.
    std::vector<SmartObjectDocId> directReferences(const Document& doc) const;

    SmartObjectDocId m_rootId;
    QHash<SmartObjectDocId, std::shared_ptr<Document>> m_documents;
    // Uncommitted working copies, keyed by the docId they will commit into.
    QHash<SmartObjectDocId, std::shared_ptr<Document>> m_editSessions;
    QString m_path;
};
