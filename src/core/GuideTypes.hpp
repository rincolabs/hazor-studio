#pragma once

#include <QColor>
#include <QSettings>
#include <QString>

enum class GuideOrientation {
    Vertical,
    Horizontal
};

enum class RulerUnit {
    Pixels,
    Percent,
    Inches,
    Centimeters,
    Millimeters
};

struct Guide {
    GuideOrientation orientation = GuideOrientation::Vertical;
    qreal position = 0.0;
    int id = 0;
};

struct RulerSettings {
    bool showRulers = true;
    RulerUnit unit = RulerUnit::Pixels;
    qreal majorTickSpacing = 100.0;
    int minorTickDivisions = 5;
    int rulerSize = 24;
    bool showMouseIndicator = true;
};

struct GuideSettings {
    bool showGuides = true;
    bool lockGuides = false;
    bool snapToGuides = true;
    QColor guideColor = QColor(76, 141, 255);
    qreal guideOpacity = 0.75;
};

struct SnapSettings {
    bool enabled = true;
    bool snapToCanvasBounds = true;
    bool snapToCanvasCenter = true;
    bool snapToGuides = true;
    qreal toleranceScreenPx = 8.0;
    bool showIndicators = true;
};

struct RulerGuideSettings {
    RulerSettings rulers;
    GuideSettings guides;
    SnapSettings snap;

    static RulerGuideSettings load();
    static void save(const RulerGuideSettings& settings);
};

QString rulerUnitToSettingsValue(RulerUnit unit);
RulerUnit rulerUnitFromSettingsValue(const QString& value);

