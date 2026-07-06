#include "ImageIO.hpp"
#include "renderer/DocumentCompositor.hpp"
#include "renderer/RenderContext.hpp"
#include "core/Document.hpp"
#include "color/ColorManagementService.hpp"
#include <QFileInfo>

QImage compositeImage(Document* doc)
{
    RenderContext ctx;
    ctx.document   = doc;
    ctx.targetType = RenderTargetType::Export;
    if (doc) ctx.outputSize = doc->size;
    return DocumentCompositor::composite(doc, ctx);
}

bool saveImage(Document* doc, const QString& path)
{
    ExportOptions opts;
    return saveImage(doc, path, opts);
}

bool saveImage(Document* doc, const QString& path, const ExportOptions& opts)
{
    QString errorMessage;
    return saveImage(doc, path, opts, &errorMessage);
}

DocumentImage compositeDocumentImage(Document* doc, const QString& sourceFormat)
{
    QImage img = compositeImage(doc);
    DocumentImage image = convertQImageToDocumentImageValue(img, QString(), sourceFormat);
    if (doc) {
        image.colorProfile = doc->colorProfile();
        image.colorProfile.setSource(doc->profileSource());
        image.iccProfile = image.colorProfile.iccBytes();
    }
    return image;
}

bool saveImage(Document* doc, const QString& path, const ExportOptions& opts, QString* errorMessage)
{
    QImage img = compositeImage(doc);
    if (img.isNull()) {
        if (errorMessage)
            *errorMessage = QStringLiteral("Could not render the document for export.");
        return false;
    }

    if (opts.resizeTo.isValid() && !opts.resizeTo.isEmpty())
        img = img.scaled(opts.resizeTo, Qt::IgnoreAspectRatio, opts.resampleMode);

    const QString extension = normalizeImageExtension(
        QFileInfo(path).suffix().isEmpty() ? opts.format : QFileInfo(path).suffix());
    ImageCodec* codec = imageCodecRegistry().findWriter(extension);
    if (!codec) {
        if (errorMessage) {
            *errorMessage = QStringLiteral(
                "This file format cannot be written by the current Qt/OpenCV build.");
        }
        return false;
    }

    ImageSaveOptions saveOptions;
    saveOptions.quality = opts.quality >= 0 ? opts.quality : 90;
    saveOptions.compressionLevel = opts.compression;
    saveOptions.progressive = opts.progressive;
    saveOptions.preserveAlpha = opts.preserveAlpha
        && opts.format.compare(QStringLiteral("JPEG"), Qt::CaseInsensitive) != 0
        && opts.format.compare(QStringLiteral("JPG"), Qt::CaseInsensitive) != 0;

    DocumentImage image = convertQImageToDocumentImageValue(img, QString(), extension);
    if (doc) {
        image.colorProfile = doc->colorProfile();
        image.colorProfile.setSource(doc->profileSource());
        image.iccProfile = image.colorProfile.iccBytes();
    }
    ExportColorOptions colorOptions;
    colorOptions.mode = opts.colorMode;
    colorOptions.selectedProfile = opts.colorSelectedProfile;
    colorOptions.conversionOptions =
        ColorManagementService::instance().defaultConversionOptions();
    ExportColorResult colorResult = ColorManagementService::instance().prepareForExport(
        image,
        doc ? doc->colorProfile() : ColorProfile::sRgb(),
        colorOptions);
    image = colorResult.image;
    image.iccProfile = colorResult.iccBytesToEmbed;
    saveOptions.embedColorProfile = (opts.colorMode != ExportColorMode::DoNotEmbedProfile)
        && !image.iccProfile.isEmpty();
    return codec->write(path, image, saveOptions, errorMessage);
}
