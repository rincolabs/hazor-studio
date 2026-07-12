#include "Layer.hpp"
#include <QPainter>
#include <algorithm>

namespace {
const QImage& emptyRasterImage()
{
    static const QImage image;
    return image;
}

core::RasterLayerStorage& emptyRasterStorage()
{
    static core::RasterLayerStorage storage;
    return storage;
}
}

void Layer::setEvaluatedRasterContent(
    const anim::CelId& id,
    std::shared_ptr<anim::RasterCelContent> content)
{
    const bool changed = !m_rasterAnimationActive || m_evaluatedCelId != id
        || m_evaluatedRasterContent != content;
    m_rasterAnimationActive = true;
    m_evaluatedCelId = id;
    m_evaluatedRasterContent = std::move(content);
    if (changed) {
        textureOutdated = true;
        pendingGpuUpload = true;
        invalidateContentBounds();
    }
}

void Layer::setEvaluatedRasterEmpty()
{
    setEvaluatedRasterContent({}, nullptr);
}

void Layer::clearEvaluatedRasterContent()
{
    if (!m_rasterAnimationActive)
        return;
    m_rasterAnimationActive = false;
    m_evaluatedCelId = {};
    m_evaluatedRasterContent.reset();
    textureOutdated = true;
    pendingGpuUpload = true;
    invalidateContentBounds();
}

const QImage& Layer::renderCpuImage() const
{
    if (!m_rasterAnimationActive)
        return cpuImage;
    return m_evaluatedRasterContent
        ? m_evaluatedRasterContent->cpuImage : emptyRasterImage();
}

QImage& Layer::writableRenderCpuImage()
{
    if (!m_rasterAnimationActive)
        return cpuImage;
    if (!m_evaluatedRasterContent) {
        m_evaluatedRasterContent = std::make_shared<anim::RasterCelContent>();
        m_evaluatedCelId = {};
    }
    return m_evaluatedRasterContent->cpuImage;
}

const core::RasterLayerStorage& Layer::renderRasterStorage() const
{
    if (!m_rasterAnimationActive)
        return rasterStorage;
    return m_evaluatedRasterContent
        ? m_evaluatedRasterContent->rasterStorage : emptyRasterStorage();
}

core::RasterLayerStorage& Layer::renderRasterStorage()
{
    if (!m_rasterAnimationActive)
        return rasterStorage;
    return m_evaluatedRasterContent
        ? m_evaluatedRasterContent->rasterStorage : emptyRasterStorage();
}

QImage Layer::compositeImage() const
{
    const auto& storage = renderRasterStorage();
    const QImage& baseImage = renderCpuImage();
    if (!storage.isEnabled())
        return baseImage.copy();

    QRect tileBounds;
    QImage tileData = storage.toImage(&tileBounds);
    const QSize baseSize = storage.baseSize();

    QImage full(baseSize, QImage::Format_RGBA8888);
    full.fill(Qt::transparent);

    QPainter p(&full);
    if (!baseImage.isNull())
        p.drawImage(0, 0, baseImage.convertToFormat(QImage::Format_RGBA8888));
    if (!tileData.isNull() && !tileBounds.isEmpty())
        p.drawImage(tileBounds.topLeft(), tileData);
    p.end();

    return full;
}

QImage Layer::compositeImageExpanded(QRect* outBounds) const
{
    const auto& storage = renderRasterStorage();
    const QImage& baseImage = renderCpuImage();
    if (!storage.isEnabled()) {
        if (outBounds) *outBounds = QRect(QPoint(0, 0), baseImage.size());
        return baseImage.copy();
    }

    QRect tileBounds;
    QImage tileData = storage.toImage(&tileBounds);
    const QRect baseRect(QPoint(0, 0), storage.baseSize());

    // Grow the buffer to span both the original base rect and any tiles painted
    // outside it. The union origin may be negative / past baseSize.
    QRect full = baseRect;
    if (!tileBounds.isEmpty())
        full = full.isEmpty() ? tileBounds : full.united(tileBounds);
    if (full.isEmpty()) {
        if (outBounds) *outBounds = baseRect;
        return QImage();
    }

    QImage out(full.size(), QImage::Format_RGBA8888);
    out.fill(Qt::transparent);
    QPainter p(&out);
    if (!baseImage.isNull())
        p.drawImage(-full.topLeft(), baseImage.convertToFormat(QImage::Format_RGBA8888));
    if (!tileData.isNull() && !tileBounds.isEmpty())
        p.drawImage(tileBounds.topLeft() - full.topLeft(), tileData);
    p.end();

    if (outBounds) *outBounds = full;
    return out;
}

QRect Layer::cpuContentBounds() const
{
    const QImage& image = renderCpuImage();
    if (image.isNull())
        return {};
    const QRect fullRect(QPoint(0, 0), image.size());
    if (!image.hasAlphaChannel())
        return fullRect; // opaque format — nothing to trim

    const qint64 key = image.cacheKey();
    if (!cpuContentBoundsDirty && key == cpuContentBoundsKey)
        return cpuContentBoundsCache;

    const int w = image.width();
    const int h = image.height();
    int minX = w, minY = h, maxX = -1, maxY = -1;

    const QImage::Format fmt = image.format();
    if (fmt == QImage::Format_RGBA8888
        || fmt == QImage::Format_RGBA8888_Premultiplied) {
        for (int y = 0; y < h; ++y) {
            const uchar* row = image.constScanLine(y);
            int first = -1, last = -1;
            for (int x = 0; x < w; ++x) {
                if (row[x * 4 + 3] == 0)
                    continue;
                if (first < 0) first = x;
                last = x;
            }
            if (first < 0)
                continue;
            if (minY > y) minY = y;
            maxY = y;
            minX = std::min(minX, first);
            maxX = std::max(maxX, last);
        }
    } else if (fmt == QImage::Format_ARGB32
               || fmt == QImage::Format_ARGB32_Premultiplied) {
        for (int y = 0; y < h; ++y) {
            const QRgb* row = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            int first = -1, last = -1;
            for (int x = 0; x < w; ++x) {
                if (qAlpha(row[x]) == 0)
                    continue;
                if (first < 0) first = x;
                last = x;
            }
            if (first < 0)
                continue;
            if (minY > y) minY = y;
            maxY = y;
            minX = std::min(minX, first);
            maxX = std::max(maxX, last);
        }
    } else {
        // Unhandled alpha layout — conservatively report full coverage rather
        // than paying a format conversion here.
        cpuContentBoundsCache = fullRect;
        cpuContentBoundsKey = key;
        cpuContentBoundsDirty = false;
        return cpuContentBoundsCache;
    }

    cpuContentBoundsCache = (maxX < minX)
        ? QRect()
        : QRect(QPoint(minX, minY), QPoint(maxX, maxY));
    cpuContentBoundsKey = key;
    cpuContentBoundsDirty = false;
    return cpuContentBoundsCache;
}

bool Layer::ensureWritableRect(const QRect& localRect, QTransform* outAdjust)
{
    if (outAdjust) *outAdjust = QTransform(); // identity — caller gets no adjustment

    // RasterLayerStorage tiles grow on demand; no QImage resize needed.
    if (rasterStorage.isEnabled()) {
        rasterStorage.tilesInRect(localRect, /*createMissing=*/true);
        return false;
    }

    const int oldW = cpuImage.width();
    const int oldH = cpuImage.height();
    const QRect current(0, 0, oldW, oldH);
    const QRect expanded = current.united(localRect);

    if (expanded == current)
        return false;

    const int offX = -expanded.left();
    const int offY = -expanded.top();
    const int newW = expanded.width();
    const int newH = expanded.height();

    QImage newImg(newW, newH, QImage::Format_RGBA8888);
    newImg.fill(Qt::transparent);
    {
        QPainter p(&newImg);
        p.drawImage(offX, offY, cpuImage);
    }
    cpuImage = newImg;

    if (outAdjust && oldW > 0 && oldH > 0) {
        // Compute the NDC-space adjustment that keeps the layer's visual position
        // unchanged after the cpuImage canvas grew. The transform maps the OLD
        // NDC quad onto the same canvas-space region inside the NEW NDC quad.
        const float sx = float(newW) / float(oldW);
        const float sy = float(newH) / float(oldH);
        const float dx = 1.0f - 2.0f * offX / newW - float(oldW) / newW;
        const float dy = float(oldH) / newH - 1.0f + 2.0f * offY / newH;
        QTransform adj;
        adj.scale(sx, sy);
        adj.translate(dx, dy);
        *outAdjust = adj;
    }
    return true;
}
