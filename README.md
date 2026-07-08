# Hazor Studio

**Hazor Studio** is a desktop raster image editor with layers and AI assistance.
It pairs a traditional, layer-based editing workflow — brush, selection, text,
shape, transform — with a chat agent that executes operations from natural
language. You can paint and edit by hand, or simply ask the AI to do things like
*"increase brightness and add a gaussian blur."*

Visit the landing page:
[https://rincolabs.org/projects/hazor-studio](https://rincolabs.org/projects/hazor-studio/).

![Hazor Studio generative fill preview](https://rincolabs.org/assets/images/hazor-studio-generative-fill-preview.webp)

## Sponsor

Hazor Studio is developed independently through Rincolabs. If you want to support the project and help keep its development moving forward, you can sponsor my work through GitHub Sponsors: [github.com/sponsors/allanesquina](https://github.com/sponsors/allanesquina).

## What it is

A native, GPU-accelerated image editor built in C++20 on Qt 6, with an OpenGL
rendering pipeline, OpenCV-backed image processing, and an integrated AI layer.

## Highlights

- **GPU canvas** — OpenGL 3.3 viewport with tiled rendering, LOD mipmaps, and an
  FBO render cache for sub-millisecond re-renders of static content.
- **Full tool set** — Move, Brush (advanced brush engine with smoothing,
  dynamics, dual/texture brushes), Eraser, Select, Shape (vector layers), Crop,
  Text, Fill Bucket, Gradient, Eyedropper, Zoom, and Hand.
- **Layers** — Raster, Text, Shape, Group, and non-destructive Adjustment
  layers; layer masks, layer styles (drop shadow, stroke, glows, overlays…),
  blend modes, grouping, and multi-layer transforms.
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
- **MCP server** — an HTTP tool endpoint (port 8080) so external agents and
  automation scripts can drive the editor (see [docs/MCP.md](docs/MCP.md)).
- **Non-linear history** — jump to any point in the undo/redo timeline.
- **Theming & shortcuts** — light/dark semantic theme system and 77 fully
  configurable keyboard shortcuts.

## Architecture

The codebase is organized into six layers — UI (Qt Widgets) → Application
(controller + command history) → Image Engine / Processing / AI Agent → Data
Model → Rendering Pipeline.

## Documentation

- **[docs/BUILD.md](docs/BUILD.md)** — building from source, dependencies, and CMake options
- **[docs/MCP.md](docs/MCP.md)** — the local tool HTTP server (port 8080) for driving the editor from external agents like Claude Code
- **[docs/RENDER-PIPELINE.md](docs/RENDER-PIPELINE.md)** — An exploded view of the render pipeline

## License

See [`LICENSE`](LICENSE).
