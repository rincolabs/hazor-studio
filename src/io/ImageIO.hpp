#pragma once

#include <QString>
#include <QSize>
#include <Qt>
#include "io/ImageCodec.hpp"
#include "color/ColorManagementService.hpp"

class Document;
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
