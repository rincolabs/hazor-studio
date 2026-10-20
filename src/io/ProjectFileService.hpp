#pragma once

#include <QImage>
#include <QString>
#include <memory>
#include <vector>

#include "core/Document.hpp"
#include "core/GuideTypes.hpp"
#include "color/ColorProfile.hpp"

class DocumentContainer;

struct ProjectLoadResult {
    bool ok = false;
    QString error;
    QString projectName;
    QSize canvasSize;
    double resolutionDpi = 300.0;
    QString colorMode = QStringLiteral("RGB Color");
    int bitDepth = 8;
    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    int activeFlatIndex = 0;
    float zoom = 1.0f;
    QPointF panOffset = QPointF(0.0, 0.0);
    std::vector<Guide> guides;
    std::vector<DocumentChannel> channels;
    ColorProfile colorProfile = ColorProfile::sRgb();
    ColorProfileSource profileSource = ColorProfileSource::GeneratedDefault;
};

struct ContainerLoadResult {
    bool ok = false;
    QString error;
    // Every document in the file: the root plus its embedded Smart Object
    // documents, already wired by docId. Never null when ok.
    std::shared_ptr<DocumentContainer> container;
};

class ProjectFileService {
public:
    static bool saveProject(const Document& doc, const QString& path, QString* error = nullptr);
    static ProjectLoadResult loadProject(const QString& path);

    // Multi-document form. Writes the container's root document plus every
    // embedded document REACHABLE FROM THE ROOT — documents orphaned by an
    // edit are kept alive in memory by the undo history but deliberately not
    // persisted, which is what stops a .hazor from accumulating dead subgraphs.
    // Updates nothing on the container; the caller owns setPath().
    static bool saveContainer(const DocumentContainer& container,
                              const QString& path, QString* error = nullptr);
    // Reads any version (1..3). A v1/v2 file — or a v3 file with no embedded
    // documents — yields a container holding exactly one document, so callers
    // need no special case for "a project without Smart Objects".
    static ContainerLoadResult loadContainer(const QString& path);
};
