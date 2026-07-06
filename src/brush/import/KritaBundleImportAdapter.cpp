#include "KritaBundleImportAdapter.hpp"
#include "KritaKppImportAdapter.hpp"
#include "BrushImportArchive.hpp"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QXmlStreamReader>

namespace {

bool looksLikeZip(const QByteArray& head)
{
    // Local file header signature "PK\x03\x04" (or empty-archive "PK\x05\x06").
    return head.size() >= 4 && head[0] == 'P' && head[1] == 'K'
        && (head[2] == 0x03 || head[2] == 0x05) && (head[3] == 0x04 || head[3] == 0x06);
}

QString baseName(const QString& path)
{
    const int slash = path.lastIndexOf(QLatin1Char('/'));
    return slash >= 0 ? path.mid(slash + 1) : path;
}

bool isImageResource(const QString& path)
{
    const QString lower = path.toLower();
    return lower.endsWith(QLatin1String(".png"))
        || lower.endsWith(QLatin1String(".jpg"))
        || lower.endsWith(QLatin1String(".jpeg"))
        || lower.endsWith(QLatin1String(".bmp"))
        || lower.endsWith(QLatin1String(".webp"));
}

} // namespace

bool KritaBundleImportAdapter::canImport(const QString& filePath) const
{
    if (QFileInfo(filePath).suffix().toLower() != QLatin1String("bundle"))
        return false;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return looksLikeZip(f.read(4));
}

ImportedBrushPackage KritaBundleImportAdapter::scan(const QString& filePath)
{
    return parse(filePath, /*scanOnly=*/true);
}

ImportedBrushPackage KritaBundleImportAdapter::importFile(const QString& filePath,
                                                          const BrushImportOptions&)
{
    return parse(filePath, /*scanOnly=*/false);
}

ImportedBrushPackage KritaBundleImportAdapter::parse(const QString& filePath, bool scanOnly)
{
    ImportedBrushPackage pkg;
    pkg.sourceFilePath = filePath;
    pkg.sourceType = BrushImportSourceType::KritaBundle;
    pkg.vendorName = QStringLiteral("Krita");
    pkg.packageName = QFileInfo(filePath).completeBaseName();

    auto fail = [&](const QString& code, const QString& msg) {
        BrushImportDiagnostic d;
        d.severity = BrushImportSeverity::Error;
        d.code = code; d.message = msg; d.sourceFile = filePath;
        pkg.diagnostics.append(d);
        return pkg;
    };

    QFileInfo fi(filePath);
    if (fi.size() > BrushImportLimits::MaxFileSize || fi.size() <= 0)
        return fail(QStringLiteral("corrupted_file"),
                    QStringLiteral("The Krita bundle is too large or empty."));

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return fail(QStringLiteral("corrupted_file"),
                    QStringLiteral("The Krita bundle could not be opened."));
    const QByteArray archive = f.readAll();
    f.close();

    BrushImportArchive::ZipReader zip(archive);
    if (!zip.isValid())
        return fail(QStringLiteral("corrupted_file"),
                    QStringLiteral("The Krita bundle could not be read. It may be corrupted."));

    // Optional metadata (bundle display name).
    if (zip.contains(QStringLiteral("meta.xml"))) {
        const QByteArray meta = zip.read(QStringLiteral("meta.xml"));
        QXmlStreamReader xml(meta);
        while (!xml.atEnd()) {
            if (xml.readNext() == QXmlStreamReader::StartElement
                && xml.name().toString().endsWith(QLatin1String("name"))) {
                const QString n = xml.readElementText().trimmed();
                if (!n.isEmpty()) { pkg.packageName = n; break; }
            }
        }
    }

    // Index image resources so presets can resolve brush tips and textures by
    // file name.
    QStringList resourceFiles;
    QStringList presetFiles;
    const QStringList names = zip.entryNames();
    for (const QString& n : names) {
        if (isImageResource(n))
            resourceFiles << n;
        if (n.startsWith(QLatin1String("paintoppresets/"), Qt::CaseInsensitive)
                 && (n.endsWith(QLatin1String(".kpp"), Qt::CaseInsensitive)
                     || n.endsWith(QLatin1String(".paintoppreset"), Qt::CaseInsensitive)))
            presetFiles << n;
    }
    presetFiles.sort();

    KritaKppImportAdapter::ResourceResolver resolver =
        [&zip, &resourceFiles](const QString& wanted) -> QByteArray {
            if (wanted.isEmpty()) return {};
            const QString want = baseName(wanted);
            for (const QString& resource : resourceFiles) {
                if (baseName(resource).compare(want, Qt::CaseInsensitive) == 0)
                    return zip.read(resource);
            }
            return {};
        };

    if (presetFiles.isEmpty()) {
        pkg.diagnostics.append({ BrushImportSeverity::Warning, QStringLiteral("partial_import"),
                                 QStringLiteral("No brush presets were found in this bundle."),
                                 filePath, QString() });
        return pkg;
    }

    int count = 0;
    for (const QString& pf : presetFiles) {
        if (count++ >= BrushImportLimits::MaxBrushesPerFile) {
            pkg.diagnostics.append({ BrushImportSeverity::Warning, QStringLiteral("partial_import"),
                                     QStringLiteral("The bundle exceeds the import limit; the "
                                                    "remaining presets were skipped."),
                                     filePath, QString() });
            break;
        }
        const QByteArray kpp = zip.read(pf);
        if (kpp.isEmpty()) {
            pkg.diagnostics.append({ BrushImportSeverity::Warning, QStringLiteral("partial_import"),
                                     QStringLiteral("A preset entry could not be read and was skipped."),
                                     filePath, baseName(pf) });
            continue;
        }
        ImportedBrushPreset preset = KritaKppImportAdapter::parsePreset(
            kpp, filePath, BrushImportSourceType::KritaBundle, scanOnly, resolver);
        // Group all bundle presets under the bundle name (folder layout).
        preset.groupName = pkg.packageName;
        pkg.presets.append(preset);
    }

    return pkg;
}
