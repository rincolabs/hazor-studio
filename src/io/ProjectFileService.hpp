#pragma once

#include <QImage>
#include <QString>
#include <memory>
#include <vector>

#include "core/Document.hpp"
#include "core/GuideTypes.hpp"
#include "color/ColorProfile.hpp"

struct ProjectLoadResult {
    bool ok = false;
    QString error;
    QString projectName;
    QSize canvasSize;
    double resolutionDpi = 300.0;
    QString colorMode = QStringLiteral("RGB Color");
    int bitDepth = 8;
    QString documentType = QStringLiteral("Photo");
    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    int activeFlatIndex = 0;
    float zoom = 1.0f;
    QPointF panOffset = QPointF(0.0, 0.0);
    std::vector<Guide> guides;
    ColorProfile colorProfile = ColorProfile::sRgb();
    ColorProfileSource profileSource = ColorProfileSource::GeneratedDefault;
    anim::AnimationModel animation;
};

class ProjectFileService {
public:
    static bool saveProject(const Document& doc, const QString& path, QString* error = nullptr);
    static ProjectLoadResult loadProject(const QString& path);
};
