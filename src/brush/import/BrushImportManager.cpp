#include "BrushImportManager.hpp"

#include "KritaKppImportAdapter.hpp"
#include "KritaBundleImportAdapter.hpp"
#include "GimpBrushImportAdapter.hpp"
#include "ImageBrushTipImportAdapter.hpp"

#include <QFileInfo>
#include <QSet>

QString brushImportSourceLabel(BrushImportSourceType type)
{
    switch (type) {
    case BrushImportSourceType::KritaKpp:     return QStringLiteral("Krita Preset");
    case BrushImportSourceType::KritaBundle:  return QStringLiteral("Krita Bundle");
    case BrushImportSourceType::GimpBrush:    return QStringLiteral("GIMP/Krita Brush");
    case BrushImportSourceType::ImageBrushTip:return QStringLiteral("Image Brush Tip");
    case BrushImportSourceType::Unknown:
    default:                                  return QStringLiteral("Unknown");
    }
}

BrushImportManager::BrushImportManager() = default;

void BrushImportManager::registerAdapter(std::unique_ptr<IBrushImportAdapter> adapter)
{
    if (adapter)
        m_adapters.push_back(std::move(adapter));
}

void BrushImportManager::registerDefaultAdapters()
{
    registerAdapter(std::make_unique<KritaKppImportAdapter>());
    registerAdapter(std::make_unique<KritaBundleImportAdapter>());
    registerAdapter(std::make_unique<GimpBrushImportAdapter>());
    // Image adapter last: it is the broad fallback for plain bitmaps.
    registerAdapter(std::make_unique<ImageBrushTipImportAdapter>());
}

QList<IBrushImportAdapter*> BrushImportManager::adaptersForFile(const QString& filePath) const
{
    QList<IBrushImportAdapter*> out;
    for (const auto& a : m_adapters) {
        if (a->canImport(filePath))
            out.append(a.get());
    }
    return out;
}

IBrushImportAdapter* BrushImportManager::firstAdapterFor(const QString& filePath) const
{
    for (const auto& a : m_adapters) {
        if (a->canImport(filePath))
            return a.get();
    }
    return nullptr;
}

QStringList BrushImportManager::supportedExtensions() const
{
    QSet<QString> exts;
    for (const auto& a : m_adapters)
        for (const QString& e : a->supportedExtensions())
            exts.insert(e.toLower());
    QStringList out(exts.begin(), exts.end());
    out.sort();
    return out;
}

QString BrushImportManager::openFileFilter() const
{
    QStringList globs;
    for (const QString& e : supportedExtensions())
        globs << QStringLiteral("*.%1").arg(e);
    return QStringLiteral("Brush files (%1);;All files (*)").arg(globs.join(QLatin1Char(' ')));
}

static ImportedBrushPackage makeUnsupportedPackage(const QString& filePath)
{
    ImportedBrushPackage pkg;
    pkg.sourceFilePath = filePath;
    pkg.sourceType = BrushImportSourceType::Unknown;
    pkg.packageName = QFileInfo(filePath).fileName();
    BrushImportDiagnostic d;
    d.severity = BrushImportSeverity::Error;
    d.code = QStringLiteral("unsupported_file");
    d.message = QStringLiteral("This file type is not supported for brush import.");
    d.sourceFile = filePath;
    pkg.diagnostics.append(d);
    return pkg;
}

ImportedBrushPackage BrushImportManager::scanFile(const QString& filePath)
{
    if (IBrushImportAdapter* a = firstAdapterFor(filePath))
        return a->scan(filePath);
    return makeUnsupportedPackage(filePath);
}

ImportedBrushPackage BrushImportManager::importFile(const QString& filePath,
                                                    const BrushImportOptions& options)
{
    if (IBrushImportAdapter* a = firstAdapterFor(filePath))
        return a->importFile(filePath, options);
    return makeUnsupportedPackage(filePath);
}

QList<ImportedBrushPackage> BrushImportManager::importFiles(const QStringList& filePaths,
                                                            const BrushImportOptions& options)
{
    QList<ImportedBrushPackage> out;
    out.reserve(filePaths.size());
    for (const QString& path : filePaths)
        out.append(importFile(path, options));
    return out;
}
