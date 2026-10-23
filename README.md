# Hazor Studio

**Hazor Studio** is a desktop raster image editor with layers and AI assistance.
It pairs a traditional, layer-based editing workflow — brush, selection, text,
shape, transform — with a chat agent that executes operations from natural
language. You can paint and edit by hand, or simply ask the AI to do things like
*"increase brightness and add a gaussian blur."*

## Sponsor

Hazor Studio is developed independently through Rincolabs. If you want to support the project and help keep its development moving forward, you can sponsor my work through GitHub Sponsors: [github.com/sponsors/allanesquina](https://github.com/sponsors/allanesquina).

## What it is

A native, GPU-accelerated image editor built in C++20 on Qt 6, with an OpenGL
rendering pipeline, OpenCV-backed image processing, and an integrated AI layer.

## Highlights

- **GPU canvas** — OpenGL 3.3 viewport with tiled rendering, LOD mipmaps, and an
  FBO render cache for sub-millisecond re-renders of static content.
- **Home workspace** — a permanent first tab with recent documents and the
  social-media template catalog. Platform filters provide common document
  dimensions for Instagram, TikTok, YouTube, Facebook, LinkedIn, Pinterest,
  and X. Recent cards share File → Open Recent's history, show cached
  aspect-preserving previews, and can open, reveal, or remove entries without
  touching the original files.
- **Contextual document tabs** — each document tab reports its file name,
  current zoom, active layer (or layer-mask editing context), colour mode, bit
  depth, and unsaved state. Titles update live and retain their complete text in
  a tooltip when the tab bar elides them.
- **Full tool set** — Move, Brush (advanced brush engine with smoothing,
  dynamics, dual/texture brushes), Eraser, Select, Shape (vector layers), Crop,
  Text, Fill Bucket, Gradient, Eyedropper, Zoom, and Hand.
- **Layers** — Raster, Text, Shape, Group, RAW, Smart Object, and
  non-destructive Adjustment layers; layer masks, layer styles (drop shadow,
  stroke, glows, overlays…), blend modes, grouping, and multi-layer transforms.
  Any layer or group can also be exported on its own (Export Layer/Group As...),
  trimmed to its rendered bounds or kept at the document's canvas size.
- **Opacity and Fill** — the layer panel carries both. **Opacity** fades a
  layer's finished contribution, its layer styles included; **Fill** fades only
  the layer's own content and leaves the styles at full strength, so a drop
  shadow, stroke or glow stays exactly as lit while the pixels casting it go
  invisible. An **adjustment layer** has a Fill too, where "content" is the
  contribution the adjustment makes to the composite: Fill at 50% is half as
  much of the same curve, not a flatter curve, and at 0% the adjustment is gone
  while its parameters, its mask and its blend mode are all still there to turn
  back up. Only groups have no Fill — they carry one opacity, the convention
  this kind of editor established — and it is a compositing control, never an
  edit: the pixels, the vector geometry, the editable text and the adjustment
  settings behind them are untouched. For eight
  blend modes (Color Burn, Linear Burn, Color Dodge, Linear Dodge, Vivid Light,
  Linear Light, Hard Mix, Difference) Fill takes part in the blend arithmetic
  itself rather than merely thinning the layer, which is what makes a reduced
  Fill on those modes keep blowing out to white or black where a reduced Opacity
  only fades toward the backdrop.
- **Clipping masks** — any layer can be clipped to the one below it
  (**Layer ▸ Create Clipping Mask**, `Ctrl+Alt+G`, Alt-click the line between two
  rows, or the layer's context menu). The clipped layer shows only where the base
  layer is opaque, reading its alpha per pixel — so partial transparency, soft
  brush edges and antialiasing come through as gradual coverage rather than a
  hard cut-out. Nothing is baked: the clipped layer keeps its own pixels, mask,
  opacity, blend mode and layer styles, several layers can share one base, and
  editing the base updates all of them. The base can be a Pixel, Text, Shape,
  RAW or Smart Object layer, or a **group** — clipping a layer to the combined
  shape of a group below it works the same way. Clipped layers live inside groups
  and nested groups, and survive duplication, reordering, undo/redo and saving.
- **Group blending: Normal or Pass Through** — a group's blend-mode menu offers
  **Pass Through** alongside the ordinary modes. Normally a group composites its
  layers in isolation and merges the finished result; Pass Through lets them
  take part in the composite around the group instead, so a blend mode inside
  the group reads the layers *below* it and an adjustment layer inside the group
  reaches them too. The group's opacity and visibility still apply, and groups
  can be nested in any combination of the two.
- **Smart Objects** — a layer that displays another document and keeps it
  editable forever. Convert any selection into one, or place an existing
  `.hazor` (or legacy `.hzs`) project either *embedded* (stored inside your
  project) or *linked* (kept in its own file and shared across projects).
  Double-click its thumbnail to edit the contents in a tab with the full
  editor and its own undo history; every
  instance updates when you are done. Instances can be reused freely — each one
  keeps its own transform, mask, opacity, blend mode and layer styles while
  sharing a single copy of the content. Smart Objects nest, and Rasterize Layer
  turns one back into ordinary pixels whenever you want. See
  [docs/SMART-OBJECTS.md](docs/SMART-OBJECTS.md).
- **RAW support** — a native RAW development engine (derived from
  RawTherapee's `rtengine`, vendored and built directly into Hazor — no
  external process) opens CR2, CR3, NEF, ARW, RAF, DNG, ORF, RW2, PEF, SRW
  and more as a non-destructive RAW Layer: the original file is never
  modified and the develop profile stays re-editable in the dedicated Hazor
  Develop window, including the shared full-featured curves editor. Its
  viewport offers Fit/100% zoom with drag panning, Before/After and a
  draggable split view, shadow and highlight clipping overlays, and a status
  line that reports what the engine is actually showing — including when a
  stage such as sharpening or noise reduction can only be judged at 100%.
  Aiming a paint or retouch tool (Brush, Eraser, Clone Stamp, Healing, Fill
  Bucket, Gradient) at a RAW Layer offers to create an editable raster copy of
  the current develop result directly above it; the original RAW Layer is kept,
  hidden and still re-developable, and the copy becomes the selected layer so
  the same tool paints on it from the next stroke on.
- **PSD/PSB import** — open PSD/PSB documents (File → Open) straight into a
  native Hazor document: layers, groups, masks, text, shapes, Smart Objects,
  adjustment layers, layer styles, blend modes, the document's alpha and spot
  channels, and document properties are converted to their Hazor equivalent
  where one exists. Conversion runs in
  the background behind a progress dialog, and a Compatibility Report lists
  anything that had no Hazor equivalent and could not be carried over.
  Saving stays in Hazor's native `.hazor` format.
- **Selections** — Rectangular, elliptical, lasso, magic wand, quick select, and
  magnetic lasso, with feather/grow/shrink refinement, **Select ▸ Save/Load
  Selection** (replace, add, subtract, intersect — across open documents of the
  same size), and quick mask.
- **Channels** — a Channels panel (**Window ▸ Channels**) with the document's
  RGB composite row, its Red/Green/Blue components, and any number of extra
  **Alpha** and **Spot** channels. Clicking a row picks it as the edit target
  and shows it; the eye is a separate, independent toggle, and shift-click arms
  several channels at once. A lone colour component renders as grey (or in its
  own hue with **Show Channels in Color**), two or more composite together, and
  hiding one never changes its data. Painting a colour component edits that
  component of the real editable layer and leaves the others — and the layer's
  transparency — exactly as they were.

  An **alpha channel** is reusable selection storage: black is unselected,
  white selected, grey partial, so feathered edges survive the round trip.
  It shows as a configurable colour overlay (colour, opacity, Masked or
  Selected Areas) when the image is visible with it, and as plain grey on its
  own. The overlay is display only — it never reaches the composite or an
  exported file.

  A **spot channel** is an ink separation with the opposite polarity (black is
  full ink) and its own ink colour, **Solidity**, and overprint order. Solidity
  drives the on-screen ink simulation and nothing else: the stored coverage is
  what a separation would print. **Merge Spot Channel** bakes it into the
  colour channels when you explicitly ask, and says first that doing so
  flattens the image.

  Channels are document data, not panel decoration: the brush, eraser,
  gradient, fill bucket, Invert, copy/paste and the grayscale-compatible
  filters all write to the armed channel with their full settings (pressure,
  flow, opacity, texture, dual brush); every operation is one undo step (a
  stroke, not a dab); crop, canvas resize and image resize carry them along
  with the right neutral value for new canvas area; `.hazor` saves and restores
  them; and PSD import reads a file's alpha and spot channels with their
  names, identifiers, colours and solidity. New/Duplicate/Rename/Reorder/
  Delete, Channel Options, Split Channels and Merge Channels are all in the
  panel's menus.
- **Filters & adjustments** — brightness/contrast/saturation/hue, blur, sharpen,
  edge detection, posterize, threshold, background removal, and more — all with
  live on-canvas preview and selection-aware application.
- **Colour picker** — one picker behind every colour control (foreground /
  background, brush, text, shape fill and stroke, fill bucket, swatches, layer
  styles, Solid Color layers, guides). It carries an **eyedropper**: arm it and
  press or drag anywhere on the image to pick the pixel under the cursor, with a
  read-out following it that shows the hex/RGBA values next to a swatch of the
  colour itself. Colours that show on the canvas —
  text, shape fill and stroke, Solid Color fill layers, layer-style colours —
  **preview live** while the picker is open; OK keeps the result as a single
  undo step and Cancel (or Esc, or closing the window) puts the original colour
  back without touching the history.
- **AI agent** — a chat panel that turns natural-language requests into tool
  calls, driven by Ollama or an OpenAI-compatible provider.
- **AI selection & matting** — ONNX-powered object selection (SAM) and
  background removal / matting (BiRefNet, ModNet, RMBG), with CPU/CUDA/TensorRT
  execution providers selectable at runtime.
- **MCP server** — a [Model Context Protocol](https://modelcontextprotocol.io)
  server (port 8085) so external agents and automation scripts can drive the
  editor (see [docs/MCP.md](docs/MCP.md)).
- **Non-linear history** — jump to any point in the undo/redo timeline.
- **Theming & shortcuts** — light/dark semantic theme system and 77 fully
  configurable keyboard shortcuts.

## Architecture

The codebase is organized into six layers — UI (Qt Widgets) → Application
(controller + command history) → Image Engine / Processing / AI Agent → Data
Model → Rendering Pipeline.

## Building

Build instructions, dependencies, and CMake options are documented separately:

➡️ **[docs/BUILD.md](docs/BUILD.md)**

## Documentation

- **[docs/BUILD.md](docs/BUILD.md)** — building from source, dependencies, and CMake options
- **[docs/MCP.md](docs/MCP.md)** — the embedded MCP server (port 8085) for driving the editor from external agents like Claude Code
- **[docs/SMART-OBJECTS.md](docs/SMART-OBJECTS.md)** — the Smart Object data model, embedded vs linked storage, and the `.hazor` version 3 format

## License

See [`LICENSE`](LICENSE).
