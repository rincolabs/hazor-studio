#pragma once

#include "BrushImportTypes.hpp"

// One interface for every external brush format. Adapters convert a file into a
// neutral ImportedBrushPackage; they never touch the Brush Engine or the preset
// library. scan() is the cheap pass used by the UI to preview a file (package
// name, preset count/names, thumbnails, obvious errors); importFile() does the
// full conversion (load bitmaps, normalise alpha, resolve names, gather every
// diagnostic). Both must be safe on malformed input — fail with a diagnostic,
// never crash or exhaust memory.
class IBrushImportAdapter {
public:
    virtual ~IBrushImportAdapter() = default;

    virtual QString adapterName() const = 0;
    virtual QStringList supportedExtensions() const = 0;
    virtual BrushImportSourceType sourceType() const = 0;

    // Cheap content sniff: extension match plus a light magic-byte check.
    virtual bool canImport(const QString& filePath) const = 0;

    virtual ImportedBrushPackage scan(const QString& filePath) = 0;
    virtual ImportedBrushPackage importFile(const QString& filePath,
                                            const BrushImportOptions& options) = 0;
};
