#include "GradientTypes.hpp"

#include <QJsonArray>
#include <algorithm>

namespace {
double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

QJsonObject colorStopToJson(const GradientColorStop& stop)
{
    QJsonObject object;
    object.insert(QStringLiteral("color"), stop.color.name(QColor::HexArgb));
    object.insert(QStringLiteral("position"), clamp01(stop.position));
    object.insert(QStringLiteral("midpoint"), clamp01(stop.midpoint));
    return object;
}

QJsonObject opacityStopToJson(const GradientOpacityStop& stop)
{
    QJsonObject object;
    object.insert(QStringLiteral("opacity"), clamp01(stop.opacity));
    object.insert(QStringLiteral("position"), clamp01(stop.position));
    object.insert(QStringLiteral("midpoint"), clamp01(stop.midpoint));
    return object;
}

GradientColorStop colorStopFromJson(const QJsonObject& object)
{
    GradientColorStop stop;
    stop.color = QColor(object.value(QStringLiteral("color")).toString(QStringLiteral("#ff000000")));
    if (!stop.color.isValid())
        stop.color = Qt::black;
    stop.position = clamp01(object.value(QStringLiteral("position")).toDouble(0.0));
    stop.midpoint = clamp01(object.value(QStringLiteral("midpoint")).toDouble(0.5));
    if (stop.midpoint <= 0.0)
        stop.midpoint = 0.001;
    if (stop.midpoint >= 1.0)
        stop.midpoint = 0.999;
    return stop;
}

GradientOpacityStop opacityStopFromJson(const QJsonObject& object)
{
    GradientOpacityStop stop;
    stop.opacity = clamp01(object.value(QStringLiteral("opacity")).toDouble(1.0));
    stop.position = clamp01(object.value(QStringLiteral("position")).toDouble(0.0));
    stop.midpoint = clamp01(object.value(QStringLiteral("midpoint")).toDouble(0.5));
    if (stop.midpoint <= 0.0)
        stop.midpoint = 0.001;
    if (stop.midpoint >= 1.0)
        stop.midpoint = 0.999;
    return stop;
}
}

bool GradientDefinition::isValid() const
{
    return colorStops.size() >= 2
        && opacityStops.size() >= 2
        && smoothness >= 0.0
        && smoothness <= 1.0;
}

void GradientDefinition::normalize()
{
    smoothness = clamp01(smoothness);

    if (colorStops.size() < 2) {
        colorStops = {
            {Qt::black, 0.0, 0.5},
            {Qt::white, 1.0, 0.5},
        };
    }
    if (opacityStops.size() < 2) {
        opacityStops = {
            {1.0, 0.0, 0.5},
            {1.0, 1.0, 0.5},
        };
    }

    for (auto& stop : colorStops) {
        stop.position = clamp01(stop.position);
        stop.midpoint = std::clamp(stop.midpoint, 0.001, 0.999);
        if (!stop.color.isValid())
            stop.color = Qt::black;
    }
    for (auto& stop : opacityStops) {
        stop.opacity = clamp01(stop.opacity);
        stop.position = clamp01(stop.position);
        stop.midpoint = std::clamp(stop.midpoint, 0.001, 0.999);
    }

    std::sort(colorStops.begin(), colorStops.end(),
              [](const auto& a, const auto& b) { return a.position < b.position; });
    std::sort(opacityStops.begin(), opacityStops.end(),
              [](const auto& a, const auto& b) { return a.position < b.position; });
}

QJsonObject GradientDefinition::toJson() const
{
    GradientDefinition copy = *this;
    copy.normalize();

    QJsonObject object;
    object.insert(QStringLiteral("name"), copy.name);
    object.insert(QStringLiteral("kind"), gradientKindKey(copy.kind));
    object.insert(QStringLiteral("type"), QStringLiteral("solid"));
    object.insert(QStringLiteral("smoothness"), copy.smoothness * 100.0);
    object.insert(QStringLiteral("reverse"), copy.reverse);
    object.insert(QStringLiteral("dither"), copy.dither);
    object.insert(QStringLiteral("transparency"), copy.transparency);
    object.insert(QStringLiteral("interpolation"), gradientInterpolationKey(copy.interpolation));

    QJsonArray colors;
    for (const auto& stop : copy.colorStops)
        colors.append(colorStopToJson(stop));
    object.insert(QStringLiteral("colorStops"), colors);

    QJsonArray opacities;
    for (const auto& stop : copy.opacityStops)
        opacities.append(opacityStopToJson(stop));
    object.insert(QStringLiteral("opacityStops"), opacities);

    return object;
}

GradientDefinition GradientDefinition::fromJson(const QJsonObject& object)
{
    GradientDefinition definition;
    definition.name = object.value(QStringLiteral("name")).toString();
    definition.kind = gradientKindFromKey(
        object.value(QStringLiteral("kind")).toString(
            object.value(QStringLiteral("type")).toString(QStringLiteral("linear"))));

    double smoothnessValue = object.value(QStringLiteral("smoothness")).toDouble(100.0);
    if (smoothnessValue > 1.0)
        smoothnessValue /= 100.0;
    definition.smoothness = clamp01(smoothnessValue);
    definition.reverse = object.value(QStringLiteral("reverse")).toBool(false);
    definition.dither = object.value(QStringLiteral("dither")).toBool(true);
    definition.transparency = object.value(QStringLiteral("transparency")).toBool(true);
    definition.interpolation = gradientInterpolationFromKey(
        object.value(QStringLiteral("interpolation")).toString(QStringLiteral("perceptual")));

    const auto colorArray = object.value(QStringLiteral("colorStops")).toArray();
    for (const auto& value : colorArray)
        definition.colorStops.append(colorStopFromJson(value.toObject()));

    const auto opacityArray = object.value(QStringLiteral("opacityStops")).toArray();
    for (const auto& value : opacityArray)
        definition.opacityStops.append(opacityStopFromJson(value.toObject()));

    definition.normalize();
    return definition;
}

QString gradientKindName(GradientKind kind)
{
    switch (kind) {
    case GradientKind::Linear: return QStringLiteral("Linear");
    case GradientKind::Radial: return QStringLiteral("Radial");
    case GradientKind::Angle: return QStringLiteral("Angle");
    case GradientKind::Reflected: return QStringLiteral("Reflected");
    case GradientKind::Diamond: return QStringLiteral("Diamond");
    }
    return QStringLiteral("Linear");
}

QString gradientKindKey(GradientKind kind)
{
    switch (kind) {
    case GradientKind::Linear: return QStringLiteral("linear");
    case GradientKind::Radial: return QStringLiteral("radial");
    case GradientKind::Angle: return QStringLiteral("angle");
    case GradientKind::Reflected: return QStringLiteral("reflected");
    case GradientKind::Diamond: return QStringLiteral("diamond");
    }
    return QStringLiteral("linear");
}

GradientKind gradientKindFromKey(const QString& key, GradientKind fallback)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QLatin1String("linear"))
        return GradientKind::Linear;
    if (normalized == QLatin1String("radial"))
        return GradientKind::Radial;
    if (normalized == QLatin1String("angle"))
        return GradientKind::Angle;
    if (normalized == QLatin1String("reflected"))
        return GradientKind::Reflected;
    if (normalized == QLatin1String("diamond"))
        return GradientKind::Diamond;
    return fallback;
}

QString gradientInterpolationName(GradientInterpolationMethod method)
{
    switch (method) {
    case GradientInterpolationMethod::Perceptual: return QStringLiteral("Perceptual");
    case GradientInterpolationMethod::Linear: return QStringLiteral("Linear");
    case GradientInterpolationMethod::Classic: return QStringLiteral("Classic");
    }
    return QStringLiteral("Perceptual");
}

QString gradientInterpolationKey(GradientInterpolationMethod method)
{
    switch (method) {
    case GradientInterpolationMethod::Perceptual: return QStringLiteral("perceptual");
    case GradientInterpolationMethod::Linear: return QStringLiteral("linear");
    case GradientInterpolationMethod::Classic: return QStringLiteral("classic");
    }
    return QStringLiteral("perceptual");
}

GradientInterpolationMethod gradientInterpolationFromKey(
    const QString& key,
    GradientInterpolationMethod fallback)
{
    const QString normalized = key.trimmed().toLower();
    if (normalized == QLatin1String("perceptual"))
        return GradientInterpolationMethod::Perceptual;
    if (normalized == QLatin1String("linear"))
        return GradientInterpolationMethod::Linear;
    if (normalized == QLatin1String("classic"))
        return GradientInterpolationMethod::Classic;
    return fallback;
}
