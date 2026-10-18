#pragma once

#include <QString>
#include <memory>
#include <vector>

#include "smartobject/SmartObjectData.hpp"

class Document;
class DocumentContainer;

// Moving documents between containers. Duplicating an instance INSIDE one
// container imports nothing — it shares the same docId, which is the whole
// point of identity being separate from instance.
namespace smartobject {

// Deep-copies `docId` and its transitive EMBEDDED dependencies from `src` into
// `dst`, remapping every docId to a fresh one and rewriting the references
// inside the copies so they point at the new ids. Linked references are carried
// over unchanged (they name files, not ids) except that their stored relative
// path is rebased onto `dst`'s own file. Sharing is preserved: a document
// referenced by three instances in `src` becomes ONE document referenced by
// three instances in `dst`.
//
// Returns the new id of `docId` in `dst`, or an empty id on failure.
SmartObjectDocId importDocumentsInto(DocumentContainer& dst,
                                     const DocumentContainer& src,
                                     const SmartObjectDocId& docId);

// The inverse: writes a new .hzs at `path` whose ROOT is a copy of `docId`,
// plus its transitive embedded dependencies. Linked references are rebased onto
// the new file's directory. Does not touch `src`.
bool exportDocumentSubgraph(const DocumentContainer& src,
                            const SmartObjectDocId& docId,
                            const QString& path,
                            QString* error = nullptr);

// Deep copy of a document's structure — clone(), not shallowClone(), so the
// copy owns its own pixel buffers. Embedded references are carried over
// UNCHANGED: the copy still names the same docIds in the same container, which
// is what an isolated edit session needs (a nested Smart Object inside the copy
// must keep displaying the one document every other instance displays).
std::shared_ptr<Document> cloneDocument(const Document& source);

// Overwrites `dst`'s canvas, metadata and layer tree with a deep copy of
// `src`'s, IN PLACE: the Document object — and therefore every shared_ptr to it
// held by the container and by undo commands — survives. That is what lets a
// content tab commit an isolated edit without re-pointing the docId at a new
// document object. Does not touch `dst`'s generation counters; the caller bumps
// them (the commit has to be observable by the staleness test).
void assignDocumentContent(Document& dst, const Document& src);

// `docId` plus every document it reaches through EMBEDDED references, `docId`
// first. Used to retain an orphaned subgraph in an undo command (conversion
// never deletes documents) and to decide what an export has to carry.
std::vector<SmartObjectDocId> embeddedSubgraphOf(const DocumentContainer& container,
                                                 const SmartObjectDocId& docId);

// Rewrites every Linked reference in `container` so its stored path is relative
// to `newContainerPath` instead of the container's current path. Call it right
// before a Save As writes the project somewhere else: the stored paths are
// relative to the .hzs that holds them, so moving the file without this leaves
// every link pointing at the wrong place. Embedded references are untouched —
// they have no path. No-op when the location is unchanged.
void rebaseLinkedPaths(DocumentContainer& container, const QString& newContainerPath);

// `targetPath` expressed relative to the .hzs at `containerPath`, so moving a
// whole project folder keeps its links intact. Falls back to the absolute path
// when the two are on different roots (different drive on Windows) or when
// `containerPath` is still empty (unsaved project).
QString relativeLinkPath(const QString& containerPath, const QString& targetPath);
// The inverse of relativeLinkPath().
QString resolveLinkPath(const QString& containerPath, const QString& storedPath);

} // namespace smartobject
