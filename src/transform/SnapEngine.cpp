#include "SnapEngine.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <QByteArray>
#include <QDebug>

namespace {
bool snapDebugEnabled()
{
    static const bool enabled = [] {
        const QByteArray value = qgetenv("IMAGE_EDITOR_SNAP_DEBUG").trimmed().toLower();
        return !value.isEmpty() && value != "0" && value != "false" && value != "off";
    }();
    return enabled;
}

struct SnapCandidate {
    qreal source = 0.0;
    qreal target = 0.0;
    QString label;
    qreal screenDistance = std::numeric_limits<qreal>::max();
};

QString guideLabel(qreal position)
{
    return QStringLiteral("Guide %1 px").arg(position, 0, 'f', 0);
}

void considerCandidate(SnapCandidate& best,
                       const QString& sourceName,
                       qreal source,
                       qreal target,
                       const QString& label,
                       qreal pixelsPerDocument,
                       qreal tolerance)
{
    if (pixelsPerDocument <= 0.0)
        return;

    const qreal distance = std::abs(target - source) * pixelsPerDocument;
    const bool withinTolerance = distance <= tolerance;
    const bool better = distance < best.screenDistance;
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[SnapEngine][candidate]"
            << "source" << sourceName
            << "sourceDoc" << source
            << "target" << label
            << "targetDoc" << target
            << "distanceScreenPx" << distance
            << "tolerance" << tolerance
            << "within" << withinTolerance
            << "better" << better;
    }

    if (withinTolerance && better) {
        best.source = source;
        best.target = target;
        best.label = label;
        best.screenDistance = distance;
    }
}

bool hasCandidate(const SnapCandidate& candidate)
{
    return candidate.screenDistance < std::numeric_limits<qreal>::max();
}
}

SnapResult SnapEngine::snapMove(const QRectF& movingBounds,
                                const QPointF& proposedDelta,
                                const SnapContext& context)
{
    QRectF moved = movingBounds.translated(proposedDelta);
    SnapResult result = snapBounds(moved, context);
    result.adjustedDelta = proposedDelta + result.snapOffset;
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[SnapEngine][move]"
            << "startBounds" << movingBounds
            << "proposedDelta" << proposedDelta
            << "movedBounds" << moved
            << "snapOffset" << result.snapOffset
            << "adjustedDelta" << result.adjustedDelta
            << "snappedX" << result.snappedX
            << "snappedY" << result.snappedY
            << "indicatorCount" << result.indicators.size();
    }
    return result;
}

SnapResult SnapEngine::snapBounds(const QRectF& movedBounds,
                                  const SnapContext& context,
                                  const SnapSources& sources)
{
    SnapResult result;
    result.adjustedDelta = QPointF(0.0, 0.0);

    if (!context.snapSettings.enabled || context.documentSize.isEmpty()) {
        if (snapDebugEnabled()) {
            qInfo().noquote()
                << "[SnapEngine][bounds] disabled"
                << "enabled" << context.snapSettings.enabled
                << "documentSize" << context.documentSize;
        }
        return result;
    }

    const qreal tolerance = std::max<qreal>(1.0, context.snapSettings.toleranceScreenPx);
    const QRectF bounds = movedBounds.normalized();
    const qreal docW = context.documentSize.width();
    const qreal docH = context.documentSize.height();

    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[SnapEngine][bounds]"
            << "bounds" << bounds
            << "documentSize" << context.documentSize
            << "pxPerDocX" << context.screenPixelsPerDocumentX
            << "pxPerDocY" << context.screenPixelsPerDocumentY
            << "toleranceScreenPx" << tolerance
            << "snapCanvasBounds" << context.snapSettings.snapToCanvasBounds
            << "snapCanvasCenter" << context.snapSettings.snapToCanvasCenter
            << "snapGuides" << context.snapSettings.snapToGuides
            << "guideSnap" << context.guideSettings.snapToGuides
            << "showGuides" << context.guideSettings.showGuides
            << "guideCount" << context.guides.size();
    }

    SnapCandidate bestX;
    SnapCandidate bestY;

    QVector<QPair<qreal, QString>> xSources;
    if (sources.left)    xSources.push_back({bounds.left(),       QStringLiteral("Left")});
    if (sources.centerX) xSources.push_back({bounds.center().x(), QStringLiteral("Center X")});
    if (sources.right)   xSources.push_back({bounds.right(),      QStringLiteral("Right")});
    QVector<QPair<qreal, QString>> ySources;
    if (sources.top)     ySources.push_back({bounds.top(),        QStringLiteral("Top")});
    if (sources.centerY) ySources.push_back({bounds.center().y(), QStringLiteral("Center Y")});
    if (sources.bottom)  ySources.push_back({bounds.bottom(),     QStringLiteral("Bottom")});

    auto considerXTarget = [&](qreal target, const QString& label) {
        for (const auto& source : xSources)
            considerCandidate(bestX, source.second, source.first, target, label,
                              context.screenPixelsPerDocumentX, tolerance);
    };
    auto considerYTarget = [&](qreal target, const QString& label) {
        for (const auto& source : ySources)
            considerCandidate(bestY, source.second, source.first, target, label,
                              context.screenPixelsPerDocumentY, tolerance);
    };

    if (context.snapSettings.snapToCanvasBounds) {
        considerXTarget(0.0, QStringLiteral("Canvas Left"));
        considerXTarget(docW, QStringLiteral("Canvas Right"));
        considerYTarget(0.0, QStringLiteral("Canvas Top"));
        considerYTarget(docH, QStringLiteral("Canvas Bottom"));
    }

    if (context.snapSettings.snapToCanvasCenter) {
        considerXTarget(docW * 0.5, QStringLiteral("Center X"));
        considerYTarget(docH * 0.5, QStringLiteral("Center Y"));
    }

    if (context.snapSettings.snapToGuides
        && context.guideSettings.snapToGuides
        && context.guideSettings.showGuides) {
        for (const Guide& guide : context.guides) {
            if (snapDebugEnabled()) {
                qInfo().noquote()
                    << "[SnapEngine][guide-target]"
                    << (guide.orientation == GuideOrientation::Vertical ? "vertical" : "horizontal")
                    << "position" << guide.position
                    << "id" << guide.id;
            }
            if (guide.orientation == GuideOrientation::Vertical)
                considerXTarget(guide.position, guideLabel(guide.position));
            else
                considerYTarget(guide.position, guideLabel(guide.position));
        }
    }

    if (hasCandidate(bestX)) {
        result.snappedX = true;
        result.snapOffset.setX(bestX.target - bestX.source);
        result.indicators.push_back({SnapAxis::X, bestX.target, bestX.label});
    }

    if (hasCandidate(bestY)) {
        result.snappedY = true;
        result.snapOffset.setY(bestY.target - bestY.source);
        result.indicators.push_back({SnapAxis::Y, bestY.target, bestY.label});
    }

    result.adjustedDelta = result.snapOffset;
    if (snapDebugEnabled()) {
        qInfo().noquote()
            << "[SnapEngine][result]"
            << "snappedX" << result.snappedX
            << "snappedY" << result.snappedY
            << "snapOffset" << result.snapOffset
            << "indicatorCount" << result.indicators.size();
    }
    return result;
}
