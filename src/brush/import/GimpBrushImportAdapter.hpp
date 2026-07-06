#pragma once

#include "IBrushImportAdapter.hpp"

// Imports a GIMP/Krita predefined brush tip: GIMP brush (.gbr) or GIMP image pipe
// (.gih). The bitmap is decoded by GimpBrushDecoder (Qt's readers cannot) and
// wrapped in a basic, pressure-driven preset around the tip — the same shape the
// plain-image adapter produces. A .gih is imported as a static brush from its
// first frame, with a diagnostic that animation is not yet supported (that is a
// later item in the brush-import roadmap).
class GimpBrushImportAdapter : public IBrushImportAdapter {
public:
    QString adapterName() const override { return QStringLiteral("GIMP/Krita Brush"); }
    QStringList supportedExtensions() const override {
        return { QStringLiteral("gbr"), QStringLiteral("gih") };
    }
    BrushImportSourceType sourceType() const override { return BrushImportSourceType::GimpBrush; }

    bool canImport(const QString& filePath) const override;

    ImportedBrushPackage scan(const QString& filePath) override;
    ImportedBrushPackage importFile(const QString& filePath,
                                    const BrushImportOptions& options) override;

private:
    ImportedBrushPackage parse(const QString& filePath, bool scanOnly);
};
