#include "GimpBrushImportAdapter.hpp"
#include "GimpBrushDecoder.hpp"

#include <QFile>
#include <QFileInfo>

namespace {

// Build an engine tip (coverage in the alpha channel) from a decoded brush tip
// image. Keeps the existing "dark → opaque" convention shared by the other
// adapters: use the alpha channel when it actually varies, otherwise derive
// coverage from inverted luminance.
QImage coverageTip(const QImage& srcIn)
{
    if (srcIn.isNull())
        return {};
    const QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);

    bool varies = false;
    for (int y = 0; y < src.height() && !varies; ++y) {
        const QRgb* r = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        for (int x = 0; x < src.width(); ++x)
            if (qAlpha(r[x]) < 255) { varies = true; break; }
    }

    QImage tip(src.size(), QImage::Format_ARGB32);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* d = reinterpret_cast<QRgb*>(tip.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            const int cov = varies ? qAlpha(s[x]) : (255 - qGray(s[x]));
            d[x] = qRgba(0, 0, 0, cov);
        }
    }
    return tip;
}

void addDiag(ImportedBrushPackage& pkg, BrushImportSeverity sev,
             const QString& code, const QString& msg, const QString& preset = {})
{
    BrushImportDiagnostic d;
    d.severity = sev;
    d.code = code;
    d.message = msg;
    d.sourceFile = pkg.sourceFilePath;
    d.presetName = preset;
    pkg.diagnostics.append(d);
}

} // namespace

bool GimpBrushImportAdapter::canImport(const QString& filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix != QLatin1String("gbr") && suffix != QLatin1String("gih"))
        return false;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    return GimpBrushDecoder::looksLikeGimpBrush(f.read(512));
}

ImportedBrushPackage GimpBrushImportAdapter::scan(const QString& filePath)
{
    return parse(filePath, /*scanOnly=*/true);
}

ImportedBrushPackage GimpBrushImportAdapter::importFile(const QString& filePath,
                                                        const BrushImportOptions&)
{
    return parse(filePath, /*scanOnly=*/false);
}

ImportedBrushPackage GimpBrushImportAdapter::parse(const QString& filePath, bool scanOnly)
{
    ImportedBrushPackage pkg;
    pkg.sourceFilePath = filePath;
    pkg.sourceType = BrushImportSourceType::GimpBrush;
    pkg.vendorName = QStringLiteral("GIMP");
    pkg.packageName = QFileInfo(filePath).completeBaseName();

    QFileInfo fi(filePath);
    if (fi.size() > BrushImportLimits::MaxFileSize || fi.size() <= 0) {
        addDiag(pkg, BrushImportSeverity::Error, QStringLiteral("corrupted_file"),
                QStringLiteral("The brush file is too large or empty."));
        return pkg;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        addDiag(pkg, BrushImportSeverity::Error, QStringLiteral("corrupted_file"),
                QStringLiteral("The brush file could not be opened."));
        return pkg;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    const GimpBrushDecoder::Result decoded = GimpBrushDecoder::decode(bytes);
    if (!decoded.valid || decoded.frames.isEmpty()) {
        addDiag(pkg, BrushImportSeverity::Error, QStringLiteral("corrupted_file"),
                QStringLiteral("The brush tip could not be decoded. It may be corrupted "
                               "or an unsupported variant."));
        return pkg;
    }

    ImportedBrushPreset preset;
    preset.sourceType = BrushImportSourceType::GimpBrush;
    preset.sourceFilePath = filePath;
    preset.name = !decoded.name.isEmpty() ? decoded.name : fi.completeBaseName();
    preset.originalEngineName = decoded.isPipe
        ? QStringLiteral("GIMP Image Pipe") : QStringLiteral("GIMP Brush");
    preset.compatibilityLabel = QStringLiteral("Compatible");
    preset.spacing = qBound(1.0f, decoded.spacingPercent, 1000.0f);

    const GimpBrushDecoder::Frame& frame = decoded.frames.first();
    const QImage tipImage = frame.image;
    preset.size = float(qBound(1, qMax(tipImage.width(), tipImage.height()), 5000));
    // A sampled tip taper-paints naturally with basic pressure-size dynamics.
    preset.dynamics.pressureAffectsSize = true;

    if (!scanOnly) {
        preset.tip.alphaImage = coverageTip(tipImage);
        preset.tip.originalSize = tipImage.size();
        preset.tip.name = preset.name;
        preset.tip.isAlphaOnly = !frame.colored;
        if (frame.colored) {
            // Carry the colour data for the (future) colour-stamp render path; the
            // engine currently paints it as a coverage mask.
            preset.tip.colorImage = tipImage;
            addDiag(pkg, BrushImportSeverity::Info, QStringLiteral("unsupported_parameter"),
                    QStringLiteral("This is a coloured brush tip; it is imported as a "
                                   "grayscale stamp until coloured brushes are supported."),
                    preset.name);
        }
    }

    if (decoded.isPipe && decoded.frames.size() > 1)
        addDiag(pkg, BrushImportSeverity::Warning, QStringLiteral("partial_import"),
                QStringLiteral("This is an animated image-pipe brush (%1 frames); it is "
                               "imported as a static brush from its first frame.")
                    .arg(decoded.frames.size()),
                preset.name);

    pkg.presets.append(preset);
    return pkg;
}
