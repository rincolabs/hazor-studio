#include "GuideTypes.hpp"

#include <algorithm>

namespace {
qreal clampedOpacity(qreal value)
{
    return std::clamp(value, 0.05, 1.0);
}
}

QString rulerUnitToSettingsValue(RulerUnit unit)
{
    switch (unit) {
    case RulerUnit::Percent: return QStringLiteral("percent");
    case RulerUnit::Inches: return QStringLiteral("in");
    case RulerUnit::Centimeters: return QStringLiteral("cm");
    case RulerUnit::Millimeters: return QStringLiteral("mm");
    case RulerUnit::Pixels:
    default:
        return QStringLiteral("px");
    }
}

RulerUnit rulerUnitFromSettingsValue(const QString& value)
{
    const QString v = value.trimmed().toLower();
    if (v == QLatin1String("percent") || v == QLatin1String("%"))
        return RulerUnit::Percent;
    if (v == QLatin1String("in"))
        return RulerUnit::Inches;
    if (v == QLatin1String("cm"))
        return RulerUnit::Centimeters;
    if (v == QLatin1String("mm"))
        return RulerUnit::Millimeters;
    return RulerUnit::Pixels;
}

RulerGuideSettings RulerGuideSettings::load()
{
    RulerGuideSettings out;
    QSettings settings;

    settings.beginGroup(QStringLiteral("Rulers"));
    out.rulers.showRulers = settings.value(QStringLiteral("showRulers"), out.rulers.showRulers).toBool();
    out.rulers.unit = rulerUnitFromSettingsValue(settings.value(QStringLiteral("unit"), QStringLiteral("px")).toString());
    out.rulers.majorTickSpacing = std::max<qreal>(1.0, settings.value(QStringLiteral("majorTickSpacing"), out.rulers.majorTickSpacing).toDouble());
    out.rulers.minorTickDivisions = std::clamp(settings.value(QStringLiteral("minorTickDivisions"), out.rulers.minorTickDivisions).toInt(), 1, 20);
    out.rulers.rulerSize = std::clamp(settings.value(QStringLiteral("rulerSize"), out.rulers.rulerSize).toInt(), 16, 64);
    out.rulers.showMouseIndicator = settings.value(QStringLiteral("showMouseIndicator"), out.rulers.showMouseIndicator).toBool();
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Guides"));
    out.guides.showGuides = settings.value(QStringLiteral("showGuides"), out.guides.showGuides).toBool();
    out.guides.lockGuides = settings.value(QStringLiteral("lockGuides"), out.guides.lockGuides).toBool();
    out.guides.snapToGuides = settings.value(QStringLiteral("snapToGuides"), out.guides.snapToGuides).toBool();
    out.guides.guideColor = settings.value(QStringLiteral("guideColor"), out.guides.guideColor).value<QColor>();
    out.guides.guideOpacity = clampedOpacity(settings.value(QStringLiteral("guideOpacity"), out.guides.guideOpacity).toDouble());
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Snap"));
    out.snap.enabled = settings.value(QStringLiteral("enabled"), out.snap.enabled).toBool();
    out.snap.snapToCanvasBounds = settings.value(QStringLiteral("snapToCanvasBounds"), out.snap.snapToCanvasBounds).toBool();
    out.snap.snapToCanvasCenter = settings.value(QStringLiteral("snapToCanvasCenter"), out.snap.snapToCanvasCenter).toBool();
    out.snap.snapToGuides = settings.value(QStringLiteral("snapToGuides"), out.snap.snapToGuides).toBool();
    out.snap.toleranceScreenPx = std::clamp<qreal>(settings.value(QStringLiteral("toleranceScreenPx"), out.snap.toleranceScreenPx).toDouble(), 1.0, 64.0);
    out.snap.showIndicators = settings.value(QStringLiteral("showIndicators"), out.snap.showIndicators).toBool();
    settings.endGroup();

    return out;
}

void RulerGuideSettings::save(const RulerGuideSettings& value)
{
    QSettings settings;

    settings.beginGroup(QStringLiteral("Rulers"));
    settings.setValue(QStringLiteral("showRulers"), value.rulers.showRulers);
    settings.setValue(QStringLiteral("unit"), rulerUnitToSettingsValue(value.rulers.unit));
    settings.setValue(QStringLiteral("majorTickSpacing"), std::max<qreal>(1.0, value.rulers.majorTickSpacing));
    settings.setValue(QStringLiteral("minorTickDivisions"), std::clamp(value.rulers.minorTickDivisions, 1, 20));
    settings.setValue(QStringLiteral("rulerSize"), std::clamp(value.rulers.rulerSize, 16, 64));
    settings.setValue(QStringLiteral("showMouseIndicator"), value.rulers.showMouseIndicator);
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Guides"));
    settings.setValue(QStringLiteral("showGuides"), value.guides.showGuides);
    settings.setValue(QStringLiteral("lockGuides"), value.guides.lockGuides);
    settings.setValue(QStringLiteral("snapToGuides"), value.guides.snapToGuides);
    settings.setValue(QStringLiteral("guideColor"), value.guides.guideColor);
    settings.setValue(QStringLiteral("guideOpacity"), clampedOpacity(value.guides.guideOpacity));
    settings.endGroup();

    settings.beginGroup(QStringLiteral("Snap"));
    settings.setValue(QStringLiteral("enabled"), value.snap.enabled);
    settings.setValue(QStringLiteral("snapToCanvasBounds"), value.snap.snapToCanvasBounds);
    settings.setValue(QStringLiteral("snapToCanvasCenter"), value.snap.snapToCanvasCenter);
    settings.setValue(QStringLiteral("snapToGuides"), value.snap.snapToGuides);
    settings.setValue(QStringLiteral("toleranceScreenPx"), std::clamp<qreal>(value.snap.toleranceScreenPx, 1.0, 64.0));
    settings.setValue(QStringLiteral("showIndicators"), value.snap.showIndicators);
    settings.endGroup();
}

