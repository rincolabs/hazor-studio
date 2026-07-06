#include "ViewportCamera.hpp"
#include <algorithm>
#include <cmath>
#include <QtMath>
#include <QSettings>

namespace core {

ViewportCamera::PanBounds ViewportCamera::panBounds(QSizeF viewportSize,
                                                    QSizeF canvasScaledSize,
                                                    bool overscrollEnabled)
{
    const double vw = viewportSize.width();
    const double vh = viewportSize.height();
    const double cw = canvasScaledSize.width();
    const double ch = canvasScaledSize.height();

    PanBounds b;

    if (!overscrollEnabled) {
        if (cw <= vw) {
            b.minX = b.maxX = (vw - cw) * 0.5;
        } else {
            b.minX = vw - cw;
            b.maxX = 0.0;
        }

        if (ch <= vh) {
            b.minY = b.maxY = (vh - ch) * 0.5;
        } else {
            b.minY = vh - ch;
            b.maxY = 0.0;
        }
    } else {
        const double marginX = vw * 0.5;
        const double marginY = vh * 0.5;

        b.minX = -cw + marginX;
        b.maxX =  vw - marginX;

        b.minY = -ch + marginY;
        b.maxY =  vh - marginY;
    }

    // Defensive: never expose an inverted range (degenerate sizes).
    if (b.minX > b.maxX) std::swap(b.minX, b.maxX);
    if (b.minY > b.maxY) std::swap(b.minY, b.maxY);
    return b;
}

QPointF ViewportCamera::clampPan(QPointF pan,
                                 QSizeF viewportSize,
                                 QSizeF canvasScaledSize,
                                 bool overscrollEnabled)
{
    const PanBounds b = panBounds(viewportSize, canvasScaledSize, overscrollEnabled);
    return QPointF(std::clamp(pan.x(), b.minX, b.maxX),
                   std::clamp(pan.y(), b.minY, b.maxY));
}

bool ViewportCamera::overscrollEnabled()
{
    QSettings settings;
    return settings.value(QStringLiteral("Viewport/overscrollEnabled"), true).toBool();
}

void ViewportCamera::setOverscrollEnabled(bool enabled)
{
    QSettings settings;
    settings.setValue(QStringLiteral("Viewport/overscrollEnabled"), enabled);
}

void ViewportCamera::updateExtents(int docW, int docH,
                                   int viewportW, int viewportH)
{
    if (docW <= 0 || docH <= 0 || viewportW <= 0 || viewportH <= 0) {
        canvasHalfExtents = {1.0f, 1.0f};
        return;
    }
    float vpAspect  = static_cast<float>(viewportW) / viewportH;
    float docAspect = static_cast<float>(docW) / docH;

    if (docAspect > vpAspect)
        canvasHalfExtents = {1.0f, vpAspect / docAspect};
    else
        canvasHalfExtents = {docAspect / vpAspect, 1.0f};
}

void ViewportCamera::zoomAt(float factor, QPointF screenAnchor,
                             int viewportW, int viewportH)
{
    // Screen → NDC before zoom
    float ndx = (2.0f * static_cast<float>(screenAnchor.x()) / viewportW) - 1.0f;
    float ndy = 1.0f - (2.0f * static_cast<float>(screenAnchor.y()) / viewportH);

    // NDC → canvas coords (inverse of pan + zoom)
    float oldCx = (ndx - panOffset.x()) / zoom;
    float oldCy = (ndy - panOffset.y()) / zoom;

    zoom *= factor;
    clampZoom();

    // Keep screenAnchor fixed: adjust panOffset so that
    // the same canvas point maps to the same screen position.
    panOffset.rx() = ndx - oldCx * zoom;
    panOffset.ry() = ndy - oldCy * zoom;
}

void ViewportCamera::clampZoom(float minZoom, float maxZoom)
{
    zoom = std::clamp(zoom, minZoom, maxZoom);
}

QMatrix4x4 ViewportCamera::viewProjection() const
{
    QMatrix4x4 m;
    m.translate(panOffset.x(), panOffset.y());
    m.scale(zoom);
    return m;
}

QMatrix4x4 ViewportCamera::viewCanvasMvp() const
{
    QMatrix4x4 m;
    m.translate(panOffset.x(), panOffset.y());
    m.scale(zoom);
    m.scale(canvasHalfExtents.x(), canvasHalfExtents.y());
    return m;
}

QRectF ViewportCamera::visibleNdcRect() const
{
    // Viewport covers NDC [-1, 1] after pan+zoom.
    // Compute the corresponding NDC rectangle before pan+zoom.
    float invZoom = 1.0f / zoom;
    float left   = (-1.0f - panOffset.x()) * invZoom;
    float right  = ( 1.0f - panOffset.x()) * invZoom;
    float top    = ( 1.0f - panOffset.y()) * invZoom;
    float bottom = (-1.0f - panOffset.y()) * invZoom;
    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

QRectF ViewportCamera::visiblePixelRect(int docW, int docH) const
{
    // Inverse chain: NDC → viewProjection₋₁ → canvasNdc → pixel
    float invZoom = 1.0f / zoom;
    float hx = canvasHalfExtents.x();
    float hy = canvasHalfExtents.y();

    // NDC [-1,1] after viewProjection → canvas NDC before half-extents
    // Then canvas NDC [-hx,hx] x [-hy,hy] → pixel [0,docW] x [0,docH]
    float halfW = docW * 0.5f;
    float halfH = docH * 0.5f;

    // Pixel X grows rightward like NDC X, so px = (ndcX/.. + 0.5) * docW.
    // Pixel Y grows DOWNWARD, opposite to NDC Y, so the NDC term is negated
    // (py = 0.5 - ndcY/..). Without this flip the visible rect comes out
    // mirrored around the document's vertical center, culling the wrong tile
    // rows while panning and leaving a transparent band on the visible side.
    float left   = ((-1.0f - panOffset.x()) * invZoom / (2.0f * hx) + 0.5f) * docW;
    float right  = (( 1.0f - panOffset.x()) * invZoom / (2.0f * hx) + 0.5f) * docW;
    float top    = (0.5f - ( 1.0f - panOffset.y()) * invZoom / (2.0f * hy)) * docH;
    float bottom = (0.5f - (-1.0f - panOffset.y()) * invZoom / (2.0f * hy)) * docH;

    if (left > right) std::swap(left, right);
    if (top > bottom) std::swap(top, bottom);

    return QRectF(QPointF(left, top), QPointF(right, bottom));
}

bool ViewportCamera::containsTile(const QRect& tileBounds,
                                  int docW, int docH) const
{
    if (docW <= 0 || docH <= 0) return false;
    QRectF vpRect = visiblePixelRect(docW, docH);
    return vpRect.intersects(QRectF(tileBounds));
}

QPointF ViewportCamera::screenToCanvasNdc(QPointF screenPos,
                                           int viewportW, int viewportH) const
{
    if (viewportW <= 0 || viewportH <= 0)
        return {};

    float ndcX = (2.0f * static_cast<float>(screenPos.x()) / viewportW) - 1.0f;
    float ndcY = 1.0f - (2.0f * static_cast<float>(screenPos.y()) / viewportH);

    // Remove viewport transform (pan + zoom + halfExtents)
    float invZoom = 1.0f / zoom;
    float cnvX = (ndcX - panOffset.x()) * invZoom;
    float cnvY = (ndcY - panOffset.y()) * invZoom;

    return QPointF(cnvX, cnvY);
}

void ViewportCamera::reset()
{
    zoom = 1.0f;
    panOffset = {0.0f, 0.0f};
}

} // namespace core
