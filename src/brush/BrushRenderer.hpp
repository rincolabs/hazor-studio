#pragma once

#include <QOpenGLShaderProgram>
#include <QOpenGLVertexArrayObject>
#include <QOpenGLBuffer>
#include <QOpenGLFunctions>
#include <QImage>
#include <QHash>
#include "BrushTypes.hpp"
#include "BrushInputState.hpp"
#include "DynamicsConfig.hpp"

class Layer;

class BrushRenderer : protected QOpenGLFunctions {
public:
    BrushRenderer();
    ~BrushRenderer();

    void initGL();
    void destroyGL();

    // Stroke lifecycle for the CPU raster path. beginStroke() resets the per-stroke
    // Wash accumulation buffers; endStroke() frees them. Safe to call for every
    // brush stroke (no-op for BuildUp / non-raster paths).
    void beginStroke();
    void endStroke();

    void updateStamp(const BrushSettings& settings);
    void drawDab(Layer* layer, const BrushInputState& state,
                 const DynamicDabParams& params,
                 const BrushSettings& settings,
                 GLuint selectMaskTex, int selectMaskW, int selectMaskH,
                 const QImage* selectMaskImage = nullptr);
    void drawCloneDab(Layer* layer, const BrushInputState& state,
                      const DynamicDabParams& params,
                      const BrushSettings& settings,
                      const CloneStampContext& cloneContext,
                      const QImage* selectMaskImage = nullptr);

    void drawMaskDab(Layer* layer, const BrushInputState& state,
                     const DynamicDabParams& params,
                     const BrushSettings& settings,
                     GLuint selectMaskTex, int selectMaskW, int selectMaskH);

    void setTextureImage(const QImage& img);
    void clearTexture();

private:
    void initMaskShader();
    void generateStamp(const BrushSettings& settings);
    void uploadStampTexture();
    void generateDualStamp(const DualBrushConfig& config);
    void uploadDualStampTexture();
    void drawDabToRasterTiles(Layer* layer, const BrushInputState& state,
                              const DynamicDabParams& params,
                              const BrushSettings& settings,
                              const QImage* selectMaskImage);
    void drawCloneDabToRasterTiles(Layer* layer, const BrushInputState& state,
                                   const DynamicDabParams& params,
                                   const BrushSettings& settings,
                                   const CloneStampContext& cloneContext,
                                   const QImage* selectMaskImage);
    void drawHealingDabToRasterTiles(Layer* layer, const BrushInputState& state,
                                     const DynamicDabParams& params,
                                     const BrushSettings& settings,
                                     const CloneStampContext& cloneContext,
                                     const QImage* selectMaskImage);

    QOpenGLShaderProgram* m_program = nullptr;
    QOpenGLVertexArrayObject m_vao;
    QOpenGLBuffer m_vbo;

    GLint m_uCenter = -1;
    GLint m_uRadius = -1;
    GLint m_uTexSize = -1;
    GLint m_uColor = -1;
    GLint m_uFlow = -1;
    GLint m_uOpacity = -1;
    GLint m_uStamp = -1;
    GLint m_uAngle = -1;
    GLint m_uRoundness = -1;
    GLint m_uUseSelectMask = -1;
    GLint m_uSelectMask = -1;
    GLint m_uSelectMaskSize = -1;
    GLint m_uLayerToDocX = -1;
    GLint m_uLayerToDocY = -1;

    GLint m_uTexStamp = -1;
    GLint m_uTexBrightness = -1;
    GLint m_uTexContrast = -1;
    GLint m_uTexDepth = -1;
    GLint m_uTexInvert = -1;
    GLint m_uTexUVScale = -1;
    GLint m_uTexUVOffset = -1;
    GLint m_uTexNeutral = -1;
    GLint m_uTexCut = -1;
    GLint m_uTexCutLo = -1;
    GLint m_uTexCutHi = -1;
    GLint m_uTexSubtract = -1;
    GLint m_uTexSoft = -1;
    GLint m_uTexSizePx = -1;

    GLuint m_stampTexture = 0;
    int m_stampSize = 0;
    QImage m_stampImage;
    BrushSettings m_lastSettings;
    bool m_stampDirty = true;

    // Cache of the image tip scaled to the current dab size, so a stroke does
    // not re-downscale a large source tip on every dab (raster-tile path).
    QImage m_rasterTipScaled;
    int m_rasterTipSide = -1;
    qint64 m_rasterTipKey = 0;

    // Cached RGBA8888 view of the clone/healing stroke snapshot: the snapshot is
    // fixed for the whole stroke, so it is converted once instead of converting
    // the document-sized image on every dab.
    const QImage& cloneSourceRgba(const QImage& src);
    QImage m_cloneSourceRgba;
    qint64 m_cloneSourceKey = -1;

    // Same tip-scaling cache as the paint path, for the clone/healing dabs.
    const QImage& cloneScaledTip(const QImage& tip, int side);
    QImage m_cloneTipScaled;
    int m_cloneTipSide = -1;
    qint64 m_cloneTipKey = 0;

    // Reused dab sprite buffer for the raster-tile path, so a fast stroke does not
    // allocate a side×side QImage per dab. BrushDab::rasterize resizes it on demand.
    QImage m_dabScratch;

    // Wash painting mode (per-stroke). For each touched tile we keep the pre-stroke
    // pixels (base) and a flow-accumulation buffer (accum). Each dab adds to accum
    // with flow, then the tile is rebuilt as base OVER (accum capped at opacity), so
    // the stroke never exceeds its opacity. Cleared by begin/endStroke().
    struct WashTile { QImage base; QImage accum; };
    QHash<qint64, WashTile> m_washTiles;
    bool m_strokeActive = false;
    // Per-stroke random texture offset (pattern pixels). Re-rolled at beginStroke so
    // each stroke samples the grain at a different origin; overlapping strokes then
    // fill each other's gaps and build up to cover the texture (paper-anchored within
    // a stroke, randomised between strokes).
    int m_texStrokeOffsetX = 0;
    int m_texStrokeOffsetY = 0;

    GLuint m_texStampTexture = 0;
    QImage m_texStampImage;
    bool m_texStampDirty = false;

    GLuint m_dualStampTexture = 0;
    int m_dualStampSize = 0;
    QImage m_dualStampImage;
    DualBrushConfig m_dualLastConfig;
    bool m_dualStampDirty = false;

    GLint m_uBlendMode = -1;
    GLint m_uDestTexture = -1;
    GLint m_uDestTexSize = -1;
    GLuint m_destCopyTexture = 0;
    int m_copyTexW = 0;
    int m_copyTexH = 0;

    QOpenGLShaderProgram* m_maskProgram = nullptr;
    QOpenGLVertexArrayObject m_maskVao;
    QOpenGLBuffer m_maskVbo;
    GLint m_muCenter = -1;
    GLint m_muRadius = -1;
    GLint m_muTexSize = -1;
    GLint m_muFlow = -1;
    GLint m_muOpacity = -1;
    GLint m_muStamp = -1;
    GLint m_muAngle = -1;
    GLint m_muRoundness = -1;
    GLint m_muMaskTarget = -1;
    GLint m_muUseSelectMask = -1;
    GLint m_muSelectMask = -1;
    GLint m_muSelectMaskSize = -1;
    GLint m_muLayerToDocX = -1;
    GLint m_muLayerToDocY = -1;
    GLint m_muTexStamp = -1;
    GLint m_muTexBrightness = -1;
    GLint m_muTexContrast = -1;
    GLint m_muTexDepth = -1;
    GLint m_muTexInvert = -1;
    GLint m_muTexUVScale = -1;
    GLint m_muTexUVOffset = -1;
    GLint m_muTexNeutral = -1;
    GLint m_muTexCut = -1;
    GLint m_muTexCutLo = -1;
    GLint m_muTexCutHi = -1;
    GLint m_muTexSubtract = -1;
    GLint m_muTexSoft = -1;
    GLint m_muTexSizePx = -1;
    GLint m_muDestMask = -1;
    GLint m_muDestTexSize = -1;
    GLuint m_maskCopyTexture = 0;
    int m_maskCopyTexW = 0;
    int m_maskCopyTexH = 0;
};
