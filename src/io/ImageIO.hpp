#pragma once

#include <QString>
#include <QSize>
#include <Qt>
#include "io/ImageCodec.hpp"
#include "color/ColorManagementService.hpp"

class Document;
class LayerTreeNode;
class QImage;

struct ExportOptions {
    int quality = -1;
    int compression = -1;
    bool progressive = false;
    bool preserveAlpha = true;
    QString format;
    QSize resizeTo;
    Qt::TransformationMode resampleMode = Qt::SmoothTransformation;

    // ── Colour management on export ──────────────────────────────
    // How the flattened image's colours are handled before writing. Default is
    // the web-safe "convert to sRGB and embed". selectedProfile is only used for
    // ConvertToSelectedProfileAndEmbed.
    ExportColorMode colorMode = ExportColorMode::ConvertToSRgbAndEmbed;
    ColorProfile colorSelectedProfile;
};

QImage compositeImage(Document* doc);
DocumentImage compositeDocumentImage(Document* doc, const QString& sourceFormat = QString());
bool saveImage(Document* doc, const QString& path);
bool saveImage(Document* doc, const QString& path, const ExportOptions& opts);
bool saveImage(Document* doc, const QString& path, const ExportOptions& opts, QString* errorMessage);

// Export Layer/Group As...: composites `node` alone (see
// DocumentCompositor::compositeIsolatedNode). trimToLayerBounds selects
// between the two dimension modes — false keeps the document's canvas size
// with the node positioned within it (Preserve Canvas Size); true crops to
// the node's own rendered bounds (Layer Bounds). Null when
// node->hasIsolatedExportContent() is false (check before offering the
// action in the UI).
QImage compositeNodeImage(Document* doc, const LayerTreeNode* node, bool trimToLayerBounds);
bool saveNodeImage(Document* doc, const LayerTreeNode* node, const QString& path,
                   const ExportOptions& opts, bool trimToLayerBounds, QString* errorMessage);
