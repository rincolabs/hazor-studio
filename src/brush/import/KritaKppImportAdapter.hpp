#pragma once

#include "IBrushImportAdapter.hpp"
#include <functional>

// Imports Krita brush presets (.kpp / .paintoppreset). A .kpp is a PNG whose
// embedded text chunk carries the preset XML, and whose pixels are the preset
// icon. We import the name, the original Krita engine (paintop id), a usable
// brush tip (embedded predefined bitmap when present, otherwise a basic brush
// derived from the auto-mask generator), and the common size/spacing/opacity/
// flow settings. Engines without an equivalent here still import as a basic
// brush using the tip, with a clear diagnostic. The parsing is reused by the
// bundle adapter through parsePreset().
class KritaKppImportAdapter : public IBrushImportAdapter {
public:
    QString adapterName() const override { return QStringLiteral("Krita Preset"); }
    QStringList supportedExtensions() const override {
        return { QStringLiteral("kpp"), QStringLiteral("paintoppreset") };
    }
    BrushImportSourceType sourceType() const override { return BrushImportSourceType::KritaKpp; }

    bool canImport(const QString& filePath) const override;

    ImportedBrushPackage scan(const QString& filePath) override;
    ImportedBrushPackage importFile(const QString& filePath,
                                    const BrushImportOptions& options) override;

    // Resolver returns the raw bytes of a referenced resource (brush tip /
    // pattern) by file name, or empty when unavailable. Standalone presets pass
    // a null resolver; the bundle adapter passes one backed by the archive.
    using ResourceResolver = std::function<QByteArray(const QString&)>;

    // Parse one preset from raw .kpp (PNG) bytes. Shared by both the standalone
    // adapter and the bundle adapter.
    static ImportedBrushPreset parsePreset(const QByteArray& pngBytes,
                                           const QString& sourceFile,
                                           BrushImportSourceType sourceType,
                                           bool scanOnly,
                                           const ResourceResolver& resolver);
};
