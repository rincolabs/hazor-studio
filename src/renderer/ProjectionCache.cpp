#include "ProjectionCache.hpp"
#include "AsyncProjectionBuilder.hpp"
#include "DocumentCompositor.hpp"
#include "RenderContext.hpp"
#include "core/Document.hpp"
#include "core/LayerTreeNode.hpp"

#include <QOpenGLContext>
#include <QOpenGLFunctions>

#include <algorithm>

ProjectionCache::ProjectionCache() = default;
ProjectionCache::~ProjectionCache() = default;

void ProjectionCache::ensureBuilder()
{
    if (m_builder)
        return;
    m_builder = std::make_unique<AsyncProjectionBuilder>();
    // Wake the host (repaint) when an off-thread projection is ready so the
    // next update() — running with a current GL context — uploads + displays it.
    QObject::connect(m_builder.get(), &AsyncProjectionBuilder::ready,
                     m_builder.get(), [this]() { if (m_onReady) m_onReady(); });
}

std::shared_ptr<Document> ProjectionCache::makeSnapshot(const Document* doc) const
{
    auto snap = std::make_shared<Document>();
    snap->size = doc->size;
    snap->compositionGeneration = doc->compositionGeneration;
    snap->roots.reserve(doc->roots.size());
    for (const auto& root : doc->roots)
        snap->roots.push_back(root->shallowClone());
    return snap;
}

unsigned int ProjectionCache::update(Document* doc)
{
    if (!doc || doc->size.isEmpty())
        return 0;

    const uint64_t gen = doc->compositionGeneration;
    const bool upToDate = m_texture != 0
                       && m_doc == doc
                       && m_generation == gen
                       && m_size == doc->size;
    if (upToDate)
        return m_texture;

    // A single full-canvas texture can't hold documents larger than the GL
    // texture limit — signal "unavailable" so the caller keeps the per-layer
    // compositor for these. Checked before any (expensive) composite.
    if (exceedsMaxTextureSize(doc->size))
        return 0;

    // An empty document (all layers removed) composites to nothing. This is
    // cheap, so build the transparent canvas synchronously instead of keeping
    // the stale texture from before the last layer was removed (otherwise the
    // deleted layer keeps showing).
    if (doc->flatCount() == 0) {
        QImage img(doc->size, QImage::Format_RGBA8888);
        img.fill(Qt::transparent);
        m_image      = img;
        m_doc        = doc;
        m_generation = gen;
        m_size       = doc->size;
        m_inFlightDoc = nullptr;
        m_inFlightGen = kInvalidGen;
        uploadTexture();
        return m_texture;
    }

    ensureBuilder();

    // 1) Pick up a finished off-thread composite. Only upload it when it still
    //    matches the live document + generation (revision discard — a result
    //    built from a now-superseded state is dropped, never shown).
    const void* resultDoc = nullptr;
    uint64_t    resultGen = 0;
    QImage      resultImg;
    if (m_builder->takeResult(resultDoc, resultGen, resultImg)) {
        m_inFlightDoc = nullptr;
        m_inFlightGen = kInvalidGen;
        if (!resultImg.isNull() && resultDoc == doc && resultGen == gen) {
            m_image      = resultImg.convertToFormat(QImage::Format_RGBA8888);
            m_doc        = doc;
            m_generation = gen;
            m_size       = doc->size;
            uploadTexture();
            return m_texture;
        }
        // Stale result — fall through to dispatch a fresh build for `gen`.
    }

    // 2) Dispatch a build for the current state if one isn't already in flight
    //    for it. The snapshot is a cheap COW copy (no pixel memcpy), safe to
    //    read off-thread while the UI keeps mutating the live document.
    const bool inFlightForThis = m_builder->isBuilding()
                              && m_inFlightDoc == doc
                              && m_inFlightGen == gen;
    if (!inFlightForThis && !m_builder->isBuilding()) {
        m_inFlightDoc = doc;
        m_inFlightGen = gen;
        m_builder->request(makeSnapshot(doc), doc, gen);
    }

    // 3) Not ready yet → tell the caller to use the GPU per-layer compositor for
    //    this frame (current state, interactive speed). No UI-thread CPU block.
    return 0;
}

void ProjectionCache::uploadTexture()
{
    auto* ctx = QOpenGLContext::currentContext();
    auto* f   = ctx ? ctx->functions() : nullptr;
    if (!f || m_image.isNull())
        return;

    if (m_texture == 0)
        f->glGenTextures(1, &m_texture);

    // Uploaded premultiplied so the mip pyramid below is correct: averaging
    // straight-alpha texels folds the (meaningless) colour of fully
    // transparent pixels into every level, which shows up as a dark halo
    // around soft edges once the canvas is zoomed out. drawProjection pairs
    // this with uSourcePremultiplied = 1.
    const QImage premul = m_image.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (premul.isNull())
        return;

    f->glBindTexture(GL_TEXTURE_2D, m_texture);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    f->glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    f->glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8,
                    premul.width(), premul.height(),
                    0, GL_RGBA, GL_UNSIGNED_BYTE, premul.constBits());
    // The pyramid is what keeps minification an exact area filter instead of
    // a fixed tap grid spread ever thinner as the zoom drops: the sampler
    // picks the level whose residual footprint is in [1,2) and boxes that
    // (renderer/SamplingShaderLib.hpp). GEGL/GIMP use the same idea
    // (gimpprojection.c, memsize x 1.33) — the pyramid costs a third of the
    // texture.
    f->glGenerateMipmap(GL_TEXTURE_2D);
    m_mipLevels = maxMipLevel(premul.size());
    f->glBindTexture(GL_TEXTURE_2D, 0);
}

int ProjectionCache::maxMipLevel(const QSize& s)
{
    int level = 0;
    for (int n = std::max(s.width(), s.height()); n > 1; n /= 2)
        ++level;
    return level;
}

bool ProjectionCache::exceedsMaxTextureSize(const QSize& s)
{
    if (m_maxTextureSize == 0) {
        auto* ctx = QOpenGLContext::currentContext();
        auto* f   = ctx ? ctx->functions() : nullptr;
        if (f)
            f->glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
        if (m_maxTextureSize <= 0)
            m_maxTextureSize = 2048; // conservative floor if the query failed
    }
    return s.width() > m_maxTextureSize || s.height() > m_maxTextureSize;
}

void ProjectionCache::releaseGL()
{
    auto* ctx = QOpenGLContext::currentContext();
    auto* f   = ctx ? ctx->functions() : nullptr;
    if (f && m_texture)
        f->glDeleteTextures(1, &m_texture);
    m_texture = 0;
    m_mipLevels = 0;
    invalidate();
}
