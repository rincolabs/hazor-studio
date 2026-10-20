#include "ChannelView.hpp"

#include "core/Document.hpp"

#include <algorithm>
#include <cmath>

namespace channelview {

namespace {

constexpr float kInv255 = 1.0f / 255.0f;

inline uint8_t toByte(float v)
{
    const int i = static_cast<int>(std::lround(v * 255.0f));
    return static_cast<uint8_t>(std::clamp(i, 0, 255));
}

// The grey a single visible colour component renders as. Equal weights, not
// luminance: the point is to show the component's VALUES, and a weighted grey
// would scale them (a pure-red pixel would read as 0.3 in the Red channel).
void setReplicate(float m[9], int component)
{
    for (int i = 0; i < 9; ++i)
        m[i] = 0.0f;
    m[0 * 3 + component] = 1.0f;
    m[1 * 3 + component] = 1.0f;
    m[2 * 3 + component] = 1.0f;
}

void setDiagonal(float m[9], const bool visible[3])
{
    for (int i = 0; i < 9; ++i)
        m[i] = 0.0f;
    for (int c = 0; c < 3; ++c)
        m[c * 3 + c] = visible[c] ? 1.0f : 0.0f;
}

// Presence of the OVERLAY for an alpha channel, which is not the same as the
// channel's selection coverage: the representation picks which side of the
// mask the colour marks. Load/Save Selection deliberately ignore it — a stored
// selection must not flip because the user changed how it is tinted.
inline float overlayCoverage(float stored, ChannelMaskRepresentation rep)
{
    return rep == ChannelMaskRepresentation::SelectedAreas ? stored : 1.0f - stored;
}

} // namespace

bool isDefaultView(const Document* doc)
{
    if (!doc)
        return true;
    if (doc->quickMask.active)
        return false;
    const ChannelState& st = doc->channelState;
    if (!st.allColorVisible())
        return false;
    for (const auto& ch : doc->channels) {
        if (ch.visible)
            return false;
    }
    return true;
}

View resolve(const Document* doc)
{
    View view;
    if (!doc)
        return view;

    const ChannelState& st = doc->channelState;
    const int visibleColors = st.visibleColorCount();

    if (visibleColors == 0) {
        for (int i = 0; i < 9; ++i)
            view.colorMatrix[i] = 0.0f;
        view.colorAlpha = 0.0f;
    } else if (visibleColors == 1 && !st.showChannelsInColor) {
        // One component on its own reads as grey — the standard way to judge a
        // separation. "Show Channels in Color" is the opt-out.
        const int component = st.colorVisible[0] ? 0 : (st.colorVisible[1] ? 1 : 2);
        setReplicate(view.colorMatrix, component);
        view.matteOnWhite = true;
    } else if (visibleColors < 3) {
        // Two visible components, or one with Show Channels in Color: keep
        // each in its own hue and let them composite.
        setDiagonal(view.colorMatrix, st.colorVisible);
        view.matteOnWhite = true;
    }
    // visibleColors == 3 keeps the identity matrix the View was built with —
    // and its alpha, since the RGB row IS the document and its transparency is
    // real. Only a component subset mattes (see View::matteOnWhite).

    // Extras in panel order. The first visible one becomes the grey base when
    // the layer stack contributes nothing, so a lone alpha/spot channel shows
    // as its own image instead of as a tint over black.
    bool needGrayBase = (visibleColors == 0);
    for (const auto& ch : doc->channels) {
        if (!ch.visible || ch.data.isNull())
            continue;
        Extra extra;
        extra.stored = &ch.data;
        extra.representation = ch.representation;
        if (needGrayBase) {
            extra.mode = Extra::Mode::GrayscaleBase;
            needGrayBase = false;
        } else if (ch.isSpot()) {
            extra.mode = Extra::Mode::SpotInk;
            extra.color = ch.inkColor;
            extra.solidity = std::clamp(ch.solidity, 0.0f, 1.0f);
        } else {
            extra.mode = Extra::Mode::AlphaOverlay;
            extra.color = ch.overlayColor;
            extra.opacity = std::clamp(ch.overlayOpacity, 0.0f, 1.0f);
        }
        view.extras.push_back(extra);
    }

    // Quick Mask sits on top of everything: it is the selection being edited,
    // not a stored channel, and it reuses the document selection as its buffer
    // so there is only one quick-mask model in the app.
    if (doc->quickMask.active && !doc->selection.image().isNull()) {
        Extra extra;
        extra.stored = &doc->selection.image();
        extra.representation = doc->quickMask.representation;
        extra.color = doc->quickMask.overlayColor;
        extra.opacity = std::clamp(doc->quickMask.overlayOpacity, 0.0f, 1.0f);
        extra.mode = needGrayBase ? Extra::Mode::GrayscaleBase
                                  : Extra::Mode::AlphaOverlay;
        view.extras.push_back(extra);
    }

    return view;
}

void apply(QImage& image, const View& view, const QRect& region)
{
    if (image.isNull() || view.isIdentity())
        return;

    const bool premultiplied =
        image.format() == QImage::Format_ARGB32_Premultiplied
        || image.format() == QImage::Format_RGBA8888_Premultiplied;
    if (!premultiplied
        && image.format() != QImage::Format_ARGB32
        && image.format() != QImage::Format_RGBA8888) {
        image = image.convertToFormat(QImage::Format_RGBA8888);
    }
    // Byte order differs between the ARGB32 family (BGRA in memory on
    // little-endian) and RGBA8888; resolving it once here keeps the pixel loop
    // free of format branches.
    const bool bgra = image.format() == QImage::Format_ARGB32
                   || image.format() == QImage::Format_ARGB32_Premultiplied;
    const int rIdx = bgra ? 2 : 0;
    const int gIdx = 1;
    const int bIdx = bgra ? 0 : 2;
    const int aIdx = 3;

    QRect rect = region.isNull() ? image.rect() : region.intersected(image.rect());
    if (rect.isEmpty())
        return;

    const float* m = view.colorMatrix;
    const bool colorIsIdentity =
        view.colorAlpha >= 1.0f
        && m[0] == 1.0f && m[1] == 0.0f && m[2] == 0.0f
        && m[3] == 0.0f && m[4] == 1.0f && m[5] == 0.0f
        && m[6] == 0.0f && m[7] == 0.0f && m[8] == 1.0f;

    for (int y = rect.top(); y <= rect.bottom(); ++y) {
        uchar* row = image.scanLine(y);
        for (int x = rect.left(); x <= rect.right(); ++x) {
            uchar* px = row + x * 4;

            float a = px[aIdx] * kInv255;
            float rgb[3];
            if (premultiplied && a > 0.0f) {
                const float inv = 1.0f / a;
                rgb[0] = px[rIdx] * kInv255 * inv;
                rgb[1] = px[gIdx] * kInv255 * inv;
                rgb[2] = px[bIdx] * kInv255 * inv;
            } else if (premultiplied) {
                rgb[0] = rgb[1] = rgb[2] = 0.0f;
            } else {
                rgb[0] = px[rIdx] * kInv255;
                rgb[1] = px[gIdx] * kInv255;
                rgb[2] = px[bIdx] * kInv255;
            }

            if (!colorIsIdentity) {
                if (view.matteOnWhite && a < 1.0f) {
                    // Paper, not a hole: a component's values are opaque, so
                    // the matte happens in STRAIGHT space before the matrix —
                    // matting afterwards would tint the paper with whichever
                    // components are visible.
                    rgb[0] += (1.0f - rgb[0]) * (1.0f - a);
                    rgb[1] += (1.0f - rgb[1]) * (1.0f - a);
                    rgb[2] += (1.0f - rgb[2]) * (1.0f - a);
                    a = 1.0f;
                }
                const float r = m[0] * rgb[0] + m[1] * rgb[1] + m[2] * rgb[2];
                const float g = m[3] * rgb[0] + m[4] * rgb[1] + m[5] * rgb[2];
                const float b = m[6] * rgb[0] + m[7] * rgb[1] + m[8] * rgb[2];
                rgb[0] = r;
                rgb[1] = g;
                rgb[2] = b;
                a *= view.colorAlpha;
            }

            for (const Extra& extra : view.extras) {
                if (!extra.stored || y >= extra.stored->height()
                    || x >= extra.stored->width())
                    continue;
                const float stored = extra.stored->constScanLine(y)[x] * kInv255;

                switch (extra.mode) {
                case Extra::Mode::GrayscaleBase:
                    // The channel's own values ARE the picture here.
                    rgb[0] = rgb[1] = rgb[2] = stored;
                    a = 1.0f;
                    break;

                case Extra::Mode::AlphaOverlay: {
                    const float cov = extra.opacity
                                    * overlayCoverage(stored, extra.representation);
                    const float cr = extra.color.redF();
                    const float cg = extra.color.greenF();
                    const float cb = extra.color.blueF();
                    rgb[0] += (cr - rgb[0]) * cov;
                    rgb[1] += (cg - rgb[1]) * cov;
                    rgb[2] += (cb - rgb[2]) * cov;
                    // The rubylith covers the whole canvas, transparent areas
                    // included — otherwise a mask over a hole would be
                    // invisible exactly where it matters most.
                    a = a + (1.0f - a) * cov;
                    break;
                }

                case Extra::Mode::SpotInk: {
                    // Inverse polarity: a stored 0 is MAXIMUM ink.
                    const float ink = 1.0f - stored;
                    const float cr = extra.color.redF();
                    const float cg = extra.color.greenF();
                    const float cb = extra.color.blueF();
                    // Solidity splits the ink into its two press behaviours
                    // and never touches the stored coverage above: the
                    // transparent fraction multiplies what is underneath, the
                    // solid fraction covers it. 0% is a pure overprint, 100%
                    // an opaque one.
                    const float transparent = ink * (1.0f - extra.solidity);
                    rgb[0] *= 1.0f + (cr - 1.0f) * transparent;
                    rgb[1] *= 1.0f + (cg - 1.0f) * transparent;
                    rgb[2] *= 1.0f + (cb - 1.0f) * transparent;
                    const float solid = ink * extra.solidity;
                    rgb[0] += (cr - rgb[0]) * solid;
                    rgb[1] += (cg - rgb[1]) * solid;
                    rgb[2] += (cb - rgb[2]) * solid;
                    a = a + (1.0f - a) * solid;
                    break;
                }
                }
            }

            rgb[0] = std::clamp(rgb[0], 0.0f, 1.0f);
            rgb[1] = std::clamp(rgb[1], 0.0f, 1.0f);
            rgb[2] = std::clamp(rgb[2], 0.0f, 1.0f);
            a = std::clamp(a, 0.0f, 1.0f);

            if (premultiplied) {
                px[rIdx] = toByte(rgb[0] * a);
                px[gIdx] = toByte(rgb[1] * a);
                px[bIdx] = toByte(rgb[2] * a);
            } else {
                px[rIdx] = toByte(rgb[0]);
                px[gIdx] = toByte(rgb[1]);
                px[bIdx] = toByte(rgb[2]);
            }
            px[aIdx] = toByte(a);
        }
    }
}

} // namespace channelview
