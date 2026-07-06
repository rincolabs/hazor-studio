#pragma once

#include <cstdint>
#include <array>
#include <memory>
#include <functional>

namespace render {

// ── Handle types (opaque, backend-agnostic) ──────────────────
// 0 = invalid. Backend implementations map these to GPU resources.

using TextureId      = uint64_t;
using FramebufferId  = uint64_t;
using VertexBufferId = uint64_t;

constexpr TextureId      InvalidTexture      = 0;
constexpr FramebufferId  InvalidFramebuffer  = 0;
constexpr VertexBufferId InvalidBuffer       = 0;

// ── Enums ─────────────────────────────────────────────────────

enum class TextureFormat {
    RGBA8,  // 4 × uint8, GL_RGBA8
    R8      // 1 × uint8, GL_R8
};

enum class TextureFilter {
    Linear,
    Nearest
};

enum class TextureWrap {
    ClampToEdge,
    Repeat
};

// All blend modes the backend must support across all draw paths.
enum class BlendOp {
    Normal, Multiply, Screen, Overlay, Darken, Lighten,
    ColorDodge, ColorBurn, HardLight, SoftLight, Difference,
    Exclusion, Hue, Saturation, Color, Luminosity
};

// ── Matrix helper ─────────────────────────────────────────────
// Column-major float[16] layout (same as QMatrix4x4 / OpenGL).

using Mat4 = std::array<float, 16>;

// ── Draw parameter structs ────────────────────────────────────
// Each maps 1:1 to a shader program the backend owns internally.
// Default field values avoid boilerplate at call sites.

struct SolidQuadParams {
    Mat4   projectionMatrix  = {};
    float  colorR            = 1.0f;
    float  colorG            = 1.0f;
    float  colorB            = 1.0f;
    float  colorA            = 1.0f;
};

struct TexturedQuadParams {
    Mat4       projectionMatrix = {};
    TextureId  texture          = InvalidTexture;
    TextureId  maskTexture      = InvalidTexture;   // optional
    float      opacity          = 1.0f;
    float      maskDensity      = 1.0f;
    bool       hasMask          = false;
};

struct BlendCompositeParams {
    Mat4       projectionMatrix = {};
    TextureId  dstTexture       = InvalidTexture;
    TextureId  srcTexture       = InvalidTexture;
    TextureId  maskTexture      = InvalidTexture;   // optional
    BlendOp    blendMode        = BlendOp::Normal;
    float      opacity          = 1.0f;
    float      maskDensity      = 1.0f;
    bool       hasMask          = false;
    int        viewportWidth    = 0;
    int        viewportHeight   = 0;
};

struct MarchingAntsParams {
    Mat4       projectionMatrix = {};
    TextureId  maskTexture      = InvalidTexture;
    float      time             = 0.0f;     // animation time (seconds)
    float      opacity          = 1.0f;     // < 0 triggers quick-mask mode
};

struct RubylithParams {
    Mat4       projectionMatrix = {};
    TextureId  maskTexture      = InvalidTexture;
    float      opacity          = 1.0f;
};

struct GrayscaleParams {
    Mat4       projectionMatrix = {};
    TextureId  texture          = InvalidTexture;
};

struct WireRectParams {
    Mat4   projectionMatrix = {};
    float  colorR           = 1.0f;
    float  colorG           = 1.0f;
    float  colorB           = 1.0f;
    float  colorA           = 1.0f;
};

struct BrushDabParams {
    Mat4       projectionMatrix = {};
    float      centerX, centerY;
    float      radius;
    float      texSizeW, texSizeH;
    float      angle, roundness;
    float      colorR, colorG, colorB, colorA;
    float      flow, opacity;
    TextureId  stamp;
    TextureId  selectMask          = InvalidTexture;
    float      selectMaskSizeW     = 0;
    float      selectMaskSizeH     = 0;
    float      layerToDocX         = 0;
    float      layerToDocY         = 0;
    TextureId  texStamp            = InvalidTexture;
    float      texBrightness       = 0;
    float      texContrast         = 0;
    float      texDepth            = 0;
    bool       texInvert           = false;
    float      texUVScaleX         = 1.0f;
    float      texUVScaleY         = 1.0f;
    float      texUVOffsetX        = 0;
    float      texUVOffsetY        = 0;
    BlendOp    blendMode           = BlendOp::Normal;
    TextureId  destCopy            = InvalidTexture;
    int        destTexSizeW        = 0;
    int        destTexSizeH        = 0;
    bool       useSelectMask       = false;
};

struct MaskDabParams {
    Mat4       projectionMatrix = {};
    float      centerX, centerY;
    float      radius;
    float      texSizeW, texSizeH;
    float      angle, roundness;
    float      flow, opacity;
    float      maskTarget;             // 0 = hide, 1 = reveal
    TextureId  stamp;
    TextureId  selectMask          = InvalidTexture;
    float      selectMaskSizeW     = 0;
    float      selectMaskSizeH     = 0;
    float      layerToDocX         = 0;
    float      layerToDocY         = 0;
    TextureId  texStamp            = InvalidTexture;
    float      texBrightness       = 0;
    float      texContrast         = 0;
    float      texDepth            = 0;
    bool       texInvert           = false;
    float      texUVScaleX         = 1.0f;
    float      texUVScaleY         = 1.0f;
    float      texUVOffsetX        = 0;
    float      texUVOffsetY        = 0;
    TextureId  destCopy            = InvalidTexture;
    int        destTexSizeW        = 0;
    int        destTexSizeH        = 0;
    bool       useSelectMask       = false;
};

// ── Abstract backend interface ────────────────────────────────
// All mid-level draw operations. The backend owns all shader
// programs, VAOs, and state internally.

class RenderBackend {
public:
    virtual ~RenderBackend() = default;

    // ── Lifecycle ────────────────────────────────────────────
    // initialize: create shaders, default VAOs, check caps.
    // Returns false on failure.
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;

    // beginFrame: clear the active framebuffer.
    // endFrame: flush / present.
    virtual void beginFrame(float clearR, float clearG,
                            float clearB, float clearA) = 0;
    virtual void endFrame() = 0;

    // ── Texture management ───────────────────────────────────
    // createTexture: allocates GPU texture, optionally uploads
    //   initial data. data may be nullptr (uninitialized).
    virtual TextureId createTexture(int w, int h, TextureFormat format,
                                     TextureFilter minFilter,
                                     TextureFilter magFilter,
                                     TextureWrap wrap,
                                     const void* data = nullptr) = 0;
    // updateSubTexture: partial update (glTexSubImage2D).
    virtual void updateSubTexture(TextureId tex, int x, int y,
                                   int w, int h, const void* data) = 0;
    // readTexture: GPU → CPU readback (glGetTexImage).
    virtual void readTexture(TextureId tex, void* outData,
                              int w, int h, TextureFormat format) = 0;
    virtual void destroyTexture(TextureId tex) = 0;

    // ── Framebuffer management ───────────────────────────────
    virtual FramebufferId createFramebuffer(TextureId colorAttachment) = 0;
    virtual void destroyFramebuffer(FramebufferId fb) = 0;
    virtual void bindFramebuffer(FramebufferId fb) = 0;
    virtual void unbindFramebuffer() = 0;

    // ── Shared quad geometry ─────────────────────────────────
    // Creates a VAO + VBO holding a quad with pos(2) + uv(2) per vertex.
    virtual VertexBufferId createQuadBuffer() = 0;
    virtual void destroyBuffer(VertexBufferId buf) = 0;

    // ── Draw operations ──────────────────────────────────────
    // Each binds its own internal shader program, sets uniforms,
    // binds the quad buffer, and issues a draw call.

    virtual void drawSolidQuad(const SolidQuadParams& p) = 0;
    virtual void drawTexturedQuad(const TexturedQuadParams& p) = 0;
    virtual void drawBlendComposite(const BlendCompositeParams& p) = 0;
    virtual void drawMarchingAnts(const MarchingAntsParams& p) = 0;
    virtual void drawRubylith(const RubylithParams& p) = 0;
    virtual void drawGrayscaleMask(const GrayscaleParams& p) = 0;
    virtual void drawWireRect(const WireRectParams& p) = 0;

    // Brush dab draws (complex, owned by BrushRenderer
    // and moved here for future QRhi compatibility).
    virtual void drawBrushDab(const BrushDabParams& p) = 0;
    virtual void drawMaskDab(const MaskDabParams& p) = 0;

    // ── Render state ─────────────────────────────────────────
    virtual void setBlendOp(BlendOp op) = 0;
    virtual void disableBlending() = 0;
    virtual void setScissor(int x, int y, int w, int h) = 0;
    virtual void disableScissor() = 0;
    virtual void setViewport(int x, int y, int w, int h) = 0;
    virtual void getViewport(int& x, int& y,
                              int& w, int& h) const = 0;

    // ── Queries ──────────────────────────────────────────────
    virtual bool hasFramebufferFetch() const = 0;
    virtual int maxTextureSize() const = 0;

    // ── Factory ──────────────────────────────────────────────
    static std::unique_ptr<RenderBackend> createOpenGL();
};

// ── Convenience helpers (QMatrix4x4 → Mat4) ──────────────────
// toMat4(const float*) works with QMatrix4x4::constData():
//   render::toMat4(mvp.constData())

inline Mat4 toMat4(const float* src)
{
    Mat4 out;
    for (int i = 0; i < 16; ++i)
        out[i] = src[i];
    return out;
}

} // namespace render
