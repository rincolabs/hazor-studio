#pragma once

#include "RenderBackend.hpp"

namespace render {

// ── OpenGL 3.3 Core backend ────────────────────────────────────
//
// Skeleton for Phase 0. All methods are stubs that satisfy the
// interface contract without allocating GPU resources.
// Full implementation arrives in Phase 3 (GPUViewport).
//
// The real backend will PIMPL all GL calls; for now we keep it
// header-only so no .cpp file is needed.

class OpenGLBackend final : public RenderBackend {
public:
    OpenGLBackend()  = default;
    ~OpenGLBackend() override = default;

    // ── Lifecycle ────────────────────────────────────────────
    bool initialize() override
    {
        // TODO Phase 3: init shader programs, VAOs, default state
        return true;
    }

    void shutdown() override
    {
        // TODO Phase 3: delete shaders, VAOs, textures
    }

    void beginFrame(float, float, float, float) override
    {
        // TODO Phase 3: glClear
    }

    void endFrame() override
    {
        // TODO Phase 3: swap buffers (handled by QOpenGLWidget)
    }

    // ── Texture management ───────────────────────────────────
    TextureId createTexture(int, int, TextureFormat,
                            TextureFilter, TextureFilter,
                            TextureWrap, const void*) override
    {
        // TODO Phase 3: glGenTextures / glTexImage2D
        return 1;
    }

    void updateSubTexture(TextureId, int, int, int, int,
                          const void*) override
    {
        // TODO Phase 3: glTexSubImage2D
    }

    void readTexture(TextureId, void*, int, int,
                     TextureFormat) override
    {
        // TODO Phase 3: glGetTexImage
    }

    void destroyTexture(TextureId) override
    {
        // TODO Phase 3: glDeleteTextures
    }

    // ── Framebuffer management ───────────────────────────────
    FramebufferId createFramebuffer(TextureId) override
    {
        // TODO Phase 3: glGenFramebuffers + glFramebufferTexture2D
        return 1;
    }

    void destroyFramebuffer(FramebufferId) override
    {
        // TODO Phase 3: glDeleteFramebuffers
    }

    void bindFramebuffer(FramebufferId) override
    {
        // TODO Phase 3: glBindFramebuffer
    }

    void unbindFramebuffer() override
    {
        // TODO Phase 3: glBindFramebuffer(0)
    }

    // ── Shared quad geometry ─────────────────────────────────
    VertexBufferId createQuadBuffer() override
    {
        // TODO Phase 3: create VBO + VAO for full-screen quad
        return 1;
    }

    void destroyBuffer(VertexBufferId) override
    {
        // TODO Phase 3: glDeleteBuffers / glDeleteVertexArrays
    }

    // ── Draw operations ──────────────────────────────────────
    void drawSolidQuad(const SolidQuadParams&) override
    {
        // TODO Phase 3: bind solid shader, set uniforms, draw
    }

    void drawTexturedQuad(const TexturedQuadParams&) override
    {
        // TODO Phase 3: bind main shader, set uniforms + textures, draw
    }

    void drawBlendComposite(const BlendCompositeParams&) override
    {
        // TODO Phase 3: bind blend shader, FBO blit + composite
    }

    void drawMarchingAnts(const MarchingAntsParams&) override
    {
        // TODO Phase 3: bind marching ants shader, draw
    }

    void drawRubylith(const RubylithParams&) override
    {
        // TODO Phase 3: bind rubylith shader, draw
    }

    void drawGrayscaleMask(const GrayscaleParams&) override
    {
        // TODO Phase 3: bind grayscale shader, draw
    }

    void drawWireRect(const WireRectParams&) override
    {
        // TODO Phase 3: bind solid shader, GL_LINE_LOOP
    }

    void drawBrushDab(const BrushDabParams&) override
    {
        // TODO Phase 3 (BrushRenderer refactor)
    }

    void drawMaskDab(const MaskDabParams&) override
    {
        // TODO Phase 3 (BrushRenderer refactor)
    }

    // ── Render state ──────────────────────────────────────────
    void setBlendOp(BlendOp) override
    {
        // TODO Phase 3: glBlendFunc / glBlendEquation
    }

    void disableBlending() override
    {
        // TODO Phase 3: glDisable(GL_BLEND)
    }

    void setScissor(int, int, int, int) override
    {
        // TODO Phase 3: glScissor + glEnable(GL_SCISSOR_TEST)
    }

    void disableScissor() override
    {
        // TODO Phase 3: glDisable(GL_SCISSOR_TEST)
    }

    void setViewport(int x, int y, int w, int h) override
    {
        (void)x; (void)y; (void)w; (void)h;
        // TODO Phase 3: glViewport
    }

    void getViewport(int&, int&, int&, int&) const override
    {
        // TODO Phase 3: glGetIntegerv(GL_VIEWPORT)
    }

    // ── Queries ───────────────────────────────────────────────
    bool hasFramebufferFetch() const override
    {
        // TODO Phase 3: check GL_EXT_shader_framebuffer_fetch
        return false;
    }

    int maxTextureSize() const override
    {
        // TODO Phase 3: glGetIntegerv(GL_MAX_TEXTURE_SIZE)
        return 16384;
    }
};

// ── Factory implementation ──────────────────────────────────────
inline std::unique_ptr<RenderBackend> RenderBackend::createOpenGL()
{
    return std::make_unique<OpenGLBackend>();
}

} // namespace render
