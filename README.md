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
- **Full tool set** — Move, Brush (advanced brush engine with smoothing,
  dynamics, dual/texture brushes), Eraser, Select, Shape (vector layers), Crop,
  Text, Fill Bucket, Gradient, Eyedropper, Zoom, and Hand.
- **Layers** — Raster, Text, Shape, Group, RAW, Smart Object, and
  non-destructive Adjustment layers; layer masks, layer styles (drop shadow,
  stroke, glows, overlays…), blend modes, grouping, and multi-layer transforms.
  Any layer or group can also be exported on its own (Export Layer/Group As...),
  trimmed to its rendered bounds or kept at the document's canvas size.
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
  adjustment layers, layer styles, blend modes and document properties are
  converted to their Hazor equivalent where one exists. Conversion runs in
  the background behind a progress dialog, and a Compatibility Report lists
  anything that had no Hazor equivalent and could not be carried over.
  Saving stays in Hazor's native `.hazor` format.
- **Selections** — Rectangular, elliptical, lasso, magic wand, quick select, and
  magnetic lasso, with feather/grow/shrink refinement, saved alpha channels, and
  quick mask.
- **Filters & adjustments** — brightness/contrast/saturation/hue, blur, sharpen,
  edge detection, posterize, threshold, background removal, and more — all with
  live on-canvas preview and selection-aware application.
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
