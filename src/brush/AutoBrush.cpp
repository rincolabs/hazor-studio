#include "AutoBrush.hpp"
#include "BrushTypes.hpp"   // brushFalloff() — Default profile parity

#include <algorithm>
#include <cmath>

namespace {
constexpr float kPi = 3.14159265358979323846f;
}

AutoBrush::AutoBrush(const Params& p)
    : m_p(p)
{
    m_isotropic = (m_p.spikes <= 2)
        && (m_p.hFade < 0.0f || m_p.vFade < 0.0f
            || std::abs(m_p.hFade - m_p.vFade) < 1e-4f);
    if (m_isotropic && m_p.hFade >= 0.0f)
        m_baseHardness = std::clamp(1.0f - m_p.hFade, 0.0f, 1.0f);
    else
        m_baseHardness = std::clamp(m_p.hardness, 0.0f, 1.0f);
}

float AutoBrush::falloff1D(float d, float hardness, float aa) const
{
    if (d >= 1.0f)
        return 0.0f;

    float base;
    if (m_p.profile == Profile::Default) {
        // Pixel-identical to the engine's historical edge.
        base = brushFalloff(d, hardness, aa);
    } else {
        const float edge = std::clamp(hardness, 0.0f, 0.999f);
        if (d <= edge) {
            base = 1.0f;
        } else {
            const float t = (d - edge) / (1.0f - edge);
            if (m_p.profile == Profile::Soft) {
                base = (1.0f - t) * (1.0f - t);              // gentle quadratic
            } else {                                          // Gaussian
                const float tail = std::exp(-4.5f);
                base = (std::exp(-4.5f * t * t) - tail) / (1.0f - tail);
                base = std::clamp(base, 0.0f, 1.0f);
            }
        }
        // Anti-alias the very edge, matching brushFalloff's smoothstep tail.
        if (d > 1.0f - aa) {
            const float at = (d - (1.0f - aa)) / aa;
            base *= 1.0f - (3.0f - 2.0f * at) * at * at;
        }
    }

    if (!m_p.softnessCurve.points.isEmpty())
        base = m_p.softnessCurve.evaluate(base);
    return std::clamp(base, 0.0f, 1.0f);
}

float AutoBrush::directionalHardness(float theta) const
{
    if (m_isotropic)
        return m_baseHardness;
    const float fadeH = (m_p.hFade >= 0.0f) ? m_p.hFade : (1.0f - m_p.hardness);
    const float fadeV = (m_p.vFade >= 0.0f) ? m_p.vFade : (1.0f - m_p.hardness);
    const float c = std::cos(theta);
    const float s = std::sin(theta);
    const float fade = fadeH * c * c + fadeV * s * s;
    return std::clamp(1.0f - fade, 0.0f, 1.0f);
}

float AutoBrush::coverage(float rx, float ry, float radius, const DabContext& ctx) const
{
    const float r = std::max(0.5f, radius);
    const float aa = m_p.antialias ? std::min(0.25f, 1.5f / r) : 1e-4f;
    const float nx = rx / r;
    const float ny = ry / r;

    float d = (m_p.shape == Shape::Rectangle)
        ? std::max(std::abs(nx), std::abs(ny))
        : std::sqrt(nx * nx + ny * ny);

    const bool needTheta = !m_isotropic || m_p.spikes > 2;
    const float theta = needTheta ? std::atan2(ny, nx) : 0.0f;

    if (m_p.spikes > 2) {
        // Regular-polygon (apothem) modulation of the radius per angular sector.
        const float sector = 2.0f * kPi / float(m_p.spikes);
        float a = std::fmod(theta, sector);
        if (a < 0.0f) a += sector;
        const float starFactor = std::cos(sector * 0.5f)
            / std::max(std::cos(a - sector * 0.5f), 1e-3f);
        d /= std::max(starFactor, 1e-3f);
    }

    const float hardness = m_isotropic ? m_baseHardness : directionalHardness(theta);
    float cov = falloff1D(d, hardness, aa);
    if (cov <= 0.0f)
        return 0.0f;

    if (m_p.randomness > 0.0f)
        cov *= 1.0f - m_p.randomness * ctx.nextRandom();
    if (m_p.density < 1.0f && ctx.nextRandom() > m_p.density)
        cov = 0.0f;

    return std::clamp(cov, 0.0f, 1.0f);
}

void AutoBrush::renderCoverage(QImage& sprite, int side, float cx, float cy,
                               float radius, const DabShape& shape,
                               const DabContext& ctx) const
{
    if (side <= 0)
        return;
    if (sprite.width() != side || sprite.height() != side
        || sprite.format() != QImage::Format_ARGB32)
        sprite = QImage(side, side, QImage::Format_ARGB32);
    sprite.fill(Qt::transparent);

    const float ca = std::cos(-shape.rotation);
    const float sa = std::sin(-shape.rotation);
    const float sx = std::max(0.05f, shape.scaleX);
    const float roundness = std::clamp(shape.scaleY / sx, 0.05f, 1.0f);
    const float fx = shape.flipX ? -1.0f : 1.0f;
    const float fy = shape.flipY ? -1.0f : 1.0f;

    for (int y = 0; y < side; ++y) {
        QRgb* row = reinterpret_cast<QRgb*>(sprite.scanLine(y));
        for (int x = 0; x < side; ++x) {
            const float dx0 = (float(x) + 0.5f - cx) * fx;
            const float dy0 = (float(y) + 0.5f - cy) * fy;
            const float rx = dx0 * ca - dy0 * sa;
            const float ry = (dx0 * sa + dy0 * ca) / roundness;
            const float c = coverage(rx, ry, radius, ctx);
            if (c <= 0.0f)
                continue;
            row[x] = qRgba(0, 0, 0, int(std::clamp(c, 0.0f, 1.0f) * 255.0f));
        }
    }
}

QImage AutoBrush::staticStamp(int diam) const
{
    diam = std::max(4, diam);
    QImage img(diam, diam, QImage::Format_Grayscale8);
    img.fill(0);

    const float cx = diam / 2.0f;
    const float cy = diam / 2.0f;
    const float radius = (diam - 2) * 0.5f;
    DabContext ctx;   // fixed seed → deterministic neutral stamp

    for (int y = 0; y < diam; ++y) {
        uchar* row = img.scanLine(y);
        for (int x = 0; x < diam; ++x) {
            const float rx = float(x) + 0.5f - cx;
            const float ry = float(y) + 0.5f - cy;
            const float c = coverage(rx, ry, radius, ctx);
            row[x] = uchar(std::clamp(c * 255.0f, 0.0f, 255.0f));
        }
    }
    return img;
}
