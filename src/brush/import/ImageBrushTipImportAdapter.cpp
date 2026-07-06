#include "ImageBrushTipImportAdapter.hpp"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QtGlobal>

namespace {

// Produce an engine tip (coverage in the alpha channel) from any image.
QImage normalizeToTip(const QImage& srcIn, bool invertAlpha, bool& usedAlpha)
{
    const QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);
    usedAlpha = src.hasAlphaChannel();
    if (usedAlpha) {
        bool varies = false;
        for (int y = 0; y < src.height() && !varies; ++y) {
            const QRgb* r = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < src.width(); ++x)
                if (qAlpha(r[x]) < 255) { varies = true; break; }
        }
        usedAlpha = varies;
    }
    QImage tip(src.size(), QImage::Format_ARGB32);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* d = reinterpret_cast<QRgb*>(tip.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            int cov = usedAlpha ? qAlpha(s[x]) : (255 - qGray(s[x]));
            if (invertAlpha) cov = 255 - cov;
            d[x] = qRgba(0, 0, 0, cov);
        }
    }
    return tip;
}

} // namespace

bool ImageBrushTipImportAdapter::canImport(const QString& filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (!supportedExtensions().contains(suffix))
        return false;
    // Trust the editor's own readable set so we never claim a format the app
    // cannot actually decode.
    return QImageReader(filePath).canRead();
}

ImportedBrushPackage ImageBrushTipImportAdapter::scan(const QString& filePath)
{
    return parse(filePath, /*scanOnly=*/true, /*invertAlpha=*/false);
}

ImportedBrushPackage ImageBrushTipImportAdapter::importFile(const QString& filePath,
                                                            const BrushImportOptions& options)
{
    return parse(filePath, /*scanOnly=*/false, options.invertImportedAlpha);
}

ImportedBrushPackage ImageBrushTipImportAdapter::parse(const QString& filePath, bool scanOnly,
                                                       bool invertAlpha)
{
    ImportedBrushPackage pkg;
    pkg.sourceFilePath = filePath;
    pkg.sourceType = BrushImportSourceType::ImageBrushTip;
    pkg.packageName = QFileInfo(filePath).completeBaseName();

    auto fail = [&](const QString& msg) {
        BrushImportDiagnostic d;
        d.severity = BrushImportSeverity::Error;
        d.code = QStringLiteral("corrupted_file");
        d.message = msg; d.sourceFile = filePath;
        pkg.diagnostics.append(d);
        return pkg;
    };

    if (QFileInfo(filePath).size() > BrushImportLimits::MaxFileSize)
        return fail(QStringLiteral("The image is too large to import as a brush tip."));

    QImageReader reader(filePath);
    const QSize size = reader.size();
    if (size.isValid()
        && (size.width() > BrushImportLimits::MaxBrushTipDimension
            || size.height() > BrushImportLimits::MaxBrushTipDimension))
        // Allow but scale down at conversion time via the engine; just warn here.
        pkg.diagnostics.append({ BrushImportSeverity::Info, QStringLiteral("fallback_used"),
                                 QStringLiteral("The image is very large and will be scaled to the "
                                                "brush size when painting."),
                                 filePath, pkg.packageName });

    ImportedBrushPreset preset;
    preset.sourceType = BrushImportSourceType::ImageBrushTip;
    preset.sourceFilePath = filePath;
    preset.name = QFileInfo(filePath).completeBaseName();
    preset.originalEngineName = QStringLiteral("Image");
    preset.compatibilityLabel = QStringLiteral("Compatible");
    preset.spacing = 25.0f;
    preset.hardness = 1.0f;
    preset.opacity = 1.0f;
    preset.flow = 1.0f;
    // Basic preset uses pressure to drive size, as the spec describes.
    preset.dynamics.pressureAffectsSize = true;

    if (scanOnly) {
        preset.size = size.isValid() ? float(qBound(1, qMax(size.width(), size.height()), 5000)) : 64.0f;
        pkg.presets.append(preset);
        return pkg;
    }

    QImage img = reader.read();
    if (img.isNull())
        return fail(QStringLiteral("The image could not be read."));

    bool usedAlpha = false;
    preset.tip.alphaImage = normalizeToTip(img, invertAlpha, usedAlpha);
    preset.tip.originalSize = img.size();
    preset.tip.name = preset.name;
    preset.tip.isAlphaOnly = true;
    preset.tip.invertedAlpha = invertAlpha;
    preset.size = float(qBound(1, qMax(img.width(), img.height()), 5000));

    if (!usedAlpha)
        pkg.diagnostics.append({ BrushImportSeverity::Info, QStringLiteral("fallback_used"),
                                 QStringLiteral("The image has no transparency; its luminance was "
                                                "used as the brush coverage."),
                                 filePath, preset.name });

    pkg.presets.append(preset);
    return pkg;
}
