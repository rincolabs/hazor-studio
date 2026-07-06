#include "MipmapCache.hpp"
#include "core/Layer.hpp"
#include "engine/ImageEngine.hpp"

#include <QOpenGLFunctions>
#include <QOpenGLContext>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

static int indexForLod(core::RenderScheduler::LOD lod)
{
    return static_cast<int>(lod);
}

const QImage* MipmapCache::level(Layer* layer, core::RenderScheduler::LOD lod)
{
    if (!layer || layer->cpuImage.isNull())
        return nullptr;

    int idx = indexForLod(lod);
    if (idx < 1 || idx > 3)
        return nullptr; // Full res is not managed here

    if (layer->lodDirty[idx] || layer->lodLevels[idx].isNull()) {
        generateLevel(layer, lod);
    }

    return layer->lodLevels[idx].isNull() ? nullptr : &layer->lodLevels[idx];
}

void MipmapCache::generateLevel(Layer* layer, core::RenderScheduler::LOD lod)
{
    int idx = indexForLod(lod);
    if (!layer || idx < 1 || idx > 3)
        return;

    const QImage& src = layer->cpuImage;
    if (src.isNull()) return;

    int shift = idx; // 1→1/2, 2→1/4, 3→1/8
    int dstW = std::max(1, src.width()  >> shift);
    int dstH = std::max(1, src.height() >> shift);

    cv::Mat cvSrc = ImageEngine::toCvMat(src);
    if (cvSrc.empty()) return;

    cv::Mat cvDst;
    cv::resize(cvSrc, cvDst, cv::Size(dstW, dstH), 0, 0, cv::INTER_AREA);
    if (cvDst.empty()) return;

    layer->lodLevels[idx] = ImageEngine::toQImage(cvDst);
    layer->lodDirty[idx] = false;
}

unsigned int MipmapCache::uploadLevel(Layer* layer, core::RenderScheduler::LOD lod)
{
    int idx = indexForLod(lod);
    if (!layer || idx < 1 || idx > 3)
        return 0;

    auto* gl = QOpenGLContext::currentContext()
                   ? QOpenGLContext::currentContext()->functions()
                   : nullptr;
    if (!gl) return 0;

    const QImage* img = level(layer, lod);
    if (!img || img->isNull())
        return 0;

    // Create GL texture if not yet created
    if (layer->lodTextures[idx] == 0) {
        gl->glGenTextures(1, &layer->lodTextures[idx]);
        gl->glBindTexture(GL_TEXTURE_2D, layer->lodTextures[idx]);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        gl->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        QImage rgba = img->convertToFormat(QImage::Format_RGBA8888);
        gl->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                         rgba.width(), rgba.height(), 0,
                         GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
    } else {
        // Re-upload existing texture
        gl->glBindTexture(GL_TEXTURE_2D, layer->lodTextures[idx]);
        QImage rgba = img->convertToFormat(QImage::Format_RGBA8888);
        gl->glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                            rgba.width(), rgba.height(),
                            GL_RGBA, GL_UNSIGNED_BYTE, rgba.constBits());
    }

    layer->lodDirty[idx] = false;
    return layer->lodTextures[idx];
}

void MipmapCache::uploadDirtyLevels(Layer* layer)
{
    if (!layer) return;

    for (int i = 1; i <= 3; ++i) {
        if (layer->lodDirty[i]) {
            uploadLevel(layer, static_cast<core::RenderScheduler::LOD>(i));
        }
    }
}

void MipmapCache::invalidate(Layer* layer)
{
    if (!layer) return;
    for (int i = 1; i <= 3; ++i) {
        layer->lodDirty[i] = true;
        layer->lodLevels[i] = QImage(); // free memory
    }
}

void MipmapCache::cleanup(Layer* layer)
{
    if (!layer) return;

    auto* gl = QOpenGLContext::currentContext()
                   ? QOpenGLContext::currentContext()->functions()
                   : nullptr;

    if (gl) {
        for (int i = 1; i <= 3; ++i) {
            if (layer->lodTextures[i]) {
                gl->glDeleteTextures(1, &layer->lodTextures[i]);
                layer->lodTextures[i] = 0;
            }
        }
    }

    invalidate(layer);
}

MipmapCache::~MipmapCache()
{
    // Cleanup is done by GPUViewport::cleanupDocumentLayers
}

void MipmapCache::clearAll()
{
    // Textures are owned by layers; cleanup is called per-layer
    // in GPUViewport::cleanupDocumentLayers
}
