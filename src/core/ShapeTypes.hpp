#pragma once

#include <QRectF>
#include <QPointF>
#include <QColor>
#include <QString>
#include <QTransform>
#include <QVariantMap>
#include <vector>

enum class ShapeToolMode {
    Rectangle = 0,
    Ellipse,
    Line,
    Polygon,
    Arrow,
    Star,
    CustomShape
};

enum class PathCommandType {
    MoveTo,
    LineTo,
    QuadTo,
    CubicTo,
    ClosePath
};

struct PathCommand {
    PathCommandType type = PathCommandType::MoveTo;
    QPointF p1;
    QPointF p2;
    QPointF p3;
};

struct VectorPath {
    std::vector<PathCommand> commands;
    Qt::FillRule fillRule = Qt::WindingFill;
    QRectF localBounds;
    bool closed = true;
};

struct ShapeStyle {
    bool fillEnabled = true;
    QColor fillColor{200, 200, 200, 255};
    bool strokeEnabled = true;
    QColor strokeColor{0, 0, 0, 255};
    double strokeWidth = 0.002;
    bool antiAlias = true;
};

struct ShapeTransform {
    QTransform localToCanvas;
};

struct ShapeMetadata {
    QString presetId;
    QVariantMap parameters;
    bool parametricEditable = false;
};

struct ShapeData {
    VectorPath path;
    ShapeStyle style;
    ShapeTransform transform;
    ShapeMetadata metadata;

    QString autoName() const {
        if (metadata.presetId == QLatin1String("rectangle")) return QStringLiteral("Rectangle");
        if (metadata.presetId == QLatin1String("ellipse")) return QStringLiteral("Ellipse");
        if (metadata.presetId == QLatin1String("line")) return QStringLiteral("Line");
        if (metadata.presetId == QLatin1String("polygon")) return QStringLiteral("Polygon");
        if (metadata.presetId == QLatin1String("arrow")) return QStringLiteral("Arrow");
        if (metadata.presetId == QLatin1String("star")) return QStringLiteral("Star");
        if (metadata.presetId == QLatin1String("custom-svg-icon")) {
            const QString name = metadata.parameters.value(QStringLiteral("sourceIconName")).toString();
            return name.isEmpty() ? QStringLiteral("Custom Shape") : name;
        }
        return QStringLiteral("Shape");
    }
};
