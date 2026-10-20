# AGENTS.md

Instructions for AI coding agents working in the **Hazor Studio** repository.

This file is the single source of truth for agent behaviour. Assistant-specific
files ([`CLAUDE.md`](CLAUDE.md), and any future `.cursorrules`, `GEMINI.md`, …)
must stay thin and link here instead of duplicating rules.

> **Scope:** this file applies to the whole repository. If a nested `AGENTS.md`
> is ever added to a subdirectory, the closest one to the file being edited
> wins. Direct instructions from the user always override this file.

---

## 1. Project snapshot

**Hazor Studio** is a native, GPU-accelerated raster image editor with layers
and an integrated AI layer.

| | |
|-|-|
| Language | C++20 |
| UI | Qt 6 (Widgets) — `Widgets`, `OpenGLWidgets`, `Network`, `Concurrent` |
| Rendering | OpenGL 3.3 Core (optional QRhi backend behind `USE_RHI`) |
| Image processing | OpenCV 4 (`core`, `imgproc`, `imgcodecs`) |
| AI inference | ONNX Runtime 1.26 (**optional**, `ENABLE_ONNXRUNTIME`) |
| Build | CMake ≥ 3.16, `AUTOMOC` + `AUTORCC` |
| License | GPL-3.0 — see [`LICENSE`](LICENSE) |
| Platforms | Linux (primary, AppImage) and Windows (MSVC 2022) |

Start with [`README.md`](README.md) for the feature set and
[`docs/BUILD.md`](docs/BUILD.md) for dependencies.

---

## 2. Hard rules

These are non-negotiable. They exist because this is a GPU desktop
application with long build times and a legacy test suite.

1. **Never launch the application.** Do not run `./scripts/run.sh`, the `Hazor`
   binary, or anything that opens the GUI. Verification is done by *building*
   and by *reading code*, not by running the editor. If a change genuinely
   needs on-screen confirmation, say so and let the user do it.
2. **Build only through [`./scripts/build.sh`](scripts/build.sh).** Do not
   invent `cmake` invocations, alternative build directories, or compiler
   flags. The script is the contract.
3. **Never commit, push, tag, or open a PR unless asked.** Branching,
   committing and releasing are explicit user actions.

---

## 3. Build and verify

```bash
./scripts/build.sh          # configure + build the Hazor target (Release)
```

Everything the script needs lives under `3rdparty/` (not in git) and can be
overridden with environment variables:

| Variable | Default |
|----------|---------|
| `BUILD_DIR` | `<root>/build` |
| `THIRD_PARTY_DIR` | `<root>/3rdparty` |
| `QT_DIR` | `$THIRD_PARTY_DIR/Qt/6.8.3/gcc_64` |
| `ONNXRUNTIME_DIR` | `$THIRD_PARTY_DIR/onnx/runtime/onnxruntime-linux-x64-gpu-1.26.0` |
| `REALESRGAN_DIR` | `$THIRD_PARTY_DIR/realesrgan` |
| `REALESRGAN_MODELS_DIR` | `$THIRD_PARTY_DIR/realesrgan/models` |

**A full build is slow.** Build once at the end of a coherent change rather
than after every edit, and report compiler errors verbatim.

Scripts that exist but that agents must not use on their own initiative:
[`run.sh`](scripts/run.sh) (launches the app — see rule 1),
[`tests.sh`](scripts/tests.sh) / [`run-tests.sh`](scripts/run-tests.sh)
(outdated suite — see rule 3).

---

## 4. Repository map

```
src/
├── core/          Data model: Document, Layer, LayerTreeNode, tiles, selection,
│                  channels, adjustments, effects, guides, swatches, render scheduling
├── controller/    ImageController (app-level operations) + CommandHistory/Commands
│                  (undo/redo). Every destructive edit goes through a command.
├── renderer/      Render pipeline: GPUViewport, LayerCompositor (GPU),
│                  DocumentCompositor (CPU), ProjectionCache, TileRenderer,
│                  CanvasView (input + overlays), shared blend spec
├── engine/        ImageEngine (OpenCV bridge), shape/vector rasterizers
├── processing/    FilterProcessor + PreviewRenderer (live filter preview)
├── brush/         Brush engine: BrushRenderer, dynamics, presets, import
├── shape/ text/ gradient/ transform/ histogram/   Tool-specific subsystems
├── color/         Color management: profiles, display transforms, soft proof
├── io/            ImageIO (open/export), ImageCodec, ProjectFileService (.hzr)
├── ai/            ONNX inference: SAM selection, matting, inpaint, upscale
├── agent/         Chat agent: AgentController, LLMClient, AgentPanel
├── tools/         ToolCatalog + ToolExecutor — the tool surface shared by the
│                  chat agent and the MCP server
├── mcp/           Local HTTP tool server (see docs/MCP.md)
├── ui/            Qt Widgets layer: MainWindow, panels, dialogs (largest module)
├── theme/         Semantic theme system (Theme, ThemeManager, builders)
├── cache/ async/ memory/   RenderCache, AsyncJobSystem, memory helpers
└── resources/     Qt resources, generated shape SVGs

docs/              BUILD.md, MCP.md, RENDER-PIPELINE.md
scripts/           build.sh, run.sh, tests.sh, run-tests.sh
shaders/           Standalone GLSL files (most shaders are inline in C++)
tests/             Outdated — see rule 3
3rdparty/          Vendored deps, contents gitignored
```

Three files are very large and should be navigated with `grep`, never read
whole: [`CanvasView.cpp`](src/renderer/CanvasView.cpp) (~11k lines),
[`ImageController.cpp`](src/controller/ImageController.cpp) (~7k),
[`MainWindow.cpp`](src/ui/MainWindow.cpp) (~6k).

---

## 5. Architecture invariants

Break one of these and the bug shows up far from your change. Read
[`docs/RENDER-PIPELINE.md`](docs/RENDER-PIPELINE.md) before touching anything
under `src/renderer/`.

1. **The CPU compositor is the visual source of truth.**
   [`DocumentCompositor`](src/renderer/DocumentCompositor.cpp) produces export,
   flatten, merge, clone sampling and the idle projection.
   [`LayerCompositor`](src/renderer/LayerCompositor.cpp) exists only to make
   *interactive* frames fast and must match the CPU result pixel for pixel. Any
   divergence appears to the user as a "pop" when a stroke ends.

2. **Blend modes are specified in three files that change together:**
   [`BlendRules.hpp`](src/renderer/BlendRules.hpp) (mode → backend primitive),
   [`BlendMath.hpp`](src/renderer/BlendMath.hpp) (C++ formulas),
   [`BlendShaderLib.hpp`](src/renderer/BlendShaderLib.hpp) (the same formulas in
   GLSL). Adding or editing a mode in only one of them is a bug.

3. **GPU adjustment passes mirror `adjustments::apply()`.** The `adjust` shader
   branches must match [`AdjustmentTypes.cpp`](src/core/AdjustmentTypes.cpp) —
   same LUT rule, same HSL model, same mask density ramp.

4. **State changes bump a generation counter.** `Document::compositionGeneration`
   invalidates [`ProjectionCache`](src/renderer/ProjectionCache.cpp) and
   [`RenderCache`](src/cache/RenderCache.cpp); `displayGeneration` covers colour
   management changes. Mutating pixels without bumping the right counter leaves
   the canvas stale; bumping needlessly kills the cache.

5. **Destructive edits go through `ImageController` + a command.** Undo/redo and
   the non-linear history depend on it. Long operations use the async
   document-state path (COW snapshot → worker → apply + undo), not UI-thread
   blocking.

6. **One brush rasterizer.** Dabs are painted once on the CPU into
   [`RasterLayerStorage`](src/core/RasterLayerStorage.cpp) tiles; both render
   paths display those same pixels. Do not add a second dab path.

7. **The tool catalog is shared.** The chat agent and the MCP server both drive
   [`ToolCatalog`](src/tools/ToolCatalog.cpp) /
   [`ToolExecutor`](src/tools/ToolExecutor.cpp). A new editor operation exposed
   to automation is registered once, there.

8. **No hardcoded colours in UI or canvas code.** Everything comes from the
   active [`Theme`](src/theme/Theme.hpp) via `ThemeManager::instance()->current()`.

9. **One source of version truth:** `project(... VERSION ...)` in
   [`CMakeLists.txt`](CMakeLists.txt) → generated `Version.hpp`. Never hardcode
   a version string.

10. **ONNX Runtime is optional.** Code under `src/ai/` must compile and the app
    must run with `ENABLE_ONNXRUNTIME=OFF`; the AI layer reports itself
    unavailable at runtime. CPU/CUDA/TensorRT are *runtime* execution
    providers, never separate builds.

---

## 6. Code style

Match the surrounding file; the codebase is consistent.

- **Formatting:** 4 spaces, no tabs. No `.clang-format` in the repo — do not
  reformat existing code, and never reformat code you are not otherwise editing.
- **Headers:** `#pragma once`, forward-declare instead of including where it
  works, project includes (`"core/Document.hpp"`) before Qt/OpenCV/std.
- **Naming:** `PascalCase` types and file names, `camelCase` functions and
  variables, `m_` prefix for members, `k` prefix for file-scope constants
  (`kGroupFboPoolCap`), lowercase namespaces (`core`, `blendmath`, `blend`).
- **Comments explain *why*, not *what*.** This codebase documents hard-won
  reasoning (coordinate conventions, cache-invalidation pitfalls, Qt/GL
  quirks) in dense block comments. Preserve them; when you change the
  behaviour they describe, update them in the same edit.
- **Qt idioms:** signals/slots for cross-module notification, `QImage`
  `Format_RGBA8888` for layer pixels and `Format_ARGB32_Premultiplied` for
  compositing stages, `QSettings` for persistence.
- **Keep diffs minimal and scoped.** No opportunistic refactors, no renames, no
  dependency additions without being asked.

---

## 7. Git conventions

- **Branches:** `main` is the default branch. Work on a topic branch; never
  commit directly to `main`.
- **Commits:** Conventional Commits with a subsystem scope, as in the existing
  history:

  ```
  fix(render): rebuild composite caches on layer sync so loaded layers aren't invisible
  feat(MCP): MCP settings
  docs(render): the render path
  ```

  Common scopes: `render`, `MCP`, `brush`, `ui`, `ai`, `io`, `color`, `core`,
  `build`. Subject in imperative mood, lower case, no trailing period. Explain
  *why* in the body when the change is not self-evident.
- **One logical change per commit.** Do not mix a fix with unrelated cleanup.

---

## 8. Documentation expectations

Update docs in the same change as the code:

| You changed | Also update |
|---|---|
| Render pipeline, compositors, blend spec, sampling | [`docs/RENDER-PIPELINE.md`](docs/RENDER-PIPELINE.md) |
| Dependencies, CMake options, build steps | [`docs/BUILD.md`](docs/BUILD.md) |
| MCP endpoints, tool schemas, default port | [`docs/MCP.md`](docs/MCP.md) |
| User-visible features | [`README.md`](README.md) |

---

## 9. Working agreement

- **Investigate before editing.** `grep` for the real call sites; this codebase
  has several near-identical coordinate/transform chains and editing the wrong
  one silently breaks another path.
- **Report honestly.** If the build fails, paste the error. If part of the task
  was skipped or blocked, say which part and why. Never claim verification that
  did not happen — and given rule 1, "I ran it and it looks right" is never a
  valid claim here.
- **Ask only when it matters.** Resolve routine ambiguity with a sensible
  default and state the assumption; stop and ask when two readings of the
  request lead to materially different work.
- **Use markdown links for code references** so they are clickable:
  `[DocumentCompositor.cpp:93](src/renderer/DocumentCompositor.cpp#L93)`.

---

## 10. Where to look first

| Task | Entry points |
|---|---|
| Canvas looks wrong / blurry / stale | [`GPUViewport.cpp`](src/renderer/GPUViewport.cpp), [`GPUViewport_Shaders.cpp`](src/renderer/GPUViewport_Shaders.cpp), [`DocumentCompositor.cpp`](src/renderer/DocumentCompositor.cpp), [`ProjectionCache.cpp`](src/renderer/ProjectionCache.cpp) |
| Zoom / pan / input mapping | [`CanvasView.cpp`](src/renderer/CanvasView.cpp), [`ViewportCamera.cpp`](src/core/ViewportCamera.cpp), [`CanvasInputMapper.hpp`](src/renderer/CanvasInputMapper.hpp) |
| Blend mode / opacity / mask semantics | [`BlendRules.hpp`](src/renderer/BlendRules.hpp) + [`BlendMath.hpp`](src/renderer/BlendMath.hpp) + [`BlendShaderLib.hpp`](src/renderer/BlendShaderLib.hpp) |
| Channels panel, channel view, channel editing | [`DocumentChannel.hpp`](src/core/DocumentChannel.hpp), [`ChannelView.cpp`](src/renderer/ChannelView.cpp), [`ImageController_Channels.cpp`](src/controller/ImageController_Channels.cpp), [`ChannelsPanel.cpp`](src/ui/ChannelsPanel.cpp) |
| Layer tree, groups, adjustments, effects | [`LayerTreeNode.hpp`](src/core/LayerTreeNode.hpp), [`Layer.hpp`](src/core/Layer.hpp), [`LayerEffect.cpp`](src/core/LayerEffect.cpp) |
| Undo/redo, merge, flatten, destructive ops | [`ImageController.cpp`](src/controller/ImageController.cpp), [`CommandHistory.cpp`](src/controller/CommandHistory.cpp) |
| Brush behaviour | [`BrushRenderer.cpp`](src/brush/BrushRenderer.cpp), [`BrushEngine.cpp`](src/brush/BrushEngine.cpp), [`DynamicsEvaluator.cpp`](src/brush/DynamicsEvaluator.cpp) |
| Filters and live preview | [`FilterProcessor.cpp`](src/processing/FilterProcessor.cpp), [`PreviewRenderer.cpp`](src/processing/PreviewRenderer.cpp) |
| Open / export / project files | [`ImageIO.cpp`](src/io/ImageIO.cpp), [`ProjectFileService.cpp`](src/io/ProjectFileService.cpp) |
| Colour management | [`src/color/`](src/color/) |
| Menus, panels, dialogs, shortcuts | [`MainWindow.cpp`](src/ui/MainWindow.cpp), [`MainWindow_Menus.cpp`](src/ui/MainWindow_Menus.cpp), [`src/ui/`](src/ui/) |
| Agent / automation surface | [`ToolCatalog.cpp`](src/tools/ToolCatalog.cpp), [`ToolExecutor.cpp`](src/tools/ToolExecutor.cpp), [`AgentController.cpp`](src/agent/AgentController.cpp), [`McpServer.cpp`](src/mcp/McpServer.cpp) |
