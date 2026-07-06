#pragma once

#include "IBrushImportAdapter.hpp"

// Imports a plain raster image as a single brush tip. If the image has an alpha
// channel it becomes the tip coverage directly; otherwise luminance is converted
// to coverage (dark → opaque, optionally inverted). A basic pressure-driven
// preset is created around the tip. Only formats the app can already read are
// accepted.
class ImageBrushTipImportAdapter : public IBrushImportAdapter {
public:
    QString adapterName() const override { return QStringLiteral("Image Brush Tip"); }
    QStringList supportedExtensions() const override {
        return { QStringLiteral("png"), QStringLiteral("jpg"), QStringLiteral("jpeg"),
                 QStringLiteral("bmp"), QStringLiteral("webp") };
    }
    BrushImportSourceType sourceType() const override { return BrushImportSourceType::ImageBrushTip; }

    bool canImport(const QString& filePath) const override;

    ImportedBrushPackage scan(const QString& filePath) override;
    ImportedBrushPackage importFile(const QString& filePath,
                                    const BrushImportOptions& options) override;

private:
    ImportedBrushPackage parse(const QString& filePath, bool scanOnly, bool invertAlpha);
};
