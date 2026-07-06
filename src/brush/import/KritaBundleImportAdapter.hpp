#pragma once

#include "IBrushImportAdapter.hpp"

// Imports Krita resource bundles (.bundle). A bundle is a ZIP archive holding
// paintop presets (paintoppresets/*.kpp), brush tips (brushes/*), patterns,
// preview and metadata. We open the archive, import each preset by reusing the
// .kpp parser, and resolve referenced brush tips against the archive's brushes/
// folder. Missing resources, unsupported engines and duplicate names degrade to
// diagnostics. The ZIP reader is hardened against path traversal and oversized
// entries (see BrushImportArchive).
class KritaBundleImportAdapter : public IBrushImportAdapter {
public:
    QString adapterName() const override { return QStringLiteral("Krita Bundle"); }
    QStringList supportedExtensions() const override { return { QStringLiteral("bundle") }; }
    BrushImportSourceType sourceType() const override { return BrushImportSourceType::KritaBundle; }

    bool canImport(const QString& filePath) const override;

    ImportedBrushPackage scan(const QString& filePath) override;
    ImportedBrushPackage importFile(const QString& filePath,
                                    const BrushImportOptions& options) override;

private:
    ImportedBrushPackage parse(const QString& filePath, bool scanOnly);
};
