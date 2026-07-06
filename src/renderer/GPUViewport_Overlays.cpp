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
#include <QDebug>
#include <QMatrix4x4>
#include <cmath>
#include <array>

static QMatrix4x4 layerRectSubMatrix(const QRect& bounds, const QSize& baseSize)
{
    const float baseW = static_cast<float>(baseSize.width());
    const float baseH = static_cast<float>(baseSize.height());
    const float left = static_cast<float>(bounds.x()) / baseW * 2.0f - 1.0f;
    const float right = static_cast<float>(bounds.x() + bounds.width()) / baseW * 2.0f - 1.0f;
    const float top = 1.0f - static_cast<float>(bounds.y()) / baseH * 2.0f;
    const float bottom = 1.0f - static_cast<float>(bounds.y() + bounds.height()) / baseH * 2.0f;

    QMatrix4x4 m;
    m.translate((left + right) * 0.5f, (top + bottom) * 0.5f);
    m.scale((right - left) * 0.5f, (top - bottom) * 0.5f);
    return m;
}

// ── Selection overlay (marching ants) ──────────────────────────

void GPUViewport::renderSelectionOverlay(const RenderParams& p,
                                          const QMatrix4x4& vm)
{
    if (!p.doc->selection.active() ||
        p.doc->selection.isEmpty() ||
        !m_selectProg || !m_selectProg->isLinked())
        return;

    if (m_selectNeedsUpload || !m_selectMaskTexture)
        uploadSelectionTexture(p);

    if (!m_selectMaskTexture) return;

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();
    QMatrix4x4 mvp = vm;
    mvp.scale(hx, hy);

    glDisable(GL_BLEND);

    m_selectProg->bind();
    m_vao.bind();

    float zoom     = static_cast<float>(p.doc->zoom > 0.01 ? p.doc->zoom : 1.0);
    QVector2D texel(1.0f / std::max(1, p.doc->selection.width()),
                    1.0f / std::max(1, p.doc->selection.height()));

    m_selectProg->setUniformValue(m_selU.transform, mvp);
    m_selectProg->setUniformValue(m_selU.mask, 0);
    m_selectProg->setUniformValue(m_selU.time,
        p.antTimer ? static_cast<float>(p.antTimer->elapsed()) / 1000.0f : 0.0f);
    m_selectProg->setUniformValue(m_selU.opacity,
        p.quickMaskMode ? -1.0f : 0.3f);
    m_selectProg->setUniformValue(m_selU.texel, texel);
    m_selectProg->setUniformValue(m_selU.zoom, zoom);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_selectMaskTexture);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_vao.release();
    m_selectProg->release();
    glEnable(GL_BLEND);
}

void GPUViewport::uploadSelectionTexture(const RenderParams& p)
{
    if (p.doc->selection.image().isNull() ||
        p.doc->selection.width() <= 0 ||
        p.doc->selection.height() <= 0) {
        m_selectNeedsUpload = false;
        return;
    }

    auto& sel = p.doc->selection;
    GLint prevUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);

    const QSize selSize(sel.width(), sel.height());
    const int bpl = sel.image().bytesPerLine();

    // Subregion upload when only part of the mask changed (quick-select dabs)
    // and the texture is already allocated with the right dimensions.
    if (m_selectMaskTexture && !m_selectUploadRect.isNull()
        && m_selectMaskTexSize == selSize) {
        const QRect r = m_selectUploadRect.intersected(
            QRect(0, 0, selSize.width(), selSize.height()));
        if (!r.isEmpty()) {
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            glPixelStorei(GL_UNPACK_ROW_LENGTH, bpl);  // 1 byte/px → stride in px
            glBindTexture(GL_TEXTURE_2D, m_selectMaskTexture);
            glTexSubImage2D(GL_TEXTURE_2D, 0, r.x(), r.y(),
                            r.width(), r.height(),
                            GL_RED, GL_UNSIGNED_BYTE,
                            sel.constBits() + r.y() * bpl + r.x());
            glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        }
    } else {
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (!m_selectMaskTexture) {
            glGenTextures(1, &m_selectMaskTexture);
            glBindTexture(GL_TEXTURE_2D, m_selectMaskTexture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, m_selectMaskTexture);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                     selSize.width(), selSize.height(), 0,
                     GL_RED, GL_UNSIGNED_BYTE,
                     sel.constBits());
        m_selectMaskTexSize = selSize;
    }

    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
    m_selectNeedsUpload = false;
    m_selectUploadRect = QRect();
}

// ── Rubber band (selection rect / lasso) ───────────────────────

void GPUViewport::renderRubberBand(const RenderParams& p,
                                    const QMatrix4x4& vm)
{
    if (!p.selectDragging && !p.lassoDrawing) return;
    if (!m_solidProg || !m_solidProg->isLinked()) return;
    if (!p.doc || p.doc->size.width() <= 0 || p.doc->size.height() <= 0) return;

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();
    QMatrix4x4 mvp = vm;
    mvp.scale(hx, hy);

    float docW = static_cast<float>(p.doc->size.width());
    float docH = static_cast<float>(p.doc->size.height());
    auto docToCanvas = [&](const QPointF& pt) {
        return QPointF(static_cast<float>(pt.x()) / docW * 2.0f - 1.0f,
                        1.0f - static_cast<float>(pt.y()) / docH * 2.0f);
    };

    m_solidProg->bind();
    m_fboVao.bind();
    m_solidProg->setUniformValue(m_solidU.transform, mvp);

    if (p.selectDragging) {
        QPointF start = docToCanvas(p.selectStart);
        QPointF current = docToCanvas(p.selectCurrent);
        if (p.selectType == 0) { // Rectangular
            float x1 = static_cast<float>(start.x());
            float y1 = static_cast<float>(start.y());
            float x2 = static_cast<float>(current.x());
            float y2 = static_cast<float>(current.y());
            float l = std::min(x1, x2), r = std::max(x1, x2);
            float t = std::max(y1, y2), b = std::min(y1, y2);
            QMatrix4x4 rectMvp = mvp;
            float mw = (r - l) * 0.5f, mh = (t - b) * 0.5f;
            float mx = (l + r) * 0.5f, my = (t + b) * 0.5f;
            rectMvp.translate(mx, my);
            rectMvp.scale(mw, mh);
            m_solidProg->setUniformValue(m_solidU.transform, rectMvp);
            m_solidProg->setUniformValue(m_solidU.color, 1.0f, 1.0f, 1.0f, 0.8f);
            glBufferData(GL_ARRAY_BUFFER, sizeof(outlineVertices), outlineVertices, GL_STATIC_DRAW);
            glDrawArrays(GL_LINE_LOOP, 0, 4);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        } else {
            int segments = 64;
            float cx = (static_cast<float>(start.x()) +
                        static_cast<float>(current.x())) * 0.5f;
            float cy = (static_cast<float>(start.y()) +
                        static_cast<float>(current.y())) * 0.5f;
            float rx = std::abs(static_cast<float>(current.x()) -
                                static_cast<float>(start.x())) * 0.5f;
            float ry = std::abs(static_cast<float>(current.y()) -
                                static_cast<float>(start.y())) * 0.5f;
            std::vector<QuadVertex> circle;
            circle.reserve(segments + 1);
            for (int i = 0; i <= segments; ++i) {
                float a = 6.283185f * i / segments;
                circle.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a), 0, 0});
            }
            QMatrix4x4 ident;
            ident.scale(1.0f / hx, 1.0f / hy);
            m_solidProg->setUniformValue(m_solidU.transform, vm * ident);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(circle.size() * sizeof(QuadVertex)),
                         circle.data(), GL_DYNAMIC_DRAW);
            m_solidProg->setUniformValue(m_solidU.color, 1.0f, 1.0f, 1.0f, 0.8f);
            glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(circle.size()));
            glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        }
    }

    if (p.lassoDrawing && p.lassoPoints && p.lassoPoints->size() > 1) {
        const auto& pts = *p.lassoPoints;
        std::vector<QuadVertex> lassoVerts;
        lassoVerts.reserve(pts.size() + 1);
        for (const auto& pt : pts) {
            QPointF canvasPt = docToCanvas(pt);
            lassoVerts.push_back({static_cast<float>(canvasPt.x()),
                                  static_cast<float>(canvasPt.y()), 0, 0});
        }
        m_solidProg->setUniformValue(m_solidU.transform, mvp);
        m_solidProg->setUniformValue(m_solidU.color, 0.8f, 0.8f, 1.0f, 0.8f);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(lassoVerts.size() * sizeof(QuadVertex)),
                     lassoVerts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(lassoVerts.size()));
        if (p.lassoCanClose && pts.size() > 2) {
            QPointF last = docToCanvas(pts.back());
            float cx = static_cast<float>(last.x());
            float cy = static_cast<float>(last.y());
            int segs = 24;
            float safeZoom = p.doc->zoom > 0.01f ? p.doc->zoom : 1.0f;
            float rad = 12.0f / (std::max(1, p.viewportW) * safeZoom * std::max(0.001f, hx));
            std::vector<QuadVertex> dot;
            dot.reserve(segs + 1);
            for (int i = 0; i <= segs; ++i) {
                float a = 6.283185f * i / segs;
                dot.push_back({cx + rad * std::cos(a), cy + rad * std::sin(a), 0, 0});
            }
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(dot.size() * sizeof(QuadVertex)),
                         dot.data(), GL_DYNAMIC_DRAW);
            glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(dot.size()));
        }
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    }

    m_fboVao.release();
    m_solidProg->release();
}

// ── Box selection overlay ────────────────────────────────────────

void GPUViewport::renderBoxSelectionOverlay(const RenderParams& p,
                                             const QMatrix4x4& vm)
{
    if (!p.boxSelecting) return;
    if (!m_solidProg || !m_solidProg->isLinked()) return;

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();
    QMatrix4x4 mvp = vm;
    mvp.scale(hx, hy);

    float x1 = std::min(p.boxSelectStart.x(), p.boxSelectCurrent.x());
    float x2 = std::max(p.boxSelectStart.x(), p.boxSelectCurrent.x());
    float y1 = std::min(p.boxSelectStart.y(), p.boxSelectCurrent.y());
    float y2 = std::max(p.boxSelectStart.y(), p.boxSelectCurrent.y());

    float mw = (x2 - x1) * 0.5f;
    float mh = (y2 - y1) * 0.5f;
    float mx = (x1 + x2) * 0.5f;
    float my = (y1 + y2) * 0.5f;

    QMatrix4x4 rectMvp = mvp;
    rectMvp.translate(mx, my);
    rectMvp.scale(mw, mh);

    m_solidProg->bind();
    m_fboVao.bind();

    // Filled semi-transparent blue rect
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    m_solidProg->setUniformValue(m_solidU.transform, rectMvp);
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.12f);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Blue outline
    glDisable(GL_BLEND);
    glBufferData(GL_ARRAY_BUFFER, sizeof(outlineVertices), outlineVertices, GL_STATIC_DRAW);
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.85f);
    glDrawArrays(GL_LINE_LOOP, 0, 4);

    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    m_fboVao.release();
    m_solidProg->release();
    glEnable(GL_BLEND);
}

// ── Bounding box ────────────────────────────────────────────────

void GPUViewport::renderBoundingBox(const RenderParams& p,
                                     const QMatrix4x4& vm)
{
    if (!m_solidProg || !m_solidProg->isLinked()) return;

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();

    if (p.transformingSelection && p.doc->selection.active()) {
        QRectF sb = p.doc->selection.bounds();
        if (sb.isEmpty()) return;
        QMatrix4x4 bbMvp = vm;
        bbMvp.scale(hx, hy);
        int dw = p.doc->size.width(), dh = p.doc->size.height();
        if (dw <= 0 || dh <= 0) return;
        float l = static_cast<float>(sb.left())   / dw * 2.0f - 1.0f;
        float r = static_cast<float>(sb.right())  / dw * 2.0f - 1.0f;
        float t_ = 1.0f - static_cast<float>(sb.top())    / dh * 2.0f;
        float b_ = 1.0f - static_cast<float>(sb.bottom()) / dh * 2.0f;
        float mw = (r - l) * 0.5f, mh = (t_ - b_) * 0.5f;
        float mx = (l + r) * 0.5f, my = (t_ + b_) * 0.5f;
        QMatrix4x4 sm = bbMvp;
        sm.translate(mx, my);
        sm.scale(mw, mh);

        m_solidProg->bind();
        m_fboVao.bind();
        m_solidProg->setUniformValue(m_solidU.transform, sm);
        m_solidProg->setUniformValue(m_solidU.color, 1.0f, 1.0f, 1.0f, 0.8f);
        glBufferData(GL_ARRAY_BUFFER, sizeof(outlineVertices), outlineVertices, GL_STATIC_DRAW);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINE_LOOP, 0, 4);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
        float hs = (10.0f / p.viewportW) * 2.0f;
        QPointF corners[4] = {{l, t_}, {r, t_}, {r, b_}, {l, b_}};
        for (auto& cp : corners) {
            QMatrix4x4 hm = bbMvp;
            hm.translate(static_cast<float>(cp.x()), static_cast<float>(cp.y()));
            hm.scale(hs / hx, hs / hy);
            m_solidProg->setUniformValue(m_solidU.transform, hm);
            m_solidProg->setUniformValue(m_solidU.color, 0.8f, 0.8f, 1.0f, 1.0f);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
        m_fboVao.release();
        m_solidProg->release();
        return;
    }

    auto* node = p.doc->activeNode();
    // A group has no layer of its own; its box is driven by the multi-bbox slots
    // (p.activeMultiTransform) just like a multi-selection.
    if (!node
        || (!node->layer && node->type != LayerTreeNode::Type::Group))
        return;
    // Adjustment layers are non-spatial — never draw a transform box for them.
    if (node->type == LayerTreeNode::Type::Adjustment)
        return;

    float bboxCenterX = 0.0f, bboxCenterY = 0.0f;
    float bboxHw = 1.0f, bboxHh = 1.0f;

    QPolygonF corners;
    if (p.doc->selectedFlatIndices.size() > 1 || p.activeMultiTransform) {
        if (p.activeMultiTransform) {
            bboxCenterX = p.multiBboxCenterX;
            bboxCenterY = p.multiBboxCenterY;
            bboxHw = p.multiBboxHw;
            bboxHh = p.multiBboxHh;

            float groupRot = p.multiBboxRotation;

            QPointF metric(
                std::max(1e-6, p.canvasHalfExtents.x() * std::max(1, p.viewportW)),
                std::max(1e-6, p.canvasHalfExtents.y() * std::max(1, p.viewportH)));
            QTransform rotatedXf = TransformController::composeVisual(
                bboxHw * metric.x(),
                bboxHh * metric.y(),
                QPointF(bboxCenterX * metric.x(), bboxCenterY * metric.y()),
                groupRot,
                p.canvasHalfExtents,
                QSize(p.viewportW, p.viewportH));
            corners = TransformController::cornersFromTransform(rotatedXf);
        } else {
            // Fallback only — CanvasView::paintGL normally provides the
            // multi-bbox via activeMultiTransform (groups included). Reaching
            // here means the selection has no valid frame; draw nothing rather
            // than a degenerate box.
            float cMinX = 1e9f, cMaxX = -1e9f, cMinY = 1e9f, cMaxY = -1e9f;
            bool any = false;
            for (int idx : p.doc->selectedFlatIndices) {
                auto* sn = p.doc->nodeAt(idx);
                if (!sn || !sn->layer) continue;
                QPolygonF sc = TransformController::cornersFromNode(sn);
                for (auto& cp : sc) {
                    const float px = static_cast<float>(cp.x());
                    const float py = static_cast<float>(cp.y());
                    if (px < cMinX) cMinX = px;
                    if (px > cMaxX) cMaxX = px;
                    if (py < cMinY) cMinY = py;
                    if (py > cMaxY) cMaxY = py;
                    any = true;
                }
            }
            if (!any)
                return;
            bboxCenterX = (cMinX + cMaxX) * 0.5f;
            bboxCenterY = (cMinY + cMaxY) * 0.5f;
            bboxHw = (cMaxX - cMinX) * 0.5f;
            bboxHh = (cMaxY - cMinY) * 0.5f;

            corners << QPointF(cMinX, cMinY) << QPointF(cMaxX, cMinY)
                    << QPointF(cMaxX, cMaxY) << QPointF(cMinX, cMaxY);
        }
    } else {
        corners = TransformController::cornersFromNode(node);
    }

    QMatrix4x4 bbMvp = vm;
    bbMvp.scale(hx, hy);
    m_solidProg->bind();
    m_fboVao.bind();
    m_fboVbo.bind();

    m_solidProg->setUniformValue(m_solidU.transform, bbMvp);
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.9f);

    std::vector<QuadVertex> box;
    for (auto& p_ : corners)
        box.push_back({static_cast<float>(p_.x()), static_cast<float>(p_.y()), 0, 0});
    box.push_back(box.front());
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(box.size() * sizeof(QuadVertex)),
                 box.data(), GL_DYNAMIC_DRAW);
    glLineWidth(1.0f);
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(box.size()));

    // Map canvas-NDC position → clip-space NDC (zoom-independent screen position).
    auto mapToScreenNdc = [&](const QPointF& cp) -> QPointF {
        QVector4D v = bbMvp * QVector4D(static_cast<float>(cp.x()),
                                        static_cast<float>(cp.y()), 0.0f, 1.0f);
        return QPointF(v.x(), v.y());
    };

    // Helper: emit a filled circle + white outline into the VBO at a screen-NDC center.
    // rX/rY are half-extents in clip space (pixel-based, zoom-independent).
    const int kSegs = 24;
    auto drawCircleHandle = [&](QPointF sc, float rX, float rY) {
        float cx = static_cast<float>(sc.x());
        float cy = static_cast<float>(sc.y());

        // White fill disc — keeps the handle readable over any image content.
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
        m_solidProg->setUniformValue(m_solidU.color, 1.0f, 1.0f, 1.0f, 0.95f);
        glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(fill.size()));

        // 1px blue outline ring — same weight/colour as the bounding-box border.
        std::vector<QuadVertex> ring;
        ring.reserve(kSegs + 1);
        for (int i = 0; i <= kSegs; ++i) {
            float a = 6.283185f * i / kSegs;
            ring.push_back({cx + rX * std::cos(a), cy + rY * std::sin(a), 0, 0});
        }
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(ring.size() * sizeof(QuadVertex)),
                     ring.data(), GL_DYNAMIC_DRAW);
        m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.95f);
        glLineWidth(1.0f);
        glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(ring.size()));
    };

    // Corner and edge handles — drawn in screen-NDC so size is zoom-independent.
    {
        QMatrix4x4 identity;
        m_solidProg->setUniformValue(m_solidU.transform, identity);

        const float rX = (2.0f * 5.0f) / std::max(1, p.viewportW);  // 5 px radius
        const float rY = (2.0f * 5.0f) / std::max(1, p.viewportH);

        std::array<QPointF, 8> handles = {
            corners[0],
            (corners[0] + corners[1]) * 0.5,
            corners[1],
            (corners[1] + corners[2]) * 0.5,
            corners[2],
            (corners[2] + corners[3]) * 0.5,
            corners[3],
            (corners[3] + corners[0]) * 0.5
        };
        for (const auto& cp : handles)
            drawCircleHandle(mapToScreenNdc(cp), rX, rY);
    }

    // Center cross — also screen-NDC.
    {
        QRectF bounds = corners.boundingRect();
        QPointF center = (p.doc->selectedFlatIndices.size() > 1)
            ? QPointF(bboxCenterX, bboxCenterY)
            : bounds.center();
        QPointF sc = mapToScreenNdc(center);
        float ch = (2.0f * 6.0f) / std::max(1, p.viewportW);
        float cv = (2.0f * 6.0f) / std::max(1, p.viewportH);
        std::vector<QuadVertex> cross = {
            {static_cast<float>(sc.x()) - ch, static_cast<float>(sc.y()), 0, 0},
            {static_cast<float>(sc.x()) + ch, static_cast<float>(sc.y()), 0, 0},
            {static_cast<float>(sc.x()), static_cast<float>(sc.y()) - cv, 0, 0},
            {static_cast<float>(sc.x()), static_cast<float>(sc.y()) + cv, 0, 0}
        };
        QMatrix4x4 identity;
        m_solidProg->setUniformValue(m_solidU.transform, identity);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(cross.size() * sizeof(QuadVertex)),
                     cross.data(), GL_DYNAMIC_DRAW);
        m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.6f);
        glDrawArrays(GL_LINES, 0, 4);
    }

    // Rotate handle — screen-NDC, already zoom-independent.
    {
        QPointF sc0 = mapToScreenNdc(corners[0]);
        QPointF sc1 = mapToScreenNdc(corners[1]);
        QPointF sc2 = mapToScreenNdc(corners[2]);
        QPointF sc3 = mapToScreenNdc(corners[3]);
        QPointF sCenter = (sc0 + sc1 + sc2 + sc3) * 0.25;
        QPointF mids[4] = {
            (sc0 + sc1) * 0.5,
            (sc1 + sc2) * 0.5,
            (sc2 + sc3) * 0.5,
            (sc3 + sc0) * 0.5
        };
        QPointF sTopMid = mids[2];
        QPointF dir = sTopMid - sCenter;
        float dlen = std::hypot(static_cast<float>(dir.x()), static_cast<float>(dir.y()));
        if (dlen > 1e-5f) {
            dir /= dlen;
            float stemLen = (2.0f * 18.0f) / std::max(1, p.viewportH);
            float radiusX = (2.0f * 6.0f) / std::max(1, p.viewportW);
            float radiusY = (2.0f * 6.0f) / std::max(1, p.viewportH);
            QPointF sHandle = sTopMid + dir * stemLen;

            QMatrix4x4 identity;
            m_solidProg->setUniformValue(m_solidU.transform, identity);

            std::vector<QuadVertex> stem = {
                {static_cast<float>(sTopMid.x()), static_cast<float>(sTopMid.y()), 0, 0},
                {static_cast<float>(sHandle.x()), static_cast<float>(sHandle.y()), 0, 0}
            };
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(stem.size() * sizeof(QuadVertex)),
                         stem.data(), GL_DYNAMIC_DRAW);
            m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.47f, 1.0f, 0.95f);
            glLineWidth(2.0f);
            glDrawArrays(GL_LINES, 0, 2);
            glLineWidth(1.0f);

            drawCircleHandle(sHandle, radiusX, radiusY);
        }
    }

    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

    m_fboVao.release();
    m_solidProg->release();
    m_vao.bind();
    m_mainProg->bind();
}

// ── Text cursor ─────────────────────────────────────────────────

void GPUViewport::renderTextCursor(const RenderParams& p,
                                    const QMatrix4x4& vm)
{
    if (p.textLayerIndex < 0 || !p.textEditor ||
        !p.textEditor->caretVisible() || !m_solidProg || !m_solidProg->isLinked())
        return;

    auto* node = p.doc->nodeAt(p.textLayerIndex);
    if (!node || !node->layer || !node->layer->textData) return;

    auto& td = *node->layer->textData;

    TextLayoutEngine eng;
    eng.setData(&td);
    QRectF cr = eng.cursorRect(p.textEditor->cursorPos());
    if (cr.isEmpty()) return;

    QImage& img = node->layer->cpuImage;
    if (img.isNull()) return;
    float imgW = static_cast<float>(img.width());
    float imgH = static_cast<float>(img.height());

    float cx   = static_cast<float>(cr.x()) + static_cast<float>(cr.width()) * 0.5f;
    float topY = static_cast<float>(cr.y());
    float botY = static_cast<float>(cr.y()) + static_cast<float>(cr.height());

    float ndcX   = cx    / imgW * 2.0f - 1.0f;
    float ndcTop = 1.0f - topY  / imgH * 2.0f;
    float ndcBot = 1.0f - botY  / imgH * 2.0f;

    QMatrix4x4 mat = vm;
    mat.scale(p.canvasHalfExtents.x(), p.canvasHalfExtents.y());
    mat = mat * qTransformToMatrix4x4(node->accumulatedTransform());

    QVector4D topV = mat * QVector4D(ndcX, ndcTop, 0, 1);
    QVector4D botV = mat * QVector4D(ndcX, ndcBot, 0, 1);
    if (topV.w() != 0) topV /= topV.w();
    if (botV.w() != 0) botV /= botV.w();

    float cursorW = 2.0f / p.viewportW * 2.0f;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_solidProg->bind();
    m_fboVao.bind();
    m_fboVbo.bind();

    QMatrix4x4 identity;
    m_solidProg->setUniformValue(m_solidU.transform, identity);
    m_solidProg->setUniformValue(m_solidU.color, 0.0f, 0.0f, 0.0f, 0.85f);

    QuadVertex verts[4] = {
        { topV.x() - cursorW, topV.y(), 0, 0 },
        { topV.x() + cursorW, topV.y(), 0, 0 },
        { botV.x() - cursorW, botV.y(), 0, 0 },
        { botV.x() + cursorW, botV.y(), 0, 0 },
    };
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_fboVao.release();
    m_solidProg->release();
    glDisable(GL_BLEND);

    updateFboQuad(p.viewportW, p.viewportH);
}

// ── Quick select circle indicator ───────────────────────────────

void GPUViewport::renderQuickSelectCircle(const RenderParams& p,
                                           const QMatrix4x4& vm)
{
    if (!m_solidProg || !m_solidProg->isLinked()) return;

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();

    float r = p.brushRadius * 0.01f;
    QMatrix4x4 mvp = vm;
    mvp.scale(1.0f / hx, 1.0f / hy);
    mvp.translate(static_cast<float>(p.brushIndicatorCenter.x()),
                  static_cast<float>(p.brushIndicatorCenter.y()));
    mvp.scale(r, r);

    int segs = 32;
    std::vector<QuadVertex> circle;
    circle.reserve(segs + 1);
    for (int i = 0; i <= segs; ++i) {
        float a = 6.283185f * i / segs;
        circle.push_back({std::cos(a), std::sin(a), 0, 0});
    }

    m_solidProg->bind();
    m_fboVao.bind();
    m_fboVbo.bind();
    m_solidProg->setUniformValue(m_solidU.transform, mvp);
    m_solidProg->setUniformValue(m_solidU.color, 0.6f, 0.6f, 1.0f, 0.8f);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(circle.size() * sizeof(QuadVertex)),
                 circle.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINE_STRIP, 0, static_cast<GLsizei>(circle.size()));
    glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);
    m_fboVao.release();
    m_solidProg->release();
}

// ── Rubylith overlay (mask editing) ─────────────────────────────

void GPUViewport::renderRubylithOverlay(GLuint maskTex, int maskUnit,
                                         const QMatrix4x4& mvp,
                                         float opacity,
                                         QVector2D maskUvOffset,
                                         QVector2D maskUvScale)
{
    if (!m_rubylithProg || !m_rubylithProg->isLinked()) return;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    // Bind the mask explicitly so the overlay is correct regardless of prior
    // texture-unit state (callers may invoke this per-tile in a loop).
    glActiveTexture(GL_TEXTURE0 + maskUnit);
    glBindTexture(GL_TEXTURE_2D, maskTex);
    glActiveTexture(GL_TEXTURE0);
    m_rubylithProg->bind();
    // Bind the standard quad VAO explicitly. The caller's last draw may have left
    // a different VAO bound — notably the *instanced* tile VAO after a tiled
    // layer's instanced draw — and the rubylith shader reads pos@0/uv@1 from
    // whatever VAO is active. Without this the overlay drew nothing for tiled
    // layers (it only appeared once the layer became rasterStorage-backed, whose
    // path happens to leave m_vao bound).
    m_vao.bind();
    m_rubylithProg->setUniformValue(m_rubyU.transform, mvp);
    m_rubylithProg->setUniformValue(m_rubyU.mask, maskUnit);
    m_rubylithProg->setUniformValue(m_rubyU.opacity, opacity);
    m_rubylithProg->setUniformValue(m_rubyU.maskUvOffset, maskUvOffset);
    m_rubylithProg->setUniformValue(m_rubyU.maskUvScale,  maskUvScale);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    m_rubylithProg->release();
    // Leave the standard quad VAO bound (the neutral state the compositor's other
    // draw paths expect on return).
}

// ── Grayscale mask view ────────────────────────────────────────

void GPUViewport::renderGrayscaleMaskView(const RenderParams& p,
                                           const QMatrix4x4& vm)
{
    if (!m_grayProg || !m_grayProg->isLinked()) return;

    auto* node = p.doc->activeNode();
    if (!node || !node->layer || node->layer->maskTextureId == 0) return;
    auto* layer = node->layer.get();

    float hx = p.canvasHalfExtents.x();
    float hy = p.canvasHalfExtents.y();
    QMatrix4x4 layerMvp = vm;
    layerMvp.scale(hx, hy);
    const bool shapeSpriteActive = layer->shapeSpriteRenderable();
    const QTransform layerTransform = shapeSpriteActive
        ? layer->shapeCache.spriteTransform
        : node->accumulatedTransform();
    layerMvp = layerMvp * qTransformToMatrix4x4(layerTransform);

    m_grayProg->bind();
    m_vao.bind();
    m_grayProg->setUniformValue(m_grayU.texture, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);

    const int mW = layer->maskImage.width();
    const int mH = layer->maskImage.height();
    if (shapeSpriteActive && mW > 0 && mH > 0) {
        m_grayProg->setUniformValue(m_grayU.transform, layerMvp);
        m_grayProg->setUniformValue(m_grayU.maskUvOffset, QVector2D(0.0f, 0.0f));
        m_grayProg->setUniformValue(m_grayU.maskUvScale,  QVector2D(1.0f, 1.0f));
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        m_vao.release();
        m_grayProg->release();
        return;
    }

    const QSize base = layer->rasterBaseSize();
    const QRect target = layer->maskTargetBounds();
    if (base.width() > 0 && base.height() > 0 && mW > 0 && mH > 0 && !target.isEmpty()) {
        const QVector2D maskOffset(static_cast<float>(target.left() - layer->maskOrigin.x()) / mW,
                                   static_cast<float>(target.top() - layer->maskOrigin.y()) / mH);
        const QVector2D maskScale(static_cast<float>(target.width()) / mW,
                                  static_cast<float>(target.height()) / mH);
        m_grayProg->setUniformValue(m_grayU.transform, layerMvp * layerRectSubMatrix(target, base));
        m_grayProg->setUniformValue(m_grayU.maskUvOffset, maskOffset);
        m_grayProg->setUniformValue(m_grayU.maskUvScale,  maskScale);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    m_vao.release();
    m_grayProg->release();
}
