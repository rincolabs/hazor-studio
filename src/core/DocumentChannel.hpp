#pragma once

#include <QColor>
#include <QImage>
#include <QString>
#include <algorithm>
#include <cstdint>
#include <vector>

// Document-owned extra channels — the Alpha and Spot rows of the Channels
// panel.
//
// A channel is a document-space grayscale image that belongs to the document,
// not to a layer: it is not a layer, not a layer mask, and neither its
// existence nor its visibility ever changes layer pixels. Two kinds share the
// same buffer with OPPOSITE polarity, which is the one thing that must not be
// confused anywhere downstream:
//
//   Alpha — reusable selection storage. 0 = masked (0% coverage), 255 = fully
//           selected. Grey is partial coverage, so feathered selections
//           survive a round trip through a channel.
//   Spot  — an ink separation. 0 = maximum ink, 255 = no ink. Inverted
//           because it records how much ink the press lays down.
//
// coverage8() hides that difference: it always answers "how much of this
// channel is present here", 0..255, so selection loading, painting and the
// preview composite are written once instead of twice.
//
// The internal RGBA the renderer uses for layer transparency is a different
// thing entirely and is never one of these channels.

enum class ChannelKind {
    Alpha = 0,
    Spot = 1,
};

// How an Alpha channel's stored values are meant to be READ by the overlay and
// by the editing tools. A display/authoring convention only: switching it never
// rewrites the buffer, which is what keeps it reversible and what stops a
// saved selection from silently inverting.
enum class ChannelMaskRepresentation {
    MaskedAreas = 0,    // the overlay covers the unselected (0) side
    SelectedAreas = 1,  // the overlay covers the selected (255) side
};

struct DocumentChannel {
    uint64_t id = 0;
    ChannelKind kind = ChannelKind::Alpha;
    QString name;
    QImage data;  // Format_Grayscale8, document-sized
    bool visible = false;

    // ── Alpha ────────────────────────────────────────────────
    ChannelMaskRepresentation representation = ChannelMaskRepresentation::MaskedAreas;
    QColor overlayColor = QColor(255, 0, 0);
    float overlayOpacity = 0.5f;  // 0..1

    // ── Spot ─────────────────────────────────────────────────
    QColor inkColor = QColor(255, 0, 255);
    // 0..1. Drives the ink SIMULATION in the preview composite only; the
    // stored coverage above is what a separation would print, and solidity
    // must never be folded into it.
    float solidity = 1.0f;
    // Ink name as the colour book spelled it, kept so an imported spot keeps
    // its identity instead of degrading to an anonymous RGB triple.
    QString inkBook;

    // Alpha identifier from the source file (PSD image resource 1053), kept so
    // a re-import re-associates the same metadata with the same buffer. 0 when
    // the channel has none.
    int externalId = 0;

    // Panel thumbnail cache. Rebuilt lazily; `thumbDirty` is set by whatever
    // mutates `data`, so a visibility toggle does not repaint it.
    mutable QImage thumbCache;
    mutable bool thumbDirty = true;
    // Panel thumbnail box the cache was built for, so changing the thumbnail
    // size rebuilds it and nothing else does.
    mutable int thumbCacheBox = -1;

    bool isAlpha() const { return kind == ChannelKind::Alpha; }
    bool isSpot() const { return kind == ChannelKind::Spot; }

    // The value an untouched pixel holds: no selection for Alpha, no ink for
    // Spot. Used to create a channel and to pad one when the canvas grows.
    uint8_t neutralValue() const { return kind == ChannelKind::Spot ? 255 : 0; }

    // Stored value → presence, 0..255, polarity resolved.
    uint8_t coverage8(uint8_t stored) const
    {
        return kind == ChannelKind::Spot ? static_cast<uint8_t>(255 - stored) : stored;
    }

    // Presence → stored value. The exact inverse of coverage8().
    uint8_t fromCoverage8(uint8_t coverage) const
    {
        return kind == ChannelKind::Spot ? static_cast<uint8_t>(255 - coverage) : coverage;
    }

    // Converting between the two kinds preserves PRESENCE, not raw bytes: an
    // Alpha's selected areas become the Spot's inked areas. Keeping the bytes
    // instead would turn a selection into its own negative.
    void convertKindPreservingCoverage(ChannelKind target)
    {
        if (kind == target)
            return;
        if (!data.isNull()) {
            if (data.format() != QImage::Format_Grayscale8)
                data = data.convertToFormat(QImage::Format_Grayscale8);
            data.detach();
            for (int y = 0; y < data.height(); ++y) {
                uint8_t* row = data.scanLine(y);
                for (int x = 0; x < data.width(); ++x)
                    row[x] = static_cast<uint8_t>(255 - row[x]);
            }
        }
        kind = target;
        thumbDirty = true;
    }

    // Grayscale buffer sized for `size`, filled with the kind's neutral value.
    static QImage makeNeutralBuffer(const QSize& size, ChannelKind kind)
    {
        QImage img(std::max(1, size.width()), std::max(1, size.height()),
                   QImage::Format_Grayscale8);
        img.fill(kind == ChannelKind::Spot ? 255 : 0);
        return img;
    }
};

// Editor state for the Channels panel: what the canvas shows and what the
// tools write to. Kept apart from the channel DATA above on purpose —
// clicking an eye or picking an edit target is not a pixel edit and must not
// enter the undo history.
struct ChannelState {
    // Colour components of an RGB document, index 0 = R, 1 = G, 2 = B.
    // Visibility and edit-selection are independent properties (an armed
    // channel can be hidden, a visible one need not be armed).
    bool colorVisible[3] = {true, true, true};
    bool colorSelected[3] = {true, true, true};

    // Extra channels armed for editing, by id. Painting writes to every armed
    // target — the colour components above AND these.
    std::vector<uint64_t> selectedChannels;

    // View > Show Channels in Color: a single visible colour component renders
    // in its own hue instead of as grey.
    bool showChannelsInColor = false;

    // Panel thumbnail size: 0 = none, 1 = small, 2 = medium, 3 = large.
    int thumbnailSize = 1;

    bool allColorSelected() const
    {
        return colorSelected[0] && colorSelected[1] && colorSelected[2];
    }

    bool anyColorSelected() const
    {
        return colorSelected[0] || colorSelected[1] || colorSelected[2];
    }

    bool allColorVisible() const
    {
        return colorVisible[0] && colorVisible[1] && colorVisible[2];
    }

    bool anyColorVisible() const
    {
        return colorVisible[0] || colorVisible[1] || colorVisible[2];
    }

    int visibleColorCount() const
    {
        return (colorVisible[0] ? 1 : 0) + (colorVisible[1] ? 1 : 0)
             + (colorVisible[2] ? 1 : 0);
    }

    // True when the composite RGB row is the edit target — the state every
    // tool behaves normally in.
    bool compositeIsEditTarget() const
    {
        return selectedChannels.empty() && allColorSelected();
    }

    bool isChannelSelected(uint64_t id) const
    {
        return std::find(selectedChannels.begin(), selectedChannels.end(), id)
            != selectedChannels.end();
    }

    void selectOnlyChannel(uint64_t id)
    {
        selectedChannels.assign(1, id);
        colorSelected[0] = colorSelected[1] = colorSelected[2] = false;
    }

    // Back to the RGB row: it is the composite's edit target AND its view, so
    // this restores the components' visibility too. Visibility and arming stay
    // independent properties everywhere else, but every caller here is either
    // a click on the RGB row or the fallback taken when the last armed target
    // disappears (a deleted channel, an un-armed component) — and those are
    // reached from a state where the components are hidden, so leaving the
    // visibility alone would arm the composite over a blank canvas.
    void selectComposite()
    {
        selectedChannels.clear();
        colorSelected[0] = colorSelected[1] = colorSelected[2] = true;
        colorVisible[0] = colorVisible[1] = colorVisible[2] = true;
    }
};

// Quick Mask — the temporary channel that edits the live selection by
// painting. Its BUFFER is the document selection itself (there is only one
// quick mask model in the app); this is just the display configuration and the
// on/off flag, parked on the document so the panel, the canvas and the tools
// read the same state instead of three copies.
struct QuickMaskState {
    bool active = false;
    ChannelMaskRepresentation representation = ChannelMaskRepresentation::MaskedAreas;
    QColor overlayColor = QColor(255, 0, 0);
    float overlayOpacity = 0.5f;
};
