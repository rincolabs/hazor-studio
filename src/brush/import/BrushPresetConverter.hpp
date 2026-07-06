#pragma once

#include "BrushImportTypes.hpp"
#include "brush/BrushPreset.hpp"

// Converts a neutral ImportedBrushPreset into the editor's own BrushPreset
// (BrushSettings + group + provenance). This is the single place format units
// are mapped to engine units — spacing %→fraction, angle deg→rad, tip image →
// base64 PNG in tipImageData so the preset is self-contained and survives a
// restart without the original file. Nothing here is format-specific;
// it only reads the neutral struct.
class BrushPresetConverter {
public:
    explicit BrushPresetConverter(float maxBrushSize = 5000.0f)
        : m_maxSize(maxBrushSize) {}

    BrushPreset convert(const ImportedBrushPreset& imported,
                        const BrushImportOptions& options) const;

    // Library folder path the preset should live in, derived from the options
    // and the source (e.g. "Imported Brushes/Krita/Concept Brushes").
    static QString groupPathFor(const ImportedBrushPreset& imported,
                                const BrushImportOptions& options);

    // Short source-type folder name ("Krita", "GIMP Brushes", "Images").
    static QString sourceFolderName(BrushImportSourceType type);

private:
    BrushSettings convertSettings(const ImportedBrushPreset& imported,
                                  const BrushImportOptions& options) const;

    float m_maxSize;
};
