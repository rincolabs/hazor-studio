#pragma once

#include <QByteArray>
#include <QImage>
#include <QList>
#include <QString>

// Decodes the GIMP/Krita predefined-brush bitmap formats that Qt's image readers
// cannot: GIMP brush (.gbr) and GIMP image pipe (.gih). These are the tips that
// Krita bundles and presets reference by file name (and, in newer presets, embed
// as base64), so without this the tip is silently dropped and the brush falls
// back to a plain round stamp.
//
// The decoder is byte-format only: it turns the raw file into a faithful Krita
// brush-tip QImage (ARGB32). The import adapters then convert that QImage into an
// engine tip with the project's existing "coverage in alpha" convention — so the
// same imageToTip logic that handles plain images also handles these. The output
// matches Krita's KisGbrBrush::init():
//   * 1 byte/pixel (mask)  → grayscale value 255 - stored byte, alpha 255
//   * 4 bytes/pixel (RGBA) → the colour data as stored (colour stamp)
//
// Every read is bounds-checked; a malformed file fails with valid == false rather
// than crashing or allocating from a bogus length.

namespace GimpBrushDecoder {

struct Frame {
    QImage image;          // ARGB32, faithful Krita tip image (see above)
    bool colored = false;  // true for a 4-byte RGBA tip that is not all-gray
};

struct Result {
    bool valid = false;
    bool isPipe = false;            // came from a .gih (multi-frame) file
    QString name;                   // brush name from the header (may be empty)
    float spacingPercent = 25.0f;   // GIMP spacing, 0..1000 (% of tip diameter)
    QString parasiteSelection;      // .gih selection mode (e.g. "random"); else empty
    QList<Frame> frames;            // one for .gbr, N for .gih

    QImage firstImage() const {
        return frames.isEmpty() ? QImage() : frames.first().image;
    }
};

// Cheap content sniff: true if the bytes look like a .gbr or .gih. Safe on short
// or empty input.
bool looksLikeGimpBrush(const QByteArray& bytes);

// Decode a .gbr or .gih (auto-detected) into one or more faithful tip images.
Result decode(const QByteArray& bytes);

} // namespace GimpBrushDecoder
