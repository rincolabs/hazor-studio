#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

// Shared raw-pointer bilinear sampler for the clone/healing paths.
//
// It lives in its own header because the clone-source *preview* overlay
// (CanvasView::cloneStampPreviewImage) has to predict what the clone/healing
// *dabs* (BrushRenderer) will actually copy. Two samplers would be two
// conventions: the overlay used to run QImage::pixelColor() per tap — four
// out-of-line calls and four QColor temporaries per sampled pixel, on the UI
// thread, for every pointer event — and it lied about the result wherever the
// two disagreed. One sampler, one convention, one hot loop.

// Samples an RGBA8888 buffer at a continuous DOCUMENT-pixel coordinate: integers
// sit on texel EDGES, so the centre of texel i is i + 0.5. The lerp works in
// texel indices (integer = texel centre), hence the half-texel shift before it.
//
// That shift is what makes an axis-aligned clone exact, and leaving it out was
// worse than an offset: the dab loops sample at pixel centres (left + x + 0.5)
// and beginCloneStampStroke() rounds the clone offset to whole pixels, so every
// tap landed at fraction 0.5 on BOTH axes and each dab pixel came out as a flat
// 2x2 box average of the source — a filter with no filtering purpose, the exact
// pattern 6aa689d/36b2764/80071f1 removed elsewhere. With the shift an integral
// offset gives fraction 0, which the fast path below turns into a texel copy.
//
// When it does interpolate (a scaled or rotated destination layer makes the
// document coordinate step fractionally), it mixes in premultiplied alpha:
// mixing straight alpha gives soft edges a dark fringe, since the arbitrary
// colour of a transparent texel still carries kernel weight (see daf03bf).
//
// Fully out-of-range samples read as transparent.
inline void sampleBilinearRgbaRaw(const unsigned char* bits, int bytesPerLine,
                                  int w, int h, double x, double y,
                                  float out[4])
{
    if (x < 0.0 || y < 0.0 || x > w || y > h) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const double ix = x - 0.5;
    const double iy = y - 0.5;
    const double fx = std::floor(ix);
    const double fy = std::floor(iy);
    const float tx = static_cast<float>(ix - fx);
    const float ty = static_cast<float>(iy - fy);
    // Clamp to edge: inside the buffer's outer half texel floor() lands outside
    // it and there is no neighbour to blend with anyway.
    const int x0 = std::clamp(static_cast<int>(fx), 0, w - 1);
    const int y0 = std::clamp(static_cast<int>(fy), 0, h - 1);
    const int x1 = std::clamp(static_cast<int>(fx) + 1, 0, w - 1);
    const int y1 = std::clamp(static_cast<int>(fy) + 1, 0, h - 1);
    const unsigned char* row0 = bits + static_cast<size_t>(y0) * bytesPerLine;
    const unsigned char* row1 = bits + static_cast<size_t>(y1) * bytesPerLine;
    const unsigned char* p00 = row0 + x0 * 4;
    const unsigned char* p10 = row0 + x1 * 4;
    const unsigned char* p01 = row1 + x0 * 4;
    const unsigned char* p11 = row1 + x1 * 4;
    if (tx == 0.0f && ty == 0.0f) {
        // The aligned clone/heal case: copy the texel instead of running a
        // kernel whose weights are 1 and 0.
        for (int c = 0; c < 4; ++c)
            out[c] = p00[c];
        return;
    }
    // Tap alphas as a 0..1 weight, so colour * weight is the premultiplied
    // colour the mix has to run on.
    const float w00 = p00[3] * (1.0f / 255.0f);
    const float w10 = p10[3] * (1.0f / 255.0f);
    const float w01 = p01[3] * (1.0f / 255.0f);
    const float w11 = p11[3] * (1.0f / 255.0f);
    const float ta = p00[3] + (p10[3] - p00[3]) * tx;
    const float ba = p01[3] + (p11[3] - p01[3]) * tx;
    const float alpha = ta + (ba - ta) * ty;
    out[3] = alpha;
    if (alpha <= 0.0f) {
        out[0] = out[1] = out[2] = 0.0f;
        return;
    }
    for (int c = 0; c < 3; ++c) {
        const float t = p00[c] * w00 + (p10[c] * w10 - p00[c] * w00) * tx;
        const float b = p01[c] * w01 + (p11[c] * w11 - p01[c] * w01) * tx;
        // Undo the premultiply (alpha is on the 0..255 scale, so the factor is
        // 255/alpha) and clamp: the caller stores 8-bit straight RGBA.
        out[c] = std::min((t + (b - t) * ty) * 255.0f / alpha, 255.0f);
    }
}
