#pragma once

#include <QPointF>
#include <QPolygonF>
#include <QTransform>
#include <QImage>
#include <array>
#include <cmath>

enum class HandlePosition {
    None,
    TopLeft, Top, TopRight,
    Right, BottomRight, Bottom,
    BottomLeft, Left,
    Center
};

enum class InteractionMode {
    Idle,
    Moving,
    Resizing,
    Rotating
};

// High-level transform editing mode (spec: Distort / Perspective extend the
// existing Move/Scale/Rotate free-transform). Only Distort and Perspective are
// driven through the quad editor below; the others are kept for completeness.
enum class TransformMode {
    Move,
    Scale,
    Rotate,
    Distort,
    Perspective
};

// Editable quadrilateral used by Distort/Perspective. Vertices are kept in a
// consistent order so they map 1:1 onto the source image corners
// (TopLeft → TopRight → BottomRight → BottomLeft).
struct TransformQuad {
    QPointF topLeft;
    QPointF topRight;
    QPointF bottomRight;
    QPointF bottomLeft;

    QPointF& operator[](int i) {
        switch (i) {
        case 0:  return topLeft;
        case 1:  return topRight;
        case 2:  return bottomRight;
        default: return bottomLeft;
        }
    }
    const QPointF& operator[](int i) const {
        switch (i) {
        case 0:  return topLeft;
        case 1:  return topRight;
        case 2:  return bottomRight;
        default: return bottomLeft;
        }
    }

    QPolygonF toPolygon() const {
        QPolygonF poly;
        poly << topLeft << topRight << bottomRight << bottomLeft;
        return poly;
    }

    // Signed-area magnitude (shoelace). Used to reject collapsed quads.
    double area() const {
        const QPointF p[4] = { topLeft, topRight, bottomRight, bottomLeft };
        double a = 0.0;
        for (int i = 0; i < 4; ++i) {
            const QPointF& c = p[i];
            const QPointF& n = p[(i + 1) % 4];
            a += c.x() * n.y() - n.x() * c.y();
        }
        return std::abs(a) * 0.5;
    }

    bool isFinite() const {
        const QPointF p[4] = { topLeft, topRight, bottomRight, bottomLeft };
        for (const QPointF& c : p) {
            if (!std::isfinite(c.x()) || !std::isfinite(c.y()))
                return false;
        }
        return true;
    }

    // A quad is usable for a homography if every point is finite and it encloses
    // at least the spec's minimum area.
    bool isValid(double minArea = 4.0) const {
        return isFinite() && area() >= minArea;
    }
};

// Non-destructive Distort/Perspective metadata carried on a Layer (alongside
// shapeData/textData). The original pixels and source quad are preserved so the
// warp can be re-edited at any time; the layer's cpuImage holds the rendered
// (warped) result and is composited through the normal affine pipeline.
struct DistortData {
    QImage        sourceImage;   // original layer pixels (RGBA, base size)
    TransformQuad sourceQuad;    // document-px rect the sourceImage maps onto
    TransformQuad quad;          // current editable quad (document px)
    TransformMode mode = TransformMode::Distort;

    // Document-px offset of the rendered result's pixel (0,0). Kept so the
    // layer transform can be rebuilt from the quad after a re-warp.
    QPoint resultOrigin{0, 0};

    // The node transform that was active when `quad`/`sourceQuad` were last made
    // consistent. If the layer is moved/scaled/rotated outside a distort session
    // the node transform changes but these doc-px quads do not; on re-entry the
    // delta (currentTransform * quadTransform^-1) is applied so the editable quad
    // tracks the layer's current position.
    QTransform quadTransform;
};

struct TransformState {
    InteractionMode mode = InteractionMode::Idle;
    HandlePosition activeHandle = HandlePosition::None;
    int flatIndex = -1;
    QPointF startMouseScreen;
    QTransform startTransform;
    float startHw = 1.0f;
    float startHh = 1.0f;
    QPointF anchorCanvas;
    QPointF center;
    float rotation = 0.0f;
};
