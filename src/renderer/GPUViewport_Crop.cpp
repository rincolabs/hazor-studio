#include "GPUViewport.hpp"
#include "core/Document.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/SelectionMask.hpp"
#include "core/LayerEffect.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include "transform/TransformController.hpp"
#include "text/TextRenderer.hpp"
#include "text/TextLayoutEngine.hpp"
#include "text/TextEditorController.hpp"
#include "LayerCompositor.hpp"

#include <QPainter>
#include <algorithm>
#include <cstring>

#include <QOpenGLExtraFunctions>
#include <QOpenGLContext>
#include <QMatrix4x4>
#include <cmath>
#include <array>

// ── Crop rendering ──────────────────────────────────────────────

void GPUViewport::renderCropOverlay(const RenderParams& p,
                                     const QMatrix4x4& vm)
{
    if (!m_solidProg || !m_solidProg->isLinked()) return;
    QRectF cr = p.cropRect;
    float l = std::max(-1.0f, std::min(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float r = std::min( 1.0f, std::max(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float t = std::min( 1.0f, std::max(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));
    float b = std::max(-1.0f, std::min(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));

    m_solidProg->bind();
    m_fboVao.bind();
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.0f, 0.0f, p.cropOverlayOpacity);

    struct { float x, y, w, h; } quads[4] = {
        {-1.0f,  1.0f, 2.0f, 1.0f - t},
        {-1.0f,  b,    2.0f, b + 1.0f},
        {-1.0f,  t,    l + 1.0f, t - b},
        { r,     t,    1.0f - r, t - b},
    };
    for (auto& q : quads) {
        if (q.w <= 0.0f || q.h <= 0.0f) continue;
        float mx = q.x + q.w * 0.5f;
        float my = q.y - q.h * 0.5f;
        QMatrix4x4 qm = vm;
        qm.scale(p.canvasHalfExtents.x(), p.canvasHalfExtents.y());
        qm.translate(mx, my);
        qm.scale(q.w * 0.5f, q.h * 0.5f);
        m_solidProg->setUniformValue(m_solidU.transform, qm);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    m_fboVao.release();
    m_solidProg->release();
}

void GPUViewport::renderCropBorder(const RenderParams& p,
                                    const QMatrix4x4& vm)
{
    if (!m_solidProg || !m_solidProg->isLinked()) return;
    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();

    QRectF cr = p.cropRect;
    float l = std::max(-1.0f, std::min(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float r = std::min( 1.0f, std::max(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float t = std::min( 1.0f, std::max(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));
    float b = std::max(-1.0f, std::min(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));

    QMatrix4x4 mvp = vm;
    mvp.scale(hx, hy);
    float mw = (r - l) * 0.5f, mh = (t - b) * 0.5f;
    float mx = (l + r) * 0.5f, my = (t + b) * 0.5f;
    QMatrix4x4 bm = mvp;
    bm.translate(mx, my);
    bm.scale(mw, mh);

    m_solidProg->bind();
    m_fboVao.bind();
    m_fboVbo.bind();
    m_solidProg->setUniformValue(m_solidU.transform, bm);
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.9f);
    glBufferData(GL_ARRAY_BUFFER, sizeof(outlineVertices), outlineVertices, GL_STATIC_DRAW);
    glLineWidth(2.0f);
    glDrawArrays(GL_LINE_LOOP, 0, 4);
    glLineWidth(1.0f);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    m_fboVao.release();
    m_solidProg->release();
}

void GPUViewport::renderCropHandles(const RenderParams& p,
                                     const QMatrix4x4& vm)
{
    if (!m_solidProg || !m_solidProg->isLinked()) return;
    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();

    QRectF cr = p.cropRect;
    float l = std::max(-1.0f, std::min(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float r = std::min( 1.0f, std::max(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float t = std::min( 1.0f, std::max(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));
    float b = std::max(-1.0f, std::min(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));

    QMatrix4x4 mvp = vm;
    mvp.scale(hx, hy);

    m_solidProg->bind();
    m_fboVao.bind();
    m_fboVbo.bind();

    // Map canvas-NDC → clip-space NDC (zoom-independent screen position)
    auto mapToScreenNdc = [&](QPointF cp) -> QPointF {
        QVector4D v = mvp * QVector4D(static_cast<float>(cp.x()),
                                      static_cast<float>(cp.y()), 0.0f, 1.0f);
        return QPointF(v.x(), v.y());
    };

    const int kSegs = 24;
    const float rX = (2.0f * 5.0f) / std::max(1, p.viewportW);
    const float rY = (2.0f * 5.0f) / std::max(1, p.viewportH);

    auto drawCircleHandle = [&](QPointF sc) {
        float cx = static_cast<float>(sc.x());
        float cy = static_cast<float>(sc.y());

        std::vector<QuadVertex> fill;
        fill.reserve(kSegs + 2);
        fill.push_back({cx, cy, 0, 0});
        for (int i = 0; i <= kSegs; ++i) {
            float a = 6.283185f * i / kSegs;
            fill.push_back({cx + rX * std::cos(a), cy + rY * std::sin(a), 0, 0});
        }
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(fill.size() * sizeof(QuadVertex)),
                     fill.data(), GL_DYNAMIC_DRAW);
        m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.95f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(fill.size()));

        float ir = 0.55f;
        std::vector<QuadVertex> ring;
        ring.reserve(kSegs + 1);
        for (int i = 0; i <= kSegs; ++i) {
            float a = 6.283185f * i / kSegs;
            ring.push_back({cx + rX * ir * std::cos(a), cy + rY * ir * std::sin(a), 0, 0});
        }
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(ring.size() * sizeof(QuadVertex)),
                     ring.data(), GL_DYNAMIC_DRAW);
        m_solidProg->setUniformValue(m_solidU.color, 1.0f, 1.0f, 1.0f, 0.98f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(ring.size()));
    };

    QMatrix4x4 identity;
    m_solidProg->setUniformValue(m_solidU.transform, identity);

    QPointF pts[8] = {
        {l, t}, {r, t}, {r, b}, {l, b},
        {(l+r)*0.5f, t}, {r, (t+b)*0.5f}, {(l+r)*0.5f, b}, {l, (t+b)*0.5f}
    };
    for (auto& pt : pts)
        drawCircleHandle(mapToScreenNdc(pt));

    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    m_fboVao.release();
    m_solidProg->release();
}

void GPUViewport::renderCropGuides(const RenderParams& p,
                                    const QMatrix4x4& vm)
{
    if (!m_solidProg || !m_solidProg->isLinked()) return;
    if (p.cropGuideType == 0) return;

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();

    QRectF cr = p.cropRect;
    float l = std::max(-1.0f, std::min(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float r = std::min( 1.0f, std::max(static_cast<float>(cr.left()), static_cast<float>(cr.right())));
    float t = std::min( 1.0f, std::max(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));
    float b = std::max(-1.0f, std::min(static_cast<float>(cr.top()), static_cast<float>(cr.bottom())));

    QMatrix4x4 mvp = vm;
    mvp.scale(hx, hy);

    m_solidProg->bind();
    m_fboVao.bind();

    auto drawLine = [&](float x1, float y1, float x2, float y2) {
        QuadVertex verts[2] = {
            {x1, y1, 0, 0}, {x2, y2, 0, 0}
        };
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, 2);
    };

    m_solidProg->setUniformValue(m_solidU.transform, mvp);
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.4f);
    glLineWidth(2.0f);

    if (p.cropGuideType == 1) {
        float x1 = l + (r - l) / 3.0f;
        float x2 = l + (r - l) * 2.0f / 3.0f;
        float y1 = b + (t - b) / 3.0f;
        float y2 = b + (t - b) * 2.0f / 3.0f;
        drawLine(x1, b, x1, t);
        drawLine(x2, b, x2, t);
        drawLine(l, y1, r, y1);
        drawLine(l, y2, r, y2);
    } else if (p.cropGuideType == 2) {
        float phi = 0.618f;
        float x1 = l + (r - l) * phi;
        float x2 = l + (r - l) * (1.0f - phi);
        float y1 = b + (t - b) * phi;
        float y2 = b + (t - b) * (1.0f - phi);
        drawLine(x1, b, x1, t);
        drawLine(x2, b, x2, t);
        drawLine(l, y1, r, y1);
        drawLine(l, y2, r, y2);
    } else if (p.cropGuideType == 3) {
        float x1 = l + (r - l) / 3.0f;
        float x2 = l + (r - l) * 2.0f / 3.0f;
        float y1 = b + (t - b) / 3.0f;
        float y2 = b + (t - b) * 2.0f / 3.0f;
        drawLine(x1, b, x1, t);
        drawLine(x2, b, x2, t);
        drawLine(l, y1, r, y1);
        drawLine(l, y2, r, y2);
        float x3 = l + (r - l) * 0.25f;
        float x4 = l + (r - l) * 0.75f;
        float y3 = b + (t - b) * 0.25f;
        float y4 = b + (t - b) * 0.75f;
        drawLine(x3, b, x3, t);
        drawLine(x4, b, x4, t);
        drawLine(l, y3, r, y3);
        drawLine(l, y4, r, y4);
    }

    glLineWidth(1.0f);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    m_fboVao.release();
    m_solidProg->release();
}
