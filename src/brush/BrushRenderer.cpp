#include "BrushRenderer.hpp"
#include "BrushDab.hpp"
#include "AutoBrush.hpp"
#include "PredefinedBrush.hpp"
#include "core/Layer.hpp"
#include "core/LayerTreeNode.hpp"
#include "renderer/ShaderCompat.hpp"

#include <QDebug>
#include <QOpenGLContext>
#include <QOpenGLExtraFunctions>
#include <QPainter>
#include <QRandomGenerator>
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>
#include <vector>

namespace {
// Pattern offset (px), mirroring the CPU dab: the fixed offset plus the
// PER-STROKE random shift, constant across the dabs of one stroke so the grain
// stays anchored to the "paper" while the brush moves, and successive strokes
// land on different pattern origins.
inline QPointF brushTexOffset(const TextureConfig& tc,
                              int strokeOffsetX, int strokeOffsetY)
{
    const float offX = tc.horizontalOffset
        + (tc.randomHorizontalOffset ? strokeOffsetX : 0);
    const float offY = tc.verticalOffset
        + (tc.randomVerticalOffset ? strokeOffsetY : 0);
    return QPointF(offX, offY);
}
} // namespace

BrushRenderer::BrushRenderer()
{
    m_lastSettings.size = -1;
    m_lastSettings.hardness = -1;
}

BrushRenderer::~BrushRenderer()
{
    destroyGL();
}

static void uploadR8Image(const QImage& image)
{
    GLint prevUnpackAlignment = 4;
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevUnpackAlignment);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8,
                 image.width(), image.height(), 0,
                 GL_RED, GL_UNSIGNED_BYTE,
                 image.constBits());
    glPixelStorei(GL_UNPACK_ALIGNMENT, prevUnpackAlignment);
}

void BrushRenderer::initGL()
{
    if (m_program) return;
    initializeOpenGLFunctions();

    m_program = new QOpenGLShaderProgram();

    const char* vSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 pos;
        layout(location = 1) in vec2 uv;
        uniform vec2 uCenter;
        uniform float uRadius;
        uniform vec2 uTexSize;
        uniform float uAngle;
        uniform float uRoundness;
        out vec2 vUV;
        out vec2 vLayerPos;
        void main() {
            vUV = uv;
            vec2 rPos = pos;
            rPos.y *= uRoundness;
            float c = cos(uAngle);
            float s = sin(uAngle);
            vec2 rotPos;
            rotPos.x = rPos.x * c - rPos.y * s;
            rotPos.y = rPos.x * s + rPos.y * c;
            vec2 pixelPos = uCenter + rotPos * uRadius;
            vLayerPos = pixelPos;
            vec2 clip = 2.0 * pixelPos / uTexSize - 1.0;
            gl_Position = vec4(clip, 0.0, 1.0);
        }
    )";

    const char* fSrc = R"(
        #version 330 core

        in vec2 vUV;
        in vec2 vLayerPos;
        out vec4 fragColor;

        uniform sampler2D uStamp;
        uniform sampler2D uTexStamp;
        uniform vec4 uColor;
        uniform float uFlow;
        uniform float uOpacity;
        uniform float uTexBrightness;
        uniform float uTexContrast;
        uniform float uTexDepth;
        uniform int uTexInvert;
        uniform vec2 uTexUVScale;
        uniform vec2 uTexUVOffset;
        uniform float uTexNeutral;
        uniform int uTexCut;
        uniform float uTexCutLo;
        uniform float uTexCutHi;
        uniform int uTexSubtract;
        uniform int uTexSoft;
        uniform vec2 uTexSizePx;
        uniform int uUseSelectMask;
        uniform sampler2D uSelectMask;
        uniform vec2 uSelectMaskSize;
        uniform vec3 uLayerToDocX;
        uniform vec3 uLayerToDocY;
        uniform int uBlendMode;
        uniform sampler2D uDestTexture;
        uniform vec2 uDestTexSize;

        vec3 blendNormal(vec3 s, vec3 d) { return s; }
        vec3 blendMultiply(vec3 s, vec3 d) { return s * d; }
        vec3 blendScreen(vec3 s, vec3 d) { return 1.0 - (1.0 - s) * (1.0 - d); }
        vec3 blendOverlay(vec3 s, vec3 d) {
            return mix(2.0 * s * d, 1.0 - 2.0 * (1.0 - s) * (1.0 - d), step(0.5, d));
        }
        vec3 blendDarken(vec3 s, vec3 d) { return min(s, d); }
        vec3 blendLighten(vec3 s, vec3 d) { return max(s, d); }
        vec3 blendColorDodge(vec3 s, vec3 d) {
            return min(d / max(1.0 - s, 0.001), 1.0);
        }
        vec3 blendColorBurn(vec3 s, vec3 d) {
            return 1.0 - min((1.0 - d) / max(s, 0.001), 1.0);
        }
        vec3 blendHardLight(vec3 s, vec3 d) {
            return mix(2.0 * s * d, 1.0 - 2.0 * (1.0 - s) * (1.0 - d), step(0.5, s));
        }
        vec3 blendSoftLight(vec3 s, vec3 d) {
            return (1.0 - 2.0 * s) * d * d + 2.0 * s * d;
        }
        vec3 blendDifference(vec3 s, vec3 d) { return abs(d - s); }
        vec3 blendExclusion(vec3 s, vec3 d) { return s + d - 2.0 * s * d; }

        vec3 rgb2hsv(vec3 c) {
            vec4 K = vec4(0.0, -1.0/3.0, 2.0/3.0, -1.0);
            vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
            vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
            float d = q.x - min(q.w, q.y);
            float e = 1.0e-10;
            return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
        }
        vec3 hsv2rgb(vec3 c) {
            vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
            vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
            return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
        }

        vec3 blendHue(vec3 s, vec3 d) {
            vec3 sh = rgb2hsv(s); vec3 dh = rgb2hsv(d);
            return hsv2rgb(vec3(sh.x, dh.y, dh.z));
        }
        vec3 blendSaturation(vec3 s, vec3 d) {
            vec3 sh = rgb2hsv(s); vec3 dh = rgb2hsv(d);
            return hsv2rgb(vec3(dh.x, sh.y, dh.z));
        }
        vec3 blendColor(vec3 s, vec3 d) {
            vec3 sh = rgb2hsv(s); vec3 dh = rgb2hsv(d);
            return hsv2rgb(vec3(sh.x, sh.y, dh.z));
        }
        vec3 blendLuminosity(vec3 s, vec3 d) {
            vec3 sh = rgb2hsv(s); vec3 dh = rgb2hsv(d);
            return hsv2rgb(vec3(dh.x, dh.y, sh.z));
        }

        vec3 blendFunc(vec3 s, vec3 d, int mode) {
            if (mode == 1) return blendMultiply(s, d);
            if (mode == 2) return blendScreen(s, d);
            if (mode == 3) return blendOverlay(s, d);
            if (mode == 4) return blendDarken(s, d);
            if (mode == 5) return blendLighten(s, d);
            if (mode == 6) return blendColorDodge(s, d);
            if (mode == 7) return blendColorBurn(s, d);
            if (mode == 8) return blendHardLight(s, d);
            if (mode == 9) return blendSoftLight(s, d);
            if (mode == 10) return blendDifference(s, d);
            if (mode == 11) return blendExclusion(s, d);
            if (mode == 12) return blendHue(s, d);
            if (mode == 13) return blendSaturation(s, d);
            if (mode == 14) return blendColor(s, d);
            if (mode == 15) return blendLuminosity(s, d);
            return blendNormal(s, d);
        }

        void main() {
            float mask = texture(uStamp, vUV).r;

            if (uTexDepth > 0.01) {
                // Anchor the pattern to LAYER space (paper-anchored, like the CPU
                // dab): texel = layerPixel/scale + offset, then normalise by the
                // texture size with REPEAT wrap so overlapping dabs reuse the grain.
                vec2 texUV = (vLayerPos * uTexUVScale + uTexUVOffset) / uTexSizePx;
                float tex = texture(uTexStamp, texUV).r;
                // Mirror BrushDab::rasterize (the CPU reference used by the raster
                // tile path) EXACTLY, so a textured brush paints the same grain on a
                // layer mask as it does on pixels: brightness subtract, contrast
                // pivots around 0.5, clamp, invert, neutral-point piecewise remap,
                // cutoff (zero outside range), then the Subtract/Multiply coverage op
                // (soft scales the source by strength instead of offsetting it).
                tex = tex - uTexBrightness;
                tex = (tex - 0.5) * uTexContrast + 0.5;
                tex = clamp(tex, 0.0, 1.0);
                if (uTexInvert != 0) tex = 1.0 - tex;
                float np = uTexNeutral;
                float nv = (np >= 1.0 || (np > 0.0 && tex <= np))
                    ? tex / max(2.0 * np, 1e-6)
                    : 0.5 + (tex - np) / (2.0 - 2.0 * np);
                tex = clamp(nv, 0.0, 1.0);
                if (uTexCut != 0 && (tex < uTexCutLo || tex > uTexCutHi))
                    tex = 0.0;
                float s = uTexDepth;
                if (uTexSubtract != 0) {
                    mask = (uTexSoft != 0)
                        ? max(0.0, mask - tex * s)
                        : max(0.0, mask - (tex + (1.0 - s)));
                } else {
                    mask = (uTexSoft != 0)
                        ? mask * (tex * s + (1.0 - s))
                        : mask * tex * s;
                }
            }

            float srcAlpha = mask * uFlow;
            if (srcAlpha < 0.003) discard;

            if (uUseSelectMask != 0) {
                vec2 docCoord = vec2(
                    uLayerToDocX.x * gl_FragCoord.x + uLayerToDocX.y * gl_FragCoord.y + uLayerToDocX.z,
                    uLayerToDocY.x * gl_FragCoord.x + uLayerToDocY.y * gl_FragCoord.y + uLayerToDocY.z
                );
                vec2 selUV = docCoord / uSelectMaskSize;
                float sel = texture(uSelectMask, selUV).r;
                srcAlpha *= sel;
                if (srcAlpha < 0.003) discard;
            }

            srcAlpha *= uOpacity;
            vec3 srcColor = uColor.rgb;

            if (uBlendMode == 0) {
                fragColor = vec4(srcColor, srcAlpha);
                return;
            }

            vec4 dst = texture(uDestTexture, gl_FragCoord.xy / uDestTexSize);

            float dstA = dst.a;
            vec3 dstC = dstA > 0.001 ? dst.rgb / dstA : vec3(0.0);

            vec3 resultC = blendFunc(srcColor, dstC, uBlendMode);
            float outA = srcAlpha + dstA * (1.0 - srcAlpha);

            fragColor = vec4(resultC * outA, outA);
        }
    )";

    const QByteArray compatVSrc = shader_compat::adaptSource(vSrc);
    const QByteArray compatFSrc = shader_compat::adaptSource(fSrc);
    shader_compat::bindQuadAttributes(m_program);

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, compatVSrc) ||
        !m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, compatFSrc) ||
        !m_program->link()) {
        qWarning("BrushRenderer shader compilation failed: %s",
                 qPrintable(m_program->log()));
        return;
    }

    m_uCenter        = m_program->uniformLocation("uCenter");
    m_uRadius        = m_program->uniformLocation("uRadius");
    m_uTexSize       = m_program->uniformLocation("uTexSize");
    m_uColor         = m_program->uniformLocation("uColor");
    m_uFlow          = m_program->uniformLocation("uFlow");
    m_uOpacity       = m_program->uniformLocation("uOpacity");
    m_uStamp         = m_program->uniformLocation("uStamp");
    m_uAngle         = m_program->uniformLocation("uAngle");
    m_uRoundness     = m_program->uniformLocation("uRoundness");
    m_uUseSelectMask = m_program->uniformLocation("uUseSelectMask");
    m_uSelectMask    = m_program->uniformLocation("uSelectMask");
    m_uSelectMaskSize = m_program->uniformLocation("uSelectMaskSize");
    m_uLayerToDocX   = m_program->uniformLocation("uLayerToDocX");
    m_uLayerToDocY   = m_program->uniformLocation("uLayerToDocY");
    m_uTexStamp      = m_program->uniformLocation("uTexStamp");
    m_uTexBrightness = m_program->uniformLocation("uTexBrightness");
    m_uTexContrast   = m_program->uniformLocation("uTexContrast");
    m_uTexDepth      = m_program->uniformLocation("uTexDepth");
    m_uTexInvert     = m_program->uniformLocation("uTexInvert");
    m_uTexUVScale    = m_program->uniformLocation("uTexUVScale");
    m_uTexUVOffset   = m_program->uniformLocation("uTexUVOffset");
    m_uTexNeutral    = m_program->uniformLocation("uTexNeutral");
    m_uTexCut        = m_program->uniformLocation("uTexCut");
    m_uTexCutLo      = m_program->uniformLocation("uTexCutLo");
    m_uTexCutHi      = m_program->uniformLocation("uTexCutHi");
    m_uTexSubtract   = m_program->uniformLocation("uTexSubtract");
    m_uTexSoft       = m_program->uniformLocation("uTexSoft");
    m_uTexSizePx     = m_program->uniformLocation("uTexSizePx");
    m_uBlendMode     = m_program->uniformLocation("uBlendMode");
    m_uDestTexture   = m_program->uniformLocation("uDestTexture");
    m_uDestTexSize   = m_program->uniformLocation("uDestTexSize");

    struct QuadVertex { float x, y, u, v; };
    static const QuadVertex verts[4] = {
        { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 1.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 1.0f, 0.0f },
    };

    m_vao.create();
    m_vbo.create();
    m_vao.bind();
    m_vbo.bind();
    m_vbo.allocate(verts, sizeof(verts));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          reinterpret_cast<void*>(offsetof(QuadVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          reinterpret_cast<void*>(offsetof(QuadVertex, u)));
    m_vao.release();

    glGenTextures(1, &m_destCopyTexture);
    glBindTexture(GL_TEXTURE_2D, m_destCopyTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    initMaskShader();
}

void BrushRenderer::destroyGL()
{
    delete m_program;
    m_program = nullptr;
    m_vao.destroy();
    m_vbo.destroy();
    delete m_maskProgram;
    m_maskProgram = nullptr;
    m_maskVao.destroy();
    m_maskVbo.destroy();
    if (m_stampTexture) {
        glDeleteTextures(1, &m_stampTexture);
        m_stampTexture = 0;
    }
    if (m_texStampTexture) {
        glDeleteTextures(1, &m_texStampTexture);
        m_texStampTexture = 0;
    }
    if (m_dualStampTexture) {
        glDeleteTextures(1, &m_dualStampTexture);
        m_dualStampTexture = 0;
    }
    if (m_destCopyTexture) {
        glDeleteTextures(1, &m_destCopyTexture);
        m_destCopyTexture = 0;
    }
    if (m_maskCopyTexture) {
        glDeleteTextures(1, &m_maskCopyTexture);
        m_maskCopyTexture = 0;
    }
}

void BrushRenderer::initMaskShader()
{
    m_maskProgram = new QOpenGLShaderProgram();

    const char* vSrc = R"(
        #version 330 core
        layout(location = 0) in vec2 pos;
        layout(location = 1) in vec2 uv;
        uniform vec2 uCenter;
        uniform float uRadius;
        uniform vec2 uTexSize;
        uniform float uAngle;
        uniform float uRoundness;
        out vec2 vUV;
        out vec2 vLayerPos;
        void main() {
            vUV = uv;
            vec2 rPos = pos;
            rPos.y *= uRoundness;
            float c = cos(uAngle);
            float s = sin(uAngle);
            vec2 rotPos;
            rotPos.x = rPos.x * c - rPos.y * s;
            rotPos.y = rPos.x * s + rPos.y * c;
            vec2 pixelPos = uCenter + rotPos * uRadius;
            vLayerPos = pixelPos;
            vec2 clip = 2.0 * pixelPos / uTexSize - 1.0;
            gl_Position = vec4(clip, 0.0, 1.0);
        }
    )";

    const char* fSrc = R"(
        #version 330 core

        in vec2 vUV;
        in vec2 vLayerPos;
        out vec4 fragMaskValue;

        uniform sampler2D uStamp;
        uniform sampler2D uTexStamp;
        uniform float uFlow;
        uniform float uOpacity;
        uniform float uMaskTarget;
        uniform float uTexBrightness;
        uniform float uTexContrast;
        uniform float uTexDepth;
        uniform int uTexInvert;
        uniform vec2 uTexUVScale;
        uniform vec2 uTexUVOffset;
        uniform float uTexNeutral;
        uniform int uTexCut;
        uniform float uTexCutLo;
        uniform float uTexCutHi;
        uniform int uTexSubtract;
        uniform int uTexSoft;
        uniform vec2 uTexSizePx;
        uniform int uUseSelectMask;
        uniform sampler2D uSelectMask;
        uniform vec2 uSelectMaskSize;
        uniform vec3 uLayerToDocX;
        uniform vec3 uLayerToDocY;
        uniform sampler2D uDestMask;
        uniform vec2 uDestTexSize;

        void main() {
            float mask = texture(uStamp, vUV).r;

            if (uTexDepth > 0.01) {
                // Anchor the pattern to LAYER space (paper-anchored, like the CPU
                // dab): texel = layerPixel/scale + offset, then normalise by the
                // texture size with REPEAT wrap so overlapping dabs reuse the grain.
                vec2 texUV = (vLayerPos * uTexUVScale + uTexUVOffset) / uTexSizePx;
                float tex = texture(uTexStamp, texUV).r;
                // Mirror BrushDab::rasterize (the CPU reference used by the raster
                // tile path) EXACTLY, so a textured brush paints the same grain on a
                // layer mask as it does on pixels: brightness subtract, contrast
                // pivots around 0.5, clamp, invert, neutral-point piecewise remap,
                // cutoff (zero outside range), then the Subtract/Multiply coverage op
                // (soft scales the source by strength instead of offsetting it).
                tex = tex - uTexBrightness;
                tex = (tex - 0.5) * uTexContrast + 0.5;
                tex = clamp(tex, 0.0, 1.0);
                if (uTexInvert != 0) tex = 1.0 - tex;
                float np = uTexNeutral;
                float nv = (np >= 1.0 || (np > 0.0 && tex <= np))
                    ? tex / max(2.0 * np, 1e-6)
                    : 0.5 + (tex - np) / (2.0 - 2.0 * np);
                tex = clamp(nv, 0.0, 1.0);
                if (uTexCut != 0 && (tex < uTexCutLo || tex > uTexCutHi))
                    tex = 0.0;
                float s = uTexDepth;
                if (uTexSubtract != 0) {
                    mask = (uTexSoft != 0)
                        ? max(0.0, mask - tex * s)
                        : max(0.0, mask - (tex + (1.0 - s)));
                } else {
                    mask = (uTexSoft != 0)
                        ? mask * (tex * s + (1.0 - s))
                        : mask * tex * s;
                }
            }

            float srcAlpha = mask * uFlow;
            if (srcAlpha < 0.003) discard;

            if (uUseSelectMask != 0) {
                vec2 docCoord = vec2(
                    uLayerToDocX.x * gl_FragCoord.x + uLayerToDocX.y * gl_FragCoord.y + uLayerToDocX.z,
                    uLayerToDocY.x * gl_FragCoord.x + uLayerToDocY.y * gl_FragCoord.y + uLayerToDocY.z
                );
                vec2 selUV = docCoord / uSelectMaskSize;
                float sel = texture(uSelectMask, selUV).r;
                srcAlpha *= sel;
                if (srcAlpha < 0.003) discard;
            }

            srcAlpha *= uOpacity;

            float dstMask = texture(uDestMask, gl_FragCoord.xy / uDestTexSize).r;

            float result = mix(dstMask, uMaskTarget, srcAlpha);
            fragMaskValue = vec4(result, 0.0, 0.0, 1.0);
        }
    )";

    const QByteArray compatVSrc = shader_compat::adaptSource(vSrc);
    const QByteArray compatFSrc = shader_compat::adaptSource(fSrc);
    shader_compat::bindQuadAttributes(m_maskProgram);

    if (!m_maskProgram->addShaderFromSourceCode(QOpenGLShader::Vertex, compatVSrc) ||
        !m_maskProgram->addShaderFromSourceCode(QOpenGLShader::Fragment, compatFSrc) ||
        !m_maskProgram->link()) {
        qWarning("BrushRenderer mask shader compilation failed: %s",
                 qPrintable(m_maskProgram->log()));
        delete m_maskProgram;
        m_maskProgram = nullptr;
        return;
    }

    m_muCenter        = m_maskProgram->uniformLocation("uCenter");
    m_muRadius        = m_maskProgram->uniformLocation("uRadius");
    m_muTexSize       = m_maskProgram->uniformLocation("uTexSize");
    m_muFlow          = m_maskProgram->uniformLocation("uFlow");
    m_muOpacity       = m_maskProgram->uniformLocation("uOpacity");
    m_muMaskTarget    = m_maskProgram->uniformLocation("uMaskTarget");
    m_muStamp         = m_maskProgram->uniformLocation("uStamp");
    m_muAngle         = m_maskProgram->uniformLocation("uAngle");
    m_muRoundness     = m_maskProgram->uniformLocation("uRoundness");
    m_muUseSelectMask = m_maskProgram->uniformLocation("uUseSelectMask");
    m_muSelectMask    = m_maskProgram->uniformLocation("uSelectMask");
    m_muSelectMaskSize = m_maskProgram->uniformLocation("uSelectMaskSize");
    m_muLayerToDocX   = m_maskProgram->uniformLocation("uLayerToDocX");
    m_muLayerToDocY   = m_maskProgram->uniformLocation("uLayerToDocY");
    m_muTexStamp      = m_maskProgram->uniformLocation("uTexStamp");
    m_muTexBrightness = m_maskProgram->uniformLocation("uTexBrightness");
    m_muTexContrast   = m_maskProgram->uniformLocation("uTexContrast");
    m_muTexDepth      = m_maskProgram->uniformLocation("uTexDepth");
    m_muTexInvert     = m_maskProgram->uniformLocation("uTexInvert");
    m_muTexUVScale    = m_maskProgram->uniformLocation("uTexUVScale");
    m_muTexUVOffset   = m_maskProgram->uniformLocation("uTexUVOffset");
    m_muTexNeutral    = m_maskProgram->uniformLocation("uTexNeutral");
    m_muTexCut        = m_maskProgram->uniformLocation("uTexCut");
    m_muTexCutLo      = m_maskProgram->uniformLocation("uTexCutLo");
    m_muTexCutHi      = m_maskProgram->uniformLocation("uTexCutHi");
    m_muTexSubtract   = m_maskProgram->uniformLocation("uTexSubtract");
    m_muTexSoft       = m_maskProgram->uniformLocation("uTexSoft");
    m_muTexSizePx     = m_maskProgram->uniformLocation("uTexSizePx");
    m_muDestMask      = m_maskProgram->uniformLocation("uDestMask");
    m_muDestTexSize   = m_maskProgram->uniformLocation("uDestTexSize");

    struct QuadVertex { float x, y, u, v; };
    static const QuadVertex verts[4] = {
        { -1.0f, -1.0f, 0.0f, 1.0f },
        {  1.0f, -1.0f, 1.0f, 1.0f },
        { -1.0f,  1.0f, 0.0f, 0.0f },
        {  1.0f,  1.0f, 1.0f, 0.0f },
    };

    m_maskVao.create();
    m_maskVbo.create();
    m_maskVao.bind();
    m_maskVbo.bind();
    m_maskVbo.allocate(verts, sizeof(verts));
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          reinterpret_cast<void*>(offsetof(QuadVertex, x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(QuadVertex),
                          reinterpret_cast<void*>(offsetof(QuadVertex, u)));
    m_maskVao.release();

    glGenTextures(1, &m_maskCopyTexture);
    glBindTexture(GL_TEXTURE_2D, m_maskCopyTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
}

void BrushRenderer::generateStamp(const BrushSettings& settings)
{
    // The stamp is a normalized profile: the GPU samples it by UV and scales the
    // quad by uRadius, so the texture never needs to match the brush's pixel
    // size. Cap it so a huge brush (e.g. 5000 px) does not rebuild a ~10000²
    // image on every size/hardness change — which is pure waste on the active
    // CPU raster-tile path, which doesn't use this stamp at all.
    constexpr int kMaxStampDiam = 1024;
    int diam = std::min(kMaxStampDiam,
                        std::max(4, static_cast<int>(std::ceil(settings.size)) * 2 + 2));
    m_stampSize = diam;

    // Layer A builds the normalized stamp from the SAME brush the canvas paints
    // with, so the GPU stamp (sampled by UV and scaled by uRadius) stays
    // consistent with the raster dab. PredefinedBrush for image tips (pyramid +
    // alpha/inverted-luminance coverage), AutoBrush for procedural round/square.
    if (settings.tipSource == BrushTipSource::Image && !settings.tipImage.isNull()) {
        PredefinedBrush::Params pp;
        pp.tip = settings.tipImage;
        pp.application = settings.application;
        pp.remap = TipRemap{ settings.tipBrightness, settings.tipContrast,
                             settings.tipMidpoint };
        m_stampImage = PredefinedBrush(pp).staticStamp(diam);
    } else {
        m_stampImage = AutoBrush(autoBrushParamsFromSettings(settings)).staticStamp(diam);
    }

    m_stampDirty = true;
}

void BrushRenderer::uploadStampTexture()
{
    if (m_stampImage.isNull()) return;

    if (m_stampTexture == 0)
        glGenTextures(1, &m_stampTexture);

    glBindTexture(GL_TEXTURE_2D, m_stampTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uploadR8Image(m_stampImage);

    m_stampDirty = false;
}

void BrushRenderer::updateStamp(const BrushSettings& settings)
{
    if (m_lastSettings.size == settings.size &&
        m_lastSettings.hardness == settings.hardness &&
        m_lastSettings.type == settings.type &&
        m_lastSettings.autoBrush.profile == settings.autoBrush.profile &&
        m_lastSettings.autoBrush.spikes == settings.autoBrush.spikes &&
        m_lastSettings.autoBrush.density == settings.autoBrush.density &&
        m_lastSettings.tipSource == settings.tipSource &&
        m_lastSettings.tipImage.cacheKey() == settings.tipImage.cacheKey())
        return;

    m_lastSettings = settings;
    generateStamp(settings);
}

void BrushRenderer::setTextureImage(const QImage& img)
{
    m_texStampImage = img.convertToFormat(QImage::Format_Grayscale8);
    m_texStampDirty = true;
}

void BrushRenderer::clearTexture()
{
    m_texStampImage = QImage();
    if (m_texStampTexture) {
        glDeleteTextures(1, &m_texStampTexture);
        m_texStampTexture = 0;
    }
    m_texStampDirty = false;
}

void BrushRenderer::generateDualStamp(const DualBrushConfig& config)
{
    int r = static_cast<int>(std::ceil(config.size));
    int diam = r * 2 + 2;
    m_dualStampSize = diam;

    m_dualStampImage = QImage(diam, diam, QImage::Format_Grayscale8);
    m_dualStampImage.fill(0);

    float cx = diam / 2.0f;
    float cy = diam / 2.0f;
    float radius = config.size;
    float hardness = std::clamp(config.hardness, 0.0f, 1.0f);
    float aa = 1.5f / radius;

    if (config.tipType == 1) {
        for (int y = 0; y < diam; ++y) {
            uchar* row = m_dualStampImage.scanLine(y);
            for (int x = 0; x < diam; ++x) {
                float dx = std::abs(x - cx);
                float dy = std::abs(y - cy);
                float d = std::max(dx, dy) / radius;
                if (d <= 1.0f)
                    row[x] = static_cast<uchar>(std::clamp(
                        brushFalloff(d, hardness, aa) * 255.0f, 0.0f, 255.0f));
            }
        }
    } else {
        for (int y = 0; y < diam; ++y) {
            uchar* row = m_dualStampImage.scanLine(y);
            for (int x = 0; x < diam; ++x) {
                float d = std::sqrt((x - cx) * (x - cx) + (y - cy) * (y - cy)) / radius;
                if (d <= 1.0f)
                    row[x] = static_cast<uchar>(std::clamp(
                        brushFalloff(d, hardness, aa) * 255.0f, 0.0f, 255.0f));
            }
        }
    }

    m_dualStampDirty = true;
}

void BrushRenderer::uploadDualStampTexture()
{
    if (m_dualStampImage.isNull()) return;

    if (m_dualStampTexture == 0)
        glGenTextures(1, &m_dualStampTexture);

    glBindTexture(GL_TEXTURE_2D, m_dualStampTexture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    uploadR8Image(m_dualStampImage);

    m_dualStampDirty = false;
}

static QPainter::CompositionMode painterModeForBrushBlend(BrushBlendMode mode)
{
    switch (mode) {
    case BrushBlendMode::Multiply: return QPainter::CompositionMode_Multiply;
    case BrushBlendMode::Screen: return QPainter::CompositionMode_Screen;
    case BrushBlendMode::Overlay: return QPainter::CompositionMode_Overlay;
    case BrushBlendMode::Darken: return QPainter::CompositionMode_Darken;
    case BrushBlendMode::Lighten: return QPainter::CompositionMode_Lighten;
    case BrushBlendMode::ColorDodge: return QPainter::CompositionMode_ColorDodge;
    case BrushBlendMode::ColorBurn: return QPainter::CompositionMode_ColorBurn;
    case BrushBlendMode::HardLight: return QPainter::CompositionMode_HardLight;
    case BrushBlendMode::SoftLight: return QPainter::CompositionMode_SoftLight;
    case BrushBlendMode::Difference: return QPainter::CompositionMode_Difference;
    case BrushBlendMode::Exclusion: return QPainter::CompositionMode_Exclusion;
    default: return QPainter::CompositionMode_SourceOver;
    }
}

// Fast bilinear sampler over a raw RGBA8888 buffer (byte order R,G,B,A).
// Fully out-of-range samples read as transparent. Used by the clone and healing
// hot loops (no per-pixel QColor/pixelColor overhead).
static inline void sampleBilinearRgbaRaw(const uchar* bits, int bytesPerLine,
                                         int w, int h, double x, double y,
                                         float out[4])
{
    if (x < 0.0 || y < 0.0 || x > w - 1 || y > h - 1) {
        out[0] = out[1] = out[2] = out[3] = 0.0f;
        return;
    }
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, w - 1);
    const int y1 = std::min(y0 + 1, h - 1);
    const float tx = static_cast<float>(x - x0);
    const float ty = static_cast<float>(y - y0);
    const uchar* row0 = bits + static_cast<size_t>(y0) * bytesPerLine;
    const uchar* row1 = bits + static_cast<size_t>(y1) * bytesPerLine;
    const uchar* p00 = row0 + x0 * 4;
    const uchar* p10 = row0 + x1 * 4;
    const uchar* p01 = row1 + x0 * 4;
    const uchar* p11 = row1 + x1 * 4;
    for (int c = 0; c < 4; ++c) {
        const float a = p00[c] + (p10[c] - p00[c]) * tx;
        const float b = p01[c] + (p11[c] - p01[c]) * tx;
        out[c] = a + (b - a) * ty;
    }
}

static QPointF layerPixelToDocumentPixel(const Layer* layer,
                                         const QSize& documentSize,
                                         float layerX, float layerY)
{
    if (!layer || documentSize.isEmpty())
        return {};

    const QSize baseSize = layer->rasterBaseSize();
    if (baseSize.isEmpty())
        return {};

    const float ndcX = -1.0f + 2.0f * layerX / static_cast<float>(baseSize.width());
    const float ndcY =  1.0f - 2.0f * layerY / static_cast<float>(baseSize.height());
    const QTransform layerToCanvas = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
    const QPointF canvas = layerToCanvas.map(QPointF(ndcX, ndcY));
    return QPointF((canvas.x() + 1.0) * 0.5 * documentSize.width(),
                   (1.0 - canvas.y()) * 0.5 * documentSize.height());
}

// Same mapping as layerPixelToDocumentPixel() / Document::selectionMaskForLayer(),
// but expressed as a single QTransform so it can be handed to BrushDab::rasterize
// for document-space selection sampling. accumulatedTransform() lives in NDC
// space; this folds in the layer-pixel↔NDC and NDC↔document-pixel conversions
// (using the layer's current rasterBaseSize). Passing the raw accumulatedTransform
// would wrongly treat that NDC transform as a layer-pixel→document-pixel map,
// which only coincides for an identity transform on a document-sized layer — that
// is why a scaled layer's selection ended up offset and the wrong size.
static QTransform layerToDocumentPixelTransform(const Layer* layer,
                                                const QSize& documentSize)
{
    if (!layer || !layer->owner || documentSize.isEmpty())
        return QTransform();

    const QSize baseSize = layer->rasterBaseSize();
    if (baseSize.isEmpty())
        return QTransform();

    const QTransform m = layer->owner->accumulatedTransform();
    const double bw = baseSize.width();
    const double bh = baseSize.height();
    const double dw = documentSize.width();
    const double dh = documentSize.height();

    const double a00 =  dw * m.m11() / bw;
    const double a01 = -dw * m.m21() / bh;
    const double a02 =  dw * 0.5 * (1.0 - m.m11() + m.m21() + m.m31());
    const double a10 = -dh * m.m12() / bw;
    const double a11 =  dh * m.m22() / bh;
    const double a12 =  dh * 0.5 * (1.0 + m.m12() - m.m22() - m.m32());

    return QTransform(a00, a10, a01, a11, a02, a12);
}

void BrushRenderer::beginStroke()
{
    m_strokeActive = true;
    m_washTiles.clear();
    // Re-roll the per-stroke texture offset. Large range; BrushDab folds it modulo
    // the pattern size. Only has an effect when the texture's random offset is on.
    m_texStrokeOffsetX = int(QRandomGenerator::global()->bounded(1 << 24));
    m_texStrokeOffsetY = int(QRandomGenerator::global()->bounded(1 << 24));
}

void BrushRenderer::endStroke()
{
    m_strokeActive = false;
    m_washTiles.clear();
}

void BrushRenderer::drawDabToRasterTiles(Layer* layer,
                                         const BrushInputState& state,
                                         const DynamicDabParams& params,
                                         const BrushSettings& settings,
                                         const QImage* selectMaskImage)
{
    if (!layer || !layer->renderRasterStorage().isEnabled())
        return;

    const float radius = std::max(0.5f, params.effectiveSize);
    const int pad = 2;
    const int left = static_cast<int>(std::floor(state.imagePos.x() - radius - pad));
    const int top = static_cast<int>(std::floor(state.imagePos.y() - radius - pad));
    const int side = static_cast<int>(std::ceil(radius * 2.0f + pad * 2.0f));
    if (side <= 0)
        return;

    const float cx = static_cast<float>(state.imagePos.x() - left);
    const float cy = static_cast<float>(state.imagePos.y() - top);
    // The selection mask is document-sized, so it doubles as the document size we
    // need to build the layer-pixel→document-pixel transform for sampling it.
    const QTransform layerToDoc = (selectMaskImage && !selectMaskImage->isNull())
        ? layerToDocumentPixelTransform(layer, selectMaskImage->size())
        : QTransform();

    // Scale the image tip to the dab size once and cache it; re-scaling a large
    // source tip on every dab is the dominant cost for big image brushes.
    if (settings.tipSource == BrushTipSource::Image && !settings.tipImage.isNull()) {
        const qint64 key = settings.tipImage.cacheKey();
        if (m_rasterTipSide != side || m_rasterTipKey != key) {
            m_rasterTipScaled = settings.tipImage.scaled(side, side,
                Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                .convertToFormat(QImage::Format_RGBA8888);
            m_rasterTipSide = side;
            m_rasterTipKey = key;
        }
    }

    // Wash mode (inking): dabs accumulate on a per-stroke buffer, and the buffer
    // is laid onto the layer capped at the preset's BASE opacity, so the stroke
    // never darkens past its opacity. BuildUp/Erase keep the direct path.
    const bool wash = m_strokeActive
        && settings.paintMode == BrushPaintMode::Wash
        && settings.mode == BrushMode::Paint;

    // Shared with the panel preview so the painted dab matches the preview dab
    // exactly (tip shape, angle, roundness, flip, texture, dual brush). For wash
    // the dab carries flow plus only the SENSOR part of the opacity (dynamic /
    // base); the base opacity is applied once when compositing the buffer, and
    // it must stay constant for the whole stroke — recompositing earlier dabs at
    // the current dab's dynamic opacity would fade (or wipe, at zero pressure)
    // everything already painted on the tile.
    DynamicDabParams dabParams = params;
    if (wash)
        dabParams.effectiveOpacity = settings.opacity > 1e-4f
            ? std::clamp(params.effectiveOpacity / settings.opacity, 0.0f, 1.0f)
            : 0.0f;
    const QImage dab = BrushDab::rasterize(dabParams, settings, dabParams.effectiveColor,
                                           left, top, cx, cy, side,
                                           settings.textureConfig.enabled ? m_texStampImage : QImage(),
                                           selectMaskImage, layerToDoc, m_rasterTipScaled,
                                           &m_dabScratch,
                                           m_texStrokeOffsetX, m_texStrokeOffsetY);

    const float washOpacity = std::clamp(settings.opacity, 0.0f, 1.0f);
    const QRect dabRect(left, top, side, side);
    auto& storage = layer->renderRasterStorage();
    const QPoint first = storage.tileCoordForPixel(dabRect.left(), dabRect.top());
    const QPoint last = storage.tileCoordForPixel(dabRect.right(), dabRect.bottom());

    for (int ty = first.y(); ty <= last.y(); ++ty) {
        for (int tx = first.x(); tx <= last.x(); ++tx) {
            const QPoint coord(tx, ty);
            const QRect tileRect = storage.tileBounds(coord);
            if (!tileRect.intersects(dabRect))
                continue;

            storage.recordBefore(coord);
            auto* tile = storage.ensureTile(coord);
            if (!tile)
                continue;

            if (wash) {
                const qint64 key = (static_cast<qint64>(tx) << 32)
                                 ^ static_cast<quint32>(ty);
                WashTile& wt = m_washTiles[key];
                if (wt.accum.isNull()) {
                    wt.base = tile->cpuImage.copy();   // pre-stroke pixels (first touch)
                    wt.accum = QImage(tileRect.size(), QImage::Format_ARGB32);
                    wt.accum.fill(Qt::transparent);
                }
                // Accumulate the flow-only dab into the stroke buffer.
                {
                    QPainter pa(&wt.accum);
                    pa.setCompositionMode(QPainter::CompositionMode_SourceOver);
                    pa.drawImage(dabRect.topLeft() - tileRect.topLeft(), dab);
                }
                // Rebuild the layer tile: base OVER (accum capped at opacity).
                tile->cpuImage = wt.base;   // COW copy; QPainter below detaches it
                QPainter pl(&tile->cpuImage);
                pl.setOpacity(washOpacity);
                pl.setCompositionMode(painterModeForBrushBlend(settings.blendMode));
                pl.drawImage(0, 0, wt.accum);
                pl.end();
            } else {
                QPainter painter(&tile->cpuImage);
                painter.setCompositionMode(settings.mode == BrushMode::Erase
                    ? QPainter::CompositionMode_DestinationOut
                    : painterModeForBrushBlend(settings.blendMode));
                painter.drawImage(dabRect.topLeft() - tileRect.topLeft(), dab);
                painter.end();
            }

            storage.markTileModified(coord);
        }
    }

    layer->pendingGpuUpload = true;
    if (layer->owner) {
        layer->owner->invalidateEffects();
        layer->owner->thumbnailDirty = true;
    }
}

// Converted once per snapshot (the stroke keeps one snapshot, so this hits the
// cache for every dab after the first).
const QImage& BrushRenderer::cloneSourceRgba(const QImage& src)
{
    if (src.cacheKey() != m_cloneSourceKey || m_cloneSourceRgba.isNull()) {
        m_cloneSourceRgba = (src.format() == QImage::Format_RGBA8888)
            ? src
            : src.convertToFormat(QImage::Format_RGBA8888);
        m_cloneSourceKey = src.cacheKey();
    }
    return m_cloneSourceRgba;
}

const QImage& BrushRenderer::cloneScaledTip(const QImage& tip, int side)
{
    const qint64 key = tip.cacheKey();
    if (m_cloneTipSide != side || m_cloneTipKey != key || m_cloneTipScaled.isNull()) {
        m_cloneTipScaled = tip.scaled(side, side,
            Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
            .convertToFormat(QImage::Format_RGBA8888);
        m_cloneTipSide = side;
        m_cloneTipKey = key;
    }
    return m_cloneTipScaled;
}

void BrushRenderer::drawCloneDabToRasterTiles(Layer* layer,
                                              const BrushInputState& state,
                                              const DynamicDabParams& params,
                                              const BrushSettings& settings,
                                              const CloneStampContext& cloneContext,
                                              const QImage* selectMaskImage)
{
    if (!layer || !layer->renderRasterStorage().isEnabled() || !cloneContext.isValid())
        return;

    const QImage& source = cloneSourceRgba(cloneContext.sourceImage);
    const float radius = std::max(0.5f, params.effectiveSize);
    const int pad = 2;
    const int left = static_cast<int>(std::floor(state.imagePos.x() - radius - pad));
    const int top = static_cast<int>(std::floor(state.imagePos.y() - radius - pad));
    const int side = static_cast<int>(std::ceil(radius * 2.0f + pad * 2.0f));
    if (side <= 0)
        return;

    QImage dab(side, side, QImage::Format_ARGB32);
    dab.fill(Qt::transparent);

    const float cx = static_cast<float>(state.imagePos.x() - left);
    const float cy = static_cast<float>(state.imagePos.y() - top);
    const float hardness = std::clamp(settings.hardness, 0.0f, 1.0f);
    const float aa = std::min(0.25f, 1.5f / radius);
    const float angle = params.effectiveAngle;
    const float ca = std::cos(-angle);
    const float sa = std::sin(-angle);
    const float roundness = std::max(0.05f, params.effectiveRoundness);
    const float baseAlpha = std::clamp(params.effectiveFlow * params.effectiveOpacity,
                                       0.0f, 1.0f);
    // Paper-anchored texture modulation, identical to the paint dab's, so a
    // textured preset clones through the same grain it paints with.
    const BrushDab::TextureSampler texture(settings.textureConfig,
                                           m_texStampImage,
                                           settings.mode == BrushMode::Erase,
                                           m_texStrokeOffsetX, m_texStrokeOffsetY);
    const bool useSelection = selectMaskImage
        && !selectMaskImage->isNull()
        && selectMaskImage->format() == QImage::Format_Grayscale8;

    // Hoist the layer-pixel → document-pixel mapping out of the per-pixel path:
    // it is affine, so evaluate it at three points and step incrementally instead
    // of recomputing the layer's accumulated transform (a tree walk) per pixel.
    // The selection mask is document-sized, so the same coordinates index it.
    const QPointF d00 = layerPixelToDocumentPixel(layer, cloneContext.documentSize,
                                                  left + 0.5f, top + 0.5f);
    const QPointF ddx = layerPixelToDocumentPixel(layer, cloneContext.documentSize,
                                                  left + 1.5f, top + 0.5f) - d00;
    const QPointF ddy = layerPixelToDocumentPixel(layer, cloneContext.documentSize,
                                                  left + 0.5f, top + 1.5f) - d00;
    const uchar* sbits = source.constBits();
    const int sbpl = source.bytesPerLine();
    const int sw = source.width();
    const int sh = source.height();
    const int selW = useSelection ? selectMaskImage->width() : 0;
    const int selH = useSelection ? selectMaskImage->height() : 0;
    const double offx = cloneContext.sourceOffset.x();
    const double offy = cloneContext.sourceOffset.y();

    auto writePixel = [&](QRgb* row, int x, int y, float maskAlpha) {
        maskAlpha = texture.apply(maskAlpha, left + x, top + y);
        maskAlpha = std::clamp(maskAlpha * baseAlpha, 0.0f, 1.0f);
        if (maskAlpha <= 0.0f)
            return;

        const double docx = d00.x() + ddx.x() * x + ddy.x() * y;
        const double docy = d00.y() + ddx.y() * x + ddy.y() * y;
        if (useSelection) {
            const int sx = static_cast<int>(std::floor(docx));
            const int sy = static_cast<int>(std::floor(docy));
            if (sx < 0 || sy < 0 || sx >= selW || sy >= selH)
                return;
            maskAlpha *= selectMaskImage->constScanLine(sy)[sx] / 255.0f;
            if (maskAlpha <= 0.0f)
                return;
        }
        float px[4];
        sampleBilinearRgbaRaw(sbits, sbpl, sw, sh, docx - offx, docy - offy, px);
        const int alpha = std::clamp(
            static_cast<int>(std::round(px[3] * maskAlpha)), 0, 255);
        if (alpha <= 0)
            return;
        row[x] = qRgba(static_cast<int>(std::lround(px[0])),
                       static_cast<int>(std::lround(px[1])),
                       static_cast<int>(std::lround(px[2])), alpha);
    };

    if (settings.tipSource == BrushTipSource::Image && !settings.tipImage.isNull()) {
        const QImage& scaledTip = cloneScaledTip(settings.tipImage, side);
        const float half = side * 0.5f;
        for (int y = 0; y < side; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
            for (int x = 0; x < side; ++x) {
                const float dx0 = static_cast<float>(x) + 0.5f - cx;
                const float dy0 = static_cast<float>(y) + 0.5f - cy;
                const float rx = dx0 * ca - dy0 * sa;
                const float ry = (dx0 * sa + dy0 * ca) / roundness;
                const int sx = static_cast<int>(rx + half);
                const int sy = static_cast<int>(ry + half);
                if (sx < 0 || sx >= side || sy < 0 || sy >= side)
                    continue;
                // scaledTip is Format_RGBA8888: bytes are R,G,B,A in memory.
                const uchar* tp = scaledTip.constScanLine(sy) + static_cast<qint64>(sx) * 4;
                writePixel(row, x, y, tp[3] / 255.0f);
            }
        }
    } else {
        for (int y = 0; y < side; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
            for (int x = 0; x < side; ++x) {
                const float dx0 = static_cast<float>(x) + 0.5f - cx;
                const float dy0 = static_cast<float>(y) + 0.5f - cy;
                const float rx = dx0 * ca - dy0 * sa;
                const float ry = (dx0 * sa + dy0 * ca) / roundness;
                const float d = settings.type == BrushType::Square
                    ? std::max(std::abs(rx), std::abs(ry)) / radius
                    : std::sqrt(rx * rx + ry * ry) / radius;
                writePixel(row, x, y, brushFalloff(d, hardness, aa));
            }
        }
    }

    const QRect dabRect(left, top, side, side);
    auto& storage = layer->renderRasterStorage();
    const QPoint first = storage.tileCoordForPixel(dabRect.left(), dabRect.top());
    const QPoint last = storage.tileCoordForPixel(dabRect.right(), dabRect.bottom());

    for (int ty = first.y(); ty <= last.y(); ++ty) {
        for (int tx = first.x(); tx <= last.x(); ++tx) {
            const QPoint coord(tx, ty);
            const QRect tileRect = storage.tileBounds(coord);
            if (!tileRect.intersects(dabRect))
                continue;

            storage.recordBefore(coord);
            auto* tile = storage.ensureTile(coord);
            if (!tile)
                continue;

            QPainter painter(&tile->cpuImage);
            painter.setCompositionMode(painterModeForBrushBlend(settings.blendMode));
            painter.drawImage(dabRect.topLeft() - tileRect.topLeft(), dab);
            painter.end();

            storage.markTileModified(coord);
        }
    }

    layer->pendingGpuUpload = true;
    if (layer->owner) {
        layer->owner->invalidateEffects();
        layer->owner->thumbnailDirty = true;
    }
}

// Healing Brush dab. Mirrors drawCloneDabToRasterTiles but replaces the direct
// source copy with frequency separation: high-frequency detail comes from the
// source patch, low-frequency tone from the destination patch (both taken from
// the same stroke-start snapshot, so the stroke never samples its own paint).
// healed = blur(destinationNeighborhood) + (sourcePixel - blur(sourceNeighborhood))
void BrushRenderer::drawHealingDabToRasterTiles(Layer* layer,
                                                const BrushInputState& state,
                                                const DynamicDabParams& params,
                                                const BrushSettings& settings,
                                                const CloneStampContext& cloneContext,
                                                const QImage* selectMaskImage)
{
    if (!layer || !layer->renderRasterStorage().isEnabled() || !cloneContext.isValid())
        return;

    const QImage& source = cloneSourceRgba(cloneContext.sourceImage);
    const float radius = std::max(0.5f, params.effectiveSize);
    const int pad = 2;
    const int left = static_cast<int>(std::floor(state.imagePos.x() - radius - pad));
    const int top = static_cast<int>(std::floor(state.imagePos.y() - radius - pad));
    const int side = static_cast<int>(std::ceil(radius * 2.0f + pad * 2.0f));
    if (side <= 0)
        return;

    // Blur radius (region of analysis) grows with the Diffusion control: a larger
    // radius lowers the frequency cutoff so more of the source's mid-tones get
    // replaced by destination tone — i.e. the healing blends harder into the
    // surroundings. Bounded for per-dab performance.
    const float diffusion = std::clamp(cloneContext.diffusion, 0.0f, 1.0f);
    const int br = std::clamp(static_cast<int>(std::lround(1.0f + diffusion * radius * 0.75f)),
                              1, 24);
    const int pside = side + 2 * br;
    const int pcount = pside * pside;

    // Build source-detail and destination-tone patches from the stroke snapshot.
    // The layer-pixel → document-pixel map is affine, so evaluate it at three
    // points and step incrementally per pixel instead of recomputing the layer's
    // accumulated transform (a tree walk) for every one of the pside² samples.
    // Pixels are read with a raw-pointer bilinear sampler rather than pixelColor.
    // Buffers are thread_local and reused across dabs (a stroke is many dabs) to
    // avoid per-dab heap churn; every element is overwritten before use.
    const size_t patchFloats = static_cast<size_t>(pcount) * 4;
    thread_local std::vector<float> srcPatch, tgtPatch, srcLow, tgtLow, blurTmp;
    srcPatch.resize(patchFloats);
    tgtPatch.resize(patchFloats);
    srcLow.resize(patchFloats);
    tgtLow.resize(patchFloats);
    blurTmp.resize(patchFloats);
    {
        const float lx0 = static_cast<float>(left - br) + 0.5f;
        const float ly0 = static_cast<float>(top - br) + 0.5f;
        const QPointF d00 = layerPixelToDocumentPixel(layer, cloneContext.documentSize, lx0, ly0);
        const QPointF ddx = layerPixelToDocumentPixel(layer, cloneContext.documentSize, lx0 + 1.0f, ly0) - d00;
        const QPointF ddy = layerPixelToDocumentPixel(layer, cloneContext.documentSize, lx0, ly0 + 1.0f) - d00;
        const double offx = cloneContext.sourceOffset.x();
        const double offy = cloneContext.sourceOffset.y();
        const uchar* sbits = source.constBits();
        const int sbpl = source.bytesPerLine();
        const int sw = source.width();
        const int sh = source.height();
        size_t i = 0;
        for (int py = 0; py < pside; ++py) {
            double dx = d00.x() + ddy.x() * py;
            double dy = d00.y() + ddy.y() * py;
            for (int px = 0; px < pside; ++px, i += 4) {
                sampleBilinearRgbaRaw(sbits, sbpl, sw, sh, dx, dy, &tgtPatch[i]);
                sampleBilinearRgbaRaw(sbits, sbpl, sw, sh, dx - offx, dy - offy, &srcPatch[i]);
                dx += ddx.x();
                dy += ddx.y();
            }
        }
    }

    // Separable box blur (running-sum) → low-frequency base for both patches.
    // Writes into the caller-provided out/tmp buffers (no per-call allocation).
    auto boxBlur = [pside](const std::vector<float>& in, std::vector<float>& out,
                           std::vector<float>& tmp, int rad) {
        const float norm = 1.0f / static_cast<float>(2 * rad + 1);
        // Horizontal.
        for (int y = 0; y < pside; ++y) {
            for (int c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (int x = -rad; x <= rad; ++x) {
                    const int cx = std::clamp(x, 0, pside - 1);
                    sum += in[(static_cast<size_t>(y) * pside + cx) * 4 + c];
                }
                for (int x = 0; x < pside; ++x) {
                    tmp[(static_cast<size_t>(y) * pside + x) * 4 + c] = sum * norm;
                    const int add = std::clamp(x + rad + 1, 0, pside - 1);
                    const int sub = std::clamp(x - rad, 0, pside - 1);
                    sum += in[(static_cast<size_t>(y) * pside + add) * 4 + c]
                         - in[(static_cast<size_t>(y) * pside + sub) * 4 + c];
                }
            }
        }
        // Vertical.
        for (int x = 0; x < pside; ++x) {
            for (int c = 0; c < 4; ++c) {
                float sum = 0.0f;
                for (int y = -rad; y <= rad; ++y) {
                    const int cy = std::clamp(y, 0, pside - 1);
                    sum += tmp[(static_cast<size_t>(cy) * pside + x) * 4 + c];
                }
                for (int y = 0; y < pside; ++y) {
                    out[(static_cast<size_t>(y) * pside + x) * 4 + c] = sum * norm;
                    const int add = std::clamp(y + rad + 1, 0, pside - 1);
                    const int sub = std::clamp(y - rad, 0, pside - 1);
                    sum += tmp[(static_cast<size_t>(add) * pside + x) * 4 + c]
                         - tmp[(static_cast<size_t>(sub) * pside + x) * 4 + c];
                }
            }
        }
    };
    boxBlur(srcPatch, srcLow, blurTmp, br);
    boxBlur(tgtPatch, tgtLow, blurTmp, br);

    QImage dab(side, side, QImage::Format_ARGB32);
    dab.fill(Qt::transparent);

    const float cx = static_cast<float>(state.imagePos.x() - left);
    const float cy = static_cast<float>(state.imagePos.y() - top);
    const float hardness = std::clamp(settings.hardness, 0.0f, 1.0f);
    const float aa = std::min(0.25f, 1.5f / radius);
    const float angle = params.effectiveAngle;
    const float ca = std::cos(-angle);
    const float sa = std::sin(-angle);
    const float roundness = std::max(0.05f, params.effectiveRoundness);
    const float baseAlpha = std::clamp(params.effectiveFlow * params.effectiveOpacity,
                                       0.0f, 1.0f);
    // Paper-anchored texture modulation, identical to the paint dab's, so a
    // textured preset heals through the same grain it paints with.
    const BrushDab::TextureSampler texture(settings.textureConfig,
                                           m_texStampImage,
                                           settings.mode == BrushMode::Erase,
                                           m_texStrokeOffsetX, m_texStrokeOffsetY);
    const bool useSelection = selectMaskImage
        && !selectMaskImage->isNull()
        && selectMaskImage->format() == QImage::Format_Grayscale8;

    // Hoist the layer-pixel → document-pixel mapping for the selection lookup
    // (affine; stepping it per pixel avoids the accumulated-transform tree walk).
    const QSize selSize = useSelection ? selectMaskImage->size() : QSize(1, 1);
    const QPointF s00 = useSelection
        ? layerPixelToDocumentPixel(layer, selSize, left + 0.5f, top + 0.5f) : QPointF();
    const QPointF sdx = useSelection
        ? layerPixelToDocumentPixel(layer, selSize, left + 1.5f, top + 0.5f) - s00 : QPointF();
    const QPointF sdy = useSelection
        ? layerPixelToDocumentPixel(layer, selSize, left + 0.5f, top + 1.5f) - s00 : QPointF();
    const int selW = useSelection ? selectMaskImage->width() : 0;
    const int selH = useSelection ? selectMaskImage->height() : 0;

    auto writePixel = [&](QRgb* row, int x, int y, float maskAlpha) {
        maskAlpha = texture.apply(maskAlpha, left + x, top + y);
        maskAlpha = std::clamp(maskAlpha * baseAlpha, 0.0f, 1.0f);
        if (maskAlpha <= 0.0f)
            return;
        if (useSelection) {
            const int sx = static_cast<int>(std::floor(s00.x() + sdx.x() * x + sdy.x() * y));
            const int sy = static_cast<int>(std::floor(s00.y() + sdx.y() * x + sdy.y() * y));
            if (sx < 0 || sy < 0 || sx >= selW || sy >= selH)
                return;
            maskAlpha *= selectMaskImage->constScanLine(sy)[sx] / 255.0f;
            if (maskAlpha <= 0.0f)
                return;
        }

        const size_t i = (static_cast<size_t>(y + br) * pside + (x + br)) * 4;
        // High-frequency detail from the source, weighted by source presence so
        // a transparent source contributes nothing (no edge artifacts).
        const float srcA = srcPatch[i + 3] / 255.0f;
        const float dstA = tgtPatch[i + 3];
        if (dstA <= 0.0f)
            return;
        float rgb[3];
        for (int c = 0; c < 3; ++c) {
            const float high = (srcPatch[i + c] - srcLow[i + c]) * srcA;
            rgb[c] = std::clamp(tgtLow[i + c] + high, 0.0f, 255.0f);
        }
        const int alpha = std::clamp(
            static_cast<int>(std::round(dstA * maskAlpha)), 0, 255);
        if (alpha <= 0)
            return;
        row[x] = qRgba(static_cast<int>(std::lround(rgb[0])),
                       static_cast<int>(std::lround(rgb[1])),
                       static_cast<int>(std::lround(rgb[2])), alpha);
    };

    if (settings.tipSource == BrushTipSource::Image && !settings.tipImage.isNull()) {
        const QImage& scaledTip = cloneScaledTip(settings.tipImage, side);
        const float half = side * 0.5f;
        for (int y = 0; y < side; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
            for (int x = 0; x < side; ++x) {
                const float dx0 = static_cast<float>(x) + 0.5f - cx;
                const float dy0 = static_cast<float>(y) + 0.5f - cy;
                const float rx = dx0 * ca - dy0 * sa;
                const float ry = (dx0 * sa + dy0 * ca) / roundness;
                const int sx = static_cast<int>(rx + half);
                const int sy = static_cast<int>(ry + half);
                if (sx < 0 || sx >= side || sy < 0 || sy >= side)
                    continue;
                // scaledTip is Format_RGBA8888: bytes are R,G,B,A in memory.
                const uchar* tp = scaledTip.constScanLine(sy) + static_cast<qint64>(sx) * 4;
                writePixel(row, x, y, tp[3] / 255.0f);
            }
        }
    } else {
        for (int y = 0; y < side; ++y) {
            QRgb* row = reinterpret_cast<QRgb*>(dab.scanLine(y));
            for (int x = 0; x < side; ++x) {
                const float dx0 = static_cast<float>(x) + 0.5f - cx;
                const float dy0 = static_cast<float>(y) + 0.5f - cy;
                const float rx = dx0 * ca - dy0 * sa;
                const float ry = (dx0 * sa + dy0 * ca) / roundness;
                const float d = settings.type == BrushType::Square
                    ? std::max(std::abs(rx), std::abs(ry)) / radius
                    : std::sqrt(rx * rx + ry * ry) / radius;
                writePixel(row, x, y, brushFalloff(d, hardness, aa));
            }
        }
    }

    const QRect dabRect(left, top, side, side);
    auto& storage = layer->renderRasterStorage();
    const QPoint first = storage.tileCoordForPixel(dabRect.left(), dabRect.top());
    const QPoint last = storage.tileCoordForPixel(dabRect.right(), dabRect.bottom());

    for (int ty = first.y(); ty <= last.y(); ++ty) {
        for (int tx = first.x(); tx <= last.x(); ++tx) {
            const QPoint coord(tx, ty);
            const QRect tileRect = storage.tileBounds(coord);
            if (!tileRect.intersects(dabRect))
                continue;

            storage.recordBefore(coord);
            auto* tile = storage.ensureTile(coord);
            if (!tile)
                continue;

            QPainter painter(&tile->cpuImage);
            painter.setCompositionMode(painterModeForBrushBlend(settings.blendMode));
            painter.drawImage(dabRect.topLeft() - tileRect.topLeft(), dab);
            painter.end();

            storage.markTileModified(coord);
        }
    }

    layer->pendingGpuUpload = true;
    if (layer->owner) {
        layer->owner->invalidateEffects();
        layer->owner->thumbnailDirty = true;
    }
}

void BrushRenderer::drawCloneDab(Layer* layer, const BrushInputState& state,
                                 const DynamicDabParams& params,
                                 const BrushSettings& settings,
                                 const CloneStampContext& cloneContext,
                                 const QImage* selectMaskImage)
{
    if (cloneContext.healing)
        drawHealingDabToRasterTiles(layer, state, params, settings, cloneContext, selectMaskImage);
    else
        drawCloneDabToRasterTiles(layer, state, params, settings, cloneContext, selectMaskImage);
}

void BrushRenderer::drawDab(Layer* layer, const BrushInputState& state,
                            const DynamicDabParams& params,
                            const BrushSettings& settings,
                            GLuint selectMaskTex,
                            int selectMaskW, int selectMaskH,
                            const QImage* selectMaskImage)
{
    if (layer && layer->renderRasterStorage().isEnabled()) {
        Q_UNUSED(selectMaskTex);
        Q_UNUSED(selectMaskW);
        Q_UNUSED(selectMaskH);
        drawDabToRasterTiles(layer, state, params, settings, selectMaskImage);
        return;
    }

    if (!layer || layer->fbo == 0 || !m_program || !m_program->isLinked())
        return;

    bool isErase = settings.mode == BrushMode::Erase;
    bool needDestRead = !isErase && settings.blendMode != BrushBlendMode::Normal;

    updateStamp(settings);

    if (m_stampDirty)
        uploadStampTexture();
    if (m_stampTexture == 0)
        return;

    if (m_texStampDirty && !m_texStampImage.isNull()) {
        if (m_texStampTexture == 0)
            glGenTextures(1, &m_texStampTexture);
        glBindTexture(GL_TEXTURE_2D, m_texStampTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        uploadR8Image(m_texStampImage);
        m_texStampDirty = false;
    }

    if (settings.dualBrushConfig.enabled) {
        const auto& dc = settings.dualBrushConfig;
        if (dc.size != m_dualLastConfig.size ||
            dc.hardness != m_dualLastConfig.hardness ||
            dc.tipType != m_dualLastConfig.tipType) {
            m_dualLastConfig = dc;
            generateDualStamp(dc);
        }
        if (m_dualStampDirty)
            uploadDualStampTexture();
    }

    auto* gl = QOpenGLContext::currentContext();
    if (!gl) return;
    auto* extra = gl->extraFunctions();
    if (!extra) return;

    QPointF imagePos = state.imagePos;

    GLint prevVp[4];
    glGetIntegerv(GL_VIEWPORT, prevVp);
    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);

    extra->glBindFramebuffer(GL_FRAMEBUFFER, layer->fbo);
    glViewport(0, 0, layer->imageWidth(), layer->imageHeight());

    m_program->bind();
    m_vao.bind();

    m_program->setUniformValue(m_uCenter,
        static_cast<float>(imagePos.x()),
        static_cast<float>(imagePos.y()));
    m_program->setUniformValue(m_uRadius, params.effectiveSize);
    m_program->setUniformValue(m_uTexSize,
        static_cast<float>(layer->imageWidth()),
        static_cast<float>(layer->imageHeight()));
    m_program->setUniformValue(m_uColor,
        params.effectiveColor.redF(),
        params.effectiveColor.greenF(),
        params.effectiveColor.blueF(),
        1.0f);
    m_program->setUniformValue(m_uFlow, params.effectiveFlow);
    m_program->setUniformValue(m_uOpacity, params.effectiveOpacity);
    m_program->setUniformValue(m_uAngle, params.effectiveAngle);
    m_program->setUniformValue(m_uRoundness, params.effectiveRoundness);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_stampTexture);
    m_program->setUniformValue(m_uStamp, 0);

    bool useTexture = settings.textureConfig.enabled && m_texStampTexture != 0;
    if (useTexture) {
        const TextureConfig& tcfg = settings.textureConfig;
        const bool texInvert = tcfg.invert
            ^ (tcfg.autoInvertForEraser && settings.mode == BrushMode::Erase);
        const bool cutPattern = tcfg.cutoffPolicy == TextureConfig::CutoffPolicy::Pattern
            || tcfg.cutoffPolicy == TextureConfig::CutoffPolicy::Both;
        m_program->setUniformValue(m_uTexStamp, 1);
        m_program->setUniformValue(m_uTexBrightness, tcfg.brightness);
        m_program->setUniformValue(m_uTexContrast, tcfg.contrast);
        m_program->setUniformValue(m_uTexDepth, tcfg.depth);
        m_program->setUniformValue(m_uTexInvert, texInvert ? 1 : 0);
        m_program->setUniformValue(m_uTexNeutral, tcfg.neutralPoint);
        m_program->setUniformValue(m_uTexCut, cutPattern ? 1 : 0);
        m_program->setUniformValue(m_uTexCutLo, tcfg.cutoffMin);
        m_program->setUniformValue(m_uTexCutHi, tcfg.cutoffMax);
        m_program->setUniformValue(m_uTexSubtract,
            tcfg.texturingMode == TextureConfig::TexturingMode::Subtract ? 1 : 0);
        m_program->setUniformValue(m_uTexSoft, tcfg.softTexturing ? 1 : 0);
        m_program->setUniformValue(m_uTexUVScale, 1.0f / tcfg.scale, 1.0f / tcfg.scale);
        const QPointF texOff = brushTexOffset(tcfg, m_texStrokeOffsetX,
                                              m_texStrokeOffsetY);
        m_program->setUniformValue(m_uTexUVOffset,
            static_cast<float>(texOff.x()), static_cast<float>(texOff.y()));
        m_program->setUniformValue(m_uTexSizePx,
            std::max(1.0f, static_cast<float>(m_texStampImage.width())),
            std::max(1.0f, static_cast<float>(m_texStampImage.height())));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texStampTexture);
    } else {
        m_program->setUniformValue(m_uTexDepth, 0.0f);
    }

    if (selectMaskTex != 0 && selectMaskW > 0 && selectMaskH > 0) {
        m_program->setUniformValue(m_uUseSelectMask, 1);
        m_program->setUniformValue(m_uSelectMask, 2);
        m_program->setUniformValue(m_uSelectMaskSize,
            static_cast<float>(selectMaskW),
            static_cast<float>(selectMaskH));

        // Compute layer→doc affine matrix from accumulated transform
        double docW = static_cast<double>(selectMaskW);
        double docH = static_cast<double>(selectMaskH);
        double lW = static_cast<double>(layer->imageWidth());
        double lH = static_cast<double>(layer->imageHeight());
        if (lW > 0.0 && lH > 0.0) {
            QTransform accumT = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
            float a00 = static_cast<float>(docW * accumT.m11() / lW);
            float a01 = static_cast<float>(-docW * accumT.m21() / lH);
            float a02 = static_cast<float>(docW * 0.5 * (1.0 - accumT.m11() + accumT.m21() + accumT.m31()));
            float a10 = static_cast<float>(-docH * accumT.m12() / lW);
            float a11 = static_cast<float>(docH * accumT.m22() / lH);
            float a12 = static_cast<float>(docH * 0.5 * (1.0 + accumT.m12() - accumT.m22() - accumT.m32()));
            m_program->setUniformValue(m_uLayerToDocX, a00, a01, a02);
            m_program->setUniformValue(m_uLayerToDocY, a10, a11, a12);
        } else {
            m_program->setUniformValue(m_uLayerToDocX, 1.0f, 0.0f, 0.0f);
            m_program->setUniformValue(m_uLayerToDocY, 0.0f, 1.0f, 0.0f);
        }

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, selectMaskTex);
    } else {
        m_program->setUniformValue(m_uUseSelectMask, 0);
    }

    if (needDestRead) {
        int w = layer->imageWidth();
        int h = layer->imageHeight();

        glActiveTexture(GL_TEXTURE3);
        if (w != m_copyTexW || h != m_copyTexH) {
            glBindTexture(GL_TEXTURE_2D, m_destCopyTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            m_copyTexW = w;
            m_copyTexH = h;
        }
        glBindTexture(GL_TEXTURE_2D, m_destCopyTexture);
        {
            const int pad = 2;
            const int r   = static_cast<int>(params.effectiveSize) + pad;
            const int cx  = static_cast<int>(imagePos.x());
            const int cy  = static_cast<int>(imagePos.y());
            const int x0  = std::max(0, cx - r);
            const int y0  = std::max(0, cy - r);
            const int x1  = std::min(w, cx + r + 1);
            const int y1  = std::min(h, cy + r + 1);
            const int cw  = x1 - x0;
            const int ch  = y1 - y0;
            if (cw > 0 && ch > 0)
                glCopyTexSubImage2D(GL_TEXTURE_2D, 0, x0, y0, x0, y0, cw, ch);
        }

        m_program->setUniformValue(m_uDestTexture, 3);
        m_program->setUniformValue(m_uDestTexSize,
            static_cast<float>(w), static_cast<float>(h));

        m_program->setUniformValue(m_uBlendMode,
            static_cast<int>(settings.blendMode));
        glDisable(GL_BLEND);
    } else {
        m_program->setUniformValue(m_uBlendMode, 0);
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        if (isErase) {
            glBlendFuncSeparate(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
        } else {
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA,
                                GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        }
    }

    // Primary dab
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    // Dual brush
    if (settings.dualBrushConfig.enabled && m_dualStampTexture != 0) {
        float dualRadius = settings.dualBrushConfig.size;
        float sizeRatio = settings.size > 0.0f ? dualRadius / settings.size : 1.0f;
        float dualEffectiveSize = params.effectiveSize * sizeRatio;

        m_program->setUniformValue(m_uRadius, dualEffectiveSize);
        m_program->setUniformValue(m_uTexDepth, 0.0f);
        m_program->setUniformValue(m_uBlendMode,
            static_cast<int>(BrushBlendMode::Normal));

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_dualStampTexture);

        glEnable(GL_BLEND);
        glBlendFunc(GL_DST_COLOR, GL_ZERO);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    }

    glDisable(GL_BLEND);

    m_vao.release();
    m_program->release();
    extra->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    extra->glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);

    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}

void BrushRenderer::drawMaskDab(Layer* layer, const BrushInputState& state,
                                const DynamicDabParams& params,
                                const BrushSettings& settings,
                                GLuint selectMaskTex,
                                int selectMaskW, int selectMaskH)
{
    if (!layer || layer->maskFbo == 0 || !m_maskProgram || !m_maskProgram->isLinked())
        return;

    updateStamp(settings);

    if (m_stampDirty)
        uploadStampTexture();
    if (m_stampTexture == 0)
        return;

    if (m_texStampDirty && !m_texStampImage.isNull()) {
        if (m_texStampTexture == 0)
            glGenTextures(1, &m_texStampTexture);
        glBindTexture(GL_TEXTURE_2D, m_texStampTexture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        uploadR8Image(m_texStampImage);
        m_texStampDirty = false;
    }

    auto* gl = QOpenGLContext::currentContext();
    if (!gl) return;
    auto* extra = gl->extraFunctions();
    if (!extra) return;

    GLint prevVp[4];
    glGetIntegerv(GL_VIEWPORT, prevVp);
    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
    glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
    GLboolean blendWasEnabled = glIsEnabled(GL_BLEND);

    int mw = layer->maskImage.width();
    int mh = layer->maskImage.height();
    if (mw <= 0 || mh <= 0)
        return;

    const float validLeft = static_cast<float>(layer->maskOrigin.x());
    const float validTop = static_cast<float>(layer->maskOrigin.y());
    const float validRight = validLeft + static_cast<float>(mw);
    const float validBottom = validTop + static_cast<float>(mh);
    if (state.imagePos.x() < validLeft ||
        state.imagePos.y() < validTop ||
        state.imagePos.x() >= validRight ||
        state.imagePos.y() >= validBottom) {
        return;
    }

    // Translate from logical layer coords to mask pixel coords using maskOrigin.
    const float centerX = static_cast<float>(state.imagePos.x() - layer->maskOrigin.x());
    const float centerY = static_cast<float>(state.imagePos.y() - layer->maskOrigin.y());

    // [MASK-AUDIT] viewport + uTexSize below use the CPU maskImage dims. If the
    // GPU texture actually attached to maskFbo has a different size, the dab is
    // rasterized into a wrong-size target → stretched/shifted, and the readback
    // (glGetTexImage) later misaligns. Query the real attachment size to confirm.
    {
        GLint mGpuW = 0, mGpuH = 0;
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, layer->maskTextureId);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH,  &mGpuW);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &mGpuH);
    }

    extra->glBindFramebuffer(GL_FRAMEBUFFER, layer->maskFbo);
    glViewport(0, 0, mw, mh);
    glDisable(GL_BLEND);

    m_maskProgram->bind();
    m_maskVao.bind();

    m_maskProgram->setUniformValue(m_muCenter, centerX, centerY);
    m_maskProgram->setUniformValue(m_muRadius, params.effectiveSize);
    m_maskProgram->setUniformValue(m_muTexSize,
        static_cast<float>(mw), static_cast<float>(mh));
    m_maskProgram->setUniformValue(m_muFlow, params.effectiveFlow);
    m_maskProgram->setUniformValue(m_muOpacity, params.effectiveOpacity);
    m_maskProgram->setUniformValue(m_muAngle, params.effectiveAngle);
    m_maskProgram->setUniformValue(m_muRoundness, params.effectiveRoundness);

    bool erasing = settings.mode == BrushMode::Erase;
    float luminance = 0.299f * params.effectiveColor.redF()
                    + 0.587f * params.effectiveColor.greenF()
                    + 0.114f * params.effectiveColor.blueF();
    float maskTarget = erasing ? (1.0f - luminance) : luminance;
    m_maskProgram->setUniformValue(m_muMaskTarget, maskTarget);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_stampTexture);
    m_maskProgram->setUniformValue(m_muStamp, 0);

    bool useTexture = settings.textureConfig.enabled && m_texStampTexture != 0;
    if (useTexture) {
        const TextureConfig& tcfg = settings.textureConfig;
        const bool texInvert = tcfg.invert
            ^ (tcfg.autoInvertForEraser && settings.mode == BrushMode::Erase);
        const bool cutPattern = tcfg.cutoffPolicy == TextureConfig::CutoffPolicy::Pattern
            || tcfg.cutoffPolicy == TextureConfig::CutoffPolicy::Both;
        m_maskProgram->setUniformValue(m_muTexStamp, 1);
        m_maskProgram->setUniformValue(m_muTexBrightness, tcfg.brightness);
        m_maskProgram->setUniformValue(m_muTexContrast, tcfg.contrast);
        m_maskProgram->setUniformValue(m_muTexDepth, tcfg.depth);
        m_maskProgram->setUniformValue(m_muTexInvert, texInvert ? 1 : 0);
        m_maskProgram->setUniformValue(m_muTexNeutral, tcfg.neutralPoint);
        m_maskProgram->setUniformValue(m_muTexCut, cutPattern ? 1 : 0);
        m_maskProgram->setUniformValue(m_muTexCutLo, tcfg.cutoffMin);
        m_maskProgram->setUniformValue(m_muTexCutHi, tcfg.cutoffMax);
        m_maskProgram->setUniformValue(m_muTexSubtract,
            tcfg.texturingMode == TextureConfig::TexturingMode::Subtract ? 1 : 0);
        m_maskProgram->setUniformValue(m_muTexSoft, tcfg.softTexturing ? 1 : 0);
        m_maskProgram->setUniformValue(m_muTexUVScale, 1.0f / tcfg.scale, 1.0f / tcfg.scale);
        const QPointF texOff = brushTexOffset(tcfg, m_texStrokeOffsetX,
                                              m_texStrokeOffsetY);
        m_maskProgram->setUniformValue(m_muTexUVOffset,
            static_cast<float>(texOff.x()), static_cast<float>(texOff.y()));
        m_maskProgram->setUniformValue(m_muTexSizePx,
            std::max(1.0f, static_cast<float>(m_texStampImage.width())),
            std::max(1.0f, static_cast<float>(m_texStampImage.height())));
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, m_texStampTexture);
    } else {
        m_maskProgram->setUniformValue(m_muTexDepth, 0.0f);
    }

    if (selectMaskTex != 0 && selectMaskW > 0 && selectMaskH > 0) {
        m_maskProgram->setUniformValue(m_muUseSelectMask, 1);
        m_maskProgram->setUniformValue(m_muSelectMask, 2);
        m_maskProgram->setUniformValue(m_muSelectMaskSize,
            static_cast<float>(selectMaskW),
            static_cast<float>(selectMaskH));

        // Canvas-sized masks are already in canvas pixel space — identity mapping.
        // Layer-sized masks (legacy) require the layer-to-doc transform.
        const bool isCanvasSized = (mw == selectMaskW && mh == selectMaskH);
        if (isCanvasSized) {
            m_maskProgram->setUniformValue(m_muLayerToDocX, 1.0f, 0.0f, 0.0f);
            m_maskProgram->setUniformValue(m_muLayerToDocY, 0.0f, 1.0f, 0.0f);
        } else {
            double docW = static_cast<double>(selectMaskW);
            double docH = static_cast<double>(selectMaskH);
            double lW = static_cast<double>(mw);
            double lH = static_cast<double>(mh);
            if (lW > 0.0 && lH > 0.0) {
                QTransform accumT = layer->owner ? layer->owner->accumulatedTransform() : QTransform();
                float a00 = static_cast<float>(docW * accumT.m11() / lW);
                float a01 = static_cast<float>(-docW * accumT.m21() / lH);
                float a02 = static_cast<float>(docW * 0.5 * (1.0 - accumT.m11() + accumT.m21() + accumT.m31()));
                float a10 = static_cast<float>(-docH * accumT.m12() / lW);
                float a11 = static_cast<float>(docH * accumT.m22() / lH);
                float a12 = static_cast<float>(docH * 0.5 * (1.0 + accumT.m12() - accumT.m22() - accumT.m32()));
                m_maskProgram->setUniformValue(m_muLayerToDocX, a00, a01, a02);
                m_maskProgram->setUniformValue(m_muLayerToDocY, a10, a11, a12);
            } else {
                m_maskProgram->setUniformValue(m_muLayerToDocX, 1.0f, 0.0f, 0.0f);
                m_maskProgram->setUniformValue(m_muLayerToDocY, 0.0f, 1.0f, 0.0f);
            }
        }

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, selectMaskTex);
    } else {
        m_maskProgram->setUniformValue(m_muUseSelectMask, 0);
    }

    {
        glActiveTexture(GL_TEXTURE3);
        if (mw != m_maskCopyTexW || mh != m_maskCopyTexH) {
            glBindTexture(GL_TEXTURE_2D, m_maskCopyTexture);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, mw, mh, 0,
                         GL_RED, GL_UNSIGNED_BYTE, nullptr);
            m_maskCopyTexW = mw;
            m_maskCopyTexH = mh;
        }
        glBindTexture(GL_TEXTURE_2D, m_maskCopyTexture);
        glCopyTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 0, 0, mw, mh);

        m_maskProgram->setUniformValue(m_muDestMask, 3);
        m_maskProgram->setUniformValue(m_muDestTexSize,
            static_cast<float>(mw), static_cast<float>(mh));
    }

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    m_maskVao.release();
    m_maskProgram->release();
    extra->glBindFramebuffer(GL_DRAW_FRAMEBUFFER, prevDrawFbo);
    extra->glBindFramebuffer(GL_READ_FRAMEBUFFER, prevReadFbo);
    if (blendWasEnabled)
        glEnable(GL_BLEND);

    glViewport(prevVp[0], prevVp[1], prevVp[2], prevVp[3]);
}
