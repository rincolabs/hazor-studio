#pragma once

#include "IBrushImportAdapter.hpp"
#include <QString>
#include <QStringList>
#include <QList>
#include <memory>
#include <vector>

// Central registry that owns the format adapters and routes a file to the right
// one by extension / content. Stateless beyond the adapter list, so a single
// instance can be shared. registerDefaultAdapters() installs the four built-in
// adapters.
class BrushImportManager {
public:
    BrushImportManager();

    void registerAdapter(std::unique_ptr<IBrushImportAdapter> adapter);
    void registerDefaultAdapters();

    // Every adapter that claims it can read the file (usually one).
    QList<IBrushImportAdapter*> adaptersForFile(const QString& filePath) const;

    // Union of supported extensions across all adapters (lower-case, no dot).
    QStringList supportedExtensions() const;
    // Convenience for QFileDialog name filters.
    QString openFileFilter() const;

    ImportedBrushPackage scanFile(const QString& filePath);
    ImportedBrushPackage importFile(const QString& filePath,
                                    const BrushImportOptions& options);
    QList<ImportedBrushPackage> importFiles(const QStringList& filePaths,
                                            const BrushImportOptions& options);

private:
    IBrushImportAdapter* firstAdapterFor(const QString& filePath) const;
    std::vector<std::unique_ptr<IBrushImportAdapter>> m_adapters;
};
