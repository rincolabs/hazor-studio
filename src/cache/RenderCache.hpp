#pragma once

#include <QPointF>
#include <cstdint>

class Document;

class RenderCache {
public:
    RenderCache() = default;
    ~RenderCache() { destroy(); }

    void destroy();

    bool isValid(const Document* doc, int viewportW, int viewportH,
                 float zoom, QPointF panOffset, QPointF canvasHalfExtents) const;

    void ensureSize(int vpW, int vpH);

    void captureCurrentFrame(int vpW, int vpH, unsigned int sourceFbo);

    void drawCached(class GPUViewport* gpu);

    void markValid(const Document* doc, int vpW, int vpH,
                   float zoom, QPointF panOffset, QPointF canvasHalfExtents);

    // Force the next isValid() to fail. Uses a sentinel that can never equal a
    // real Document::compositionGeneration (which starts at 0), so it invalidates
    // even a freshly loaded document still at generation 0 — 0 would not.
    void markInvalid() { m_cachedGeneration = kInvalidGeneration; }

private:
    static constexpr uint64_t kInvalidGeneration = ~0ull;

    unsigned int m_fbo = 0;
    unsigned int m_texture = 0;
    int m_cachedVpW = 0;
    int m_cachedVpH = 0;
    float m_cachedZoom = 1.0f;
    QPointF m_cachedPan;
    QPointF m_cachedCanvasHalfExtents;
    const Document* m_cachedDoc = nullptr;
    uint64_t m_cachedGeneration = kInvalidGeneration;
    uint64_t m_cachedDisplayGeneration = 0;
};
