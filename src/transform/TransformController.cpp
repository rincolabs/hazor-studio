#include "TransformController.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/ShapeTypes.hpp"
#include "engine/ShapeRenderer.hpp"
#include "engine/ShapeVectorRenderer.hpp"

#include <QTransform>
#include <QPolygonF>
#include <QVector4D>
#include <QMatrix4x4>
#include <algorithm>
#include <cmath>

void TransformController::decompose(const QTransform& t, float& hw, float& hh,
                                     QPointF& center, float& rotation)
{
    hw = std::sqrt(t.m11() * t.m11() + t.m12() * t.m12());
    hh = std::sqrt(t.m21() * t.m21() + t.m22() * t.m22());
    // A negative determinant means the frame is mirrored (flip). Canonical
    // form keeps hw positive and carries the flip on hh's sign, so
    // compose(decompose(M)) == M for any rotation+flip matrix — without this
    // a flipped layer silently unflips on the next decompose/compose round
    // trip (shear is still not representable and is handled by the
    // delta-based application in CanvasView).
    if (t.m11() * t.m22() - t.m12() * t.m21() < 0.0)
        hh = -hh;
    rotation = std::atan2(t.m12(), t.m11());
    center = QPointF(t.m31(), t.m32());
}

QTransform TransformController::compose(float hw, float hh,
                                          const QPointF& center, float rotation)
{
    float c = std::cos(rotation);
    float s = std::sin(rotation);
    QTransform t;
    t.setMatrix(c * hw,  s * hw,   0.0,
                -s * hh, c * hh,   0.0,
                center.x(), center.y(), 1.0);
    return t;
}

static QPointF visualMetric(const QPointF& canvasHalfExtents,
                            const QSize& viewportSize)
{
    return QPointF(
        std::max(1e-6, canvasHalfExtents.x() * std::max(1, viewportSize.width())),
        std::max(1e-6, canvasHalfExtents.y() * std::max(1, viewportSize.height())));
}

static QTransform rectFrame(const QRectF& rect)
{
    QTransform t;
    t.setMatrix(rect.width() * 0.5, 0.0, 0.0,
                0.0, rect.height() * 0.5, 0.0,
                rect.center().x(), rect.center().y(), 1.0);
    return t;
}

static QTransform parentAccumulatedTransform(const LayerTreeNode* node)
{
    QTransform parentAccum;
    for (auto* p = node ? node->parent : nullptr; p; p = p->parent)
        parentAccum = parentAccum * p->transform();
    return parentAccum;
}

void TransformController::decomposeVisual(const QTransform& t,
                                           const QPointF& canvasHalfExtents,
                                           const QSize& viewportSize,
                                           float& hw, float& hh,
                                           QPointF& center, float& rotation)
{
    const QPointF metric = visualMetric(canvasHalfExtents, viewportSize);
    const double mx = metric.x();
    const double my = metric.y();

    const double x0 = mx * t.m11();
    const double y0 = my * t.m12();
    const double x1 = mx * t.m21();
    const double y1 = my * t.m22();

    hw = static_cast<float>(std::sqrt(x0 * x0 + y0 * y0));
    hh = static_cast<float>(std::sqrt(x1 * x1 + y1 * y1));
    // Flip detection — same canonical form as decompose() (sign on hh).
    // mx/my are positive, so the visual-space determinant has the same sign
    // as the raw one.
    if (x0 * y1 - y0 * x1 < 0.0)
        hh = -hh;
    rotation = static_cast<float>(std::atan2(y0, x0));
    center = QPointF(mx * t.m31(), my * t.m32());
}

QTransform TransformController::composeVisual(float hw, float hh,
                                               const QPointF& center,
                                               float rotation,
                                               const QPointF& canvasHalfExtents,
                                               const QSize& viewportSize)
{
    const QPointF metric = visualMetric(canvasHalfExtents, viewportSize);
    const double mx = metric.x();
    const double my = metric.y();
    QTransform visual = compose(hw, hh, center, rotation);

    QTransform t;
    t.setMatrix(visual.m11() / mx, visual.m12() / my, 0.0,
                visual.m21() / mx, visual.m22() / my, 0.0,
                visual.m31() / mx, visual.m32() / my, 1.0);
    return t;
}

QPolygonF TransformController::cornersFromTransform(const QTransform& t)
{
    QPolygonF poly;
    poly << t.map(QPointF(-1, -1))
         << t.map(QPointF( 1, -1))
         << t.map(QPointF( 1,  1))
         << t.map(QPointF(-1,  1));
    return poly;
}

QPolygonF TransformController::cornersFromTransformAndBounds(
    const QTransform& t,
    const QRectF& localPixelBounds,
    const QSizeF& baseSize)
{
    if (localPixelBounds.isEmpty()
        || baseSize.width() <= 0.0
        || baseSize.height() <= 0.0) {
        return cornersFromTransform(t);
    }

    const qreal left = (localPixelBounds.left() / baseSize.width()) * 2.0 - 1.0;
    const qreal right = ((localPixelBounds.left() + localPixelBounds.width())
                         / baseSize.width()) * 2.0 - 1.0;
    const qreal top = 1.0 - (localPixelBounds.top() / baseSize.height()) * 2.0;
    const qreal bottom = 1.0 - ((localPixelBounds.top() + localPixelBounds.height())
                                / baseSize.height()) * 2.0;

    QPolygonF localCorners;
    localCorners << QPointF(left, bottom)
                 << QPointF(right, bottom)
                 << QPointF(right, top)
                 << QPointF(left, top);

    QPolygonF corners;
    for (const QPointF& corner : localCorners)
        corners << t.map(corner);
    return corners;
}

QPolygonF TransformController::cornersFromNode(const LayerTreeNode* node)
{
    if (!node)
        return {};
    return cornersFromTransform(visualFrameForNode(node));
}

// Builds a Shape layer's visual (oriented content) frame, given the LOCAL
// "natural base" transform the live display is measured against. The base is the
// transform an un-transformed freshly-baked raster of this shape would carry;
// `appliedDelta` then re-introduces any live (not-yet-baked) gesture sitting in
// node->transform(). Callers choose the base: the geometry AABB (rectFrame) for the
// canvas box, or the pixel-exact raster transform for a drift-free numeric readback.
static QTransform shapeVisualFrame(const LayerTreeNode* node,
                                   const QTransform& naturalLocal)
{
    const ShapeData& shape = *node->layer->shapeData;
    QRectF localBounds = shape.path.localBounds;
    if (localBounds.isEmpty())
        localBounds = ShapeVectorRenderer::buildPath(shape.path).boundingRect();
    if (localBounds.isEmpty())
        return node->accumulatedTransform();

    const QTransform parentAccum = parentAccumulatedTransform(node);
    const QTransform baseWorld = naturalLocal * parentAccum;
    bool invertible = false;
    const QTransform visualDelta =
        baseWorld.inverted(&invertible) * node->accumulatedTransform();
    const QTransform appliedDelta = invertible ? visualDelta : QTransform();
    return rectFrame(localBounds) * shape.transform.localToCanvas * parentAccum * appliedDelta;
}

QTransform TransformController::visualFrameForNode(const LayerTreeNode* node)
{
    if (!node)
        return {};

    const QTransform transform = node->accumulatedTransform();
    if (!node->layer)
        return transform;

    const Layer* layer = node->layer.get();

    if (layer->shapeData) {
        return shapeVisualFrame(node,
            rectFrame(ShapeRenderer::rasterBounds(*layer->shapeData)));
    }

    // Text keeps its box semantics (the frame is the text box, not the glyph
    // ink bounds — caret/box editing depends on it).
    if (layer->textData)
        return transform;

    // Content bounds: tile-tracked for rasterStorage (dab) layers, cached alpha
    // scan for flat raster layers — so a flat layer with transparency (pasted
    // pixels, deleted regions) gets the same tight, content-hugging transform
    // outline a dab layer has. A fully-covered flat layer resolves to the full
    // rect and falls through to the plain transform below (no extra math).
    QRectF bounds;
    QSizeF baseSize;
    if (layer->usesRasterStorage()) {
        bounds = layer->contentImageBounds();
        baseSize = QSizeF(layer->rasterBaseSize());
    } else {
        bounds = QRectF(layer->cpuContentBounds());
        baseSize = QSizeF(layer->cpuImage.size());
        if (bounds == QRectF(QPointF(0, 0), baseSize))
            return transform;
    }
    if (bounds.isEmpty() || baseSize.width() <= 0.0 || baseSize.height() <= 0.0)
        return transform;

    // Map the unit quad (-1..1) onto the content sub-rect in layer NDC, then
    // through the accumulated transform — so the returned frame's corners equal
    // cornersFromTransformAndBounds() exactly (Y flipped, as in NDC).
    const qreal left = (bounds.left() / baseSize.width()) * 2.0 - 1.0;
    const qreal right = ((bounds.left() + bounds.width()) / baseSize.width()) * 2.0 - 1.0;
    const qreal top = 1.0 - (bounds.top() / baseSize.height()) * 2.0;
    const qreal bottom = 1.0 - ((bounds.top() + bounds.height()) / baseSize.height()) * 2.0;

    const qreal sx = (right - left) * 0.5;
    const qreal sy = (top - bottom) * 0.5;
    const qreal cx = (left + right) * 0.5;
    const qreal cy = (top + bottom) * 0.5;
    const QTransform sub(sx, 0.0, 0.0, sy, cx, cy);
    return sub * transform;
}

QTransform TransformController::visualFrameForNode(const LayerTreeNode* node,
                                                   const QSize& documentSize)
{
    // Only Shape layers benefit; everything else (and missing info) uses the
    // geometry-AABB base via the no-arg version.
    if (!node || !node->layer || !node->layer->shapeData
        || !documentSize.isValid() || documentSize.isEmpty()
        || node->layer->cpuImage.isNull())
        return visualFrameForNode(node);

    const Layer* layer = node->layer.get();
    const ShapeData& shape = *layer->shapeData;

    // Pixel-exact natural base — identical to ImageController's
    // rasterTransformForShape (cpuImage size / document size, centred on the
    // raster bounds), which is exactly what node->transform() holds once the shape
    // is committed. Using this base makes appliedDelta resolve to identity in the
    // settled state, so the readback equals the geometry box with no floor/ceil
    // pixel-snap residue.
    const QRectF rb = ShapeRenderer::rasterBounds(shape);
    const double halfW = layer->cpuImage.width()
        / static_cast<double>(std::max(1, documentSize.width()));
    const double halfH = layer->cpuImage.height()
        / static_cast<double>(std::max(1, documentSize.height()));
    QTransform naturalLocal;
    naturalLocal.setMatrix(halfW, 0.0, 0.0,
                           0.0, halfH, 0.0,
                           rb.center().x(), rb.center().y(), 1.0);
    return shapeVisualFrame(node, naturalLocal);
}

QPointF TransformController::screenToCanvasNdc(
    const QPointF& screenPos,
    float zoom, const QPointF& panOffset,
    const QPointF& canvasHalfExtents,
    const QSize& viewportSize)
{
    float ndcX = 2.0f * static_cast<float>(screenPos.x()) / viewportSize.width() - 1.0f;
    float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / viewportSize.height();

    float invZoom = 1.0f / zoom;
    float docNdcX = (ndcX - panOffset.x()) * invZoom;
    float docNdcY = (ndcY - panOffset.y()) * invZoom;

    float canvasNdcX = docNdcX / canvasHalfExtents.x();
    float canvasNdcY = docNdcY / canvasHalfExtents.y();

    return {canvasNdcX, canvasNdcY};
}

QPointF TransformController::canvasNdcToScreen(
    const QPointF& canvasNdc,
    float zoom, const QPointF& panOffset,
    const QPointF& canvasHalfExtents,
    const QSize& viewportSize)
{
    float docNdcX = canvasNdc.x() * canvasHalfExtents.x();
    float docNdcY = canvasNdc.y() * canvasHalfExtents.y();

    float ndcX = docNdcX * zoom + panOffset.x();
    float ndcY = docNdcY * zoom + panOffset.y();

    float screenX = (ndcX + 1.0f) * 0.5f * viewportSize.width();
    float screenY = (1.0f - ndcY) * 0.5f * viewportSize.height();

    return {screenX, screenY};
}

HandlePosition TransformController::handleAt(
    const QPointF& screenPos,
    const QPolygonF& cornersScreen,
    const QSize& viewportSize,
    float handlePixelSize)
{
    if (cornersScreen.size() < 4) return HandlePosition::None;

    float hwNdc = handlePixelSize / viewportSize.width() * 2.0f;
    float hhNdc = handlePixelSize / viewportSize.height() * 2.0f;
    float maxDist = std::max(hwNdc, hhNdc);

    // 8 handle positions in screen NDC:
    // indices 0-3: corners TL, TR, BR, BL
    // indices 4-7: edge midpoints Top, Right, Bottom, Left
    QPointF handles[8];
    handles[0] = cornersScreen[0]; // TL
    handles[1] = cornersScreen[1]; // TR
    handles[2] = cornersScreen[2]; // BR
    handles[3] = cornersScreen[3]; // BL
    handles[4] = (cornersScreen[0] + cornersScreen[1]) * 0.5; // Top
    handles[5] = (cornersScreen[1] + cornersScreen[2]) * 0.5; // Right
    handles[6] = (cornersScreen[2] + cornersScreen[3]) * 0.5; // Bottom
    handles[7] = (cornersScreen[3] + cornersScreen[0]) * 0.5; // Left

    // Convert screenPos to screen NDC
    float ndcX = 2.0f * static_cast<float>(screenPos.x()) / viewportSize.width() - 1.0f;
    float ndcY = 1.0f - 2.0f * static_cast<float>(screenPos.y()) / viewportSize.height();
    QPointF mouseNdc(ndcX, ndcY);

    // Corner handles
    static constexpr int cornerToHandle[] = {
        static_cast<int>(HandlePosition::TopLeft),
        static_cast<int>(HandlePosition::TopRight),
        static_cast<int>(HandlePosition::BottomRight),
        static_cast<int>(HandlePosition::BottomLeft)
    };

    // Edge handles
    static constexpr int edgeToHandle[] = {
        static_cast<int>(HandlePosition::Top),
        static_cast<int>(HandlePosition::Right),
        static_cast<int>(HandlePosition::Bottom),
        static_cast<int>(HandlePosition::Left)
    };

    // Check corners first (higher priority)
    for (int i = 0; i < 4; ++i) {
        QPointF d = mouseNdc - handles[i];
        if (std::abs(d.x()) <= hwNdc && std::abs(d.y()) <= hhNdc)
            return static_cast<HandlePosition>(cornerToHandle[i]);
    }

    // Then edges
    for (int i = 0; i < 4; ++i) {
        QPointF d = mouseNdc - handles[4 + i];
        if (std::abs(d.x()) <= hwNdc && std::abs(d.y()) <= hhNdc)
            return static_cast<HandlePosition>(edgeToHandle[i]);
    }

    // Check if inside body
    if (cornersScreen.containsPoint(mouseNdc, Qt::OddEvenFill))
        return HandlePosition::Center;

    return HandlePosition::None;
}

QTransform TransformController::updateResize(
    const TransformState& state,
    const QPointF& currentMouseScreen,
    float zoom, const QPointF& panOffset,
    const QPointF& canvasHalfExtents,
    const QSize& viewportSize,
    Qt::KeyboardModifiers modifiers)
{
    float hx = 0, hy = 0;
    switch (state.activeHandle) {
    case HandlePosition::TopLeft:      hx = -1; hy = -1; break;
    case HandlePosition::Top:          hx =  0; hy = -1; break;
    case HandlePosition::TopRight:     hx =  1; hy = -1; break;
    case HandlePosition::Right:        hx =  1; hy =  0; break;
    case HandlePosition::BottomRight:  hx =  1; hy =  1; break;
    case HandlePosition::Bottom:       hx =  0; hy =  1; break;
    case HandlePosition::BottomLeft:   hx = -1; hy =  1; break;
    case HandlePosition::Left:         hx = -1; hy =  0; break;
    default: return state.startTransform;
    }

    QPointF mouseCanvas = screenToCanvasNdc(currentMouseScreen, zoom, panOffset,
                                             canvasHalfExtents, viewportSize);
    const QPointF metric = visualMetric(canvasHalfExtents, viewportSize);
    QPointF mouseVisual(mouseCanvas.x() * metric.x(),
                        mouseCanvas.y() * metric.y());
    float a = state.rotation;
    float c = std::cos(a);
    float s = std::sin(a);
    bool altCenter = modifiers & Qt::AltModifier;

    QPointF refPoint = altCenter ? state.center : state.anchorCanvas;
    QPointF handleDelta = mouseVisual - refPoint;

    // Rotate delta into layer local space
    float localDx = handleDelta.x() * c + handleDelta.y() * s;
    float localDy = -handleDelta.x() * s + handleDelta.y() * c;

    float newHw = state.startHw;
    float newHh = state.startHh;

    auto clampNonZero = [](float v) -> float {
        if (std::abs(v) < 0.001f)
            return (v >= 0.0f) ? 0.001f : -0.001f;
        return v;
    };

    const float divisor = altCenter ? 1.0f : 2.0f;
    if (hx != 0)
        newHw = clampNonZero(localDx / (divisor * hx));
    if (hy != 0)
        newHh = clampNonZero(localDy / (divisor * hy));

    // Default: maintain aspect ratio. Shift = free transform.
    //
    // Project the cursor onto the fixed anchor→corner diagonal and scale both
    // dimensions by that single factor, rather than picking whichever of
    // width/height changed more and deriving the other from it. The old
    // max-delta selection flipped the driving axis mid-drag; each flip snapped
    // the layer between two sizes (newHw/startHw vs newHh/startHh applied to
    // both axes) and the snap grew with drag distance — the "jumps from small
    // to big on large drags" bug. A projection is continuous, so the scale
    // tracks the cursor smoothly. On the diagonal it reduces to the
    // unconstrained scale; off it, it picks the nearest aspect-locked size.
    if (!(modifiers & Qt::ShiftModifier)) {
        const float refVx = divisor * hx * state.startHw;
        const float refVy = divisor * hy * state.startHh;
        const float denom = refVx * refVx + refVy * refVy;
        if (denom > 1e-12f) {
            const float k = (localDx * refVx + localDy * refVy) / denom;
            newHw = clampNonZero(state.startHw * k);
            newHh = clampNonZero(state.startHh * k);
        }
    }

    if (modifiers & Qt::ControlModifier) {
        float step = 0.05f;
        newHw = std::round(newHw / step) * step;
        newHh = std::round(newHh / step) * step;
    }

    // Prevent zero-crossing singularity (but allow negative = flip)
    newHw = clampNonZero(newHw);
    newHh = clampNonZero(newHh);

    QPointF newCenter;
    if (altCenter) {
        newCenter = state.center;
    } else {
        newCenter = refPoint + QPointF(c * newHw * hx - s * newHh * hy,
                                        s * newHw * hx + c * newHh * hy);
    }

    return composeVisual(newHw, newHh, newCenter, a,
                         canvasHalfExtents, viewportSize);
}

QTransform TransformController::updateRotate(
    const TransformState& state,
    const QPointF& currentMouseScreen,
    float zoom, const QPointF& panOffset,
    const QPointF& canvasHalfExtents,
    const QSize& viewportSize,
    Qt::KeyboardModifiers modifiers)
{
    QPointF mouseCanvas = screenToCanvasNdc(currentMouseScreen, zoom, panOffset,
                                             canvasHalfExtents, viewportSize);
    QPointF startCanvas = screenToCanvasNdc(state.startMouseScreen, zoom, panOffset,
                                             canvasHalfExtents, viewportSize);

    const QPointF metric = visualMetric(canvasHalfExtents, viewportSize);
    QPointF centerScaled = state.center;
    QPointF mouseScaled(mouseCanvas.x() * metric.x(),
                        mouseCanvas.y() * metric.y());
    QPointF startScaled(startCanvas.x() * metric.x(),
                        startCanvas.y() * metric.y());

    QPointF d = mouseScaled - centerScaled;
    QPointF startD = startScaled - centerScaled;
    float angle = std::atan2(d.y(), d.x());
    float startAngle = std::atan2(startD.y(), startD.x());

    float deltaAngle = angle - startAngle;

    if (modifiers & Qt::ShiftModifier) {
        float snapDeg = 15.0f;
        float snapRad = snapDeg * M_PI / 180.0f;
        deltaAngle = std::round(deltaAngle / snapRad) * snapRad;
    }

    return composeVisual(state.startHw, state.startHh,
                         state.center, state.rotation + deltaAngle,
                         canvasHalfExtents, viewportSize);
}

// Index of the corner sharing an edge with `cornerIndex`, on the same
// horizontal edge (top edge couples TL↔TR, bottom edge couples BL↔BR).
static int horizontalNeighbor(int cornerIndex)
{
    switch (cornerIndex) {
    case 0: return 1; // TL ↔ TR
    case 1: return 0; // TR ↔ TL
    case 2: return 3; // BR ↔ BL
    default: return 2; // BL ↔ BR
    }
}

// Index of the corner sharing a vertical edge (left edge couples TL↔BL,
// right edge couples TR↔BR).
static int verticalNeighbor(int cornerIndex)
{
    switch (cornerIndex) {
    case 0: return 3; // TL ↔ BL
    case 3: return 0; // BL ↔ TL
    case 1: return 2; // TR ↔ BR
    default: return 1; // BR ↔ TR
    }
}

TransformQuad TransformController::distortDragCorner(
    const TransformQuad& startQuad,
    int cornerIndex,
    const QPointF& startCorner,
    const QPointF& currentMouse,
    bool constrainAxis)
{
    QPointF delta = currentMouse - startCorner;
    if (constrainAxis) {
        if (std::abs(delta.x()) >= std::abs(delta.y()))
            delta.setY(0.0);
        else
            delta.setX(0.0);
    }

    TransformQuad q = startQuad;
    q[cornerIndex] = startQuad[cornerIndex] + delta;
    return q;
}

TransformQuad TransformController::perspectiveDragCorner(
    const TransformQuad& startQuad,
    int cornerIndex,
    const QPointF& startCorner,
    const QPointF& currentMouse,
    bool forceAxis,
    bool axisIsHorizontal)
{
    QPointF delta = currentMouse - startCorner;

    const bool horizontal = forceAxis ? axisIsHorizontal
                                      : (std::abs(delta.x()) >= std::abs(delta.y()));

    TransformQuad q = startQuad;

    if (horizontal) {
        // Move this corner's x by deltaX; the same-edge horizontal neighbour
        // moves by the opposite amount (top/bottom narrows or widens). The
        // opposite edge stays fixed.
        const int neighbor = horizontalNeighbor(cornerIndex);
        q[cornerIndex].setX(startQuad[cornerIndex].x() + delta.x());
        q[neighbor].setX(startQuad[neighbor].x() - delta.x());
    } else {
        // Move this corner's y by deltaY; the same-edge vertical neighbour moves
        // by the opposite amount (left/right narrows or widens).
        const int neighbor = verticalNeighbor(cornerIndex);
        q[cornerIndex].setY(startQuad[cornerIndex].y() + delta.y());
        q[neighbor].setY(startQuad[neighbor].y() - delta.y());
    }

    return q;
}

QCursor TransformController::cursorForHandle(HandlePosition handle)
{
    switch (handle) {
    case HandlePosition::TopLeft:
    case HandlePosition::BottomRight:
        return QCursor(Qt::SizeFDiagCursor);
    case HandlePosition::TopRight:
    case HandlePosition::BottomLeft:
        return QCursor(Qt::SizeBDiagCursor);
    case HandlePosition::Top:
    case HandlePosition::Bottom:
        return QCursor(Qt::SizeVerCursor);
    case HandlePosition::Left:
    case HandlePosition::Right:
        return QCursor(Qt::SizeHorCursor);
    case HandlePosition::Center:
        return QCursor(Qt::SizeAllCursor);
    default:
        return QCursor(Qt::ArrowCursor);
    }
}
