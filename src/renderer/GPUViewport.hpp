#pragma once

#include "RenderParams.hpp"
#include "TileRenderer.hpp"
#include "MipmapCache.hpp"
#include "BlendRules.hpp"
#include "ProjectionCache.hpp"
#include "StyleBakePool.hpp"
#include "cache/RenderCache.hpp"

#include <QOpenGLFunctions>
#include <QOpenGLBuffer>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLShaderProgram>
#include <QMatrix4x4>
#include <QVector2D>
#include <QTransform>
#include <QColor>
#include <vector>
#include <memory>
#include <functional>
#include <optional>

class Document;
class Layer;
class LayerTreeNode;

class GPUViewport : protected QOpenGLFunctions {
public:
    GPUViewport();
    ~GPUViewport();

    // ── Lifecycle ───────────────────────────────────────────
    // Called from CanvasView::initializeGL
    void initialize();

    // ── Per-frame render ────────────────────────────────────
    void render(const RenderParams& p);

    // ── CPU-projection display (Fase B) ─────────────────────
    // Kill-switch: when disabled, every frame goes through the per-layer GPU
    // compositor (the pre-refactor behaviour).
    void setProjectionDisplayEnabled(bool e) { m_useProjectionDisplay = e; }
    bool projectionDisplayEnabled() const { return m_useProjectionDisplay; }
    void invalidateProjection() { m_projection.invalidate(); }
    // Last CPU composite, before the channel view. Null until one has
    // been built.
    const QImage& projectionBaseImage() const { return m_projection.baseImage(); }

    // Host repaint hook for the async projection (Fase C). When an off-thread
    // composite finishes, the render cache is invalidated (so the next frame
    // redraws through the freshly-built projection, the authoritative result)
    // and `cb` is invoked to request a repaint.
    void setProjectionRepaintCallback(std::function<void()> cb)
    {
        m_projectionRepaint = std::move(cb);
        m_projection.setReadyCallback([this]() {
            m_renderCache.markInvalid();
            if (m_projectionRepaint) m_projectionRepaint();
        });
    }

    // Host repaint hook for the async layer-style bake (Fase D). When an
    // off-thread style bake finishes, the render cache is invalidated and
    // `cb` is invoked to request a repaint — the next frame's needsLayerSync
    // poll uploads the freshly baked effect/styled-base texture.
    void setStyleBakeRepaintCallback(std::function<void()> cb)
    {
        m_styleBakeRepaint = std::move(cb);
        m_styleBakes.setReadyCallback([this]() {
            m_renderCache.markInvalid();
            if (m_styleBakeRepaint) m_styleBakeRepaint();
        });
    }

    // Host hook for "a layer-style bake is running". Drives the app's standard
    // delayed loading dialog for a bake the user is waiting on — the policy
    // (which bakes those are) lives in ImageController::setStyleBakeBusy, not
    // here. See StyleBakePool::setBusyCallback.
    void setStyleBakeBusyCallback(std::function<void(bool)> cb)
    {
        m_styleBakes.setBusyCallback(std::move(cb));
    }

    // ── GPU sync (called from outside render) ───────────────
    void syncLayersToGpu(Document* doc, Layer* editingMaskLayer = nullptr);
    void syncLayerFromGpu(Layer* layer);
    void syncLayerMaskFromGpu(Layer* layer);
    void uploadLayerTextureCpu(Layer* layer);
    void setupLayerFBO(Layer* layer);
    void setupMaskFBO(Layer* layer);
    void uploadMaskTexture(Layer* layer);
    void cleanupDocumentLayers(Document* doc);

    // ── Selection mask ──────────────────────────────────────
    // Null region = full re-upload. A valid region accumulates into a
    // subrect uploaded with glTexSubImage2D (quick-select dabs would
    // otherwise re-send the whole document mask on every mouse move).
    void markSelectNeedsUpload(const QRect& region = QRect())
    {
        if (!m_selectNeedsUpload)
            m_selectUploadRect = region;
        else if (!m_selectUploadRect.isNull() && !region.isNull())
            m_selectUploadRect = m_selectUploadRect.united(region);
        else
            m_selectUploadRect = QRect();  // any full request wins
        m_selectNeedsUpload = true;
    }
    unsigned int selectMaskTexture() const { return m_selectMaskTexture; }

    // ── Tiling (delegated to internal TileRenderer) ─────────
    void uploadDirtyTiles(Layer* layer);
    void uploadDirtyRasterTiles(Layer* layer);
    std::vector<TileDraw> computeTilesForLayer(
        Layer* layer, int docW, int docH,
        const QRectF& viewportPixelDocRect,
        const QMatrix4x4& fullMvp,
        core::RenderScheduler::LOD lod = core::RenderScheduler::LOD::Full);
    std::vector<TileDraw> computeRasterTilesForLayer(
        Layer* layer, int docW, int docH,
        const QRectF& viewportPixelDocRect,
        const QMatrix4x4& layerMvp);

    // ── Instanced rendering (Phase 10.2) ────────────────────
    void uploadInstanceData(const std::vector<TileDraw>& tiles);
    void drawInstanced(int tileCount);

    // ── Mipmap / LOD ──────────────────────────────────────
    // Returns the correct GL texture ID for the given LOD.
    // For Full LOD, returns layer->textureId directly.
    // For lower LODs, generates/uploads via MipmapCache.
    unsigned int lodTextureId(Layer* layer, core::RenderScheduler::LOD lod);
    void invalidateMipmaps(Layer* layer) { m_mipmapCache.invalidate(layer); }
    void uploadDirtyMipmaps(Layer* layer) { m_mipmapCache.uploadDirtyLevels(layer); }

    // ── Shader access for LayerCompositor ───────────────────
    QOpenGLShaderProgram* mainProgram()   const { return m_mainProg; }
    QOpenGLShaderProgram* solidProgram()  const { return m_solidProg; }
    QOpenGLShaderProgram* blendProgram()  const { return m_blendProg; }

    void bindMainVao();
    void unbindMainVao();

    // Uniform setters used by LayerCompositor.
    //
    // `fillOpacity` is the layer's Fill (LayerTreeNode::compositeFillOpacity).
    // It rides in the opacity uniform on purpose: the main shader only ever
    // draws the Normal blend, where Fill and Opacity are the same factor on
    // the source coverage. The eight modes where they are not go through the
    // blend shader, which carries Fill as its own uniform — and keeping it out
    // of the main shader means no direct-uniform draw site (the projection,
    // the canvas decorations, the overlays) can be left with an unset Fill.
    void setMainUniforms(const QMatrix4x4& mvp, float opacity, bool hasMask,
                         float maskDensity, float fillOpacity = 1.0f);
    void setMainTexture(GLenum unit, GLuint tex);
    void setUVUniforms(const QVector2D& scale, const QVector2D& offset);
    void setMaskUVUniforms(const QVector2D& scale, const QVector2D& offset);

    // Overlays used from LayerCompositor
    void renderRubylithOverlay(GLuint maskTex, int maskUnit,
                                const QMatrix4x4& mvp,
                                float opacity = 0.5f,
                                QVector2D maskUvOffset = QVector2D(0.0f, 0.0f),
                                QVector2D maskUvScale  = QVector2D(1.0f, 1.0f));
    void renderGrayscaleMaskView(const RenderParams& p,
                                  const QMatrix4x4& vm);

    // Blend helpers used by LayerCompositor
    static bool needsShaderBlend(int blendMode);
    void applyFixedBlend(int blendMode);
    void drawShaderBlend(GLuint srcTexture,
                         GLuint maskTexture,
                         const QMatrix4x4& mvp,
                         int blendMode,
                         float opacity,
                         float fillOpacity,
                         bool hasMask,
                         float maskDensity,
                         int viewportW,
                         int viewportH,
                         const QVector2D& maskUvScale = QVector2D(1.0f, 1.0f),
                         const QVector2D& maskUvOffset = QVector2D(0.0f, 0.0f));

    // Group FBO.
    //
    // seedFromTarget starts the FBO as a 1:1 copy of the current render target
    // instead of transparent — the Pass Through group model, where the children
    // must keep blending against the backdrop outside the group. It only has
    // an effect while another group FBO of the same size is already bound (the
    // live stack always isolates into one, see LayerCompositor::composite); a
    // target of a different size cannot be copied 1:1 and is cleared as usual.
    //
    // passThrough makes the pop an interpolation towards the FBO instead of a
    // composite over the target, because a seeded FBO already contains the
    // backdrop. Push and pop must agree on it — see popGroupFbo.
    //
    // fillOpacity overrides the Fill the pop would otherwise read off
    // `groupNode` (LayerTreeNode::compositeFillOpacity). Only the caller can
    // tell a clip-alpha capture's sole node — whose Fill must stay out of the
    // captured silhouette — from an ordinary composite-back, so the one caller
    // that needs the distinction passes it in; everyone else leaves it unset.
    void pushGroupFbo(int vpW, int vpH, bool seedFromTarget = false);
    void popGroupFbo(LayerTreeNode* groupNode,
                     const QPointF& canvasHalfExtents,
                     const QMatrix4x4& viewMvp,
                     bool editingMask,
                     bool passThrough = false,
                     std::optional<float> fillOpacity = std::nullopt);

    // ── Clipping-mask alpha (docs/RENDER-PIPELINE.md §4.6) ──
    //
    // Captures ONE layer's isolated alpha into a dedicated canvas-sized texture
    // that the layers clipped to it then multiply their own alpha by. The target
    // is pushed onto the same stack the group FBOs use (so a nested push/pop
    // inside the capture — a Single-Layer-Mode base isolating itself — restores
    // the capture target and not the stack entry below it), but it is NOT
    // composited anywhere on pop: the texture *is* the result.
    //
    // It must be its own buffer rather than the current render target: the clip
    // reads the base layer's own alpha, never the alpha accumulated from the
    // layers underneath it.
    //
    // `owner` identifies whose alpha the texture holds, so a caller can tell a
    // reusable capture from a stale one even when a nested level re-captured in
    // between. Returns false when a capture is already open or the target could
    // not be allocated; `endClipAlphaCapture` is a no-op in that case.
    bool beginClipAlphaCapture(int vpW, int vpH, const void* owner);
    void endClipAlphaCapture();
    const void* clipAlphaOwner() const { return m_clipOwner; }

    // Drops the captured alpha and disarms. Must run once per frame: a capture
    // is only valid for the frame it was taken in — the base's pixels, mask,
    // transform or opacity may all have changed since. Within a frame the owner
    // is what lets one capture serve the whole run of layers clipped to it.
    void resetClipAlpha();

    // Arms the capture for the draws that follow: while enabled, every main- and
    // blend-shader draw multiplies its source alpha by the captured alpha. Must
    // be disarmed again around anything that is not the clipped layer's own
    // merge into its target — see LayerCompositor::renderNodes.
    void setClipAlphaEnabled(bool enabled);

    // Adjustment-layer pass (live GPU path). Copies the current render target
    // into the blend scratch texture and re-draws the canvas quad with the
    // adjustment applied — non-destructive, mirroring the CPU compositor's
    // applyAdjustmentNode. `adjustmentType` is adjustments::shaderId();
    // mask UVs map the canvas quad onto the (document-space) mask texture.
    // `curveLutRgba`, when non-null, is a 256-texel RGBA8 buffer (combined
    // per-channel transfer LUTs) uploaded into the shader's LUT texture for the
    // "curves" and "colorbalance" adjustments; null for adjustments that don't
    // use a LUT. `preserveLuminosity` enables the Color Balance luma-restoring
    // pass on top of the LUT.
    //
    // `fillOpacity` is the node's Fill (LayerTreeNode::compositeFillOpacity):
    // the share of the adjustment's contribution that reaches the composite,
    // independent of `opacity` and applied after the mask — §4.7. An
    // adjustment never splits its Fill with a style bake, so the whole of it
    // arrives here.
    void drawAdjustmentPass(int adjustmentType,
                            float opacity,
                            float fillOpacity,
                            GLuint maskTexture,
                            bool hasMask,
                            float maskDensity,
                            const QVector2D& maskUvScale,
                            const QVector2D& maskUvOffset,
                            const QMatrix4x4& mvp,
                            int viewportW,
                            int viewportH,
                            bool preserveLuminosity = false,
                            const unsigned char* curveLutRgba = nullptr,
                            const float* hsHue = nullptr,
                            const float* hsSat = nullptr,
                            const float* hsLight = nullptr,
                            bool colorize = false,
                            const float* hsBandRange = nullptr,
                            const QColor& solidColor = QColor(),
                            int solidBlendMode = -1);

    QMatrix4x4 currentViewMvp() const { return m_cachedViewMvp; }

    void refreshShapeLayerSprites(Document* doc, const RenderParams& p);

private:
    // ── Shader programs (10) ──
    QOpenGLShaderProgram* m_mainProg      = nullptr;
    QOpenGLShaderProgram* m_solidProg     = nullptr;
    QOpenGLShaderProgram* m_blendProg     = nullptr;
    QOpenGLShaderProgram* m_selectProg    = nullptr;
    QOpenGLShaderProgram* m_rubylithProg  = nullptr;
    QOpenGLShaderProgram* m_grayProg      = nullptr;
    QOpenGLShaderProgram* m_adjustProg    = nullptr;
    QOpenGLShaderProgram* m_displayProg   = nullptr; // final display-CM pass (3D LUT)
    QOpenGLShaderProgram* m_dotProg       = nullptr; // pasteboard dot pattern
    QOpenGLShaderProgram* m_shadowProg    = nullptr; // canvas drop shadow (contact + ambient)

    // ── VAO / VBO (3 sets) ──
    QOpenGLVertexArrayObject m_vao, m_canvasVao, m_fboVao;
    QOpenGLBuffer m_vbo, m_canvasVbo, m_fboVbo;

    unsigned int m_checkerTexture = 0;

    // ── Instanced data VAO / VBO ──
    QOpenGLVertexArrayObject m_instancedVao;
    QOpenGLBuffer m_instancedVbo{QOpenGLBuffer::VertexBuffer};

    // ── Blend FBO (two-pass blending) ──
    unsigned int m_blendFbo = 0, m_blendTex = 0;
    int m_blendTexW = 0, m_blendTexH = 0;

    // ── Display colour-management (final stage) ──
    // The document composite (projection OR live LayerCompositor) is rendered
    // into m_sceneFbo, then a fullscreen pass applies m_displayLutTex (a sampled
    // document→monitor + soft-proof 3D LUT) on the way to screen, so BOTH paths
    // are colour-managed identically (no mid-stroke "pop"). Skipped entirely when
    // the transform is identity (sRGB on sRGB, proof off) — m_displayLutIdentity.
    unsigned int m_sceneFbo = 0, m_sceneTex = 0;
    int m_sceneTexW = 0, m_sceneTexH = 0;
    unsigned int m_displayLutTex = 0;   // GL_TEXTURE_3D, RGB8
    quint64 m_displayLutToken = 0;      // identity of the currently uploaded LUT
    bool m_displayLutIdentity = true;
    bool m_displayLutInitialized = false; // false until the first LUT decision
    int m_managedPrevFbo = 0;           // draw FBO to restore after the scene pass
    int m_managedVp[4] = {0, 0, 0, 0};  // screen viewport to restore
    // The base render target the top-level composite (and popGroupFbo) draws to.
    // 0 (screen) normally; redirected to m_sceneFbo while a managed composite is
    // active so grouped layers composite back into the scene buffer, not screen.
    unsigned int m_compositeBaseFbo = 0;

    // ── Group FBO stack ──
    // `vp` is the GL viewport, and `scissorBox`/`scissorEnabled` the scissor
    // state, active when this group FBO was pushed. Both are restored on pop so
    // the composite-back targets the correct (screen or parent-FBO) viewport and
    // canvas-clipped scissor, instead of leaving them set for the FBO. While the
    // FBO's children are rendered the scissor is disabled (the screen-space
    // scissor would otherwise clip the canvas-sized FBO to a central rectangle).
    struct FboEntry {
        unsigned int fbo, tex;
        int texW, texH;
        GLint vp[4];
        GLint scissorBox[4];
        GLboolean scissorEnabled;
    };
    std::vector<FboEntry> m_groupFboStack;

    // ── Group FBO pool ──
    // The live compositor isolates the stack into a canvas-sized group FBO on
    // every interactive frame (see LayerCompositor::composite), so the FBO and
    // its full-canvas texture are recycled across frames instead of being
    // allocated with glTexImage2D and destroyed again each frame. Entries whose
    // size no longer matches the request (document resized) are purged on push.
    struct PooledFbo {
        unsigned int fbo = 0, tex = 0;
        int w = 0, h = 0;
    };
    std::vector<PooledFbo> m_groupFboPool;

    // ── Clipping-mask alpha ──
    // One canvas-sized buffer, kept outside the group pool because it survives
    // the push/pop of the clipped layer's own isolation and is read as a texture
    // while that happens. A single buffer is enough because a clipping run is
    // contiguous: between a capture and the last draw that uses it, only the
    // run's own layers are drawn, and none of them captures.
    unsigned int m_clipFbo = 0, m_clipTex = 0;
    int m_clipTexW = 0, m_clipTexH = 0;
    bool m_clipCapturing = false;        // a capture is open (guards re-entry)
    bool m_clipEnabled = false;          // the next draws multiply by m_clipTex
    const void* m_clipOwner = nullptr;   // whose alpha m_clipTex holds

    // ── Tile renderer ──
    TileRenderer m_tileRenderer;

    // ── Mipmap cache ──
    MipmapCache m_mipmapCache;

    // ── Selection mask texture (GPU-side) ──
    unsigned int m_selectMaskTexture = 0;
    bool m_selectNeedsUpload = false;
    QRect m_selectUploadRect;       // null = full upload
    QSize m_selectMaskTexSize;      // dims of the allocated texture

    // ── Viewport composite cache (Phase 5) ──
    RenderCache m_renderCache;

    // ── CPU projection display (Fase B) ──
    // The CPU compositor (DocumentCompositor) is the single source of truth;
    // for non-interactive frames the GPU just displays this cached projection
    // texture. Live editing of the active layer (brush stroke, transform drag,
    // mask edit, filter preview) intentionally keeps the per-layer GPU
    // compositor (LayerCompositor) — the standard layer compositor model.
    ProjectionCache m_projection;
    bool m_useProjectionDisplay = true;
    std::function<void()> m_projectionRepaint; // async-projection repaint hook

    // ── Async layer-style bake (Fase D) ──
    // Keeps the costly layer-style blur (drop shadow/glow/stroke/...) off the
    // UI thread during live preview — see StyleBakePool.
    StyleBakePool m_styleBakes;
    std::function<void()> m_styleBakeRepaint;
    // Returns true if the projection was displayed; false if unavailable
    // (e.g. document exceeds GL_MAX_TEXTURE_SIZE) so the caller falls back to
    // the per-layer compositor.
    // allowStale displays the previous projection when the one for the
    // current generation is not composited yet — used while a channel view
    // is active, where the per-layer fallback would show the wrong image.
    bool drawProjection(const RenderParams& p, const QMatrix4x4& viewMvp,
                        bool allowStale = false);

    // ── Cached view MVP (computed in render, used by LayerCompositor) ──
    QMatrix4x4 m_cachedViewMvp;

    // ── Uniform cache (Phase 10.4) ──────────────────────────
    void resetUniformCache();

    struct UniformCache {
        // Main program
        QMatrix4x4 mainTransform;
        float mainOpacity = -2.0f;
        float mainHasMask = -2.0f;
        float mainMaskDensity = -2.0f;
        float mainSourcePremultiplied = -2.0f;
        float mainAlphaWeightedSampling = -2.0f;
        float mainSharpMagnify = -2.0f;
        int   mainMipLevels = -2;
        QVector2D mainUvScale{-2.0f, -2.0f};
        QVector2D mainUvOffset{-2.0f, -2.0f};
        QVector2D mainMaskUvScale{-2.0f, -2.0f};
        QVector2D mainMaskUvOffset{-2.0f, -2.0f};
        float mainHasClip = -2.0f;
        QVector2D mainClipSize{-2.0f, -2.0f};
        // Solid program
        QMatrix4x4 solidTransform;
        QVector4D solidColor{-2.0f, -2.0f, -2.0f, -2.0f};
        // Blend program
        QMatrix4x4 blendTransform;
        float blendOpacity = -2.0f;
        int   blendModeVal = -2;
    };
    UniformCache m_uc;

    // ── Uniform layout: main program ──
    struct MainUniforms {
        GLint transform = -1, texture = -1, opacity = -1;
        GLint maskTexture = -1, hasMask = -1, maskDensity = -1;
        GLint sourcePremultiplied = -1, alphaWeightedSampling = -1;
        GLint sharpMagnify = -1, mipLevels = -1;
        GLint uvScale = -1, uvOffset = -1;
        GLint maskUvScale = -1, maskUvOffset = -1;
        GLint instanced = -1;
        GLint clipTexture = -1, hasClip = -1, clipSize = -1;
    } m_mainU;

    // ── Uniform layout: solid program ──
    struct { GLint transform = -1, color = -1; } m_solidU;

    // ── Uniform layout: blend program ──
    struct {
        GLint transform = -1, srcTexture = -1, dstTexture = -1;
        GLint maskTexture = -1, blendMode = -1, opacity = -1, fillOpacity = -1;
        GLint viewportSize = -1, hasMask = -1, maskDensity = -1;
        GLint maskUvScale = -1, maskUvOffset = -1;
        GLint srcPremultiplied = -1, dstPremultiplied = -1;
        GLint sharpMagnify = -1, mipLevels = -1;
        GLint clipTexture = -1, hasClip = -1, clipSize = -1;
    } m_blendU;

    // ── Uniform layout: selection program ──
    struct {
        GLint transform = -1, mask = -1, time = -1, opacity = -1, texel = -1, zoom = -1;
    } m_selU;

    // ── Uniform layout: rubylith program ──
    struct {
        GLint transform = -1, mask = -1, opacity = -1;
        GLint maskUvOffset = -1, maskUvScale = -1;
    } m_rubyU;

    // ── Uniform layout: grayscale program ──
    struct {
        GLint transform = -1, texture = -1;
        GLint maskUvOffset = -1, maskUvScale = -1;
    } m_grayU;

    // ── Uniform layout: adjustment program ──
    struct {
        GLint transform = -1, dstTexture = -1, maskTexture = -1;
        GLint viewportSize = -1, opacity = -1, fillOpacity = -1;
        GLint hasMask = -1, maskDensity = -1;
        GLint adjustmentType = -1, maskUvScale = -1, maskUvOffset = -1;
        GLint curveLut = -1;
        GLint preserveLuminosity = -1;
        GLint hsHue = -1, hsSat = -1, hsLight = -1, colorize = -1;
        GLint hsBandRange = -1;
        GLint solidColor = -1, solidBlendMode = -1;
    } m_adjustU;

    // ── Uniform layout: display-CM program ──
    struct { GLint scene = -1, lut = -1; } m_displayU;

    // ── Uniform layout: pasteboard dot pattern program ──
    struct {
        GLint bgColor = -1, dotColor = -1, spacing = -1, radius = -1;
    } m_dotU;

    // ── Uniform layout: canvas shadow program ──
    struct {
        GLint rectCenter = -1, rectHalfSize = -1, sigma = -1, color = -1;
    } m_shadowU;

    // 256×1 RGBA8 LUT texture for the "curves" adjustment (per-channel combined
    // LUTs). Lazily created, re-uploaded each adjustment pass that needs it.
    unsigned int m_curveLutTex = 0;

    // ── Render helpers ──
    void initShaders();
    void initQuad();
    void initInstancedQuad();
    void initFboQuad();
    void updateFboQuad(int vpW, int vpH);
    void initCheckerTexture();
    void collectUniforms();
    bool ensureBlendTarget(int viewportW, int viewportH);
    bool ensureClipTarget(int viewportW, int viewportH);
    // Pushes the clipping-mask uniforms of the CURRENT arm state onto the main
    // program. Called by setMainUniforms and by every popGroupFbo branch that
    // sets the main uniforms by hand, so no main-shader draw can miss (or
    // inherit) the clip.
    void applyMainClipUniforms();
    void applyBlendClipUniforms();
    void setMainSourcePremultiplied(bool premultiplied);
    void setMainAlphaWeightedSampling(bool enabled);
    // Lets the resampling box narrow below one texel when magnifying (crisp
    // zoom-in). Only valid for draws whose source texture is already in
    // document space and whose MVP is the pure view mapping — drawProjection
    // and the top-level popGroupFbo. Those are view operations, so they cannot
    // diverge from the CPU compositor, which is the visual source of truth at
    // document resolution; a layer magnified by its own transform must stay
    // bilinear because that scaling is baked into the document.
    void setMainSharpMagnification(bool enabled);
    // Highest level of a premultiplied mip pyramid on the bound texture (0 =
    // none). The sampler picks the level whose residual footprint lands in
    // [1,2) so minification stays an exact area filter at any zoom instead of
    // undersampling a fixed tap grid. Only the projection texture has one.
    void setMainMipLevels(int levels);

    // ── Display colour-management (final stage) ──
    // Rebuilds + uploads the 3D LUT when the display state changes; returns true
    // when a managed pass is needed (non-identity). Never throws — a build
    // failure falls back to identity.
    bool ensureDisplayLut(const RenderParams& p);
    bool ensureSceneTarget(int viewportW, int viewportH);
    // Redirects the document composite into the scene FBO (returns true) so the
    // following composite draws are color-managed by endManagedComposite.
    bool beginManagedComposite(const RenderParams& p);
    // Resolves the scene FBO to screen through the display LUT (source-over).
    void endManagedComposite(const RenderParams& p);

    void renderPasteboardDots();
    void renderCanvasDecorations(const RenderParams& p, const QMatrix4x4& vm);
    void renderSelectionOverlay(const RenderParams& p, const QMatrix4x4& vm);
    void renderRubberBand(const RenderParams& p, const QMatrix4x4& vm);
    void renderBoundingBox(const RenderParams& p, const QMatrix4x4& vm);
    void renderTextCursor(const RenderParams& p, const QMatrix4x4& vm);
    void renderCropOverlay(const RenderParams& p, const QMatrix4x4& vm);
    void renderCropBorder(const RenderParams& p, const QMatrix4x4& vm);
    void renderCropHandles(const RenderParams& p, const QMatrix4x4& vm);
    void renderCropGuides(const RenderParams& p, const QMatrix4x4& vm);
    void renderQuickSelectCircle(const RenderParams& p, const QMatrix4x4& vm);
    void renderBoxSelectionOverlay(const RenderParams& p, const QMatrix4x4& vm);
    void uploadSelectionTexture(const RenderParams& p);
};

// ── Shared quad geometry ─────────────────────────────────────────

struct QuadVertex { float x, y, u, v; };
inline const QuadVertex quadVertices[4] = {
    { -1.0f, -1.0f,  0.0f, 1.0f },
    {  1.0f, -1.0f,  1.0f, 1.0f },
    { -1.0f,  1.0f,  0.0f, 0.0f },
    {  1.0f,  1.0f,  1.0f, 0.0f },
};
inline const QuadVertex outlineVertices[4] = {
    { -1.0f, -1.0f, 0.0f, 0.0f },
    {  1.0f, -1.0f, 0.0f, 0.0f },
    {  1.0f,  1.0f, 0.0f, 0.0f },
    { -1.0f,  1.0f, 0.0f, 0.0f },
};
inline const QuadVertex canvasQuadVertices[4] = {
    { -1.0f, -1.0f,  0.0f,  0.0f },
    {  1.0f, -1.0f,  32.0f, 0.0f },
    { -1.0f,  1.0f,  0.0f,  32.0f },
    {  1.0f,  1.0f,  32.0f, 32.0f },
};

// ── Helpers (moved from CanvasView.cpp) ────────────────────────

static inline QMatrix4x4 qTransformToMatrix4x4(const QTransform& t)
{
    QMatrix4x4 m;
    m(0,0) = t.m11(); m(0,1) = t.m21(); m(0,3) = t.m31();
    m(1,0) = t.m12(); m(1,1) = t.m22(); m(1,3) = t.m32();
    return m;
}

// Maps a BlendMode enum value to the blend shader's `uBlendMode` id.
// Thin wrapper over blend::shaderId (renderer/BlendRules.hpp) kept for the
// existing `int`-based call sites in GPUViewport.cpp.
static inline int blendModeMap(int bm)
{
    return blend::shaderId(static_cast<BlendMode>(bm));
}
