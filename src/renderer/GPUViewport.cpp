#include "GPUViewport.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "core/LayerEffect.hpp"
#include "engine/ShapeVectorRenderer.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "transform/TransformController.hpp"
#include "text/TextRenderer.hpp"
#include "text/TextLayoutEngine.hpp"
#include "text/TextEditorController.hpp"
#include "LayerCompositor.hpp"
#include "color/ColorManagementService.hpp"
#include "color/DisplayProfileService.hpp"

#include <QPainter>
#include <QDebug>
#include <algorithm>
#include <cstring>

#include <QOpenGLExtraFunctions>
#include <QOpenGLContext>
#include <QMatrix4x4>
#include <cmath>
#include <array>

// ── Static helpers ─────────────────────────────────────────────

// RAII guard that disables the GL scissor test for its lifetime and restores
// the previous enable state on destruction. Use it around any operation that
// must touch the whole render target rather than the scissored canvas rect —
// full-target framebuffer blits, clears, or cache captures. The render keeps a
// canvas-sized scissor enabled globally (clip layers to the document), so these
// full-target ops would otherwise be cropped to the central canvas rectangle.
namespace {
struct ScopedScissorDisable {
    ScopedScissorDisable()
    {
        auto* f = QOpenGLContext::currentContext()
                      ? QOpenGLContext::currentContext()->functions() : nullptr;
        if (!f) return;
        m_funcs = f;
        m_wasEnabled = f->glIsEnabled(GL_SCISSOR_TEST);
        if (m_wasEnabled) f->glDisable(GL_SCISSOR_TEST);
    }
    ~ScopedScissorDisable()
    {
        if (m_funcs && m_wasEnabled) m_funcs->glEnable(GL_SCISSOR_TEST);
    }
    ScopedScissorDisable(const ScopedScissorDisable&) = delete;
    ScopedScissorDisable& operator=(const ScopedScissorDisable&) = delete;

    QOpenGLFunctions* m_funcs = nullptr;
    GLboolean m_wasEnabled = GL_FALSE;
};

void uploadR8TextureFromImage(const QImage& image)
{
    GLint prevUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 image.width(), image.height(),
                 0, GL_RED, GL_UNSIGNED_BYTE,
                 image.constBits());
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
}

void readR8TextureToImage(QImage& image)
{
    GLint prevPackAlignment = 4;
    glGetIntegerv(GL_PACK_ALIGNMENT, &prevPackAlignment);
    glPixelStorei(GL_PACK_ALIGNMENT, 4);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RED, GL_UNSIGNED_BYTE,
                  image.bits());
    glPixelStorei(GL_PACK_ALIGNMENT, prevPackAlignment);
}
} // namespace

bool GPUViewport::needsShaderBlend(int blendMode)
{
    return blend::needsShaderBlend(static_cast<BlendMode>(blendMode));
}

void GPUViewport::applyFixedBlend(int blendMode)
{
    // Straight-alpha "over". RGB uses (SRC_ALPHA, ONE_MINUS_SRC_ALPHA); the alpha
    // channel must accumulate as (ONE, ONE_MINUS_SRC_ALPHA), NOT the same factors
    // as RGB. With a single glBlendFunc, alpha would compute as
    // src.a*src.a + dst.a*(1-src.a), so a 50% dab edge over an opaque layer yields
    // 0.75 instead of 1.0. That deficit is invisible on the (opaque) screen, but
    // when a blend mode forces the live stack into a transparent isolation FBO
    // (see LayerCompositor::composite), the reduced FBO alpha lets the checkerboard
    // bleed through semi-transparent dab/effect edges, diverging from the CPU
    // projection. glBlendFuncSeparate keeps dst.a = src.a + dst.a*(1-src.a).
    switch (static_cast<BlendMode>(blendMode)) {
    case BlendMode::Normal:
    default:
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                            GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glBlendEquation(GL_FUNC_ADD);
        break;
    }
}

// ── Constructor / Destructor ───────────────────────────────────

GPUViewport::GPUViewport() = default;
GPUViewport::~GPUViewport()
{
    m_projection.releaseGL();
    if (m_mainProg)     delete m_mainProg;
    if (m_solidProg)    delete m_solidProg;
    if (m_blendProg)    delete m_blendProg;
    if (m_selectProg)   delete m_selectProg;
    if (m_rubylithProg) delete m_rubylithProg;
    if (m_grayProg)     delete m_grayProg;
    if (m_adjustProg)   delete m_adjustProg;
    if (m_displayProg)  delete m_displayProg;
    if (m_curveLutTex)   glDeleteTextures(1, &m_curveLutTex);
    if (m_displayLutTex) glDeleteTextures(1, &m_displayLutTex);
    if (m_sceneTex)      glDeleteTextures(1, &m_sceneTex);
    if (m_sceneFbo)      glDeleteFramebuffers(1, &m_sceneFbo);
    for (auto& e : m_groupFboPool) {
        if (e.fbo) glDeleteFramebuffers(1, &e.fbo);
        if (e.tex) glDeleteTextures(1, &e.tex);
    }
}

// ── Initialize ─────────────────────────────────────────────────

void GPUViewport::initialize()
{
    initializeOpenGLFunctions();
    initShaders();
    initQuad();
    initInstancedQuad();
    initFboQuad();
    initCheckerTexture();
    m_tileRenderer.initGL();
}

void GPUViewport::initQuad()
{
    m_vao.create();
    m_vao.bind();
    m_vbo.create();
    m_vbo.bind();
    m_vbo.allocate(quadVertices, sizeof(quadVertices));
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          reinterpret_cast<const void*>(offsetof(QuadVertex, u)));
    glEnableVertexAttribArray(1);
    m_vao.release();
}

// ── Instanced rendering VAO ─────────────────────────────────────

// Per-instance data layout (aligned to 4 bytes):
// [0..15]  mvp — mat4 (column-major, 16 floats)
// [16..17] uvOffset — vec2 (2 floats)
// [18..19] uvScale — vec2 (2 floats)
// total: 20 floats = 80 bytes
struct alignas(16) InstanceData {
    float mvp[16]{};
    float uvOffset[2]{};
    float uvScale[2]{};
};

void GPUViewport::initInstancedQuad()
{
    auto* ctx = QOpenGLContext::currentContext();
    auto* glx = ctx ? ctx->extraFunctions() : nullptr;
    if (!glx) return;

    m_instancedVao.create();
    m_instancedVao.bind();

    // Reuse the same vertex buffer (pos + uv)
    m_vbo.bind();

    // Location 0: pos (vec2) — per-vertex, divisor=0
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), nullptr);
    glEnableVertexAttribArray(0);

    // Location 1: uv (vec2) — per-vertex, divisor=0
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          reinterpret_cast<const void*>(offsetof(QuadVertex, u)));
    glEnableVertexAttribArray(1);

    // Set up instance data buffer
    m_instancedVbo.create();
    m_instancedVbo.bind();

    // Location 2: aMvp0 (vec4) — per-instance, divisor=1
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          reinterpret_cast<const void*>(0));
    glEnableVertexAttribArray(2);
    glx->glVertexAttribDivisor(2, 1);

    // Location 3: aMvp1 (vec4)
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          reinterpret_cast<const void*>(16));
    glEnableVertexAttribArray(3);
    glx->glVertexAttribDivisor(3, 1);

    // Location 4: aMvp2 (vec4)
    glVertexAttribPointer(4, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          reinterpret_cast<const void*>(32));
    glEnableVertexAttribArray(4);
    glx->glVertexAttribDivisor(4, 1);

    // Location 5: aMvp3 (vec4)
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          reinterpret_cast<const void*>(48));
    glEnableVertexAttribArray(5);
    glx->glVertexAttribDivisor(5, 1);

    // Location 6: aUvOffset (vec2)
    glVertexAttribPointer(6, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          reinterpret_cast<const void*>(64));
    glEnableVertexAttribArray(6);
    glx->glVertexAttribDivisor(6, 1);

    // Location 7: aUvScale (vec2)
    glVertexAttribPointer(7, 2, GL_FLOAT, GL_FALSE, sizeof(InstanceData),
                          reinterpret_cast<const void*>(72));
    glEnableVertexAttribArray(7);
    glx->glVertexAttribDivisor(7, 1);

    m_instancedVao.release();
}

void GPUViewport::uploadInstanceData(const std::vector<TileDraw>& tiles)
{
    if (tiles.empty()) return;

    std::vector<InstanceData> data(tiles.size());
    for (size_t i = 0; i < tiles.size(); ++i) {
        const auto& td = tiles[i];
        // Store mvp as float array (column-major, already computed in td.mvp)
        std::memcpy(data[i].mvp, td.mvp.constData(), 16 * sizeof(float));
        data[i].uvOffset[0] = td.uvOffset.x();
        data[i].uvOffset[1] = td.uvOffset.y();
        data[i].uvScale[0]  = td.uvScale.x();
        data[i].uvScale[1]  = td.uvScale.y();
    }

    m_instancedVbo.bind();
    m_instancedVbo.allocate(data.data(),
                            static_cast<int>(data.size() * sizeof(InstanceData)));
}

void GPUViewport::drawInstanced(int tileCount)
{
    if (tileCount <= 0) return;
    auto* ctx = QOpenGLContext::currentContext();
    auto* glx = ctx ? ctx->extraFunctions() : nullptr;
    if (!glx) return;

    m_instancedVao.bind();
    m_mainProg->setUniformValue(m_mainU.instanced, 1);
    glx->glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, tileCount);
    m_instancedVao.release();
    m_mainProg->setUniformValue(m_mainU.instanced, 0);
}

void GPUViewport::initFboQuad()
{
    m_fboVao.create();
    m_fboVao.bind();
    m_fboVbo.create();
    m_fboVbo.bind();
    m_fboVbo.allocate(quadVertices, sizeof(quadVertices));
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex), nullptr);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          reinterpret_cast<const void*>(offsetof(QuadVertex, u)));
    glEnableVertexAttribArray(1);
    m_fboVao.release();
}

void GPUViewport::updateFboQuad(int vpW, int vpH)
{
    float hx = static_cast<float>(vpW) / vpH;
    QuadVertex fboQuad[4];
    for (int i = 0; i < 4; ++i) {
        fboQuad[i].x = quadVertices[i].x;
        fboQuad[i].y = quadVertices[i].y * hx;
        fboQuad[i].u = (quadVertices[i].x * 0.5f + 0.5f) * vpW;
        fboQuad[i].v = (quadVertices[i].y * 0.5f + 0.5f) * vpH;
    }
    m_fboVao.bind();
    m_fboVbo.bind();
    m_fboVbo.write(0, fboQuad, sizeof(fboQuad));
    m_fboVao.release();
}

void GPUViewport::initCheckerTexture()
{
    static const unsigned char checkerData[] = {
        255,255,255,255,  192,192,192,255,
        192,192,192,255,  255,255,255,255,
    };
    glGenTextures(1, &m_checkerTexture);
    glBindTexture(GL_TEXTURE_2D, m_checkerTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 2, 2, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, checkerData);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

bool GPUViewport::ensureBlendTarget(int viewportW, int viewportH)
{
    if (viewportW <= 0 || viewportH <= 0) return false;

    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    if (m_blendFbo == 0)
        glGenFramebuffers(1, &m_blendFbo);
    if (m_blendTex == 0)
        glGenTextures(1, &m_blendTex);

    if (m_blendFbo == 0 || m_blendTex == 0) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
        return false;
    }

    if (viewportW != m_blendTexW || viewportH != m_blendTexH) {
        glBindTexture(GL_TEXTURE_2D, m_blendTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewportW, viewportH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, m_blendFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_blendTex, 0);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            qWarning("Blend FBO incomplete: %x", status);
            m_blendTexW = 0;
            m_blendTexH = 0;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
            return false;
        }

        m_blendTexW = viewportW;
        m_blendTexH = viewportH;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    return true;
}

// ── VAO binding helpers ───────────────────────────────────────

void GPUViewport::bindMainVao()
{
    m_vao.bind();
}

void GPUViewport::unbindMainVao()
{
    m_vao.release();
}

void GPUViewport::resetUniformCache()
{
    m_uc = UniformCache{};
}

void GPUViewport::setMainUniforms(const QMatrix4x4& mvp, float opacity,
                                   bool hasMask, float maskDensity)
{
    m_mainProg->setUniformValue(m_mainU.texture, 0);
    m_mainProg->setUniformValue(m_mainU.maskTexture, 1);
    setMainSourcePremultiplied(false);
    setMainAlphaWeightedSampling(true);

    float hm = hasMask ? 1.0f : 0.0f;
    if (m_uc.mainTransform != mvp) {
        m_mainProg->setUniformValue(m_mainU.transform, mvp);
        m_uc.mainTransform = mvp;
    }
    if (qFuzzyCompare(m_uc.mainOpacity, opacity)) {
        // skip
    } else {
        m_mainProg->setUniformValue(m_mainU.opacity, opacity);
        m_uc.mainOpacity = opacity;
    }
    if (qFuzzyCompare(1.0f + m_uc.mainHasMask, 1.0f + hm)) {
        // skip
    } else {
        m_mainProg->setUniformValue(m_mainU.hasMask, hm);
        m_uc.mainHasMask = hm;
    }
    if (m_uc.mainMaskDensity != maskDensity) {
        m_mainProg->setUniformValue(m_mainU.maskDensity, maskDensity);
        m_uc.mainMaskDensity = maskDensity;
    }
}

// ── Display colour-management (final 3D-LUT stage) ─────────────

bool GPUViewport::ensureSceneTarget(int viewportW, int viewportH)
{
    if (viewportW <= 0 || viewportH <= 0) return false;

    GLint prevDrawFbo = 0, prevReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    if (m_sceneFbo == 0) glGenFramebuffers(1, &m_sceneFbo);
    if (m_sceneTex == 0) glGenTextures(1, &m_sceneTex);
    if (m_sceneFbo == 0 || m_sceneTex == 0) {
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
        return false;
    }

    if (viewportW != m_sceneTexW || viewportH != m_sceneTexH) {
        glBindTexture(GL_TEXTURE_2D, m_sceneTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewportW, viewportH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_sceneTex, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            qWarning("Scene FBO incomplete: %x", status);
            m_sceneTexW = 0;
            m_sceneTexH = 0;
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
            return false;
        }
        m_sceneTexW = viewportW;
        m_sceneTexH = viewportH;
    }

    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    return true;
}

bool GPUViewport::ensureDisplayLut(const RenderParams& p)
{
    if (!p.doc) return false;

    auto& cms = ColorManagementService::instance();
    const ColorProfile docProfile = p.doc->colorProfile();
    const SoftProofSettings proof = p.doc->softProofSettings();

    // The monitor profile is still a fallback (sRGB) until the platform
    // detection of Etapa 6 lands; the service handles the wiring regardless.
    static DisplayProfileService s_displayService;
    const DisplayColorContext display =
        s_displayService.currentDisplayContextForWindow(0);

    const quint64 token = colorDisplayToken(
        docProfile, proof, display, cms.settings().enableDisplayColorManagement);

    if (m_displayLutInitialized && token == m_displayLutToken)
        return !m_displayLutIdentity;

    // Display state changed (profile assigned/converted, soft proof, monitor
    // profile, or color settings). The cached final frame (RenderCache) is keyed
    // on the composition + per-doc displayGeneration; a monitor/settings change
    // doesn't bump that, so drop the cached frame here too. Skipped on first init.
    if (m_displayLutInitialized)
        m_renderCache.markInvalid();
    m_displayLutToken = token;
    m_displayLutInitialized = true;

    const DisplayLut lut = cms.buildDisplayLut(docProfile, proof, display);
    m_displayLutIdentity = lut.identity;
    if (lut.identity)
        return false;

    const int n = lut.size;
    if (n < 2 || lut.rgb.size() != static_cast<size_t>(n) * n * n * 3) {
        m_displayLutIdentity = true; // malformed → identity, never crash
        return false;
    }

    if (m_displayLutTex == 0)
        glGenTextures(1, &m_displayLutTex);
    auto* gl3 = QOpenGLContext::currentContext()
                    ? QOpenGLContext::currentContext()->extraFunctions() : nullptr;
    if (!gl3 || m_displayLutTex == 0) {
        m_displayLutIdentity = true;
        return false;
    }
    glBindTexture(GL_TEXTURE_3D, m_displayLutTex);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    gl3->glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB8, n, n, n, 0,
                      GL_RGB, GL_UNSIGNED_BYTE, lut.rgb.data());
    glBindTexture(GL_TEXTURE_3D, 0);
    return true;
}

bool GPUViewport::beginManagedComposite(const RenderParams& p)
{
    Q_UNUSED(p);
    GLint vp[4] = {0, 0, 0, 0};
    glGetIntegerv(GL_VIEWPORT, vp);
    if (vp[2] <= 0 || vp[3] <= 0) return false;
    if (!ensureSceneTarget(vp[2], vp[3])) return false;

    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &m_managedPrevFbo);
    for (int i = 0; i < 4; ++i) m_managedVp[i] = vp[i];

    glBindFramebuffer(GL_FRAMEBUFFER, m_sceneFbo);
    m_compositeBaseFbo = m_sceneFbo; // grouped layers composite back into the scene
    glViewport(0, 0, vp[2], vp[3]);

    // Clear the whole scene buffer to transparent (ignore the canvas scissor so
    // no stale pixels from a previous pan/zoom survive outside the new content).
    const GLboolean hadScissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (hadScissor) glEnable(GL_SCISSOR_TEST);
    return true;
}

void GPUViewport::endManagedComposite(const RenderParams& p)
{
    Q_UNUSED(p);
    m_compositeBaseFbo = 0; // base target back to screen for subsequent frames
    // Back to the screen (the QOpenGLWidget's own FBO) and its viewport.
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(m_managedPrevFbo));
    glViewport(m_managedVp[0], m_managedVp[1], m_managedVp[2], m_managedVp[3]);

    // Straight-alpha source-over (same as every other layer): transparent areas
    // of the scene reveal the checkerboard drawn in renderCanvasDecorations.
    applyFixedBlend(static_cast<int>(BlendMode::Normal));

    m_displayProg->bind();
    m_displayProg->setUniformValue(m_displayU.scene, 0);
    m_displayProg->setUniformValue(m_displayU.lut, 1);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_sceneTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, m_displayLutTex);

    m_fboVao.bind();
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_fboVao.release();

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    m_displayProg->release();
    // Restore the state the overlay/cache code expects after the composite.
    m_mainProg->bind();
    m_vao.bind();
}

void GPUViewport::setMainSourcePremultiplied(bool premultiplied)
{
    const float value = premultiplied ? 1.0f : 0.0f;
    if (qFuzzyCompare(1.0f + m_uc.mainSourcePremultiplied, 1.0f + value))
        return;
    m_mainProg->setUniformValue(m_mainU.sourcePremultiplied, value);
    m_uc.mainSourcePremultiplied = value;
}

void GPUViewport::setMainAlphaWeightedSampling(bool enabled)
{
    const float value = enabled ? 1.0f : 0.0f;
    if (qFuzzyCompare(1.0f + m_uc.mainAlphaWeightedSampling, 1.0f + value))
        return;
    m_mainProg->setUniformValue(m_mainU.alphaWeightedSampling, value);
    m_uc.mainAlphaWeightedSampling = value;
}

void GPUViewport::setMainTexture(GLenum unit, GLuint tex)
{
    glActiveTexture(unit);
    glBindTexture(GL_TEXTURE_2D, tex);
}

void GPUViewport::setUVUniforms(const QVector2D& scale, const QVector2D& offset)
{
    if (m_uc.mainUvScale != scale) {
        m_mainProg->setUniformValue(m_mainU.uvScale, scale);
        m_uc.mainUvScale = scale;
    }
    if (m_uc.mainUvOffset != offset) {
        m_mainProg->setUniformValue(m_mainU.uvOffset, offset);
        m_uc.mainUvOffset = offset;
    }
    setMaskUVUniforms(scale, offset);
}

void GPUViewport::setMaskUVUniforms(const QVector2D& scale, const QVector2D& offset)
{
    if (m_uc.mainMaskUvScale != scale) {
        m_mainProg->setUniformValue(m_mainU.maskUvScale, scale);
        m_uc.mainMaskUvScale = scale;
    }
    if (m_uc.mainMaskUvOffset != offset) {
        m_mainProg->setUniformValue(m_mainU.maskUvOffset, offset);
        m_uc.mainMaskUvOffset = offset;
    }
}

void GPUViewport::drawShaderBlend(GLuint srcTexture,
                                  GLuint maskTexture,
                                  const QMatrix4x4& mvp,
                                  int blendMode,
                                  float opacity,
                                  bool hasMask,
                                  float maskDensity,
                                  int viewportW,
                                  int viewportH,
                                  const QVector2D& maskUvScale,
                                  const QVector2D& maskUvOffset)
{
    auto drawNormalFallback = [&]() {
        applyFixedBlend(static_cast<int>(BlendMode::Normal));
        m_mainProg->bind();
        m_vao.bind();
        setMainUniforms(mvp, opacity, hasMask, maskDensity);
        setMainTexture(GL_TEXTURE0, srcTexture);
        setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
        setMaskUVUniforms(maskUvScale, maskUvOffset);
        if (hasMask) {
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, maskTexture);
        }
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    };

    if (!m_blendProg || !m_blendProg->isLinked()) {
        drawNormalFallback();
        return;
    }

    auto* blendGl = QOpenGLContext::currentContext()
                        ? QOpenGLContext::currentContext()->extraFunctions()
                        : nullptr;
    if (!blendGl || !ensureBlendTarget(viewportW, viewportH)) {
        drawNormalFallback();
        return;
    }

    GLint curDrawFbo = 0;
    GLint curReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &curReadFbo);

    // glBlitFramebuffer is clipped by the scissor test on the draw FBO. The
    // canvas scissor (the document rect, smaller than the window) is still
    // enabled here, so leaving it on would copy only the central document
    // rectangle into m_blendTex — the rest stays uninitialised, which shows up
    // as a transparent border once the layer is dragged past the canvas bounds.
    // The guard disables scissor for this full-target dst copy and restores it.
    {
        ScopedScissorDisable noScissor;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, curDrawFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blendFbo);
        blendGl->glBlitFramebuffer(0, 0, viewportW, viewportH,
                                    0, 0, viewportW, viewportH,
                                    GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, curDrawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, curReadFbo);
    }

    glDisable(GL_BLEND);
    m_blendProg->bind();
    m_vao.bind();

    m_blendProg->setUniformValue(m_blendU.transform, mvp);
    m_blendProg->setUniformValue(m_blendU.srcTexture, 0);
    m_blendProg->setUniformValue(m_blendU.dstTexture, 1);
    m_blendProg->setUniformValue(m_blendU.maskTexture, 2);
    m_blendProg->setUniformValue(m_blendU.viewportSize,
                                 static_cast<float>(viewportW),
                                 static_cast<float>(viewportH));
    m_blendProg->setUniformValue(m_blendU.blendMode, blendModeMap(blendMode));
    m_blendProg->setUniformValue(m_blendU.opacity, opacity);
    m_blendProg->setUniformValue(m_blendU.hasMask, hasMask ? 1.0f : 0.0f);
    m_blendProg->setUniformValue(m_blendU.maskDensity, maskDensity);
    m_blendProg->setUniformValue(m_blendU.maskUvScale, maskUvScale);
    m_blendProg->setUniformValue(m_blendU.maskUvOffset, maskUvOffset);
    m_blendProg->setUniformValue(m_blendU.srcPremultiplied, 0.0f);
    m_blendProg->setUniformValue(m_blendU.dstPremultiplied, 1.0f);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, srcTexture);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, m_blendTex);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, hasMask ? maskTexture : 0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_vao.release();
    m_blendProg->release();
    glEnable(GL_BLEND);
    m_mainProg->bind();
    m_vao.bind();
}

void GPUViewport::drawAdjustmentPass(int adjustmentType,
                                     float opacity,
                                     GLuint maskTexture,
                                     bool hasMask,
                                     float maskDensity,
                                     const QVector2D& maskUvScale,
                                     const QVector2D& maskUvOffset,
                                     const QMatrix4x4& mvp,
                                     int viewportW,
                                     int viewportH,
                                     bool preserveLuminosity,
                                     const unsigned char* curveLutRgba,
                                     const float* hsHue,
                                     const float* hsSat,
                                     const float* hsLight,
                                     bool colorize,
                                     const float* hsBandRange,
                                     const QColor& solidColor,
                                     int solidBlendMode)
{
    if (adjustmentType < 0 || opacity <= 0.0f)
        return;
    if (!m_adjustProg || !m_adjustProg->isLinked())
        return;

    auto* extraGl = QOpenGLContext::currentContext()
                        ? QOpenGLContext::currentContext()->extraFunctions()
                        : nullptr;
    if (!extraGl || viewportW <= 0 || viewportH <= 0
        || !ensureBlendTarget(viewportW, viewportH))
        return;

    GLint curDrawFbo = 0;
    GLint curReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &curReadFbo);

    // Snapshot the current target into the blend scratch texture so the quad
    // below can sample the backdrop it is replacing. Scissor must be off for
    // the full-target copy (see drawShaderBlend).
    {
        ScopedScissorDisable noScissor;
        glBindFramebuffer(GL_READ_FRAMEBUFFER, curDrawFbo);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blendFbo);
        extraGl->glBlitFramebuffer(0, 0, viewportW, viewportH,
                                   0, 0, viewportW, viewportH,
                                   GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, curDrawFbo);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, curReadFbo);
    }

    // Replace-pass: the shader outputs the final mix, no GL blending.
    glDisable(GL_BLEND);
    m_adjustProg->bind();
    m_vao.bind();

    m_adjustProg->setUniformValue(m_adjustU.transform, mvp);
    m_adjustProg->setUniformValue(m_adjustU.dstTexture, 0);
    m_adjustProg->setUniformValue(m_adjustU.maskTexture, 1);
    m_adjustProg->setUniformValue(m_adjustU.curveLut, 2);
    m_adjustProg->setUniformValue(m_adjustU.viewportSize,
                                  static_cast<float>(viewportW),
                                  static_cast<float>(viewportH));
    m_adjustProg->setUniformValue(m_adjustU.opacity, opacity);
    m_adjustProg->setUniformValue(m_adjustU.hasMask, hasMask ? 1.0f : 0.0f);
    m_adjustProg->setUniformValue(m_adjustU.maskDensity, maskDensity);
    m_adjustProg->setUniformValue(m_adjustU.adjustmentType, adjustmentType);
    m_adjustProg->setUniformValue(m_adjustU.preserveLuminosity,
                                  preserveLuminosity ? 1.0f : 0.0f);
    m_adjustProg->setUniformValue(m_adjustU.maskUvScale, maskUvScale);
    m_adjustProg->setUniformValue(m_adjustU.maskUvOffset, maskUvOffset);

    // Solid Color fill colour (straight rgb + alpha in 0..1). Only read by the
    // uAdjustmentType == 4 branch; harmless for the others.
    {
        const QColor sc = solidColor.isValid() ? solidColor : QColor(Qt::white);
        m_adjustProg->setUniformValue(m_adjustU.solidColor,
                                      sc.redF(), sc.greenF(), sc.blueF(), sc.alphaF());
        m_adjustProg->setUniformValue(m_adjustU.solidBlendMode, solidBlendMode);
    }

    // Hue/Saturation per-range sliders (7 floats each) + global Colorize flag.
    m_adjustProg->setUniformValue(m_adjustU.colorize, colorize ? 1.0f : 0.0f);
    if (hsHue && hsSat && hsLight) {
        if (m_adjustU.hsHue >= 0)
            m_adjustProg->setUniformValueArray(m_adjustU.hsHue, hsHue, 7, 1);
        if (m_adjustU.hsSat >= 0)
            m_adjustProg->setUniformValueArray(m_adjustU.hsSat, hsSat, 7, 1);
        if (m_adjustU.hsLight >= 0)
            m_adjustProg->setUniformValueArray(m_adjustU.hsLight, hsLight, 7, 1);
    }
    // Editable per-band hue geometry: 6 × vec4 (outerStart, innerStart,
    // innerEnd, outerEnd). Mirrors HueRange::weight() in the shader.
    if (hsBandRange && m_adjustU.hsBandRange >= 0)
        m_adjustProg->setUniformValueArray(m_adjustU.hsBandRange, hsBandRange, 6, 4);

    // Upload the per-channel curve LUT (256×1 RGBA8) for the curves adjustment.
    if (curveLutRgba) {
        if (m_curveLutTex == 0) {
            glGenTextures(1, &m_curveLutTex);
            glBindTexture(GL_TEXTURE_2D, m_curveLutTex);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
        glBindTexture(GL_TEXTURE_2D, m_curveLutTex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, curveLutRgba);
    }

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_blendTex);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, hasMask ? maskTexture : 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, m_curveLutTex);
    glActiveTexture(GL_TEXTURE0);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_vao.release();
    m_adjustProg->release();
    glEnable(GL_BLEND);
    m_mainProg->bind();
    m_vao.bind();
}

// ── Group FBO ──────────────────────────────────────────────────

void GPUViewport::pushGroupFbo(int vpW, int vpH)
{
    unsigned int fbo = 0, tex = 0;
    // Reuse a pooled canvas-sized FBO when one matches; purge stale sizes so a
    // document resize doesn't leave dead full-canvas textures behind. The pool
    // exists because the live compositor pushes a group FBO every interactive
    // frame — allocating and destroying a full-canvas texture per frame is the
    // expensive part, not rendering into it.
    for (size_t i = 0; i < m_groupFboPool.size();) {
        auto& e = m_groupFboPool[i];
        if (e.w == vpW && e.h == vpH) {
            fbo = e.fbo;
            tex = e.tex;
            m_groupFboPool.erase(m_groupFboPool.begin()
                                 + static_cast<std::ptrdiff_t>(i));
            break;
        }
        glDeleteFramebuffers(1, &e.fbo);
        glDeleteTextures(1, &e.tex);
        m_groupFboPool.erase(m_groupFboPool.begin()
                             + static_cast<std::ptrdiff_t>(i));
    }

    if (fbo == 0) {
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vpW, vpH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenFramebuffers(1, &fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, tex, 0);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE) {
            qWarning("Group FBO incomplete: %x", status);
            glDeleteFramebuffers(1, &fbo);
            glDeleteTextures(1, &tex);
            return;
        }
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    }
    // Remember the viewport and scissor active before we redirect rendering into
    // the FBO so popGroupFbo can restore them. The FBO is canvas-sized while the
    // composite-back target (screen / ancestor FBO) may differ, and the active
    // scissor is in screen-window pixels — leaving it enabled would clip the
    // canvas-sized FBO to a central rectangle. Disable it for the FBO render and
    // restore it on pop (where the composite-back should be canvas-clipped).
    GLint savedVp[4] = {0, 0, vpW, vpH};
    glGetIntegerv(GL_VIEWPORT, savedVp);
    GLint savedScissor[4] = {0, 0, vpW, vpH};
    glGetIntegerv(GL_SCISSOR_BOX, savedScissor);
    GLboolean scissorOn = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_SCISSOR_TEST);

    glViewport(0, 0, vpW, vpH);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    m_groupFboStack.push_back(
        {fbo, tex, vpW, vpH,
         {savedVp[0], savedVp[1], savedVp[2], savedVp[3]},
         {savedScissor[0], savedScissor[1], savedScissor[2], savedScissor[3]},
         scissorOn});

}

void GPUViewport::popGroupFbo(LayerTreeNode* groupNode,
                               const QPointF& canvasHalfExtents,
                               const QMatrix4x4& viewMvp,
                               bool editingMask)
{
    if (m_groupFboStack.empty()) return;
    auto entry = m_groupFboStack.back();
    m_groupFboStack.pop_back();

    // Restore parent FBO. At the top level this is m_compositeBaseFbo — 0
    // (screen) normally, or the scene FBO while a managed display composite is
    // active, so a grouped document still composites into the color-managed
    // scene buffer rather than straight to screen.
    if (!m_groupFboStack.empty())
        glBindFramebuffer(GL_FRAMEBUFFER, m_groupFboStack.back().fbo);
    else
        glBindFramebuffer(GL_FRAMEBUFFER, m_compositeBaseFbo);

    // Restore the viewport and scissor that were active when this group was
    // pushed (the screen at top level, or the parent group FBO when nested) — not
    // the canvas-sized FBO state — so the composite-back is positioned and
    // canvas-clipped correctly.
    glViewport(entry.vp[0], entry.vp[1], entry.vp[2], entry.vp[3]);
    glScissor(entry.scissorBox[0], entry.scissorBox[1],
              entry.scissorBox[2], entry.scissorBox[3]);
    if (entry.scissorEnabled)
        glEnable(GL_SCISSOR_TEST);
    else
        glDisable(GL_SCISSOR_TEST);

    // Compute the composite-back MVP. The group FBO holds its children in pure
    // canvas-NDC (filling [-1,1]), exactly like the CPU projection texture, so
    // it is mapped back the same way drawProjection maps that texture:
    //   - top level  → viewMvp * scale(hx,hy)  (apply the canvas footprint + pan/zoom)
    //   - nested      → identity                (the parent group FBO is also
    //                                            canvas-NDC, so it is a 1:1 copy)
    const bool nested = !m_groupFboStack.empty();
    QMatrix4x4 mvp;
    if (!nested) {
        mvp = viewMvp;
        mvp.scale(static_cast<float>(canvasHalfExtents.x()),
                  static_cast<float>(canvasHalfExtents.y()));
    }

    if (needsShaderBlend(static_cast<int>(groupNode->blendMode))) {
        // Two-pass blend: copy the current target (dst) into m_blendTex, then run
        // the blend shader sampling src = group FBO, dst = m_blendTex. All sizing
        // uses the restored target viewport (entry.vp) — not the canvas-sized FBO —
        // so the dst copy and the shader's gl_FragCoord/uViewportSize normalisation
        // line up with where we actually draw.
        const GLint tw = entry.vp[2], th = entry.vp[3];
        const GLint tx = entry.vp[0], ty = entry.vp[1];
        auto* blendGl = QOpenGLContext::currentContext()
                            ? QOpenGLContext::currentContext()->extraFunctions()
                            : nullptr;
        bool shaderBlendDrawn = false;
        if (blendGl && tw > 0 && th > 0 && ensureBlendTarget(tw, th)) {
            GLint curDrawFbo;
            GLint curReadFbo;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &curDrawFbo);
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &curReadFbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, curDrawFbo);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_blendFbo);
            blendGl->glBlitFramebuffer(tx, ty, tx + tw, ty + th, 0, 0, tw, th,
                                       GL_COLOR_BUFFER_BIT, GL_NEAREST);
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, curDrawFbo);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, curReadFbo);

            glDisable(GL_BLEND);
            m_blendProg->bind();

            // Deterministic replay quad with FBO-convention UVs (render target
            // row 0 = bottom): the top vertex (y=+1) maps to v=1 so the group FBO
            // is sampled upright, matching the fixedNormal branch's V-flip. The
            // blend shader reads `uv` directly (no UV-scale uniform), so the
            // orientation must live in the geometry. m_fboVbo is shared scratch —
            // restore the default quad afterwards.
            const QuadVertex replayQuad[4] = {
                { -1.0f, -1.0f, 0.0f, 0.0f },
                {  1.0f, -1.0f, 1.0f, 0.0f },
                { -1.0f,  1.0f, 0.0f, 1.0f },
                {  1.0f,  1.0f, 1.0f, 1.0f },
            };
            m_fboVao.bind();
            m_fboVbo.bind();
            glBufferData(GL_ARRAY_BUFFER, sizeof(replayQuad), replayQuad, GL_DYNAMIC_DRAW);

            m_blendProg->setUniformValue(m_blendU.transform, mvp);
            m_blendProg->setUniformValue(m_blendU.srcTexture, 0);
            m_blendProg->setUniformValue(m_blendU.dstTexture, 1);
            m_blendProg->setUniformValue(m_blendU.viewportSize,
                                         static_cast<float>(tw),
                                         static_cast<float>(th));
            m_blendProg->setUniformValue(m_blendU.blendMode,
                                         blendModeMap(static_cast<int>(groupNode->blendMode)));
            m_blendProg->setUniformValue(m_blendU.opacity, groupNode->opacity);
            m_blendProg->setUniformValue(m_blendU.hasMask, 0.0f);
            m_blendProg->setUniformValue(m_blendU.maskDensity, 0.0f);
            m_blendProg->setUniformValue(m_blendU.maskUvScale, QVector2D(1.0f, 1.0f));
            m_blendProg->setUniformValue(m_blendU.maskUvOffset, QVector2D(0.0f, 0.0f));
            m_blendProg->setUniformValue(m_blendU.srcPremultiplied, 1.0f);
            m_blendProg->setUniformValue(m_blendU.dstPremultiplied, 1.0f);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, entry.tex);
            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_blendTex);

            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_DYNAMIC_DRAW);
            m_fboVao.release();
            m_blendProg->release();
            glEnable(GL_BLEND);
            m_mainProg->bind();
            m_vao.bind();
            shaderBlendDrawn = true;
        }

        if (!shaderBlendDrawn) {
            // Fallback (blend target unavailable): plain Normal composite. Same
            // V-flip as the fixedNormal branch so the group is still upright.
            applyFixedBlend(static_cast<int>(BlendMode::Normal));
            m_mainProg->bind();
            m_vao.bind();
            m_mainProg->setUniformValue(m_mainU.transform, mvp);
            m_uc.mainTransform = mvp; // keep cache coherent (see renderCanvasDecorations)
            m_mainProg->setUniformValue(m_mainU.texture, 0);
            m_mainProg->setUniformValue(m_mainU.opacity, groupNode->opacity);
            m_mainProg->setUniformValue(m_mainU.hasMask, 0.0f);
            m_mainProg->setUniformValue(m_mainU.maskDensity, 0.0f);
            m_mainProg->setUniformValue(m_mainU.instanced, 0);
            setMainSourcePremultiplied(true);
            setMainAlphaWeightedSampling(true);
            setUVUniforms(QVector2D(1.0f, -1.0f), QVector2D(0.0f, 1.0f));
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, entry.tex);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            setMainSourcePremultiplied(false);
            setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
        }
    } else {
        // Normal blend: draw with applyBlendMode
        applyFixedBlend(static_cast<int>(groupNode->blendMode));
        m_mainProg->bind();
        m_vao.bind();
        m_mainProg->setUniformValue(m_mainU.transform, mvp);
        m_uc.mainTransform = mvp; // keep cache coherent (see renderCanvasDecorations)
        m_mainProg->setUniformValue(m_mainU.texture, 0);
        m_mainProg->setUniformValue(m_mainU.opacity, groupNode->opacity);
        m_mainProg->setUniformValue(m_mainU.hasMask, 0.0f);
        m_mainProg->setUniformValue(m_mainU.instanced, 0);
        setMainSourcePremultiplied(true);
        setMainAlphaWeightedSampling(true);
        // The group FBO is an OpenGL render target (row 0 = bottom), but the
        // shared quad's UVs assume an uploaded image (row 0 = top). Flip V while
        // sampling so the composed group is not drawn upside-down (which, because
        // the canvas content is not vertically centred in the buffer, also showed
        // up as a vertical offset). Restored to identity afterwards so later
        // draws are unaffected.
        setUVUniforms(QVector2D(1.0f, -1.0f), QVector2D(0.0f, 1.0f));
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, entry.tex);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        setMainSourcePremultiplied(false);
        setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));

        if (editingMask) {
            // Not typically needed for groups, but kept for consistency
        }
    }

    // Return the FBO to the pool for the next frame's push instead of paying a
    // full-canvas texture destroy/realloc per interactive frame. A small cap
    // (covering the top-level isolation plus nested groups) bounds VRAM.
    static constexpr size_t kGroupFboPoolCap = 3;
    if (m_groupFboPool.size() < kGroupFboPoolCap) {
        m_groupFboPool.push_back({entry.fbo, entry.tex, entry.texW, entry.texH});
    } else {
        glDeleteFramebuffers(1, &entry.fbo);
        glDeleteTextures(1, &entry.tex);
    }
}

// ── Canvas decorations ─────────────────────────────────────────

void GPUViewport::renderCanvasDecorations(const RenderParams& p,
                                           const QMatrix4x4& vm)
{
    if (!m_solidProg || !m_mainProg || !m_checkerTexture) return;
    if (!p.doc || p.doc->size.isNull() || p.doc->size.isEmpty()) return;

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();
    QMatrix4x4 sm = vm;
    sm.scale(hx, hy);

    // Shadow
    m_solidProg->bind();
    m_solidProg->setUniformValue(m_solidU.transform, sm);
    QMatrix4x4 shadowMvp = sm;
    shadowMvp.translate(0.006f, -0.006f);
    m_solidProg->setUniformValue(m_solidU.transform, shadowMvp);
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.0f, 0.0f, 0.3f);
    m_vao.bind();
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBlendEquation(GL_FUNC_ADD);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Checkerboard: compact, screen-space squares like professional editors.
    if (m_checkerTexture) {
        float checkerPx = 8.0f;
        float zoom = p.doc ? std::max(0.01f, p.doc->zoom) : 1.0f;
        float sx = hx * zoom * p.viewportW / (2.0f * checkerPx);
        float sy = hy * zoom * p.viewportH / (2.0f * checkerPx);
        m_mainProg->bind();
        m_mainProg->setUniformValue(m_mainU.transform, sm);
        // Keep the uniform cache coherent with this direct uTransform write.
        // m_uc.mainTransform defaults to identity (a real, common MVP) rather
        // than an impossible sentinel like the other cached uniforms, so if it
        // is left stale here, the next setMainUniforms() whose MVP is identity
        // — e.g. a canvas-aligned, untransformed layer drawn into a group FBO —
        // is skipped and silently inherits sm, shrinking that layer to the
        // canvas footprint with a transparent, squished border.
        m_uc.mainTransform = sm;
        m_mainProg->setUniformValue(m_mainU.texture, 0);
        m_mainProg->setUniformValue(m_mainU.opacity, 1.0f);
        m_mainProg->setUniformValue(m_mainU.hasMask, 0.0f);
        setMainSourcePremultiplied(false);
        setMainAlphaWeightedSampling(false);
        m_mainProg->setUniformValue(m_mainU.uvScale, sx, sy);
        m_mainProg->setUniformValue(m_mainU.uvOffset, 0.0f, 0.0f);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_checkerTexture);
        m_vao.bind();
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        m_mainProg->release();
    }

    // Border
    m_solidProg->bind();
    m_solidProg->setUniformValue(m_solidU.transform, sm);
    m_solidProg->setUniformValue(m_solidU.color, 0.45f, 0.45f, 0.45f, 1.0f);
    m_fboVao.bind();
    m_fboVbo.bind();
    glBufferData(GL_ARRAY_BUFFER, sizeof(outlineVertices), outlineVertices, GL_STATIC_DRAW);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    m_fboVao.release();
    m_solidProg->release();

    m_vao.bind();
    m_mainProg->bind();
}

// ── Texture / FBO management ───────────────────────────────────

void GPUViewport::setupLayerFBO(Layer* layer)
{
    if (layer->fbo != 0) {
        glDeleteFramebuffers(1, &layer->fbo);
        layer->fbo = 0;
    }
    glGenFramebuffers(1, &layer->fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, layer->fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, layer->textureId, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qWarning("FBO incomplete: %x", status);
        glDeleteFramebuffers(1, &layer->fbo);
        layer->fbo = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GPUViewport::setupMaskFBO(Layer* layer)
{
    if (!layer || layer->maskTextureId == 0) return;
    // This may run mid-render — e.g. a mask re-upload (maskTextureOutdated)
    // happens inside renderNodes while a group FBO is bound (blend-mode path).
    // Restore whatever framebuffer was bound on entry instead of forcing the
    // default (0): unbinding to the screen here let the remaining layer draws
    // land outside the group FBO at the wrong canvas footprint, which showed up
    // as a one-frame offset after a mask brush stroke on a blend-mode document.
    GLint prevFbo = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    if (layer->maskFbo != 0) {
        glDeleteFramebuffers(1, &layer->maskFbo);
        layer->maskFbo = 0;
    }
    glGenFramebuffers(1, &layer->maskFbo);
    glBindFramebuffer(GL_FRAMEBUFFER, layer->maskFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, layer->maskTextureId, 0);
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        qWarning("Mask FBO incomplete: %x", status);
        glDeleteFramebuffers(1, &layer->maskFbo);
        layer->maskFbo = 0;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));

    // Query actual GPU texture dimensions for the log
    GLint tw = 0, th = 0;
    glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &tw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void GPUViewport::uploadMaskTexture(Layer* layer)
{
    if (!layer || layer->maskImage.isNull()) return;
    if (layer->maskTextureId == 0) {
        glGenTextures(1, &layer->maskTextureId);
    }
    glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uploadR8TextureFromImage(layer->maskImage);
    glBindTexture(GL_TEXTURE_2D, 0);
    layer->maskTextureOutdated = false;
    setupMaskFBO(layer);
}

void GPUViewport::syncLayerFromGpu(Layer* layer)
{
    if (!layer || layer->textureId == 0) return;
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    glBindTexture(GL_TEXTURE_2D, layer->textureId);
    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                  layer->cpuImage.bits());
    // In-place readback keeps the QImage cacheKey — invalidate the cached alpha
    // content bounds explicitly so the transform outline re-measures.
    layer->invalidateContentBounds();
}

void GPUViewport::syncLayerMaskFromGpu(Layer* layer)
{
    if (!layer || layer->maskTextureId == 0) return;
    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
    readR8TextureToImage(layer->maskImage);
}

void GPUViewport::uploadLayerTextureCpu(Layer* layer)
{
    if (!layer || layer->cpuImage.isNull()) return;
    if (layer->fbo != 0) {
        glDeleteFramebuffers(1, &layer->fbo);
        layer->fbo = 0;
    }
    if (layer->textureId != 0) {
        glDeleteTextures(1, &layer->textureId);
        layer->textureId = 0;
    }
    const QImage rgba = layer->cpuImage.convertToFormat(QImage::Format_RGBA8888);
    glGenTextures(1, &layer->textureId);
    glBindTexture(GL_TEXTURE_2D, layer->textureId);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                 rgba.width(), rgba.height(),
                 0, GL_RGBA, GL_UNSIGNED_BYTE,
                 rgba.constBits());
    layer->textureOutdated = false;
}

void GPUViewport::syncLayersToGpu(Document* doc, Layer* editingMaskLayer)
{
    initializeOpenGLFunctions();
    if (!doc) return;

    // A full layer sync means the layer stack / pixels were replaced wholesale
    // (project load, tab switch, undo/redo). The cached CPU projection and the
    // captured render frame are keyed on (doc, compositionGeneration, size), and
    // loading a project does NOT bump compositionGeneration — so an earlier
    // projection built while the document was still empty (flatCount == 0, e.g.
    // the first paintGL that runs before roots are populated) would otherwise be
    // considered up-to-date and replayed transparent, hiding every freshly loaded
    // layer until something bumps the generation (toggling a layer's visibility).
    // Drop both caches here so the next frame recomposites from the real layers.
    invalidateProjection();
    m_renderCache.markInvalid();

    auto flat = doc->flatten();
    for (auto* node : flat) {
        if (!node->layer) continue;
        auto* layer = node->layer.get();

        if (node->type == LayerTreeNode::Type::Adjustment) {
            // Adjustment layers have no drawable pixels — only the mask
            // (handled below) is synced; uploading the doc-sized transparent
            // cpuImage would just waste VRAM.
        } else if (layer->rasterStorage.isEnabled()) {
            // Don't upload stale cpuImage; tiles are uploaded by LayerCompositor.
            // Only invalidate every tile when the CPU pixels were replaced
            // wholesale (textureOutdated): incremental edits (brush dabs,
            // undo/redo, color conversion) mark their own tiles dirty, and
            // unconditionally re-flagging the whole layer here forced a full
            // re-upload of every tile on the next live frame — the main reason
            // dab-heavy layers were slow to paint on and move.
            if (layer->textureOutdated) {
                layer->rasterStorage.markAllGpuDirty();
                layer->pendingGpuUpload = true;
                layer->textureOutdated = false;
            }
        } else {
            const bool hasShapeSprite = layer->shapeSpriteRenderable();
            const QImage& uploadImage = hasShapeSprite
                ? layer->shapeCache.image
                : layer->cpuImage;
            if (!uploadImage.isNull()) {
                if (layer->textureId == 0) {
                    glGenTextures(1, &layer->textureId);
                    glBindTexture(GL_TEXTURE_2D, layer->textureId);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                                 uploadImage.width(), uploadImage.height(),
                                 0, GL_RGBA, GL_UNSIGNED_BYTE,
                                 uploadImage.constBits());
                    setupLayerFBO(layer);
                    layer->textureOutdated = false;
                } else if (layer->textureOutdated) {
                    glBindTexture(GL_TEXTURE_2D, layer->textureId);
                    if (layer->tiledSystem) {
                        if (uploadImage.width() > 0 && uploadImage.height() > 0) {
                            QImage rgba = uploadImage.convertToFormat(QImage::Format_RGBA8888);
                            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                                         rgba.width(), rgba.height(),
                                         0, GL_RGBA, GL_UNSIGNED_BYTE,
                                         rgba.constBits());
                            layer->tileManager.init(rgba.width(), rgba.height(),
                                                    layer->tileManager.tileSize());
                        }
                    } else {
                        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                                     uploadImage.width(), uploadImage.height(),
                                     0, GL_RGBA, GL_UNSIGNED_BYTE,
                                     uploadImage.constBits());
                    }
                    setupLayerFBO(layer);
                    // Force mask FBO recreation: the mask was also resized by the same
                    // undo that set textureOutdated. Deleting maskFbo here ensures the
                    // subsequent setupMaskFBO call below rebuilds it at the correct size.
                    if (layer->maskFbo != 0) {
                        glDeleteFramebuffers(1, &layer->maskFbo);
                        layer->maskFbo = 0;
                    }
                    layer->textureOutdated = false;
                }
            }
        }

        // Effects texture (layer effects and/or Single-Layer-Mode adjustments).
        // Skip the per-frame CPU bake for layers whose clipped adjustments are
        // previewed live on the GPU (FBO isolation in LayerCompositor) — that is
        // the whole point of the GPU path. The CPU projection commit still bakes
        // them as the source of truth; this only affects the live preview.
        if (node->usesEffectedPipeline() && !node->clippedAdjustmentsOnGpu()) {
            QImage effected = node->computeEffectedImage();
            if (!effected.isNull()) {
                if (!node->effectedTexture)
                    glGenTextures(1, &node->effectedTexture);
                glBindTexture(GL_TEXTURE_2D, node->effectedTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             effected.width(), effected.height(),
                             0, GL_RGBA, GL_UNSIGNED_BYTE,
                             effected.constBits());
                node->effectedTextureOutdated = false;
            }
        }

        // Styled-base texture for the live GPU clipped-adjustment path on layers
        // that carry styles: the style baked WITHOUT the clipped adjustments, so
        // LayerCompositor isolates it into an FBO and runs drawAdjustmentPass over
        // it (instead of a per-frame computeEffectedImage CPU bake). Uploaded only
        // when outdated — i.e. on a style/pixel/mask change — never per adjustment
        // frame, so the costly style blur runs once and the drag stays on the GPU.
        if (node->clippedAdjustmentsOnGpu() && !node->effects.empty()
            && (node->styledBaseTextureOutdated || node->styledBaseTexture == 0)) {
            QImage styled = node->computeStyledBaseImage();
            if (!styled.isNull()) {
                if (!node->styledBaseTexture)
                    glGenTextures(1, &node->styledBaseTexture);
                glBindTexture(GL_TEXTURE_2D, node->styledBaseTexture);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                             styled.width(), styled.height(),
                             0, GL_RGBA, GL_UNSIGNED_BYTE,
                             styled.constBits());
                node->styledBaseTextureOutdated = false;
            }
        }

        // Mask texture — skip CPU→GPU re-upload while actively editing:
        // the GPU FBO is the source of truth during brush strokes.
        if (layer->maskTextureId != 0 && !layer->maskImage.isNull()
            && layer != editingMaskLayer) {
            glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
            uploadR8TextureFromImage(layer->maskImage);
            layer->maskTextureOutdated = false;
        } else if (!layer->maskImage.isNull() && layer->maskTextureId == 0) {
            glGenTextures(1, &layer->maskTextureId);
            glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            uploadR8TextureFromImage(layer->maskImage);
            layer->maskTextureOutdated = false;
        } else if (layer->maskImage.isNull() && layer->maskFbo != 0) {
            glDeleteFramebuffers(1, &layer->maskFbo);
            layer->maskFbo = 0;
        }
        if (!layer->maskImage.isNull() && layer->maskFbo == 0)
            setupMaskFBO(layer);
    }
}

void GPUViewport::cleanupDocumentLayers(Document* doc)
{
    initializeOpenGLFunctions();
    if (!doc) return;
    auto flat = doc->flatten();
    for (auto* node : flat) {
        if (!node->layer) continue;
        auto* layer = node->layer.get();
        if (layer->textureId) {
            glDeleteTextures(1, &layer->textureId);
            layer->textureId = 0;
        }
        if (layer->fbo) {
            glDeleteFramebuffers(1, &layer->fbo);
            layer->fbo = 0;
        }
        if (layer->maskTextureId) {
            glDeleteTextures(1, &layer->maskTextureId);
            layer->maskTextureId = 0;
        }
        if (layer->maskFbo) {
            glDeleteFramebuffers(1, &layer->maskFbo);
            layer->maskFbo = 0;
        }
        if (node->effectedTexture) {
            glDeleteTextures(1, &node->effectedTexture);
            node->effectedTexture = 0;
        }
        if (node->styledBaseTexture) {
            glDeleteTextures(1, &node->styledBaseTexture);
            node->styledBaseTexture = 0;
        }
        // Clean up LOD textures
        for (int i = 1; i <= 3; ++i) {
            if (layer->lodTextures[i]) {
                glDeleteTextures(1, &layer->lodTextures[i]);
                layer->lodTextures[i] = 0;
            }
        }
    }
}

// ── Tiling (delegated to TileRenderer) ─────────────────────────

void GPUViewport::uploadDirtyTiles(Layer* layer)
{
    m_tileRenderer.uploadDirtyTiles(layer);
}

void GPUViewport::uploadDirtyRasterTiles(Layer* layer)
{
    m_tileRenderer.uploadDirtyRasterTiles(layer);
}

std::vector<TileDraw> GPUViewport::computeTilesForLayer(
    Layer* layer, int docW, int docH,
    const QRectF& viewportPixelDocRect,
    const QMatrix4x4& fullMvp,
    core::RenderScheduler::LOD lod)
{
    return m_tileRenderer.computeTilesForLayer(layer, docW, docH,
                                                viewportPixelDocRect, fullMvp, lod);
}

std::vector<TileDraw> GPUViewport::computeRasterTilesForLayer(
    Layer* layer, int docW, int docH,
    const QRectF& viewportPixelDocRect,
    const QMatrix4x4& layerMvp)
{
    return m_tileRenderer.computeRasterTilesForLayer(layer, docW, docH,
                                                     viewportPixelDocRect,
                                                     layerMvp);
}

unsigned int GPUViewport::lodTextureId(Layer* layer,
                                        core::RenderScheduler::LOD lod)
{
    if (!layer) return 0;
    if (lod == core::RenderScheduler::LOD::Full)
        return layer->textureId;
    return m_mipmapCache.uploadLevel(layer, lod);
}

void GPUViewport::refreshShapeLayerSprites(Document* doc, const RenderParams& p)
{
    if (!doc) return;

    auto flat = doc->flatten();
    for (auto* node : flat) {
        if (!node->layer || !node->layer->isShapeLayer())
            continue;

        auto* layer = node->layer.get();
        auto& cache = layer->shapeCache;

        QTransform accum = node->accumulatedTransform();
        float curSx = std::hypot(static_cast<float>(accum.m11()),
                                  static_cast<float>(accum.m12()));
        float curSy = std::hypot(static_cast<float>(accum.m21()),
                                  static_cast<float>(accum.m22()));

        bool shapeChanged = cache.dirty
            || !cache.shapeSnapshot
            || !ShapeVectorRenderer::sameShapeData(*cache.shapeSnapshot, *layer->shapeData);
        bool zoomChanged = std::abs(cache.zoom - doc->zoom) > 0.0001f;
        bool docChanged = cache.documentSize != doc->size;
        bool transformChanged = cache.transformSnapshot != accum;
        bool scaleChanged = std::abs(cache.scaleX - curSx) > 0.0001f
                         || std::abs(cache.scaleY - curSy) > 0.0001f;

        if (!shapeChanged && !zoomChanged && !docChanged && !transformChanged && !scaleChanged)
            continue;

        auto sprite = ShapeVectorRenderer::renderSprite(
            *layer->shapeData,
            accum,
            doc->size,
            doc->zoom,
            p.canvasHalfExtents,
            p.viewportW,
            p.viewportH);

        if (!sprite.image.isNull()) {
            cache.image = sprite.image;
            cache.spriteTransform = sprite.spriteTransform;
            cache.dirty = false;
            cache.zoom = doc->zoom;
            cache.documentSize = doc->size;
            cache.transformSnapshot = accum;
            cache.scaleX = curSx;
            cache.scaleY = curSy;
            if (!cache.shapeSnapshot)
                cache.shapeSnapshot = std::make_shared<ShapeData>();
            *cache.shapeSnapshot = *layer->shapeData;
            layer->textureOutdated = true;
            if (!node->effects.empty())
                node->invalidateEffects();
        }
    }
}

// ── CPU projection display (Fase B) ────────────────────────────

// Draws the CPU-composited projection texture across the canvas rect with the
// viewport's zoom/pan. This replaces the per-layer GPU compositor for
// non-interactive frames: the projection already contains every layer's
// blend/opacity/mask/effect (see DocumentCompositor). Mirrors how a
// full-canvas, identity-transform layer is drawn, so orientation matches the
// per-layer path exactly.
bool GPUViewport::drawProjection(const RenderParams& p, const QMatrix4x4& viewMvp)
{
    const unsigned int tex = m_projection.update(p.doc);
    if (!tex) return false;

    const float hx = p.canvasHalfExtents.x();
    const float hy = p.canvasHalfExtents.y();
    QMatrix4x4 mvp = viewMvp;
    mvp.scale(hx, hy);

    // Keep the texture state aligned with the live per-layer compositor. The
    // main shader applies alpha-weighted sampling for straight-alpha content, so
    // idle projection frames and live layer frames handle transparent edges the
    // same way.
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Normal/source-over blend so transparent areas reveal the checkerboard.
    applyFixedBlend(static_cast<int>(BlendMode::Normal));
    m_mainProg->bind();
    m_vao.bind();
    m_mainProg->setUniformValue(m_mainU.instanced, 0);
    setMainUniforms(mvp, 1.0f, false, 0.0f);
    setMainTexture(GL_TEXTURE0, tex);
    setUVUniforms(QVector2D(1.0f, 1.0f), QVector2D(0.0f, 0.0f));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    return true;
}

// ── Main render entry point ────────────────────────────────────

void GPUViewport::render(const RenderParams& p)
{
    resetUniformCache();
    initializeOpenGLFunctions();

    auto* t = ThemeManager::instance()->current();
    const QColor canvasBg = t->canvasBackground;
    glClearColor(canvasBg.redF(), canvasBg.greenF(), canvasBg.blueF(), 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);

    if (!p.doc || !m_mainProg || !m_mainProg->isLinked()) return;

    // Clean up selection texture if selection cleared
    if (m_selectMaskTexture &&
        (!p.doc->selection.active() || p.doc->selection.isEmpty())) {
        glDeleteTextures(1, &m_selectMaskTexture);
        m_selectMaskTexture = 0;
        m_selectNeedsUpload = false;
        m_selectUploadRect = QRect();
        m_selectMaskTexSize = QSize();
    }

    // Build view MVP
    m_cachedViewMvp = QMatrix4x4{};
    m_cachedViewMvp.translate(
        static_cast<float>(p.doc->panOffset.x()),
        static_cast<float>(p.doc->panOffset.y()));
    m_cachedViewMvp.scale(p.doc->zoom);

    // Decorations (shadow, checkerboard, border)
    renderCanvasDecorations(p, m_cachedViewMvp);

    // Scissor test — clip to canvas bounds
    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();

    QVector4D corners[4] = {
        m_cachedViewMvp * QVector4D(-hx, -hy, 0, 1),
        m_cachedViewMvp * QVector4D( hx, -hy, 0, 1),
        m_cachedViewMvp * QVector4D(-hx,  hy, 0, 1),
        m_cachedViewMvp * QVector4D( hx,  hy, 0, 1),
    };
    float leftNdc = 1e9f, rightNdc = -1e9f;
    float bottomNdc = 1e9f, topNdc = -1e9f;
    for (auto& c : corners) {
        float nx = c.x() / c.w();
        float ny = c.y() / c.w();
        if (nx < leftNdc)  leftNdc  = nx;
        if (nx > rightNdc) rightNdc = nx;
        if (ny < bottomNdc) bottomNdc = ny;
        if (ny > topNdc)    topNdc    = ny;
    }
    GLint vp[4];
    glGetIntegerv(GL_VIEWPORT, vp);
    int sx = static_cast<int>(std::floor((leftNdc   + 1.0f) * 0.5f * vp[2] + vp[0]));
    int sy = static_cast<int>(std::floor((bottomNdc + 1.0f) * 0.5f * vp[3] + vp[1]));
    int sw = static_cast<int>(std::ceil ((rightNdc  - leftNdc)  * 0.5f * vp[2]));
    int sh = static_cast<int>(std::ceil ((topNdc    - bottomNdc) * 0.5f * vp[3]));

    glEnable(GL_SCISSOR_TEST);
    glScissor(sx, sy, sw, sh);
    glEnable(GL_BLEND);

    m_mainProg->bind();
    m_vao.bind();

    refreshShapeLayerSprites(p.doc, p);

    bool needsLayerSync = false;
    for (auto* node : p.doc->flatten()) {
        if (!node || !node->layer) continue;
        auto* layer = node->layer.get();
        const bool isAdjustment = node->type == LayerTreeNode::Type::Adjustment;
        const bool hasRaster = !layer->cpuImage.isNull();
        const bool hasShapeSprite = layer->isShapeLayer()
            && !layer->shapeCache.image.isNull();
        if (!hasRaster && !hasShapeSprite) continue;

        // Adjustment layers never upload a color texture (see syncLayersToGpu),
        // so only their mask matters here. RasterStorage (dab) layers keep their
        // pixels in per-tile textures and never allocate layer->textureId, so a
        // zero id is their normal steady state — only a genuine CPU-side pixel
        // replacement (textureOutdated) needs a sync. Treating id==0 as
        // "missing" made this loop fire — and run the full syncLayersToGpu
        // (markAllGpuDirty + mask re-uploads + effect re-bakes) — on EVERY
        // frame of any document containing brush dabs.
        if (!isAdjustment && layer->textureOutdated) {
            needsLayerSync = true;
            break;
        }
        if (!isAdjustment && !layer->rasterStorage.isEnabled()
            && layer->textureId == 0) {
            needsLayerSync = true;
            break;
        }
        // A mask created on an already-uploaded layer has CPU data but no GPU
        // texture yet. The projection path doesn't need it, but the per-layer
        // path (mask overlay / mask editing) does — without this, a freshly
        // created mask had maskTextureId == 0 and the rubylith overlay never
        // appeared until some other edit forced an upload.
        if (!layer->maskImage.isNull() && layer->maskTextureId == 0) {
            needsLayerSync = true;
            break;
        }
        // Key the GPU re-upload off effectedTextureOutdated, NOT effectsDirty():
        // a layer-panel thumbnail repaint on the same imageChanged calls
        // computeEffectedImage() and clears effectsDirty() before this runs, so
        // relying on it left the Single-Layer-Mode live preview drawing a stale
        // effectedTexture. effectedTextureOutdated is only cleared by the actual
        // upload above, so the live frame always re-uploads the recomposited bake.
        // Clipped-adjustment layers handled live on the GPU never upload an
        // effectedTexture (see syncLayersToGpu) — don't force a sync (and thus a
        // CPU bake) for them; LayerCompositor isolates them per frame instead.
        if (node->usesEffectedPipeline()
            && !node->clippedAdjustmentsOnGpu()
            && (node->effectedTextureOutdated || node->effectedTexture == 0)) {
            needsLayerSync = true;
            break;
        }
        // Styled host of a live GPU clipped adjustment: ensure the styled-base
        // texture exists / is fresh. styledBaseTextureOutdated is set only on a
        // style/pixel/mask change (NOT on adjustment param drags), so this fires
        // once after such a change and never per drag frame — the drag itself
        // reuses the texture and runs entirely through drawAdjustmentPass.
        if (node->clippedAdjustmentsOnGpu() && !node->effects.empty()
            && (node->styledBaseTextureOutdated || node->styledBaseTexture == 0)) {
            needsLayerSync = true;
            break;
        }
    }
    if (needsLayerSync) {
        Layer* editingMaskLayer = p.editingMask ? p.doc->activeLayer() : nullptr;
        syncLayersToGpu(p.doc, editingMaskLayer);
        m_mainProg->bind();
        m_vao.bind();
    }

    // ── Cache check ─────────────────────────────────────────────
    const bool paintToolActive = p.currentTool == 1   // Brush
                              || p.currentTool == 2   // Eraser
                              || p.currentTool == 11; // Clone Stamp (clone + healing modes)

    bool cacheEligible = !p.liveEdit
                      && !p.editingMask
                      && !p.grayscaleMaskView
                      && !p.hasPreview
                      && !p.showMaskOverlay  // rubylith draws only in the per-layer path
                      && p.currentTool != 0  // Move transform controls mutate independently
                      && p.currentTool != 1  // Brush indicator
                      && p.currentTool != 2  // Eraser indicator
                      && p.currentTool != 3  // Select drag feedback is drawn by CanvasView overlay
                      && p.currentTool != 4  // Zoom mutates camera; avoid caching transitional frames
                      && p.currentTool != 5  // Hand/pan mutates camera
                      && p.currentTool != 6  // Text editing updates the active layer texture live
                      && p.currentTool != 7  // Crop overlay/handles mutate independently of composition
                      && p.currentTool != 8  // Fill bucket has immediate feedback/preview edge cases
                      && p.currentTool != 9  // Eyedropper overlay/sampling path should stay uncached
                      && p.currentTool != 10  // Shape creation preview mutates outside composition cache
                      && p.currentTool != 11; // Clone Stamp (clone + healing) live stroke/overlay mutates like Brush
    bool canReadCache = cacheEligible;
    bool canCaptureCache = cacheEligible
                        && !p.selectDragging
                        && !p.lassoDrawing; // Select may pre-cache while idle, but never during overlay drag.
    bool cacheValid = canReadCache
        && m_renderCache.isValid(p.doc, vp[2], vp[3],
                                 p.doc->zoom, p.doc->panOffset,
                                 p.canvasHalfExtents);

    // Fase B: the CPU compositor (DocumentCompositor) is the single source of
    // truth. For non-interactive frames the GPU just displays its cached
    // projection. Live edits of the active layer (brush stroke, transform/shape
    // drag, text editing) and the mask/preview/grayscale views still use the
    // per-layer GPU compositor below — this is intentional (keeping the active
    // layer on the GPU); the two paths share blend semantics
    // via renderer/BlendRules.hpp. Overlays are always drawn on top afterwards.
    const bool useProjection = m_useProjectionDisplay
                            && !p.liveEdit
                            && !p.editingMask
                            && !p.grayscaleMaskView
                            && !p.hasPreview
                            && !p.showMaskOverlay;

    // Color-managed display (final stage): the document composite — projection
    // OR live per-layer — is rendered into the scene FBO, then resolved to screen
    // through the display LUT, so BOTH paths are color-managed identically (no
    // mid-stroke "pop"). Skipped for the identity case (sRGB on sRGB display,
    // proof off) and for non-document views (mask edit / grayscale / overlay),
    // where it stays the pre-existing direct-to-screen path.
    const bool managedEligible = !p.grayscaleMaskView
                              && !p.showMaskOverlay
                              && !p.editingMask;

    bool projectionShown = false;
    if (cacheValid) {
        // Fast path: draw the cached pre-overlay frame as a full viewport image.
        // The cache stores the already-color-managed frame (captured after the
        // display pass), so it is replayed straight to screen.
        glDisable(GL_SCISSOR_TEST);
        glDisable(GL_BLEND);
        m_renderCache.drawCached(this);
        glEnable(GL_BLEND);
    } else {
        const bool managed = managedEligible
                          && ensureDisplayLut(p)
                          && beginManagedComposite(p);

        if (useProjection && drawProjection(p, m_cachedViewMvp)) {
            // Scissor stays clipped to the canvas; blend stays on so transparent
            // projection pixels reveal the checkerboard drawn underneath.
            projectionShown = true;
        }

        if (!projectionShown) {
            // Per-layer composite path: live edits, mask/preview/grayscale views,
            // or a safety fallback when the projection could not be built (e.g.
            // the document exceeds GL_MAX_TEXTURE_SIZE).
            if (!p.grayscaleMaskView) {
                LayerCompositor compositor;
                compositor.composite(p.doc, m_cachedViewMvp, p.canvasHalfExtents,
                                     p.editingMask, p.grayscaleMaskView,
                                     p.hasPreview, p.previewTexture,
                                     paintToolActive,
                                     p.showMaskOverlay, p.maskOverlayOpacity,
                                     this);
            } else {
                renderGrayscaleMaskView(p, m_cachedViewMvp);
                m_mainProg->bind();
                m_vao.bind();
            }
        }

        // Resolve scene → screen through the display LUT (covers both paths).
        if (managed)
            endManagedComposite(p);

        // Capture to cache for future frames (per-layer path only; the screen now
        // holds the color-managed result). Projection frames keep their own
        // ProjectionCache. Full target, so disable scissor.
        if (!projectionShown && canCaptureCache) {
            GLint currentDrawFbo = 0;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &currentDrawFbo);
            m_renderCache.ensureSize(vp[2], vp[3]);
            {
                ScopedScissorDisable noScissor;
                m_renderCache.captureCurrentFrame(vp[2], vp[3],
                                                  static_cast<unsigned int>(currentDrawFbo));
            }
            m_renderCache.markValid(p.doc, vp[2], vp[3],
                                    p.doc->zoom, p.doc->panOffset,
                                    p.canvasHalfExtents);
        }
    }

    glDisable(GL_SCISSOR_TEST);

    // Overlays (selection, crop, bounding box, text cursor)
    renderSelectionOverlay(p, m_cachedViewMvp);
    renderRubberBand(p, m_cachedViewMvp);

    if (p.boxSelecting)
        renderBoxSelectionOverlay(p, m_cachedViewMvp);

    if (p.currentTool == 7) { // Crop
        if (p.cropActive) {
            renderCropOverlay(p, m_cachedViewMvp);
            renderCropBorder(p, m_cachedViewMvp);
            renderCropGuides(p, m_cachedViewMvp);
            renderCropHandles(p, m_cachedViewMvp);
        }
    }

    // Quick Selection's circular cursor is drawn by the CanvasView
    // BrushPreviewOverlay (same zoom-aware QWidget as the paint brushes), so the
    // GPU circle is no longer used here.

    if ((p.currentTool == 0 || p.currentTool == 6) && p.showTransformControls)
        renderBoundingBox(p, m_cachedViewMvp);

    if (p.textToolState == 2 && p.textEditor && p.textEditor->caretVisible())
        renderTextCursor(p, m_cachedViewMvp);
}
