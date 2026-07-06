#include "RenderCache.hpp"
#include "core/Document.hpp"
#include "renderer/GPUViewport.hpp"

#include <GL/gl.h>
#include <QMatrix4x4>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>

void RenderCache::destroy()
{
    if (m_fbo) {
        auto* ctx = QOpenGLContext::currentContext();
        if (ctx) {
            auto* gl = ctx->extraFunctions();
            if (gl) {
                gl->glDeleteFramebuffers(1, &m_fbo);
                gl->glDeleteTextures(1, &m_texture);
            }
        }
        m_fbo = 0;
        m_texture = 0;
    }
    m_cachedGeneration = 0;
    m_cachedVpW = m_cachedVpH = 0;
}

bool RenderCache::isValid(const Document* doc, int viewportW, int viewportH,
                           float zoom, QPointF panOffset,
                           QPointF canvasHalfExtents) const
{
    if (!m_fbo || !m_texture) return false;
    if (m_cachedVpW != viewportW || m_cachedVpH != viewportH) return false;
    if (m_cachedZoom != zoom) return false;
    if (m_cachedPan != panOffset) return false;
    if (m_cachedCanvasHalfExtents != canvasHalfExtents) return false;
    if (!doc) return false;
    // Key on the display generation too: Assign Profile / Convert / soft proof /
    // monitor changes bump displayGeneration without touching the composition,
    // so the final pre-overlay frame must be re-rendered (the projection below
    // re-derives its display image) instead of replaying the stale colours.
    if (m_cachedDisplayGeneration != doc->displayGeneration) return false;
    return m_cachedGeneration == doc->compositionGeneration;
}

void RenderCache::ensureSize(int vpW, int vpH)
{
    if (m_fbo && m_cachedVpW == vpW && m_cachedVpH == vpH)
        return;

    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    auto* gl = ctx->extraFunctions();
    if (!gl) return;

    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    ::glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    ::glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    if (m_fbo) {
        gl->glDeleteFramebuffers(1, &m_fbo);
        gl->glDeleteTextures(1, &m_texture);
    }

    gl->glGenFramebuffers(1, &m_fbo);
    gl->glGenTextures(1, &m_texture);

    ::glBindTexture(GL_TEXTURE_2D, m_texture);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    ::glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    ::glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                   vpW, vpH, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);

    gl->glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    gl->glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, m_texture, 0);

    m_cachedVpW = vpW;
    m_cachedVpH = vpH;

    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
}

void RenderCache::captureCurrentFrame(int vpW, int vpH, unsigned int sourceFbo)
{
    if (!m_fbo) return;

    auto* ctx = QOpenGLContext::currentContext();
    if (!ctx) return;
    auto* gl = ctx->extraFunctions();
    if (!gl) return;

    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    ::glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    ::glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_fbo);
    gl->glBlitFramebuffer(0, 0, vpW, vpH,
                          0, 0, m_cachedVpW, m_cachedVpH,
                          GL_COLOR_BUFFER_BIT, GL_NEAREST);
    gl->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    gl->glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
}

void RenderCache::drawCached(GPUViewport* gpu)
{
    if (!gpu || !m_texture) return;

    QMatrix4x4 screenMvp;
    gpu->mainProgram()->bind();
    gpu->bindMainVao();
    gpu->setMainUniforms(screenMvp, 1.0f, false, 0.0f);
    gpu->setMainTexture(GL_TEXTURE0, m_texture);
    gpu->setUVUniforms(QVector2D(1.0f, -1.0f), QVector2D(0.0f, 1.0f));
    ::glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void RenderCache::markValid(const Document* doc, int vpW, int vpH,
                             float zoom, QPointF panOffset,
                             QPointF canvasHalfExtents)
{
    m_cachedVpW = vpW;
    m_cachedVpH = vpH;
    m_cachedZoom = zoom;
    m_cachedPan = panOffset;
    m_cachedCanvasHalfExtents = canvasHalfExtents;
    m_cachedGeneration = doc ? doc->compositionGeneration : 0;
    m_cachedDisplayGeneration = doc ? doc->displayGeneration : 0;
}
