#include "ProjectionCache.hpp"
#include "AsyncProjectionBuilder.hpp"
#include "ChannelView.hpp"
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
    const uint64_t chanGen = doc->channelViewGeneration;
    const bool compositeUpToDate = m_texture != 0
                                && m_doc == doc
                                && m_generation == gen
                                && m_size == doc->size;
    if (compositeUpToDate) {
        if (m_channelGeneration == chanGen)
            return m_texture;
        // Only the channel view moved. The layer composite in m_baseImage is
        // still valid, so re-derive the display image instead of re-running
        // the compositor — this is what makes toggling an eye, dragging an
        // overlay opacity or painting into a channel cheap.
        QRect dirty;
        bool full = true;
        doc->consumeChannelViewDirty(dirty, full);
        applyChannelView(doc, full, dirty);
        m_channelGeneration = chanGen;
        return m_texture;
    }

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
        m_baseImage  = img;
        m_doc        = doc;
        m_generation = gen;
        m_size       = doc->size;
        m_inFlightDoc = nullptr;
        m_inFlightGen = kInvalidGen;
        QRect dirty;
        bool full = true;
        doc->consumeChannelViewDirty(dirty, full);
        applyChannelView(doc, true, QRect());
        m_channelGeneration = chanGen;
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
            m_baseImage  = resultImg.convertToFormat(QImage::Format_RGBA8888);
            m_doc        = doc;
            m_generation = gen;
            m_size       = doc->size;
            QRect dirty;
            bool full = true;
            doc->consumeChannelViewDirty(dirty, full);
            applyChannelView(doc, true, QRect());
            m_channelGeneration = chanGen;
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

void ProjectionCache::applyChannelView(Document* doc, bool full, const QRect& rect)
{
    if (m_baseImage.isNull())
        return;

    const channelview::View view = channelview::resolve(doc);

    if (view.isIdentity()) {
        // Nothing to transform: share the base buffer (COW, no copy) so the
        // default view costs exactly what it did before channels existed.
        const bool wasTransformed = m_image.constBits() != m_baseImage.constBits();
        m_image = m_baseImage;
        if (wasTransformed || m_texture == 0)
            uploadTexture();
        return;
    }

    QRect region = full ? QRect() : rect.intersected(m_baseImage.rect());
    if (!full && region.isEmpty())
        return;

    if (m_image.size() != m_baseImage.size()
        || m_image.format() != m_baseImage.format()
        || m_image.constBits() == m_baseImage.constBits()) {
        // First transformed frame for this composite (or a size change): take
        // a private copy to transform into, leaving the base pristine.
        m_image = m_baseImage.copy();
        region = QRect();
    } else {
        // m_image still holds the PREVIOUS view's result, and the transform is
        // neither idempotent nor invertible: replicating Green over an
        // already-grey Red view shows Red again, and a component the previous
        // view zeroed can never come back. So every pixel about to be
        // re-derived is restored from the base first — the dab's rect on a
        // partial update, the whole canvas on a full one (which is also how a
        // freshly composited m_baseImage reaches the screen).
        const QRect restore = region.isEmpty() ? m_baseImage.rect() : region;
        for (int y = restore.top(); y <= restore.bottom(); ++y) {
            const uchar* src = m_baseImage.constScanLine(y) + restore.left() * 4;
            uchar* dst = m_image.scanLine(y) + restore.left() * 4;
            std::copy(src, src + static_cast<size_t>(restore.width()) * 4, dst);
        }
    }

    channelview::apply(m_image, view, region);

    if (region.isEmpty())
        uploadTexture();
    else
        uploadSubImage(region);
}

void ProjectionCache::uploadSubImage(const QRect& rect)
{
    auto* ctx = QOpenGLContext::currentContext();
    auto* f   = ctx ? ctx->functions() : nullptr;
    if (!f || m_texture == 0 || m_image.isNull() || rect.isEmpty()) {
        uploadTexture();
        return;
    }

    // The texture holds premultiplied samples (see uploadTexture), so the sub
    // rect has to be converted the same way before it is pushed.
    const QImage patch = m_image.copy(rect)
        .convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    if (patch.isNull()) {
        uploadTexture();
        return;
    }

    GLint prevAlign = 4;
    f->glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    f->glBindTexture(GL_TEXTURE_2D, m_texture);
    f->glTexSubImage2D(GL_TEXTURE_2D, 0, rect.x(), rect.y(),
                       rect.width(), rect.height(),
                       GL_RGBA, GL_UNSIGNED_BYTE, patch.constBits());
    // The mip pyramid feeds minification (SamplingShaderLib); leaving it stale
    // would show the pre-dab channel view as soon as the canvas is zoomed out.
    f->glGenerateMipmap(GL_TEXTURE_2D);
    m_mipLevels = maxMipLevel(m_image.size());
    f->glBindTexture(GL_TEXTURE_2D, 0);
    f->glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
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
