#pragma once

#include <QByteArray>
#include <QSize>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

#include "color/ColorProfile.hpp"
#include "core/DocumentChannel.hpp"
#include "core/GuideTypes.hpp"
#include "smartobject/SmartObjectData.hpp"

class Document;
class LayerTreeNode;
namespace psd { struct PsdFile; }

// PSD/PSB → native Hazor document conversion. The parser (src/psd/) stays
// independent of Hazor's document model; every bit of PSD → Document mapping
// lives here, centralizing the conversion as a single service (File → Open
// just builds a psd::PsdFile and hands it to PsdImportService::import()).
//
// Only features with a real Hazor equivalent are converted; anything else is
// skipped and named in `warnings` (one line per dropped/simplified feature,
// surfaced to the user as the "Compatibility Report" dialog) — see
// PsdImportService.cpp for the full policy.
struct PsdImportResult {
    bool ok = false;
    QString errorMessage; // Set when the file could not be imported at all.

    QSize canvasSize;
    double resolutionDpi = 72.0;
    QString colorMode = QStringLiteral("RGB Color");
    int bitDepth = 8;
    ColorProfile colorProfile = ColorProfile::sRgb();
    ColorProfileSource profileSource = ColorProfileSource::GeneratedDefault;

    std::vector<std::unique_ptr<LayerTreeNode>> roots;
    std::vector<Guide> guides;
    // The document's extra channels (Alpha + Spot), in file order. Built from
    // the merged Image Data Section's extra planes plus the alpha-metadata
    // resources; layer transparency and layer masks are NOT in here (see
    // PsdImportService.cpp).
    std::vector<DocumentChannel> channels;
    // Content documents for the Smart Object layers found among `roots`
    // (keyed by the docId their layer's SmartObjectData::docId points at).
    // The caller must register each of these in whatever DocumentContainer
    // ends up owning the imported document (DocumentContainer::
    // addDocumentWithId) — PsdImportResult has no container of its own,
    // since the destination tab/container does not exist yet when
    // PsdImportService::import() runs.
    std::vector<std::pair<SmartObjectDocId, std::shared_ptr<Document>>> embeddedSmartObjects;

    // One entry per feature that could not be converted (or was simplified),
    // in the order encountered. Empty when the import is fully faithful.
    QStringList warnings;
};

class PsdImportService {
public:
    static PsdImportResult import(const psd::PsdFile& file);

    // Parses `bytes` as a PSD/PSB file and converts it, in one call that
    // never needs psd::PsdFile in the caller's own translation unit —
    // notably MainWindow.cpp, a QObject header whose Qt `slots` keyword
    // macro is already active by the time it would reach psd/Tagged.hpp,
    // corrupting psd::FilterEffectsItem::slots. Sets ok=false and
    // errorMessage when `bytes` is not a well-formed PSD/PSB file.
    static PsdImportResult importFromBytes(const QByteArray& bytes);
};
