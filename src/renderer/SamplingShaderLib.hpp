#pragma once

// ─────────────────────────────────────────────────────────────────────────
// Shared GLSL sampling library — the GPU half of the resampling
// specification (docs/RENDER-PIPELINE.md §4.2).
//
// Mirror of resampleArea() / buildAxisTaps() in
// renderer/DocumentCompositor.cpp (the CPU half), so there is exactly ONE
// definition of "how Hazor resamples" per backend. Injected into the main
// shader and the blend shader, which used to carry two drifting copies of
// the same sampler.
//
// Everything is one kernel: an *exact* box filter of `width` texels,
// evaluated on premultiplied samples. The box width is the screen-pixel
// footprint, and the three regimes fall out of the same weights:
//
//   width <  1   magnification. The box is narrower than a texel, so texel
//                interiors stay flat and the blend is confined to the texel
//                boundary — crisp like GIMP's magnified view, still
//                antialiased rather than hard nearest-neighbour. Only the
//                document→screen *view* step asks for this (uSharpMagnify);
//                a layer magnified by its own transform must stay bilinear
//                to match what the CPU compositor bakes into the document.
//   width == 1   plain bilinear (and an exact copy when the sample centres
//                land on texel centres, i.e. an unscaled, grid-aligned
//                frame is bit-identical to the source).
//   width >  1   minification: the exact area average over the footprint.
//
// Premultiplying before weighting is what stops fully transparent texels
// from bleeding their (meaningless) colour into the edges of a resampled
// layer; the result is unpremultiplied again on the way out because both
// shaders emit straight alpha.
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

    vec4 sm_unpremultiply(vec4 c) {
        if (c.a > 0.0) {
            c.rgb /= c.a;
        } else {
            c.rgb = vec3(0.0);
        }
        return c;
    }

    vec4 sm_texel(sampler2D tex, ivec2 p, int level, float straightAlpha) {
        vec4 c = texelFetch(tex, p, level);
        if (straightAlpha > 0.5)
            c.rgb *= c.a;
        return c;
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

    // Separable exact box over premultiplied texels. Out-of-range taps clamp
    // to the edge texel, which re-homes their weight instead of dropping it,
    // so the weights still sum to 1 at the borders.
    vec4 sm_sampleBox(sampler2D tex, vec2 p, vec2 width, int level, float straightAlpha) {
        vec2 bw = min(width, vec2(SM_MAX_WIDTH));
        int fx, fy;
        float wx[SM_MAX_TAPS];
        float wy[SM_MAX_TAPS];
        int nx = sm_boxWeights(p.x, bw.x, fx, wx);
        int ny = sm_boxWeights(p.y, bw.y, fy, wy);
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
    // "fit to window" actually lands on.
    vec4 sm_sampleSurface(sampler2D tex, vec2 uv, float straightAlpha,
                          float sharpMagnify, int mipLevels) {
        vec2 duv = fwidth(uv);
        int level = 0;
        if (mipLevels > 0) {
            vec2 raw0 = duv * vec2(textureSize(tex, 0));
            level = clamp(int(floor(log2(max(max(raw0.x, raw0.y), 1.0)))), 0, mipLevels);
        }
        vec2 ts = vec2(textureSize(tex, level));
        vec2 p = uv * ts - vec2(0.5);
        vec2 raw = duv * ts;
        vec2 width = (sharpMagnify > 0.5) ? max(raw, vec2(1.0e-4))
                                          : max(raw, vec2(1.0));
        return sm_unpremultiply(sm_sampleBox(tex, p, width, level, straightAlpha));
    }
)";

} // namespace sampling_glsl
