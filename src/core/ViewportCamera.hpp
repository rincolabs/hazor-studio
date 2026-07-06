#pragma once

#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QMatrix4x4>

namespace core {

class ViewportCamera {
public:
    ViewportCamera() = default;

    // ── Pan/scroll clamp (overscroll) ───────────────────────
    // Inclusive limits for the canvas top-left position (screen-pixel
    // convention, see clampPan). Single source of truth shared by clampPan()
    // and the viewport scrollbars so the Hand tool and the bars always agree.
    struct PanBounds {
        double minX = 0.0, maxX = 0.0, minY = 0.0, maxY = 0.0;
    };
    static PanBounds panBounds(QSizeF viewportSize,
                               QSizeF canvasScaledSize,
                               bool overscrollEnabled);

    // Single source of truth for limiting how far the canvas may be panned
    // inside the viewport. Works in the screen-pixel convention where `pan`
    // is the position of the canvas top-left corner relative to the viewport
    // top-left corner (so a centred small canvas has pan = (vp-canvas)/2).
    //
    //  • overscrollEnabled = false → the canvas can never leave the viewport:
    //    centred on any axis where it is smaller than the viewport, otherwise
    //    bounded so an edge can reach but not cross the matching viewport edge.
    //  • overscrollEnabled = true  → the canvas may be dragged past the edges
    //    by an extra margin of 50% of the viewport per axis, so corners/edges
    //    can be parked near the viewport centre. It can never fully disappear.
    static QPointF clampPan(QPointF pan,
                            QSizeF viewportSize,
                            QSizeF canvasScaledSize,
                            bool overscrollEnabled);

    // Global "Overscroll" preference (persisted via QSettings, default true).
    static bool overscrollEnabled();
    static void setOverscrollEnabled(bool enabled);

    // ── State ───────────────────────────────────────────────
    float   zoom            = 1.0f;
    QPointF panOffset       = {0.0f, 0.0f};
    QPointF canvasHalfExtents = {1.0f, 1.0f};

    // ── Extents ─────────────────────────────────────────────
    // Same logic as CanvasView::updateCanvasRect().
    void updateExtents(int docW, int docH, int viewportW, int viewportH);

    // ── Zoom ────────────────────────────────────────────────
    void zoomAt(float factor, QPointF screenAnchor,
                int viewportW, int viewportH);
    void clampZoom(float minZoom = 0.01f, float maxZoom = 100.0f);

    // ── Projection matrices ─────────────────────────────────
    // translate(panOffset) * scale(zoom)
    QMatrix4x4 viewProjection() const;

    // translate(panOffset) * scale(zoom) * scale(canvasHalfExtents)
    QMatrix4x4 viewCanvasMvp() const;

    // ── Visible bounds ──────────────────────────────────────
    QRectF visibleNdcRect() const;
    QRectF visiblePixelRect(int docW, int docH) const;

    // ── Tile test ───────────────────────────────────────────
    bool containsTile(const QRect& tileBounds,
                      int docW, int docH) const;

    // ── Coordinate conversion ───────────────────────────────
    QPointF screenToCanvasNdc(QPointF screenPos,
                               int viewportW, int viewportH) const;

    void reset();
};

} // namespace core
