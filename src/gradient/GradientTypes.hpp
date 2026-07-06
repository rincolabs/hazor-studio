#pragma once

#include "core/Layer.hpp"

#include <QColor>
#include <QJsonObject>
#include <QMetaType>
#include <QPointF>
#include <QRect>
#include <QString>
#include <QVector>

enum class GradientKind {
    Linear = 0,
    Radial,
    Angle,
    Reflected,
    Diamond
};

enum class GradientInterpolationMethod {
    Perceptual = 0,
    Linear,
    Classic
};

struct GradientColorStop {
    QColor color = Qt::black;
    double position = 0.0;
    double midpoint = 0.5;
};

struct GradientOpacityStop {
    double opacity = 1.0;
    double position = 0.0;
    double midpoint = 0.5;
};

struct GradientDefinition {
    QString name;
    GradientKind kind = GradientKind::Linear;
    QVector<GradientColorStop> colorStops;
    QVector<GradientOpacityStop> opacityStops;
    double smoothness = 1.0;
    bool reverse = false;
    bool dither = true;
    bool transparency = true;
    GradientInterpolationMethod interpolation = GradientInterpolationMethod::Perceptual;

    bool isValid() const;
    void normalize();

    QJsonObject toJson() const;
    static GradientDefinition fromJson(const QJsonObject& object);
};

struct GradientFillData {
    GradientDefinition definition;
    QPointF startPoint;
    QPointF endPoint;
    BlendMode blendMode = BlendMode::Normal;
    double opacity = 1.0;
};

struct GradientApplication {
    GradientDefinition definition;
    QPointF startPoint;
    QPointF endPoint;
    BlendMode blendMode = BlendMode::Normal;
    double opacity = 1.0;
    bool lockAlpha = false;
    QRect affectedRegion;
};

QString gradientKindName(GradientKind kind);
QString gradientKindKey(GradientKind kind);
GradientKind gradientKindFromKey(const QString& key, GradientKind fallback = GradientKind::Linear);

QString gradientInterpolationName(GradientInterpolationMethod method);
QString gradientInterpolationKey(GradientInterpolationMethod method);
GradientInterpolationMethod gradientInterpolationFromKey(
    const QString& key,
    GradientInterpolationMethod fallback = GradientInterpolationMethod::Perceptual);

Q_DECLARE_METATYPE(GradientDefinition)
Q_DECLARE_METATYPE(GradientApplication)

