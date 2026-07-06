#include "GPUViewport.hpp"
#include "ShaderCompat.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QOpenGLExtraFunctions>
#include <QOpenGLContext>
#include <QMatrix4x4>

// ── Shader init ────────────────────────────────────────────────

void GPUViewport::initShaders()
{
    auto* t = ThemeManager::instance()->current();

    // ── Main program (textured quad + mask) ──
    m_mainProg = new QOpenGLShaderProgram();

    const char* vSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 pos;
        layout(location = 1) in vec2 uv;
        layout(location = 2) in vec4 aMvp0;
        layout(location = 3) in vec4 aMvp1;
        layout(location = 4) in vec4 aMvp2;
        layout(location = 5) in vec4 aMvp3;
        layout(location = 6) in vec2 aUvOffset;
        layout(location = 7) in vec2 aUvScale;
        uniform mat4 uTransform;
        uniform int uInstanced;
        out vec2 vUV;
        void main() {
            if (uInstanced > 0) {
                mat4 mvp = mat4(aMvp0, aMvp1, aMvp2, aMvp3);
                gl_Position = mvp * vec4(pos, 0.0, 1.0);
                vUV = uv * aUvScale + aUvOffset;
            } else {
                gl_Position = uTransform * vec4(pos, 0.0, 1.0);
                vUV = uv;
            }
        }
    )";

    const char* fSrc = R"(
        #version 330 core
        in vec2 vUV;
        out vec4 fragColor;
        uniform sampler2D uTexture;
        uniform sampler2D uMaskTexture;
        uniform float uOpacity;
        uniform float uHasMask;
        uniform float uMaskDensity;
        uniform float uSourcePremultiplied;
        uniform float uAlphaWeightedSampling;
        uniform vec2 uUVScale;
        uniform vec2 uUVOffset;
        uniform vec2 uMaskUVScale;
        uniform vec2 uMaskUVOffset;
        vec4 unpremultiply(vec4 c) {
            if (c.a > 0.0) {
                c.rgb /= c.a;
            } else {
                c.rgb = vec3(0.0);
            }
            return c;
        }
        vec4 premulTexel(sampler2D tex, ivec2 p, float straightAlpha) {
            vec4 c = texelFetch(tex, p, 0);
            if (straightAlpha > 0.5)
                c.rgb *= c.a;
            return c;
        }
        vec4 samplePremultipliedAt(sampler2D tex, vec2 p, float straightAlpha) {
            ivec2 ts = textureSize(tex, 0);
            ivec2 hi = ts - ivec2(1);
            ivec2 i0 = ivec2(floor(p));
            ivec2 i1 = i0 + ivec2(1);
            vec2 f = fract(p);

            ivec2 p00 = clamp(i0, ivec2(0), hi);
            ivec2 p10 = clamp(ivec2(i1.x, i0.y), ivec2(0), hi);
            ivec2 p01 = clamp(ivec2(i0.x, i1.y), ivec2(0), hi);
            ivec2 p11 = clamp(i1, ivec2(0), hi);

            vec4 c00 = premulTexel(tex, p00, straightAlpha);
            vec4 c10 = premulTexel(tex, p10, straightAlpha);
            vec4 c01 = premulTexel(tex, p01, straightAlpha);
            vec4 c11 = premulTexel(tex, p11, straightAlpha);

            vec4 c0 = mix(c00, c10, f.x);
            vec4 c1 = mix(c01, c11, f.x);
            return mix(c0, c1, f.y);
        }
        vec4 sampleAlphaWeighted(sampler2D tex, vec2 uv, float straightAlpha) {
            ivec2 ts = textureSize(tex, 0);
            vec2 p = uv * vec2(ts) - vec2(0.5);
            vec2 footprint = max(fwidth(uv) * vec2(ts), vec2(1.0));
            if (max(footprint.x, footprint.y) <= 1.25)
                return unpremultiply(samplePremultipliedAt(tex, p, straightAlpha));

            vec2 stepPx = footprint / 4.0;
            vec2 first = p - footprint * 0.5 + stepPx * 0.5;
            vec4 sum = vec4(0.0);
            for (int y = 0; y < 4; ++y) {
                for (int x = 0; x < 4; ++x) {
                    sum += samplePremultipliedAt(
                        tex, first + vec2(float(x), float(y)) * stepPx, straightAlpha);
                }
            }
            return unpremultiply(sum * 0.0625);
        }
        vec4 sampleSource(sampler2D tex, vec2 uv) {
            vec4 c = texture(tex, uv);
            if (uAlphaWeightedSampling > 0.5)
                return sampleAlphaWeighted(
                    tex, uv, uSourcePremultiplied > 0.5 ? 0.0 : 1.0);
            if (uSourcePremultiplied > 0.5)
                return unpremultiply(c);
            return c;
        }
        void main() {
            vec2 tileUV = vUV * uUVScale + uUVOffset;
            vec2 maskUV = vUV * uMaskUVScale + uMaskUVOffset;
            vec4 c = sampleSource(uTexture, tileUV);
            float maskAlpha = 1.0;
            if (uHasMask > 0.5
                    && all(greaterThanEqual(maskUV, vec2(0.0)))
                    && all(lessThanEqual(maskUV, vec2(1.0)))) {
                maskAlpha = texture(uMaskTexture, maskUV).r;
            }
            maskAlpha = mix(1.0, maskAlpha, uMaskDensity);
            fragColor = vec4(c.rgb, c.a * uOpacity * maskAlpha);
        }
    )";

    const QByteArray mainVSrc = shader_compat::adaptSource(vSrc);
    const QByteArray mainFSrc = shader_compat::adaptSource(fSrc);
    shader_compat::bindInstancedAttributes(m_mainProg);

    if (!m_mainProg->addShaderFromSourceCode(QOpenGLShader::Vertex, mainVSrc) ||
        !m_mainProg->addShaderFromSourceCode(QOpenGLShader::Fragment, mainFSrc) ||
        !m_mainProg->link()) {
        qWarning("Main shader compilation failed: %s", qPrintable(m_mainProg->log()));
        return;
    }

    collectUniforms();

    // ── Solid color program ──
    m_solidProg = new QOpenGLShaderProgram();
    const QByteArray solidVSrc = shader_compat::adaptSource(R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            uniform mat4 uTransform;
            void main() {
                gl_Position = uTransform * vec4(pos, 0.0, 1.0);
            }
        )");
    const QByteArray solidFSrc = shader_compat::adaptSource(R"(
            #version 330 core
            out vec4 fragColor;
            uniform vec4 uColor;
            void main() { fragColor = uColor; }
        )");
    m_solidProg->bindAttributeLocation("pos", 0);
    if (!m_solidProg->addShaderFromSourceCode(QOpenGLShader::Vertex, solidVSrc) ||
        !m_solidProg->addShaderFromSourceCode(QOpenGLShader::Fragment, solidFSrc) ||
        !m_solidProg->link()) {
        qWarning("Solid shader failed: %s", qPrintable(m_solidProg->log()));
    }
    m_solidU.transform = m_solidProg->uniformLocation("uTransform");
    m_solidU.color     = m_solidProg->uniformLocation("uColor");

    // ── Blend shader program (advanced blend modes) ──
    m_blendProg = new QOpenGLShaderProgram();
    const QByteArray blendVSrc = shader_compat::adaptSource(R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            layout(location = 1) in vec2 uv;
            uniform mat4 uTransform;
            out vec2 vUV;
            void main() {
                vUV = uv;
                gl_Position = uTransform * vec4(pos, 0.0, 1.0);
            }
        )");
    const QByteArray blendFSrc = shader_compat::adaptSource(R"(
            #version 330 core
            in vec2 vUV;
            out vec4 fragColor;
            uniform sampler2D uDstTexture;
            uniform sampler2D uSrcTexture;
            uniform sampler2D uMaskTexture;
            uniform vec2 uViewportSize;
            uniform float uOpacity;
            uniform float uHasMask;
            uniform float uMaskDensity;
            uniform vec2 uMaskUVScale;
            uniform vec2 uMaskUVOffset;
            uniform int uBlendMode;
            uniform float uSrcPremultiplied;
            uniform float uDstPremultiplied;

            vec4 unpremultiply(vec4 c) {
                if (c.a > 0.0) {
                    c.rgb /= c.a;
                } else {
                    c.rgb = vec3(0.0);
                }
                return c;
            }
            vec4 premulTexel(sampler2D tex, ivec2 p, float straightAlpha) {
                vec4 c = texelFetch(tex, p, 0);
                if (straightAlpha > 0.5)
                    c.rgb *= c.a;
                return c;
            }
            vec4 samplePremultipliedAt(sampler2D tex, vec2 p, float straightAlpha) {
                ivec2 ts = textureSize(tex, 0);
                ivec2 hi = ts - ivec2(1);
                ivec2 i0 = ivec2(floor(p));
                ivec2 i1 = i0 + ivec2(1);
                vec2 f = fract(p);

                ivec2 p00 = clamp(i0, ivec2(0), hi);
                ivec2 p10 = clamp(ivec2(i1.x, i0.y), ivec2(0), hi);
                ivec2 p01 = clamp(ivec2(i0.x, i1.y), ivec2(0), hi);
                ivec2 p11 = clamp(i1, ivec2(0), hi);

                vec4 c00 = premulTexel(tex, p00, straightAlpha);
                vec4 c10 = premulTexel(tex, p10, straightAlpha);
                vec4 c01 = premulTexel(tex, p01, straightAlpha);
                vec4 c11 = premulTexel(tex, p11, straightAlpha);

                vec4 c0 = mix(c00, c10, f.x);
                vec4 c1 = mix(c01, c11, f.x);
                return mix(c0, c1, f.y);
            }
            vec4 sampleAlphaWeighted(sampler2D tex, vec2 uv, float straightAlpha) {
                ivec2 ts = textureSize(tex, 0);
                vec2 p = uv * vec2(ts) - vec2(0.5);
                vec2 footprint = max(fwidth(uv) * vec2(ts), vec2(1.0));
                if (max(footprint.x, footprint.y) <= 1.25)
                    return unpremultiply(samplePremultipliedAt(tex, p, straightAlpha));

                vec2 stepPx = footprint / 4.0;
                vec2 first = p - footprint * 0.5 + stepPx * 0.5;
                vec4 sum = vec4(0.0);
                for (int y = 0; y < 4; ++y) {
                    for (int x = 0; x < 4; ++x) {
                        sum += samplePremultipliedAt(
                            tex, first + vec2(float(x), float(y)) * stepPx, straightAlpha);
                    }
                }
                return unpremultiply(sum * 0.0625);
            }
            vec4 sampleSource(sampler2D tex, vec2 uv) {
                return sampleAlphaWeighted(
                    tex, uv, uSrcPremultiplied > 0.5 ? 0.0 : 1.0);
            }
            float lum(vec3 c) {
                return 0.299*c.r + 0.587*c.g + 0.114*c.b;
            }
            vec3 clipColor(vec3 c) {
                float l = lum(c);
                float n = min(min(c.r,c.g),c.b);
                float x = max(max(c.r,c.g),c.b);
                if (n < 0.0)
                    c = l + (c - l) * l / (l - n);
                if (x > 1.0)
                    c = l + (c - l) * (1.0 - l) / (x - l);
                return c;
            }
            vec3 setLum(vec3 c, float l) {
                float d = l - lum(c);
                return clipColor(c + d);
            }
            float sat(vec3 c) { return max(max(c.r,c.g),c.b) - min(min(c.r,c.g),c.b); }
            vec3 setSat(vec3 c, float s) {
                float mn = min(min(c.r,c.g),c.b);
                float mx = max(max(c.r,c.g),c.b);
                if (mn == mx) return vec3(0.0);
                vec3 r;
                if (c.r <= mn) {
                    if (c.g <= mn) {
                        r.b = mn + (c.b - mn) * s / (mx - mn);
                        r.r = r.g = mn;
                    } else if (c.b <= mn) {
                        r.r = mn + (c.r - mn) * s / (mx - mn);
                        r.g = r.b = mn;
                    }
                } else if (c.g <= mn) {
                    if (c.b <= mn) {
                        r.r = mn + (c.r - mn) * s / (mx - mn);
                        r.g = r.b = mn;
                    }
                }
                return r;
            }
            void main() {
                vec2 st = gl_FragCoord.xy / uViewportSize;
                vec4 dst = texture(uDstTexture, st);
                vec4 src = sampleSource(uSrcTexture, vUV);
                vec2 maskUV = vUV * uMaskUVScale + uMaskUVOffset;
                float maskAlpha = 1.0;
                if (uHasMask > 0.5
                        && all(greaterThanEqual(maskUV, vec2(0.0)))
                        && all(lessThanEqual(maskUV, vec2(1.0)))) {
                    maskAlpha = texture(uMaskTexture, maskUV).r;
                }
                maskAlpha = mix(1.0, maskAlpha, uMaskDensity);
                vec3 dstColor = (uDstPremultiplied > 0.5 && dst.a > 0.0)
                    ? dst.rgb / dst.a
                    : dst.rgb;
                vec3 dstPremul = (uDstPremultiplied > 0.5)
                    ? dst.rgb
                    : dst.rgb * dst.a;
                vec3 srcColor = src.rgb;
                src.a *= uOpacity * maskAlpha;

                vec3 Cr;
                float sa = src.a, da = dst.a;
                if (uBlendMode == 0) { // Overlay
                    Cr.r = (dstColor.r < 0.5) ? 2.0*srcColor.r*dstColor.r : 1.0 - 2.0*(1.0-srcColor.r)*(1.0-dstColor.r);
                    Cr.g = (dstColor.g < 0.5) ? 2.0*srcColor.g*dstColor.g : 1.0 - 2.0*(1.0-srcColor.g)*(1.0-dstColor.g);
                    Cr.b = (dstColor.b < 0.5) ? 2.0*srcColor.b*dstColor.b : 1.0 - 2.0*(1.0-srcColor.b)*(1.0-dstColor.b);
                } else if (uBlendMode == 1) { // HardLight
                    Cr.r = (srcColor.r < 0.5) ? 2.0*srcColor.r*dstColor.r : 1.0 - 2.0*(1.0-srcColor.r)*(1.0-dstColor.r);
                    Cr.g = (srcColor.g < 0.5) ? 2.0*srcColor.g*dstColor.g : 1.0 - 2.0*(1.0-srcColor.g)*(1.0-dstColor.g);
                    Cr.b = (srcColor.b < 0.5) ? 2.0*srcColor.b*dstColor.b : 1.0 - 2.0*(1.0-srcColor.b)*(1.0-dstColor.b);
                } else if (uBlendMode == 2) { // SoftLight
                    Cr.r = (srcColor.r < 0.5) ? dstColor.r - (1.0-2.0*srcColor.r)*dstColor.r*(1.0-dstColor.r) : dstColor.r + (2.0*srcColor.r-1.0)*( (dstColor.r<0.25 ? ((16.0*dstColor.r-12.0)*dstColor.r+4.0)*dstColor.r : sqrt(dstColor.r)) - dstColor.r);
                    Cr.g = (srcColor.g < 0.5) ? dstColor.g - (1.0-2.0*srcColor.g)*dstColor.g*(1.0-dstColor.g) : dstColor.g + (2.0*srcColor.g-1.0)*( (dstColor.g<0.25 ? ((16.0*dstColor.g-12.0)*dstColor.g+4.0)*dstColor.g : sqrt(dstColor.g)) - dstColor.g);
                    Cr.b = (srcColor.b < 0.5) ? dstColor.b - (1.0-2.0*srcColor.b)*dstColor.b*(1.0-dstColor.b) : dstColor.b + (2.0*srcColor.b-1.0)*( (dstColor.b<0.25 ? ((16.0*dstColor.b-12.0)*dstColor.b+4.0)*dstColor.b : sqrt(dstColor.b)) - dstColor.b);
                } else if (uBlendMode == 3) { // ColorDodge
                    Cr = dstColor / (1.0 - srcColor);
                } else if (uBlendMode == 4) { // ColorBurn
                    Cr = 1.0 - (1.0 - dstColor) / srcColor;
                } else if (uBlendMode == 5) { // Difference
                    Cr = abs(dstColor - srcColor);
                } else if (uBlendMode == 6) { // Exclusion
                    Cr = dstColor + srcColor - 2.0*dstColor*srcColor;
                } else if (uBlendMode == 7) { // Hue
                    Cr = setLum(setSat(srcColor, sat(dstColor)), lum(dstColor));
                } else if (uBlendMode == 8) { // Saturation
                    Cr = setLum(setSat(dstColor, sat(srcColor)), lum(dstColor));
                } else if (uBlendMode == 9) { // Color
                    Cr = setLum(srcColor, lum(dstColor));
                } else if (uBlendMode == 11) { // Multiply
                    Cr = srcColor * dstColor;
                } else if (uBlendMode == 12) { // Screen
                    Cr = srcColor + dstColor - srcColor * dstColor;
                } else if (uBlendMode == 13) { // Darken
                    Cr = min(srcColor, dstColor);
                } else if (uBlendMode == 14) { // Lighten
                    Cr = max(srcColor, dstColor);
                } else { // Luminosity
                Cr = setLum(dstColor, lum(srcColor));
            }
            Cr = clamp(Cr, 0.0, 1.0);
            // Blend modes are only meaningful where a backdrop exists. Over a
            // transparent backdrop (da < 1) — e.g. the bottom layer of an
            // isolated group, drawn against the freshly-cleared group FBO —
            // fade the blended color back toward the source so darken-type modes
            // (Multiply/Darken/ColorBurn/Overlay…) don't collapse to black.
            // W3C compositing: Cs' = (1.0 - ab)*Cs + ab*B(Cb, Cs).
            Cr = mix(srcColor, Cr, da);
            fragColor = vec4(dstPremul * (1.0 - sa) + Cr * sa, da + sa * (1.0 - da));
            }
        )");
    shader_compat::bindQuadAttributes(m_blendProg);
    if (!m_blendProg->addShaderFromSourceCode(QOpenGLShader::Vertex, blendVSrc) ||
        !m_blendProg->addShaderFromSourceCode(QOpenGLShader::Fragment, blendFSrc) ||
        !m_blendProg->link()) {
        qWarning("Blend shader failed: %s", qPrintable(m_blendProg->log()));
    }
    m_blendU.transform    = m_blendProg->uniformLocation("uTransform");
    m_blendU.srcTexture   = m_blendProg->uniformLocation("uSrcTexture");
    m_blendU.dstTexture   = m_blendProg->uniformLocation("uDstTexture");
    m_blendU.maskTexture  = m_blendProg->uniformLocation("uMaskTexture");
    m_blendU.blendMode    = m_blendProg->uniformLocation("uBlendMode");
    m_blendU.opacity      = m_blendProg->uniformLocation("uOpacity");
    m_blendU.viewportSize = m_blendProg->uniformLocation("uViewportSize");
    m_blendU.hasMask       = m_blendProg->uniformLocation("uHasMask");
    m_blendU.maskDensity  = m_blendProg->uniformLocation("uMaskDensity");
    m_blendU.maskUvScale = m_blendProg->uniformLocation("uMaskUVScale");
    m_blendU.maskUvOffset = m_blendProg->uniformLocation("uMaskUVOffset");
    m_blendU.srcPremultiplied = m_blendProg->uniformLocation("uSrcPremultiplied");
    m_blendU.dstPremultiplied = m_blendProg->uniformLocation("uDstPremultiplied");

    // ── Selection / Marching Ants program ──
    m_selectProg = new QOpenGLShaderProgram();
    const QByteArray selectVSrc = shader_compat::adaptSource(R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            layout(location = 1) in vec2 uv;
            uniform mat4 uTransform;
            out vec2 vUV;
            void main() {
                vUV = uv;
                gl_Position = uTransform * vec4(pos, 0.0, 1.0);
            }
        )");
    const QByteArray selectFSrc = shader_compat::adaptSource(R"(
            #version 330 core
            in vec2 vUV;
            out vec4 fragColor;
            uniform sampler2D uMask;
            uniform float uTime;
            uniform float uOpacity;
            uniform vec2 uTexel;
            uniform float uZoom;
            void main() {
                float safeZoom = max(uZoom, 0.01);
                // Sample 1 screen-pixel away (uTexel = 1 doc-pixel; divide by zoom to stay at 1 screen-pixel)
                vec2 sampleStep = uTexel / safeZoom;
                float mRaw = texture(uMask, vUV).r;
                // Ants follow the 50% level of the mask: a heavily feathered
                // selection has per-pixel gradients far below any fixed
                // threshold, so edge-detect the binarized value instead.
                float m  = step(0.5, mRaw);
                float mL = step(0.5, texture(uMask, vUV - vec2(sampleStep.x, 0.0)).r);
                float mR = step(0.5, texture(uMask, vUV + vec2(sampleStep.x, 0.0)).r);
                float mT = step(0.5, texture(uMask, vUV + vec2(0.0, sampleStep.y)).r);
                float mB = step(0.5, texture(uMask, vUV - vec2(0.0, sampleStep.y)).r);
                float edge = max(max(abs(m - mL), abs(m - mR)),
                                 max(abs(m - mT), abs(m - mB)));
                if (edge < 0.5) discard;
                vec2 p = vUV / max(uTexel, vec2(1e-6));
                if (uOpacity < 0.0) {
                    float ant = abs(sin(p.x * 0.5 * safeZoom + uTime * 4.0) * cos(p.y * 0.5 * safeZoom + uTime * 4.0));
                    float glow = 0.5 + 0.5 * ant;
                    fragColor = vec4(0.8, 0.2, 0.2, glow * 0.6 + (1.0 - mRaw) * 0.3);
                    return;
                }
                float dash = step(0.5, fract((p.x + p.y) * 0.125 * safeZoom + uTime * 2.5));
                vec3 col = mix(vec3(0.0), vec3(1.0), dash);
                fragColor = vec4(col, 1.0);
            }
        )");
    shader_compat::bindQuadAttributes(m_selectProg);
    if (!m_selectProg->addShaderFromSourceCode(QOpenGLShader::Vertex, selectVSrc) ||
        !m_selectProg->addShaderFromSourceCode(QOpenGLShader::Fragment, selectFSrc) ||
        !m_selectProg->link()) {
        qWarning("Selection shader failed: %s", qPrintable(m_selectProg->log()));
    }
    m_selU.transform = m_selectProg->uniformLocation("uTransform");
    m_selU.mask      = m_selectProg->uniformLocation("uMask");
    m_selU.time      = m_selectProg->uniformLocation("uTime");
    m_selU.opacity   = m_selectProg->uniformLocation("uOpacity");
    m_selU.texel     = m_selectProg->uniformLocation("uTexel");
    m_selU.zoom      = m_selectProg->uniformLocation("uZoom");

    // ── Rubylith program ──
    m_rubylithProg = new QOpenGLShaderProgram();
    const QByteArray rubylithVSrc = shader_compat::adaptSource(R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            layout(location = 1) in vec2 uv;
            uniform mat4 uTransform;
            out vec2 vUV;
            void main() {
                vUV = uv;
                gl_Position = uTransform * vec4(pos, 0.0, 1.0);
            }
        )");
    const QByteArray rubylithFSrc = shader_compat::adaptSource(R"(
            #version 330 core
            in vec2 vUV;
            out vec4 fragColor;
            uniform sampler2D uMask;
            uniform float uOpacity;
            uniform vec2 uMaskUvOffset;
            uniform vec2 uMaskUvScale;
            void main() {
                vec2 maskUv = uMaskUvOffset + vUV * uMaskUvScale;
                if (!all(greaterThanEqual(maskUv, vec2(0.0)))
                        || !all(lessThanEqual(maskUv, vec2(1.0)))) {
                    discard;
                }
                float m = texture(uMask, maskUv).r;
                if (1.0 - m < 0.01) discard;
                fragColor = vec4(0.8, 0.2, 0.2, (1.0 - m) * uOpacity);
            }
        )");
    shader_compat::bindQuadAttributes(m_rubylithProg);
    if (!m_rubylithProg->addShaderFromSourceCode(QOpenGLShader::Vertex, rubylithVSrc) ||
        !m_rubylithProg->addShaderFromSourceCode(QOpenGLShader::Fragment, rubylithFSrc) ||
        !m_rubylithProg->link()) {
        qWarning("Rubylith shader failed: %s", qPrintable(m_rubylithProg->log()));
    }
    m_rubyU.transform     = m_rubylithProg->uniformLocation("uTransform");
    m_rubyU.mask          = m_rubylithProg->uniformLocation("uMask");
    m_rubyU.opacity       = m_rubylithProg->uniformLocation("uOpacity");
    m_rubyU.maskUvOffset  = m_rubylithProg->uniformLocation("uMaskUvOffset");
    m_rubyU.maskUvScale   = m_rubylithProg->uniformLocation("uMaskUvScale");

    // ── Grayscale mask program ──
    m_grayProg = new QOpenGLShaderProgram();
    const QByteArray grayVSrc = shader_compat::adaptSource(R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            layout(location = 1) in vec2 uv;
            uniform mat4 uTransform;
            out vec2 vUV;
            void main() {
                vUV = uv;
                gl_Position = uTransform * vec4(pos, 0.0, 1.0);
            }
        )");
    const QByteArray grayFSrc = shader_compat::adaptSource(R"(
            #version 330 core
            in vec2 vUV;
            out vec4 fragColor;
            uniform sampler2D uTexture;
            uniform vec2 uMaskUvOffset;
            uniform vec2 uMaskUvScale;
            void main() {
                vec2 maskUv = uMaskUvOffset + vUV * uMaskUvScale;
                float m = 1.0;
                if (all(greaterThanEqual(maskUv, vec2(0.0)))
                        && all(lessThanEqual(maskUv, vec2(1.0)))) {
                    m = texture(uTexture, maskUv).r;
                }
                fragColor = vec4(m, m, m, 1.0);
            }
        )");
    shader_compat::bindQuadAttributes(m_grayProg);
    if (!m_grayProg->addShaderFromSourceCode(QOpenGLShader::Vertex, grayVSrc) ||
        !m_grayProg->addShaderFromSourceCode(QOpenGLShader::Fragment, grayFSrc) ||
        !m_grayProg->link()) {
        qWarning("Grayscale shader failed: %s", qPrintable(m_grayProg->log()));
    }
    m_grayU.transform = m_grayProg->uniformLocation("uTransform");
    m_grayU.texture   = m_grayProg->uniformLocation("uTexture");
    m_grayU.maskUvOffset = m_grayProg->uniformLocation("uMaskUvOffset");
    m_grayU.maskUvScale  = m_grayProg->uniformLocation("uMaskUvScale");

    // ── Adjustment-layer program (Normal-Mode adjustment pass) ──
    // Re-draws the canvas quad sampling the backdrop copy (uDstTexture, the
    // blend scratch texture) and outputs mix(backdrop, adjusted(backdrop),
    // opacity * mask). uAdjustmentType branches must mirror
    // adjustments::apply() in core/AdjustmentTypes.cpp exactly so the live
    // GPU frame matches the CPU projection pixel-for-pixel.
    m_adjustProg = new QOpenGLShaderProgram();
    const QByteArray adjustVSrc = shader_compat::adaptSource(R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            layout(location = 1) in vec2 uv;
            uniform mat4 uTransform;
            out vec2 vUV;
            void main() {
                vUV = uv;
                gl_Position = uTransform * vec4(pos, 0.0, 1.0);
            }
        )");
    const QByteArray adjustFSrc = shader_compat::adaptSource(R"(
            #version 330 core
            in vec2 vUV;
            out vec4 fragColor;
            uniform sampler2D uDstTexture;
            uniform sampler2D uMaskTexture;
            uniform sampler2D uCurveLut;
            uniform vec2 uViewportSize;
            uniform float uOpacity;
            uniform float uHasMask;
            uniform float uMaskDensity;
            uniform int uAdjustmentType;
            uniform float uPreserveLuminosity;
            uniform vec2 uMaskUvScale;
            uniform vec2 uMaskUvOffset;
            // Solid Color fill: straight colour in .rgb, alpha in .a, and the
            // blend-shader id (renderer/BlendRules.hpp; -1 = Normal) so the live
            // fill matches the CPU projection for every blend mode.
            uniform vec4 uSolidColor;
            uniform int uSolidBlendMode;
            // Hue/Saturation: per-range sliders [Master, Reds, Yellows, Greens,
            // Cyans, Blues, Magentas] plus the global Colorize flag. Must mirror
            // huesaturation::HueSaturationData::applyPixel() exactly.
            uniform float uHsHue[7];
            uniform float uHsSat[7];
            uniform float uHsLight[7];
            uniform float uColorize;
            // Editable per-band hue geometry [Reds..Magentas]; each vec4 is
            // (outerStart, innerStart, innerEnd, outerEnd) in the same continuous
            // degree frame as core/HueSaturationData.cpp (Reds stored around 0
            // with a negative outerStart for the 0/360 wrap).
            uniform vec4 uHsBandRange[6];
            float cbLum(vec3 c) { return 0.299*c.r + 0.587*c.g + 0.114*c.b; }
            vec3 cbClipColor(vec3 c) {
                float l = cbLum(c);
                float n = min(min(c.r, c.g), c.b);
                float x = max(max(c.r, c.g), c.b);
                if (n < 0.0)
                    c = l + (c - l) * l / (l - n);
                if (x > 1.0)
                    c = l + (c - l) * (1.0 - l) / (x - l);
                return c;
            }
            vec3 cbSetLum(vec3 c, float l) {
                return cbClipColor(c + (l - cbLum(c)));
            }
            // Saturation helpers for the Solid Color blend branch (mirror the
            // blend shader's sat()/setSat()).
            float scSat(vec3 c) { return max(max(c.r,c.g),c.b) - min(min(c.r,c.g),c.b); }
            vec3 scSetSat(vec3 c, float s) {
                float mn = min(min(c.r,c.g),c.b);
                float mx = max(max(c.r,c.g),c.b);
                if (mn == mx) return vec3(0.0);
                return (c - mn) * s / (mx - mn);
            }
            // ── Hue/Saturation helpers (mirror core/HueSaturationData.cpp) ──
            float hsWrap360(float h) { h = mod(h, 360.0); if (h < 0.0) h += 360.0; return h; }
            float hsHueDist(float h, float c) {
                float d = abs(hsWrap360(h) - c);
                if (d > 180.0) d = 360.0 - d;
                return d;
            }
            float hsBandWeight(float h, float c) {
                return clamp((15.0 + 30.0 - hsHueDist(h, c)) / 30.0, 0.0, 1.0);
            }
            // Brings absolute hue h into a continuous frame within ±180 of mid,
            // so the four ordered edges can be compared without wrap special-
            // casing (mirrors toCenterFrame() on the CPU).
            float hsToCenterFrame(float h, float mid) {
                float hh = hsWrap360(h) - mid;
                hh = mod(hh + 180.0, 360.0);
                if (hh < 0.0) hh += 360.0;
                return mid + (hh - 180.0);
            }
            // Per-band membership weight from edges e=(outerStart,innerStart,
            // innerEnd,outerEnd). Mirrors HueRange::weight() exactly.
            float hsRangeWeight(float h, vec4 e) {
                float mid = 0.5 * (e.y + e.z);
                float hh = hsToCenterFrame(h, mid);
                if (hh <= e.x || hh >= e.w) return 0.0;
                if (hh < e.y) { float d = e.y - e.x; return d > 1e-4 ? clamp((hh - e.x) / d, 0.0, 1.0) : 1.0; }
                if (hh > e.z) { float d = e.w - e.z; return d > 1e-4 ? clamp((e.w - hh) / d, 0.0, 1.0) : 1.0; }
                return 1.0;
            }
            vec3 hsRgb2Hsl(vec3 c) {
                float mx = max(max(c.r, c.g), c.b);
                float mn = min(min(c.r, c.g), c.b);
                float l = 0.5 * (mx + mn);
                float h = 0.0, s = 0.0;
                if (mx > mn) {
                    float d = mx - mn;
                    s = l > 0.5 ? d / (2.0 - mx - mn) : d / (mx + mn);
                    if (mx == c.r) h = (c.g - c.b) / d + (c.g < c.b ? 6.0 : 0.0);
                    else if (mx == c.g) h = (c.b - c.r) / d + 2.0;
                    else h = (c.r - c.g) / d + 4.0;
                    h *= 60.0;
                }
                return vec3(h, s, l);
            }
            float hsHue2Rgb(float p, float q, float t) {
                if (t < 0.0) t += 1.0;
                if (t > 1.0) t -= 1.0;
                if (t < 1.0 / 6.0) return p + (q - p) * 6.0 * t;
                if (t < 1.0 / 2.0) return q;
                if (t < 2.0 / 3.0) return p + (q - p) * (2.0 / 3.0 - t) * 6.0;
                return p;
            }
            vec3 hsHsl2Rgb(float h, float s, float l) {
                if (s <= 0.0) return vec3(l);
                float hn = hsWrap360(h) / 360.0;
                float q = l < 0.5 ? l * (1.0 + s) : l + s - l * s;
                float p = 2.0 * l - q;
                return vec3(hsHue2Rgb(p, q, hn + 1.0 / 3.0),
                            hsHue2Rgb(p, q, hn),
                            hsHue2Rgb(p, q, hn - 1.0 / 3.0));
            }
            float hsApplySat(float s, float adj) {
                float a = clamp(adj / 100.0, -1.0, 1.0);
                return clamp(s * (1.0 + a), 0.0, 1.0);
            }
            float hsApplyLight(float l, float adj) {
                float a = clamp(adj / 100.0, -1.0, 1.0);
                return a >= 0.0 ? l + a * (1.0 - l) : l * (1.0 + a);
            }
            void main() {
                vec2 st = gl_FragCoord.xy / uViewportSize;
                vec4 dst = texture(uDstTexture, st); // premultiplied backdrop
                vec3 adjusted = dst.rgb;
                if (uAdjustmentType == 0) {
                    // Grayscale — linear, so premultiplied-safe.
                    adjusted = vec3(0.299*dst.r + 0.587*dst.g + 0.114*dst.b);
                } else if (uAdjustmentType == 1) {
                    // Curves — non-linear, so un-premultiply, run the per-channel
                    // LUT (256x1, .r/.g/.b = combined R/G/B curves), re-premultiply.
                    vec3 straight = dst.a > 0.0039 ? dst.rgb / dst.a : dst.rgb;
                    straight = clamp(straight, 0.0, 1.0);
                    float lr = texture(uCurveLut, vec2(straight.r * (255.0/256.0) + 0.5/256.0, 0.5)).r;
                    float lg = texture(uCurveLut, vec2(straight.g * (255.0/256.0) + 0.5/256.0, 0.5)).g;
                    float lb = texture(uCurveLut, vec2(straight.b * (255.0/256.0) + 0.5/256.0, 0.5)).b;
                    adjusted = vec3(lr, lg, lb) * dst.a;
                } else if (uAdjustmentType == 2) {
                    // Color Balance — per-channel transfer LUT (.r/.g/.b carry the
                    // Cyan-Red / Magenta-Green / Yellow-Blue axes), then optionally
                    // restore the source luma so only the colour balance shifts.
                    vec3 straight = dst.a > 0.0039 ? dst.rgb / dst.a : dst.rgb;
                    straight = clamp(straight, 0.0, 1.0);
                    float br = texture(uCurveLut, vec2(straight.r * (255.0/256.0) + 0.5/256.0, 0.5)).r;
                    float bg = texture(uCurveLut, vec2(straight.g * (255.0/256.0) + 0.5/256.0, 0.5)).g;
                    float bb = texture(uCurveLut, vec2(straight.b * (255.0/256.0) + 0.5/256.0, 0.5)).b;
                    vec3 balanced = vec3(br, bg, bb);
                    if (uPreserveLuminosity > 0.5)
                        balanced = cbSetLum(balanced, cbLum(straight));
                    adjusted = balanced * dst.a;
                } else if (uAdjustmentType == 3) {
                    // Hue/Saturation — un-premultiply, run the shared HSL model,
                    // re-premultiply. Mirrors HueSaturationData::applyPixel().
                    vec3 straight = dst.a > 0.0039 ? dst.rgb / dst.a : dst.rgb;
                    straight = clamp(straight, 0.0, 1.0);
                    vec3 outc;
                    if (uColorize > 0.5) {
                        float luma = cbLum(straight);
                        float targetHue = hsWrap360(uHsHue[0]);
                        float cs = clamp(uHsSat[0], 0.0, 100.0) / 100.0;
                        float l = hsApplyLight(luma, uHsLight[0]);
                        outc = hsHsl2Rgb(targetHue, cs, l);
                    } else {
                        // Per-band weights come from the editable range edges
                        // (uHsBandRange), mirroring HueRange::weight() so the
                        // live GPU preview matches the CPU projection/export.
                        vec3 hsl = hsRgb2Hsl(straight);
                        float hueShift = uHsHue[0];
                        float satAdj = uHsSat[0];
                        float lightAdj = uHsLight[0];
                        for (int i = 0; i < 6; ++i) {
                            float w = hsRangeWeight(hsl.x, uHsBandRange[i]);
                            hueShift += w * uHsHue[i + 1];
                            satAdj += w * uHsSat[i + 1];
                            lightAdj += w * uHsLight[i + 1];
                        }
                        float h = hsWrap360(hsl.x + hueShift);
                        float s = hsApplySat(hsl.y, satAdj);
                        float l = hsApplyLight(hsl.z, lightAdj);
                        outc = hsHsl2Rgb(h, s, l);
                    }
                    adjusted = outc * dst.a;
                }
                float maskAlpha = 1.0;
                if (uHasMask > 0.5) {
                    vec2 maskUv = vUV * uMaskUvScale + uMaskUvOffset;
                    if (all(greaterThanEqual(maskUv, vec2(0.0)))
                            && all(lessThanEqual(maskUv, vec2(1.0)))) {
                        maskAlpha = texture(uMaskTexture, maskUv).r;
                    }
                }
                maskAlpha = mix(1.0, maskAlpha, uMaskDensity);
                float f = clamp(uOpacity * maskAlpha, 0.0, 1.0);
                if (uAdjustmentType == 4) {
                    // Solid Color — a fill, not a backdrop transform. Composite a
                    // premultiplied colour over the backdrop with the node's blend
                    // mode (mirrors the blend shader + DocumentCompositor so the
                    // live frame matches the CPU projection for every mode). f folds
                    // opacity*mask; uSolidColor.a folds the colour's own alpha.
                    float sa = clamp(uSolidColor.a * f, 0.0, 1.0);
                    vec3 srcColor = uSolidColor.rgb;
                    float da = dst.a;                         // backdrop alpha
                    vec3 dstColor = da > 0.0039 ? dst.rgb / da : dst.rgb;
                    int bm = uSolidBlendMode;
                    vec3 Cr;
                    if (bm < 0) {                  // Normal
                        Cr = srcColor;
                    } else if (bm == 0) {          // Overlay
                        Cr.r = (dstColor.r < 0.5) ? 2.0*srcColor.r*dstColor.r : 1.0 - 2.0*(1.0-srcColor.r)*(1.0-dstColor.r);
                        Cr.g = (dstColor.g < 0.5) ? 2.0*srcColor.g*dstColor.g : 1.0 - 2.0*(1.0-srcColor.g)*(1.0-dstColor.g);
                        Cr.b = (dstColor.b < 0.5) ? 2.0*srcColor.b*dstColor.b : 1.0 - 2.0*(1.0-srcColor.b)*(1.0-dstColor.b);
                    } else if (bm == 1) {          // HardLight
                        Cr.r = (srcColor.r < 0.5) ? 2.0*srcColor.r*dstColor.r : 1.0 - 2.0*(1.0-srcColor.r)*(1.0-dstColor.r);
                        Cr.g = (srcColor.g < 0.5) ? 2.0*srcColor.g*dstColor.g : 1.0 - 2.0*(1.0-srcColor.g)*(1.0-dstColor.g);
                        Cr.b = (srcColor.b < 0.5) ? 2.0*srcColor.b*dstColor.b : 1.0 - 2.0*(1.0-srcColor.b)*(1.0-dstColor.b);
                    } else if (bm == 2) {          // SoftLight
                        Cr.r = (srcColor.r < 0.5) ? dstColor.r - (1.0-2.0*srcColor.r)*dstColor.r*(1.0-dstColor.r) : dstColor.r + (2.0*srcColor.r-1.0)*( (dstColor.r<0.25 ? ((16.0*dstColor.r-12.0)*dstColor.r+4.0)*dstColor.r : sqrt(dstColor.r)) - dstColor.r);
                        Cr.g = (srcColor.g < 0.5) ? dstColor.g - (1.0-2.0*srcColor.g)*dstColor.g*(1.0-dstColor.g) : dstColor.g + (2.0*srcColor.g-1.0)*( (dstColor.g<0.25 ? ((16.0*dstColor.g-12.0)*dstColor.g+4.0)*dstColor.g : sqrt(dstColor.g)) - dstColor.g);
                        Cr.b = (srcColor.b < 0.5) ? dstColor.b - (1.0-2.0*srcColor.b)*dstColor.b*(1.0-dstColor.b) : dstColor.b + (2.0*srcColor.b-1.0)*( (dstColor.b<0.25 ? ((16.0*dstColor.b-12.0)*dstColor.b+4.0)*dstColor.b : sqrt(dstColor.b)) - dstColor.b);
                    } else if (bm == 3) {          // ColorDodge
                        Cr = dstColor / (1.0 - srcColor);
                    } else if (bm == 4) {          // ColorBurn
                        Cr = 1.0 - (1.0 - dstColor) / srcColor;
                    } else if (bm == 5) {          // Difference
                        Cr = abs(dstColor - srcColor);
                    } else if (bm == 6) {          // Exclusion
                        Cr = dstColor + srcColor - 2.0*dstColor*srcColor;
                    } else if (bm == 7) {          // Hue
                        Cr = cbSetLum(scSetSat(srcColor, scSat(dstColor)), cbLum(dstColor));
                    } else if (bm == 8) {          // Saturation
                        Cr = cbSetLum(scSetSat(dstColor, scSat(srcColor)), cbLum(dstColor));
                    } else if (bm == 9) {          // Color
                        Cr = cbSetLum(srcColor, cbLum(dstColor));
                    } else if (bm == 10) {         // Luminosity
                        Cr = cbSetLum(dstColor, cbLum(srcColor));
                    } else if (bm == 11) {         // Multiply
                        Cr = srcColor * dstColor;
                    } else if (bm == 12) {         // Screen
                        Cr = srcColor + dstColor - srcColor * dstColor;
                    } else if (bm == 13) {         // Darken
                        Cr = min(srcColor, dstColor);
                    } else {                       // Lighten (14)
                        Cr = max(srcColor, dstColor);
                    }
                    Cr = clamp(Cr, 0.0, 1.0);
                    // W3C: blend modes only meaningful over an existing backdrop;
                    // fade back to the source where the backdrop is transparent.
                    Cr = mix(srcColor, Cr, da);
                    // Source-over with premultiplied output (dst.rgb is premul).
                    fragColor = vec4(dst.rgb * (1.0 - sa) + Cr * sa,
                                     da + sa * (1.0 - da));
                } else {
                    fragColor = vec4(mix(dst.rgb, adjusted, f), dst.a);
                }
            }
        )");
    shader_compat::bindQuadAttributes(m_adjustProg);
    if (!m_adjustProg->addShaderFromSourceCode(QOpenGLShader::Vertex, adjustVSrc) ||
        !m_adjustProg->addShaderFromSourceCode(QOpenGLShader::Fragment, adjustFSrc) ||
        !m_adjustProg->link()) {
        qWarning("Adjustment shader failed: %s", qPrintable(m_adjustProg->log()));
    }
    m_adjustU.transform      = m_adjustProg->uniformLocation("uTransform");
    m_adjustU.dstTexture     = m_adjustProg->uniformLocation("uDstTexture");
    m_adjustU.maskTexture    = m_adjustProg->uniformLocation("uMaskTexture");
    m_adjustU.viewportSize   = m_adjustProg->uniformLocation("uViewportSize");
    m_adjustU.opacity        = m_adjustProg->uniformLocation("uOpacity");
    m_adjustU.hasMask        = m_adjustProg->uniformLocation("uHasMask");
    m_adjustU.maskDensity    = m_adjustProg->uniformLocation("uMaskDensity");
    m_adjustU.adjustmentType = m_adjustProg->uniformLocation("uAdjustmentType");
    m_adjustU.preserveLuminosity = m_adjustProg->uniformLocation("uPreserveLuminosity");
    m_adjustU.maskUvScale    = m_adjustProg->uniformLocation("uMaskUvScale");
    m_adjustU.maskUvOffset   = m_adjustProg->uniformLocation("uMaskUvOffset");
    m_adjustU.curveLut       = m_adjustProg->uniformLocation("uCurveLut");
    m_adjustU.hsHue          = m_adjustProg->uniformLocation("uHsHue");
    m_adjustU.hsSat          = m_adjustProg->uniformLocation("uHsSat");
    m_adjustU.hsLight        = m_adjustProg->uniformLocation("uHsLight");
    m_adjustU.colorize       = m_adjustProg->uniformLocation("uColorize");
    m_adjustU.hsBandRange    = m_adjustProg->uniformLocation("uHsBandRange");
    m_adjustU.solidColor     = m_adjustProg->uniformLocation("uSolidColor");
    m_adjustU.solidBlendMode = m_adjustProg->uniformLocation("uSolidBlendMode");

    // ── Display colour-management program (final 3D-LUT pass) ──
    // Fullscreen pass over the scene FBO: un-premultiplies the composited colour,
    // looks it up through the document→monitor (+ soft proof) 3D LUT, and emits
    // straight alpha — mirroring the main shader's convention so it composites to
    // screen with the same applyFixedBlend(Normal) as every other layer.
    m_displayProg = new QOpenGLShaderProgram();
    const QByteArray displayVSrc = shader_compat::adaptSource(R"(
            #version 330 core
            layout(location = 0) in vec2 pos;
            out vec2 vUV;
            void main() {
                vUV = pos * 0.5 + 0.5;
                gl_Position = vec4(pos, 0.0, 1.0);
            }
        )");
    const QByteArray displayFSrc = shader_compat::adaptSource(R"(
            #version 330 core
            in vec2 vUV;
            out vec4 fragColor;
            uniform sampler2D uScene;
            uniform sampler3D uLut;
            void main() {
                vec4 c = texture(uScene, vUV);
                vec3 straight = (c.a > 0.0) ? c.rgb / c.a : c.rgb;
                vec3 managed = texture(uLut, clamp(straight, 0.0, 1.0)).rgb;
                fragColor = vec4(managed, c.a);
            }
        )");
    shader_compat::bindQuadAttributes(m_displayProg);
    if (!m_displayProg->addShaderFromSourceCode(QOpenGLShader::Vertex, displayVSrc) ||
        !m_displayProg->addShaderFromSourceCode(QOpenGLShader::Fragment, displayFSrc) ||
        !m_displayProg->link()) {
        qWarning("Display-CM shader failed: %s", qPrintable(m_displayProg->log()));
    }
    m_displayU.scene = m_displayProg->uniformLocation("uScene");
    m_displayU.lut   = m_displayProg->uniformLocation("uLut");
}

void GPUViewport::collectUniforms()
{
    m_mainU.transform   = m_mainProg->uniformLocation("uTransform");
    m_mainU.texture     = m_mainProg->uniformLocation("uTexture");
    m_mainU.maskTexture = m_mainProg->uniformLocation("uMaskTexture");
    m_mainU.opacity     = m_mainProg->uniformLocation("uOpacity");
    m_mainU.hasMask     = m_mainProg->uniformLocation("uHasMask");
    m_mainU.maskDensity = m_mainProg->uniformLocation("uMaskDensity");
    m_mainU.sourcePremultiplied = m_mainProg->uniformLocation("uSourcePremultiplied");
    m_mainU.alphaWeightedSampling = m_mainProg->uniformLocation("uAlphaWeightedSampling");
    m_mainU.uvScale     = m_mainProg->uniformLocation("uUVScale");
    m_mainU.uvOffset    = m_mainProg->uniformLocation("uUVOffset");
    m_mainU.maskUvScale = m_mainProg->uniformLocation("uMaskUVScale");
    m_mainU.maskUvOffset = m_mainProg->uniformLocation("uMaskUVOffset");
    m_mainU.instanced   = m_mainProg->uniformLocation("uInstanced");
}
