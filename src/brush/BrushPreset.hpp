#pragma once

#include "BrushTypes.hpp"
#include <QString>
#include <QJsonObject>

struct BrushPreset {
    QString name;
    BrushSettings settings;

    // Optional library folder path the preset lives in, e.g.
    // "Imported Brushes/Krita/Concept Brushes". Empty = ungrouped (the
    // default library behaviour, fully back-compatible).
    QString group;

    // Optional import provenance (source type, original file, imported-at,
    // original preset name). Empty for hand-made presets.
    QJsonObject sourceInfo;

    // Optional preset preview supplied by the source file (e.g. an imported brush's
    // own icon), stored as a base64 PNG. When present the brush list shows it as the
    // thumbnail; otherwise the list falls back to rendering the tip. Empty for
    // hand-made presets.
    QString previewImageData;

    QJsonObject toJson() const;
    static BrushPreset fromJson(const QJsonObject& obj);
};
