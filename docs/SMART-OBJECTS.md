# Smart Objects

A **Smart Object Layer** displays the rendered result of *another document*,
and keeps that document editable forever. Several instances can point at the
same document; each instance has its own transform, opacity, blend mode, mask
and layer styles.

A Smart Object is not a new node type. It is a `LayerTreeNode::Type::Layer`
whose `Layer` carries a `smartObjectData` payload — the fifth member of a family
that already existed: Text, Shape, Distort, RAW and Smart Object are all "a
layer whose `cpuImage` is derived from a re-editable, non-destructive payload".
RAW is the closest relative and the pattern this was built on.

For *how it renders* (and the thread-safety reasoning behind it), see
[RENDER-PIPELINE.md §3.4](RENDER-PIPELINE.md). This document covers the data
model, storage and file format.

---

## The model in one picture

```
Project.hazor
└── DocumentContainer                    one per physical file, app-wide
    ├── rootId   = "a1b2…"
    └── documents (docId → Document)     flat storage, no nesting
        ├── a1b2…  (root)       → SmartObject(embedded) → c3d4…
        ├── c3d4…               → SmartObject(linked)   → Other.hazor
        └── e5f6…               (shared by 3 instances)
```

| Concept | Type | Lives in |
|---|---|---|
| Document identity | `SmartObjectDocId` (a UUID string) | [`SmartObjectData.hpp`](../src/smartobject/SmartObjectData.hpp) |
| All documents of one file | `DocumentContainer` | [`DocumentContainer.hpp`](../src/smartobject/DocumentContainer.hpp) |
| One container per file, app-wide | `ContainerRegistry` | [`ContainerRegistry.hpp`](../src/smartobject/ContainerRegistry.hpp) |
| The instance's payload | `SmartObjectData` | [`SmartObjectData.hpp`](../src/smartobject/SmartObjectData.hpp) |
| Rendering + invalidation | `SmartObjectRenderService` | [`SmartObjectRenderService.hpp`](../src/smartobject/SmartObjectRenderService.hpp) |
| Operations | `ImageController` | [`ImageController_SmartObjects.cpp`](../src/controller/ImageController_SmartObjects.cpp) |

**Document identity and instance identity are separate.** The payload stores a
`docId`, not a document. Two instances of the same content share one `docId`,
one stored document, and one render; what distinguishes them (transform,
opacity, blend, mask, effects) lives on the node and the `Layer`. Duplicating an
instance therefore needs no special handling at all — `shallowClone()` copies the
`shared_ptr<const SmartObjectData>`, and because the value is `const` neither
copy can mutate the other's view of it.

---

## Embedded vs Linked

| | Embedded | Linked |
|---|---|---|
| Where the document lives | this `.hazor`, in its `DocumentContainer` | another `.hazor` |
| Payload field | `docId` | `linkedPath` (relative to this `.hazor` when possible) + the target's root `docId` |
| Can go stale | no — editing it invalidates its instances at once | yes — the file can change behind Hazor's back |
| Refresh | automatic | **Layer ▸ Smart Object ▸ Update Modified Content** |
| Travels with the project | yes | no |

There is **no file watcher** anywhere in Hazor, by design, so a Linked Smart
Object refreshes on an explicit command. Linked RAW layers already work this way.

**A Linked reference always resolves through `ContainerRegistry`.** If the target
file is already open in a tab, that same container — and that same `Document` —
is what the instance reads. There is deliberately no separate "linked document
cache": such a cache *would be* this registry, and keeping both is exactly the
bug it avoids. Two `Document`s for one file means diverging state and two save
paths racing for the same bytes. With one registry:

- one container per file ⇒ one dirty flag, one save path;
- editing `B.hazor` in its own tab invalidates the instances in `A.hazor` through the
  ordinary generation mechanism — a feature, not a workaround;
- *Edit Contents* on a Linked instance opens `B.hazor` as a normal tab, or
  reactivates it when it is already open;
- closing `B.hazor`'s tab does not discard the container while a link still holds
  it (refcount);
- cycle detection works over `(canonical path, docId)` pairs, so the cross-file
  case `A.hazor → B.hazor → A.hazor` is caught. `DocumentContainer::canReference()`
  only covers the in-file half.

### Unresolvable links

The instance's render is persisted in the `.hazor`, so a broken link still
displays. The payload is marked `linkBroken`, the Layers panel shows a tinted
badge, and *Relink…* is offered. This is exactly the policy the RAW loader
already documents for a missing embedded blob: the layer still loads and renders
from the cached `cpuImage`; only re-rendering is unavailable.

This fallback is **the only reason** the render is persisted at all. It is not a
second source of truth — see [RENDER-PIPELINE.md §3.4](RENDER-PIPELINE.md).

### What happens on open

Opening a project validates every Smart Object reference (so a Linked target
that moved gets its badge immediately) and then:

- **embedded** instances re-render in the background. A freshly loaded document
  starts at generation 0, so every instance reads as stale and regenerates from
  the document that *is* the source of truth, rather than trusting a bitmap. The
  persisted render is displayed until each one lands, so nothing flickers;
- **linked** instances do not. Their files are not opened, so the persisted
  render stands until you run *Update Modified Content*.

Neither marks the project modified. Two separate things ensure that: the load
pass flags its renders as non-dirtying (above), and a render commit deliberately
does not emit `layerChanged` — every listener of that signal reads it as "the
user edited this layer" and sets the dirty flag.

---

## Tabs, ownership and saving

`DocumentTab` holds `shared_ptr<Document>` (not `unique_ptr`) plus the container
and the `docId` it edits. Refcounting is what makes the editing flow safe:

- *Edit Contents* opens the referenced document in a **tab**, so the whole editor
  (tools, panels, shortcuts, its own `CommandHistory`) applies to it;
- one tab per document — a second would give one `Document` two command
  histories;
- an undo in the parent that removes the instance does **not** invalidate the
  child tab;
- closing the child tab does **not** destroy the stored document (the container
  still holds it, and it still has to be saved);
- the raw `Document*` inside undo commands stays valid for the life of the tab's
  history.

**The content tab edits a COPY, not the stored document.** `Edit Contents` opens
an *edit session*: `DocumentContainer::beginEditSession()` registers a
`cloneDocument()` of the stored document and the tab borrows that. The contents
are a document of their own — a separate thing you save — and that is what makes
the three statements below true at the same time:

- until `Ctrl+S`, the instances keep showing the stored content and the project
  genuinely still matches its file;
- `Ctrl+S` in the content tab is a real commit: the working copy is copied back
  **in place** into the stored document (so the container, and any undo command
  holding that document, keep pointing at the same object), its
  `compositionGeneration` is bumped, every instance re-renders and the project
  becomes unsaved;
- closing the content tab really does discard the experiment — so it prompts.

The session is also what the render cascade walks alongside the stored documents
(`ImageController::smartObjectHosts()`): a Smart Object nested *inside* an open
working copy still has to re-render when its own content is committed. It is
deliberately absent from `documentIds()` and `reachableFromRoot()`, so no save
can ever write content the user has not committed.

**Saving is a container operation, but committing content is not.** One file has
one save path, and the file name belongs to the root document's tab. What
`Ctrl+S` does depends on which tab you are in — see "Saving" below. Closing the
root tab closes its content tabs.

**Orphaned documents are never deleted.** A conversion that orphans a subgraph
keeps a `shared_ptr` to it in the undo command, so undo re-points at the same
documents instead of rebuilding them. Persistence is *reachability from the
root at save time*, so an orphan held only by the history lives in memory and is
simply not written. Save, close and reopen and nothing that still mattered is
lost.

---

## File format — version 3

`.hazor`, magic `HZS1`, `manifest["format"] = "UPS_PROJECT"`. The older `.hzs`
extension is still accepted everywhere a project is opened — only Save/Save As
and "Convert to Linked" write `.hazor`.

| Version | Adds |
|---|---|
| 1 | everything before RAW layers |
| 2 | the `rawData` node field + its blobs |
| 3 | `smartObjectData`, the root document's `docId` + `embeddedDocuments`, and `docs/<docId>/`-prefixed blobs |

The saver writes the **lowest** version the document actually needs
(`documentHasRawLayer()` / `documentHasSmartObject()`), so a project with no
Smart Object still saves exactly as before — same blob keys, same JSON shape.

```jsonc
{
  "name": "...", "canvas": { ... }, "roots": [ ... ],
  "docId": "a1b2…",
  "embeddedDocuments": [
    { "docId": "c3d4…", "document": { /* a whole document, same schema */ } }
  ]
}
```

Blob keys are namespaced **only for non-root documents**: the root keeps
`layers/layer_0001.png`, embedded documents use
`docs/<docId>/layers/layer_0001.png`. The prefix is baked into the key stored in
the JSON, so the loader needs no prefix of its own.

### Compatibility, stated plainly

| Guarantee | Status |
|---|---|
| This build opens `.hazor` v1 and v2 | **Yes** |
| A document without Smart Objects keeps the older format | **Yes** |
| An older build opens a v3 file | **No** — it fails at the header check with *"Unsupported project version."* |

The last row is not a partial read and not a cached-composite fallback: the
version check in `loadProject`/`loadContainer` runs before the document JSON is
parsed, so an older build never reaches the node data. What v3 guarantees is
**functional** round-tripping — a document survives save/load without losing
information — not binary identity of the file.

> Comparing two saves byte-for-byte never matches: `manifest["createdAt"]` and
> `manifest["modifiedAt"]` are stamped with the current time on every save. To
> check that a serialization change is stable, compare the document JSON and the
> blob map, ignoring the manifest — or simply reopen the file and inspect it.

---

## Colour and resolution

The referenced document has its own `ColorProfile`, `resolutionDpi` and
`bitDepth`, and keeps them — it is a real document.

- **Colour:** the composite is converted into the **parent** document's profile
  before it becomes the instance's `cpuImage`, through the same
  `ColorManagementService` path an imported image takes. Parents with different
  profiles get separate render jobs.
- **Resolution:** a differing DPI does **not** rescale. The render comes out in
  the referenced document's canvas pixels and the instance is positioned by its
  own transform, like any imported layer. `state.sourceDpi` is kept for display
  and for a future "Place at source DPI".
- **Bit depth:** irrelevant today — the compositor is 8-bit
  (`Format_RGBA8888`). Recorded here so it is not a surprise if that changes.

---

## What is blocked, and what is not

`Layer::hasDirectlyEditablePixels()` is false for a Smart Object, which is the
single gate that makes `ImageController::checkDestructiveOp()` reject brush,
fill, filters and clear with the right message, and `canEditPixels()` disable
the matching menu items. No per-tool code was needed.

Everything that is *not* a pixel edit keeps working: transform, opacity, blend
mode, mask, layer styles, grouping, clipping, export. **Rasterize Layer** drops
the reference and keeps the rendered pixels — the referenced document is
untouched and stays in the container for its other instances and for Undo.

---

## User interface

The access paths follow the convention this kind of editor established, so the
habits transfer.

| Where | Entry |
|---|---|
| **File** | Place Embedded…; Place Linked… — bringing an external document *in* is a file operation, so it lives here and is not repeated under Layer |
| **Layer ▸ Smart Objects** | Convert to Smart Object; New Smart Object via Copy ⋅ Edit Contents; Replace Contents…; Relink to File…; Update Modified Content ⋅ Convert to Linked…; Embed Linked |
| **Layers panel** | corner badge on the thumbnail (embedded / linked / broken); double-click the thumbnail opens the contents |
| **Layers panel context menu** | only what applies to the clicked layer: a common layer offers Convert to Smart Object and nothing else; a Smart Object offers Edit Contents, New Smart Object via Copy, Replace Contents…, then the storage-specific pair, then Rasterize Layer |
| **Properties panel** | referenced document, storage mode, file, source size + DPI, instance count, and the per-instance actions |
| **Layer ▸ Rasterize Layer** | existing action, now also drops a Smart Object reference |

Every one of those paths triggers the same `ImageController` operation — the
context menu reports *which* command was chosen and `MainWindow` runs the same
slot the menu action runs, so there is no second implementation to keep in sync.

### Enablement

| Command | Enabled when |
|---|---|
| Convert to Smart Object | a layer, group, adjustment or multi-selection that is not fully locked. Converting a Smart Object into another one is allowed — that is how you nest them |
| New Smart Object via Copy | the layer is a Smart Object |
| Edit Contents | the layer is a Smart Object |
| Replace Contents… | Smart Object, pixels not locked |
| Relink to File… / Update Modified Content / Embed Linked | **linked** Smart Object |
| Convert to Linked… | **embedded** Smart Object |

Relink, Update and Embed Linked are linked-only because embedded content has no
file to re-point at and can never be out of date: editing it invalidates its
instances immediately.

### Duplicate vs. copy

This is the distinction the whole sharing model rests on, and the UI makes it
explicit:

| Action | Result |
|---|---|
| **Ctrl+J** (Duplicate Layer) | a second **instance** of the *same* document. The payload is shared, so editing the content once updates both — each layer keeps its own transform, mask, opacity, blend and styles |
| **New Smart Object via Copy** | an **independent** document (fresh `docId`). Editing one no longer affects the other. On a *linked* instance the copy comes back embedded, because a private copy is the only form "independent" can take when the content lives in someone else's file |

### Saving

`Ctrl+S` means different things in different tabs: editing a Smart Object's
contents and saving them is **not** the same act as writing the project to
disk.

| Tab | `Ctrl+S` |
|---|---|
| Main document | writes the `.hazor` — root document, every reachable embedded document, and the cached renders |
| **Embedded** contents | **commits in memory only.** Copies the working copy into the embedded document, pushes it through to every instance of it (including nested ones), marks the main document unsaved, and **does not touch the disk** |
| **Linked** contents | writes that *external* file, because the tab is that project's own tab. Projects that link to it then re-render and become unsaved themselves — their files are **not** written |

The embedded case is the one worth stating plainly: writing the whole `.hazor`
from a content tab would also flush unrelated edits the user made in the main
document and had not chosen to save yet. Embedded content reaches the disk when
the project itself is saved, and only then.

So there are two distinct moments, and the UI shows both:

| | Content tab | Main tab |
|---|---|---|
| you edit the contents | `*` | — |
| `Ctrl+S` in the content tab | clean | `*` |
| `Ctrl+S` in the main tab | clean | clean — on disk |

The main document is **not** marked unsaved while you are merely editing the
contents: the project does not contain those edits yet, so saying otherwise
would be a lie about what is on disk. It is the commit that changes the project.

**Save As** from a content tab still means "save the project somewhere else" —
the container is the only thing there is a file for. Writing just the contents
to their own file is *Convert to Linked*, which is exactly that operation. It
commits the working copy first, because a copy of the project that silently left
out the edits on screen would be the wrong file.

**Closing a content tab discards the uncommitted working copy**, so it prompts —
with *Commit* (not *Save*: nothing is written), *Discard* and *Cancel*. Anything
already committed stays in the project, which is where the `.hazor` will pick it
up. Closing the *main* document asks for each of its content tabs first, then
for the project itself, so a commit confirmed in the first prompt is still
offered a save in the second.

### When a document becomes unsaved

A Smart Object render is part of the `.hazor` — it is what an unresolvable link
falls back to — so a render that changes makes the document holding it differ
from its file. Every render commit therefore marks the owning project unsaved,
with exactly one exception: the pass that runs right after a load, which
regenerates renders already on disk. Flagging a project as edited for merely
being opened would be a lie, so that pass carries a flag that suppresses it.

### Replace Contents

Replacing is a **document** operation, not an instance one: the stored document
is swapped under the same `docId`, so every instance of that content updates at
once. On a linked instance there is no document of ours to swap, so it is a
relink. Undo puts the original document object back — nothing is destroyed.

## Automation ([MCP.md](MCP.md), AGENTS.md invariant 7)

`smart_object_create`, `smart_object_place_embedded`,
`smart_object_place_linked`, `smart_object_via_copy`, `smart_object_replace`,
`smart_object_update`, `smart_object_rasterize`.

*Edit Contents* is intentionally absent: it opens a tab rather than performing a
document operation, so it has nothing to report back to a caller. The storage
conversions are absent too — both need a destination path chosen by the user.
