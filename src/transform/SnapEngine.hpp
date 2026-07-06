#pragma once

#include "core/GuideTypes.hpp"

#include <QRectF>
#include <QSizeF>
#include <QVector>

enum class SnapAxis {
    X,
    Y
};

struct SnapIndicator {
    SnapAxis axis = SnapAxis::X;
    qreal position = 0.0;
    QString label;
};

struct SnapContext {
    QSizeF documentSize;
    std::vector<Guide> guides;
    SnapSettings snapSettings;
    GuideSettings guideSettings;
    qreal screenPixelsPerDocumentX = 1.0;
    qreal screenPixelsPerDocumentY = 1.0;
};

struct SnapResult {
    QPointF adjustedDelta;
    QPointF snapOffset;
    bool snappedX = false;
    bool snappedY = false;
    QVector<SnapIndicator> indicators;

    bool snapped() const { return snappedX || snappedY; }
};

// Which edges/centers of the moving bounds are allowed to snap. Move uses all
// of them; a resize must only snap the edges its active handle drags (otherwise
// the fixed anchor edge can win the snap and the dragged edge never reaches the
// target — "snap sometimes doesn't work" on scale).
struct SnapSources {
    bool left = true;
    bool centerX = true;
    bool right = true;
    bool top = true;
    bool centerY = true;
    bool bottom = true;
};

class SnapEngine {
public:
    static SnapResult snapMove(const QRectF& movingBounds,
                               const QPointF& proposedDelta,
                               const SnapContext& context);
    static SnapResult snapBounds(const QRectF& movedBounds,
                                 const SnapContext& context,
                                 const SnapSources& sources = {});
};

