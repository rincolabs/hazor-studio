#include "PredefinedBrush.hpp"

#include <algorithm>
#include <cmath>

PredefinedBrush::PredefinedBrush(const Params& p)
    : m_p(p)
{
    if (!m_p.tip.isNull()) {
        m_source = (m_p.tip.format() == QImage::Format_RGBA8888)
            ? m_p.tip
            : m_p.tip.convertToFormat(QImage::Format_RGBA8888);
    }
}

PredefinedBrush::Sample PredefinedBrush::applyTip(const uchar* rgba,
                                                  BrushApplication app,
                                                  const TipRemap& remap,
                                                  const QColor& ink)
{
    Sample s;
    s.coverage = rgba[3] / 255.0f;
    switch (app) {
    case BrushApplication::ColorStamp:
        s.r = rgba[0];
        s.g = rgba[1];
        s.b = rgba[2];
        break;
    case BrushApplication::LightnessMap: {
        float l = (0.299f * rgba[0] + 0.587f * rgba[1] + 0.114f * rgba[2]) / 255.0f;
        l = remap.apply(l);
        s.r = static_cast<int>(ink.red()   * l);
        s.g = static_cast<int>(ink.green() * l);
        s.b = static_cast<int>(ink.blue()  * l);
        break;
    }
    case BrushApplication::AlphaMask:
    default:
        s.r = ink.red();
        s.g = ink.green();
        s.b = ink.blue();
        break;
    }
    return s;
}

void PredefinedBrush::ensurePyramid() const
{
    if (m_pyramidBuilt)
        return;
    m_pyramidBuilt = true;
    m_pyramid.clear();
    if (m_source.isNull())
        return;

    // Level 0 = source; each subsequent level is half the size (smooth), so a
    // request for a much smaller dab downscales from a near-size level instead of
    // collapsing the full-resolution source every time.
    m_pyramid.push_back(m_source);
    QImage cur = m_source;
    while (cur.width() > 2 && cur.height() > 2) {
        cur = cur.scaled(std::max(1, cur.width() / 2), std::max(1, cur.height() / 2),
                         Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
        if (cur.format() != QImage::Format_RGBA8888)
            cur = cur.convertToFormat(QImage::Format_RGBA8888);
        m_pyramid.push_back(cur);
    }
}

QImage PredefinedBrush::scaledTip(int side) const
{
    ensurePyramid();
    if (m_pyramid.isEmpty() || side <= 0)
        return QImage();

    // Smallest level whose every dimension is still >= side, so we always scale
    // DOWN to the final size (avoids blurring from an over-shrunk level).
    const QImage* level = &m_pyramid.first();
    for (const QImage& img : m_pyramid) {
        if (std::min(img.width(), img.height()) >= side)
            level = &img;
        else
            break;
    }

    QImage out = level->scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    if (out.format() != QImage::Format_RGBA8888)
        out = out.convertToFormat(QImage::Format_RGBA8888);
    return out;
}

void PredefinedBrush::renderCoverage(QImage& sprite, int side, float cx, float cy,
                                     float radius, const DabShape& shape,
                                     const DabContext& ctx) const
{
    (void)radius;
    (void)ctx;
    if (side <= 0)
        return;
    if (sprite.width() != side || sprite.height() != side
        || sprite.format() != QImage::Format_ARGB32)
        sprite = QImage(side, side, QImage::Format_ARGB32);
    sprite.fill(Qt::transparent);

    const QImage tip = scaledTip(side);
    if (tip.isNull())
        return;

    const float ca = std::cos(-shape.rotation);
    const float sa = std::sin(-shape.rotation);
    const float sx = std::max(0.05f, shape.scaleX);
    const float roundness = std::clamp(shape.scaleY / sx, 0.05f, 1.0f);
    const float fx = shape.flipX ? -1.0f : 1.0f;
    const float fy = shape.flipY ? -1.0f : 1.0f;
    const float half = side * 0.5f;

    for (int y = 0; y < side; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(sprite.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const float dx0 = (float(x) + 0.5f - cx) * fx;
            const float dy0 = (float(y) + 0.5f - cy) * fy;
            const float rx = dx0 * ca - dy0 * sa;
            const float ry = (dx0 * sa + dy0 * ca) / roundness;
            const int sxi = static_cast<int>(rx + half);
            const int syi = static_cast<int>(ry + half);
            if (sxi < 0 || sxi >= side || syi < 0 || syi >= side)
                continue;
            const uchar* tp = tip.constScanLine(syi) + static_cast<qint64>(sxi) * 4;
            const int alpha = tp[3];
            if (alpha <= 0)
                continue;
            // Brush contract: AlphaMask leaves RGB 0 (caller multiplies the ink),
            // ColorStamp keeps the tip colour, LightnessMap stores the remapped
            // grayscale lightness for the caller to modulate the ink with.
            int r = 0, g = 0, b = 0;
            if (m_p.application == BrushApplication::ColorStamp) {
                r = tp[0]; g = tp[1]; b = tp[2];
            } else if (m_p.application == BrushApplication::LightnessMap) {
                float l = (0.299f * tp[0] + 0.587f * tp[1] + 0.114f * tp[2]) / 255.0f;
                const int v = static_cast<int>(std::clamp(m_p.remap.apply(l), 0.0f, 1.0f) * 255.0f);
                r = g = b = v;
            }
            row[x] = qRgba(r, g, b, alpha);
        }
    }
}

QImage PredefinedBrush::staticStamp(int diam) const
{
    diam = std::max(4, diam);
    QImage out(diam, diam, QImage::Format_Grayscale8);
    out.fill(0);

    const QImage scaled = scaledTip(diam);
    if (scaled.isNull())
        return out;

    // Match the engine's historical image-stamp rule: use the tip's own alpha when
    // it varies, else fall back to inverted luminance (dark = opaque) so a flat
    // tip without alpha still reads as a usable mask.
    bool hasAlpha = false;
    for (int y = 0; y < scaled.height() && !hasAlpha; ++y) {
        const uchar* s = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width() && !hasAlpha; ++x) {
            if (s[x * 4 + 3] < 255) hasAlpha = true;
        }
    }

    for (int y = 0; y < diam; ++y) {
        const uchar* src = scaled.constScanLine(y);
        uchar* dst = out.scanLine(y);
        for (int x = 0; x < diam; ++x) {
            if (hasAlpha) {
                dst[x] = src[x * 4 + 3];
            } else {
                const int lum = (src[x * 4] * 77 + src[x * 4 + 1] * 150
                                 + src[x * 4 + 2] * 29) >> 8;
                dst[x] = static_cast<uchar>(255 - lum);
            }
        }
    }
    return out;
}
