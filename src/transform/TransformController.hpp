#pragma once

#include "TransformTypes.hpp"
#include <QPointF>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QPolygonF>
#include <QTransform>
#include <QCursor>
#include <Qt>

class Layer;
class LayerTreeNode;
class QTransform;

class TransformController {
public:
    static void decompose(const QTransform& t, float& hw, float& hh,
                          QPointF& center, float& rotation);
    static QTransform compose(float hw, float hh,
                              const QPointF& center, float rotation);
    static void decomposeVisual(const QTransform& t,
                                const QPointF& canvasHalfExtents,
                                const QSize& viewportSize,
                                float& hw, float& hh,
                                QPointF& center, float& rotation);
    static QTransform composeVisual(float hw, float hh,
                                    const QPointF& center, float rotation,
                                    const QPointF& canvasHalfExtents,
                                    const QSize& viewportSize);

    static QPolygonF cornersFromTransform(const QTransform& t);
    static QPolygonF cornersFromTransformAndBounds(const QTransform& t,
                                                   const QRectF& localPixelBounds,
                                                   const QSizeF& baseSize);
    static QPolygonF cornersFromNode(const LayerTreeNode* node);

    // World-space frame whose unit quad (-1..1) maps exactly to the node's
    // rendered transform box: the content-bounds sub-rect for raster layers,
    // the full accumulated frame otherwise. Resize/rotate start frames must use
    // this (not the raw accumulatedTransform) so the gesture anchor sits on the
    // visible box corner — otherwise sparsely-painted layers (whose content is
    // a fraction of the full base) jump on the first drag frame.
    static QTransform visualFrameForNode(const LayerTreeNode* node);

    // documentSize-aware variant: for Shape layers it anchors the frame on the
    // pixel-exact natural base (the same transform a freshly-baked raster
    // carries, i.e. node->transform once committed) instead of the geometry AABB.
    // Use this where a stable numeric readback is needed (Transform panel /
    // options bar) — it removes the sub-pixel W/H/X/Y drift the geometry-AABB base
    // leaves after a shape commit. Falls back to the no-arg version for other
    // layer types or when the size/raster is unavailable.
    static QTransform visualFrameForNode(const LayerTreeNode* node,
                                         const QSize& documentSize);

    // handlePixelSize is the hit-test half-extent in screen pixels around each
    // handle (kept larger than the ~5px drawn radius so handles are easy to grab).
    static HandlePosition handleAt(const QPointF& screenPos,
                                    const QPolygonF& cornersScreen,
                                    const QSize& viewportSize,
                                    float handlePixelSize = 14.0f);

    static QPointF screenToCanvasNdc(const QPointF& screenPos,
                                      float zoom, const QPointF& panOffset,
                                      const QPointF& canvasHalfExtents,
                                      const QSize& viewportSize);

    static QPointF canvasNdcToScreen(const QPointF& canvasNdc,
                                      float zoom, const QPointF& panOffset,
                                      const QPointF& canvasHalfExtents,
                                      const QSize& viewportSize);

    static QTransform updateResize(const TransformState& state,
                                    const QPointF& currentMouseScreen,
                                    float zoom, const QPointF& panOffset,
                                    const QPointF& canvasHalfExtents,
                                    const QSize& viewportSize,
                                    Qt::KeyboardModifiers modifiers);

    static QTransform updateRotate(const TransformState& state,
                                    const QPointF& currentMouseScreen,
                                    float zoom, const QPointF& panOffset,
                                    const QPointF& canvasHalfExtents,
                                    const QSize& viewportSize,
                                    Qt::KeyboardModifiers modifiers);

    static QCursor cursorForHandle(HandlePosition handle);

    // ── Distort / Perspective quad editing ──────────────────────
    // All quad points are in document-pixel space.

    // Distort: move a single corner freely, the other three stay put.
    // When constrainAxis is set, the moved corner is locked to the dominant
    // axis (horizontal/vertical) relative to its original position.
    static TransformQuad distortDragCorner(const TransformQuad& startQuad,
                                           int cornerIndex,
                                           const QPointF& startCorner,
                                           const QPointF& currentMouse,
                                           bool constrainAxis);

    // Perspective: dragging one corner moves the adjacent corner on the same
    // edge in mirrored fashion (coupled trapezoid). The opposite edge stays
    // fixed. The dominant drag axis decides horizontal vs vertical perspective.
    // forceAxis (Shift) keeps using axisIsHorizontal instead of recomputing it.
    static TransformQuad perspectiveDragCorner(const TransformQuad& startQuad,
                                               int cornerIndex,
                                               const QPointF& startCorner,
                                               const QPointF& currentMouse,
                                               bool forceAxis,
                                               bool axisIsHorizontal);
};
