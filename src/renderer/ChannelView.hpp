#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <vector>

#include "core/DocumentChannel.hpp"

class Document;

// ─────────────────────────────────────────────────────────────────────────
// ChannelView — how the Channels panel's visibility state changes what the
// canvas shows.
//
// This is a VIEW transform over the finished document composite, never a step
// of compositing: it reads the channel buffers and the composite, and writes a
// display image. Nothing here touches layer pixels, the channel buffers, or
// what Export/Flatten produce — which is exactly why an alpha overlay cannot
// leak into an exported file and a spot preview cannot contaminate the RGB
// pixels.
//
// It runs in DOCUMENT space, on the CPU, as a post-pass over the projection
// (DocumentCompositor's output). Both render paths therefore display the same
// pixels for the same reason the rest of the pipeline does: there is one
// implementation. GPUViewport keeps the canvas on the projection path while a
// non-default channel view is active instead of mirroring this in GLSL — the
// transform is data-dependent (it samples N channel buffers in document
// space), so a screen-space shader pass could not reproduce it bit for bit,
// and a second implementation is precisely what §4's "results must be
// equivalent" forbids.
//
// Cost is kept off the full-frame path by applying only over a region: a dab
// painted into a channel re-derives the display image for the dab's rect from
// the cached base composite, not for the whole canvas.
//
// Three kinds of frame cannot show it, all for the same reason — the
// projection is not what is on screen:
//
//   * a document past GL_MAX_TEXTURE_SIZE, where no projection texture can
//     exist at all (ProjectionCache::update returns 0 forever and the canvas
//     stays on the per-layer compositor). A hardware limit, not a policy.
//   * a live filter preview (RenderParams::hasPreview),
//   * the grayscale mask view (RenderParams::grayscaleMaskView).
//
// The last two are other view modes outranking this one. In none of the three
// does the channel STATE change: an armed channel still receives its dabs and
// the panel still shows what is armed — what is missing is only the preview of
// it, which is why nothing here can silently corrupt a channel's buffer.
// ─────────────────────────────────────────────────────────────────────────

namespace channelview {

// One extra channel's contribution, in panel order (bottom first = the
// overprint order the panel shows).
struct Extra {
    enum class Mode {
        // This channel IS the image being shown: no colour component is
        // visible, so its stored values render as grey. Raw stored values, so
        // a Spot still reads "black = full ink" and an Alpha "white =
        // selected".
        GrayscaleBase,
        // Rubylith over the composite.
        AlphaOverlay,
        // Ink simulation over the composite.
        SpotInk,
    };

    Mode mode = Mode::AlphaOverlay;
    // Document-sized grayscale buffer holding the channel's STORED values.
    // Borrowed; the caller guarantees it outlives the View.
    const QImage* stored = nullptr;
    ChannelMaskRepresentation representation = ChannelMaskRepresentation::MaskedAreas;
    QColor color;
    float opacity = 0.5f;   // AlphaOverlay
    float solidity = 0.0f;  // SpotInk
};

struct View {
    // Row-major 3x3 matrix on the composite's straight RGB. Covers every
    // colour-component case: identity for RGB, diag(flags) for a subset, and
    // the replicate-one-component form for the single-channel grey view.
    float colorMatrix[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    // 0 when no colour component is visible: the layer stack contributes
    // nothing and only the extras below are drawn.
    float colorAlpha = 1.0f;
    // True while a SUBSET of the colour components is on display. A colour
    // component is an opaque grayscale plane — transparency is not one of its
    // values — so the composite's alpha is matted against paper white before
    // the matrix and the result is opaque. Without it the canvas showed the
    // checkerboard through a component view while the panel's thumbnail for
    // the same component (which has always matted on white) showed paper, and
    // the two disagreed on every document with transparency. Never set for the
    // identity (RGB) view, which keeps the document's own alpha.
    bool matteOnWhite = false;
    std::vector<Extra> extras;

    bool isIdentity() const
    {
        return extras.empty() && colorAlpha >= 1.0f
            && colorMatrix[0] == 1.0f && colorMatrix[1] == 0.0f && colorMatrix[2] == 0.0f
            && colorMatrix[3] == 0.0f && colorMatrix[4] == 1.0f && colorMatrix[5] == 0.0f
            && colorMatrix[6] == 0.0f && colorMatrix[7] == 0.0f && colorMatrix[8] == 1.0f;
    }
};

// Resolves the document's channel state into a View. Cheap: it copies
// settings and borrows buffers, it never touches pixels. The Extras point into
// `doc`'s channel buffers (and its selection, for Quick Mask), so the View is
// only valid while `doc` is unchanged.
View resolve(const Document* doc);

// True when the canvas shows the plain document composite — the fast path
// every caller should check before doing any of the work below.
bool isDefaultView(const Document* doc);

// Applies `view` to `image` in place. `image` must be
// Format_ARGB32_Premultiplied or Format_RGBA8888 at document resolution;
// `region` limits the work (empty = whole image).
void apply(QImage& image, const View& view, const QRect& region = QRect());

} // namespace channelview
