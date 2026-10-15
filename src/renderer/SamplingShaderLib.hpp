#pragma once

// ─────────────────────────────────────────────────────────────────────────
// Shared GLSL sampling library — the GPU half of the resampling
// specification (docs/RENDER-PIPELINE.md §4.2).
//
// Mirror of boxWeights() / cubicWeights() / resampleAffine() in
// renderer/DocumentCompositor.cpp (the CPU half), so there is exactly ONE
// definition of "how Hazor resamples" per backend. Injected into the main
// shader and the blend shader, which used to carry two drifting copies of
// the same sampler.
//
// Two kernels, picked per axis by the screen-pixel footprint (texels per
// output pixel along that axis) — never by taste:
//
//   footprint > 1   minification. An *exact box* of `footprint` texels: the
//                   area average, the same thing cv::INTER_AREA computes.
//                   Degenerates continuously into plain bilinear as the
//                   footprint approaches 1, so there is no seam to place a
//                   threshold at.
//   footprint ≤ 1   magnification, rotation, or a merely sub-pixel offset.
//                   A cubic (Keys, A = -0.75), which is what the CPU bakes
//                   into the document — bilinear here would throw away
//                   detail a scale-up is supposed to keep. At an integral
//                   sample position its weights are exactly (0,1,0,0), so a
//                   grid-aligned 1:1 frame is still a bit-exact copy.
//
// The one exception is the document→screen *view* step (uSharpMagnify),
// which keeps the box below footprint 1 as well: the box is then narrower
// than a texel, so texel interiors stay flat and the blend is confined to
// the texel boundary — crisp like GIMP's magnified view, still antialiased
// rather than hard nearest-neighbour. That step looks at finished pixels, so
// it is free to be crisp; a layer magnified by its own transform is a
// *document* edit and must match what DocumentCompositor bakes.
//
// Premultiplying before weighting is what stops fully transparent texels
// from bleeding their (meaningless) colour into the edges of a resampled
// layer; the result is unpremultiplied again on the way out because both
// shaders emit straight alpha.
//
// The CPU mirror differs in one documented way (docs/RENDER-PIPELINE.md
// §4.4): it does not cap the tap count, because it writes the export rather
// than a frame.
// ─────────────────────────────────────────────────────────────────────────
namespace sampling_glsl {

inline constexpr char kLib[] = R"(
    // ── sm_* : shared sampling (mirror of DocumentCompositor.cpp) ──

    // A box wider than this many texels cannot be evaluated exactly with a
    // fixed tap budget. The pyramid below keeps the residual footprint in
    // [1,2) whenever the texture has mip levels, so the cap only ever binds
    // for pyramid-less live-preview textures at extreme zoom-out, where it
    // degrades to a narrower centred box (the same undersampling the old
    // 4x4 tap grid had) instead of losing energy.
    const int SM_MAX_TAPS = 8;
    const float SM_MAX_WIDTH = 7.0;

    // Keys cubic in OpenCV's formulation: A = -0.75 is exactly
    // cv::INTER_CUBIC, which every destructive path already bakes
    // (ImageEngine::warpAffinePremultiplied / resizePremultiplied,
    // docs/RENDER-PIPELINE.md §4.3). Sharing the coefficient means a
    // non-destructive layer scale and the same scale applied for real land on
    // the same pixels instead of differing by a kernel.
    const int SM_CUBIC_TAPS = 4;
    const float SM_CUBIC_A = -0.75;

    // An axis minifying by less than 0.1% needs no prefilter, so it counts as
    // 1:1 and takes the cubic. The slack is what keeps the two backends on the
    // same kernel: a layer rotated at scale 1 lands exactly on footprint 1,
    // where the CPU's transform algebra and the shader's screen-space
    // derivatives agree only to within rounding.
    const float SM_CUBIC_MAX_FOOTPRINT = 1.001;

    vec4 sm_unpremultiply(vec4 c) {
        if (c.a > 0.0) {
            c.rgb /= c.a;
        } else {
            c.rgb = vec3(0.0);
        }
        return c;
    }

    // The cubic's negative lobes let a tap sum leave the premultiplied range:
    // alpha can overshoot 1, and a colour channel can end up above alpha,
    // which unpremultiplies to brighter than white and rings as a halo (and
    // in Qt's 8-bit qUnpremultiply, wraps to a dark speck instead — the same
    // overshoot ImageEngine::unpremultiplyInPlace clamps for the INTER_CUBIC
    // paths, docs/RENDER-PIPELINE.md §4.3). Clamping before the division
    // keeps the overshoot a sharpening artefact.
    //
    // Only applied when a cubic axis ran: the box has non-negative weights
    // that sum to 1 and cannot overshoot, while a group FBO *can*
    // legitimately hold additive colour above its alpha, which the view step
    // must pass through untouched.
    vec4 sm_clampPremultiplied(vec4 c) {
        c.a = clamp(c.a, 0.0, 1.0);
        c.rgb = clamp(c.rgb, vec3(0.0), vec3(c.a));
        return c;
    }

    vec4 sm_texel(sampler2D tex, ivec2 p, int level, float straightAlpha) {
        vec4 c = texelFetch(tex, p, level);
        if (straightAlpha > 0.5)
            c.rgb *= c.a;
        return c;
    }

    // Per-axis footprint in texels: how far the sampled coordinate travels for
    // one screen pixel, measured *along each source axis* (the gradient norm)
    // rather than as fwidth()'s |du/dx| + |du/dy|. The L1 sum is the width of
    // the bounding box of the rotated footprint, so it reaches 1.41 texels for
    // a 45 degree rotation at scale 1 and prefilters a layer that needs none;
    // the gradient norm is 1 there, which is also what the CPU's per-axis
    // transform scale computes. For an axis-aligned draw dFdy(uv.x) is 0 and
    // the two are bit-identical, so the view step is unchanged.
    //
    // The measures coincide for any scale+rotation (the chain layer transforms
    // are built from); a genuine shear is the one case where they still
    // differ, and there the gradient norm is the physically correct one.
    vec2 sm_footprint(vec2 uv, vec2 texSize) {
        vec2 ddx = dFdx(uv);
        vec2 ddy = dFdy(uv);
        return vec2(length(vec2(ddx.x, ddy.x)),
                    length(vec2(ddx.y, ddy.y))) * texSize;
    }

    // Exact 1-D box weights for a `width`-texel box centred at `p`, in
    // texel-centre coordinates (texel i covers [i-0.5, i+0.5]): the weight
    // of texel i is its overlap with [p-width/2, p+width/2] over `width`.
    // Returns the tap count and the index of the first texel.
    int sm_boxWeights(float p, float width, out int first, out float w[SM_MAX_TAPS]) {
        float lo = p - width * 0.5;
        float hi = p + width * 0.5;
        first = int(floor(lo + 0.5));
        int n = clamp(int(floor(hi + 0.5)) - first + 1, 1, SM_MAX_TAPS);
        float inv = 1.0 / width;
        for (int i = 0; i < SM_MAX_TAPS; ++i) {
            float c = float(first + i);
            w[i] = (i < n)
                 ? max(min(hi, c + 0.5) - max(lo, c - 0.5), 0.0) * inv
                 : 0.0;
        }
        return n;
    }

    // Keys cubic weights for a sample at `p`, same texel-centre coordinates:
    // four taps at floor(p)-1 .. floor(p)+2, by distance x+1, x, 1-x, 2-x.
    // The last weight is 1 minus the other three (OpenCV does the same), which
    // makes the partition of unity exact in float instead of approximate.
    int sm_cubicWeights(float p, out int first, out float w[SM_MAX_TAPS]) {
        float c = floor(p);
        first = int(c) - 1;
        float x  = p - c;
        float x1 = x + 1.0;
        float x2 = 1.0 - x;
        float A  = SM_CUBIC_A;
        w[0] = ((A * x1 - 5.0 * A) * x1 + 8.0 * A) * x1 - 4.0 * A;
        w[1] = ((A + 2.0) * x - (A + 3.0)) * x * x + 1.0;
        w[2] = ((A + 2.0) * x2 - (A + 3.0)) * x2 * x2 + 1.0;
        w[3] = 1.0 - w[0] - w[1] - w[2];
        for (int i = SM_CUBIC_TAPS; i < SM_MAX_TAPS; ++i)
            w[i] = 0.0;
        return SM_CUBIC_TAPS;
    }

    int sm_axisWeights(float p, float width, bool cubic, out int first,
                       out float w[SM_MAX_TAPS]) {
        if (cubic)
            return sm_cubicWeights(p, first, w);
        return sm_boxWeights(p, width, first, w);
    }

    // Separable kernel over premultiplied texels, box or cubic per axis.
    // Out-of-range taps clamp to the edge texel, which re-homes their weight
    // instead of dropping it, so the weights still sum to 1 at the borders.
    vec4 sm_sampleSeparable(sampler2D tex, vec2 p, vec2 width, bvec2 cubic,
                            int level, float straightAlpha) {
        vec2 bw = min(width, vec2(SM_MAX_WIDTH));
        int fx, fy;
        float wx[SM_MAX_TAPS];
        float wy[SM_MAX_TAPS];
        int nx = sm_axisWeights(p.x, bw.x, cubic.x, fx, wx);
        int ny = sm_axisWeights(p.y, bw.y, cubic.y, fy, wy);
        ivec2 hi = textureSize(tex, level) - ivec2(1);
        vec4 sum = vec4(0.0);
        for (int y = 0; y < SM_MAX_TAPS; ++y) {
            if (y >= ny) break;
            int py = clamp(fy + y, 0, hi.y);
            vec4 row = vec4(0.0);
            for (int x = 0; x < SM_MAX_TAPS; ++x) {
                if (x >= nx) break;
                int px = clamp(fx + x, 0, hi.x);
                row += wx[x] * sm_texel(tex, ivec2(px, py), level, straightAlpha);
            }
            sum += wy[y] * row;
        }
        return sum;
    }

    // Resample `tex` at `uv` for the current fragment's footprint, returning
    // straight alpha.
    //
    // `mipLevels` is the highest level of a *premultiplied* mip pyramid on
    // the texture (0 = no pyramid). When there is one, the level whose
    // residual footprint lands in [1,2) is picked and the exact box runs on
    // that level. This is the GEGL/GIMP pyramid idea, deliberately not
    // GL_LINEAR_MIPMAP_LINEAR: trilinear cross-fades level L and L+1, which
    // is *blurrier* than a direct area filter at the non-power-of-two scales
    // "fit to window" actually lands on. A pyramid level always leaves a
    // residual footprint above 1, so it never meets the cubic.
    vec4 sm_sampleSurface(sampler2D tex, vec2 uv, float straightAlpha,
                          float sharpMagnify, int mipLevels) {
        int level = 0;
        if (mipLevels > 0) {
            vec2 raw0 = sm_footprint(uv, vec2(textureSize(tex, 0)));
            level = clamp(int(floor(log2(max(max(raw0.x, raw0.y), 1.0)))), 0, mipLevels);
        }
        vec2 ts = vec2(textureSize(tex, level));
        vec2 p = uv * ts - vec2(0.5);
        vec2 raw = sm_footprint(uv, ts);
        bool viewStep = sharpMagnify > 0.5;
        // The view step keeps the sub-texel box all the way down; every other
        // draw is a document edit, so magnification/rotation take the cubic the
        // CPU compositor bakes.
        bvec2 cubic = viewStep ? bvec2(false)
                               : lessThanEqual(raw, vec2(SM_CUBIC_MAX_FOOTPRINT));
        vec2 width = viewStep ? max(raw, vec2(1.0e-4))
                              : max(raw, vec2(1.0));
        vec4 c = sm_sampleSeparable(tex, p, width, cubic, level, straightAlpha);
        if (any(cubic))
            c = sm_clampPremultiplied(c);
        return sm_unpremultiply(c);
    }
)";

} // namespace sampling_glsl
