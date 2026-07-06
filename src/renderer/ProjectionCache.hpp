#pragma once

#include <QImage>
#include <QSize>
#include <cstdint>
#include <functional>
#include <memory>

class Document;
class AsyncProjectionBuilder;

// ─────────────────────────────────────────────────────────────────────────
// ProjectionCache — the CPU-composited "projection" of the whole document.
//
// Fase B of the compositor refactor: the CPU (DocumentCompositor) is the
// single source of truth and produces the full-resolution flattened image of
// the document. This class caches that image as a GL texture so the GPU can
// simply *display* it (with the viewport's zoom/pan and mipmapping for sharp
// minification) instead of re-running its own layer compositor.
//
// The cache is keyed on the document pointer + its compositionGeneration, so
// it only recomposites/re-uploads when the composition actually changed.
//
// Fase C — async: the (expensive) CPU composite runs on a worker thread
// (AsyncProjectionBuilder) so finalizing a live edit never blocks the UI.
// While a fresh projection is being built, update() returns 0 and the caller
// falls back to the GPU per-layer compositor (the same path used mid-drag), so
// the canvas keeps showing the current state at interactive speed; when the
// worker finishes, onReady fires a repaint and the next update() uploads it.
// ─────────────────────────────────────────────────────────────────────────
class ProjectionCache {
public:
    ProjectionCache();
    ~ProjectionCache();

    // Returns the up-to-date projection texture id, or 0 when it isn't ready
    // yet (async build in flight) or unavailable (document exceeds the GL
    // texture limit). Requires a current GL context. Never blocks on the CPU
    // composite. The caller must fall back to the per-layer compositor on 0.
    unsigned int update(Document* doc);

    unsigned int texture() const { return m_texture; }
    QSize        size()    const { return m_size; }
    const QImage& image()  const { return m_image; }

    // Invoked (on the UI thread) when an async projection result becomes
    // available — the host repaints so the next update() uploads + displays it.
    void setReadyCallback(std::function<void()> cb) { m_onReady = std::move(cb); }

    // Force a rebuild on the next update() (e.g. after a GL context reset).
    void invalidate()
    {
        m_generation = kInvalidGen;
        m_doc = nullptr;
        m_size = QSize();
        m_inFlightDoc = nullptr;
        m_inFlightGen = kInvalidGen;
    }

    // Delete the GL texture. Must be called with a current GL context.
    void releaseGL();

private:
    void uploadTexture();
    void ensureBuilder();
    // Builds a thread-safe COW snapshot of `doc` for off-thread compositing.
    std::shared_ptr<Document> makeSnapshot(const Document* doc) const;
    // A single full-canvas texture can't represent documents larger than the
    // GL texture limit; in that case update() returns 0 and the caller falls
    // back to the per-layer compositor.
    bool exceedsMaxTextureSize(const QSize& s);

    static constexpr uint64_t kInvalidGen = ~0ull;

    QImage        m_image;
    const void*   m_doc        = nullptr;
    unsigned int  m_texture    = 0;
    uint64_t      m_generation = kInvalidGen;
    QSize         m_size;
    int           m_maxTextureSize = 0; // cached GL_MAX_TEXTURE_SIZE

    std::unique_ptr<AsyncProjectionBuilder> m_builder;
    std::function<void()>                   m_onReady;
    // Identity of the build currently dispatched to the worker (so update()
    // doesn't re-snapshot every frame while one is already in flight).
    const void*   m_inFlightDoc = nullptr;
    uint64_t      m_inFlightGen = kInvalidGen;
};
