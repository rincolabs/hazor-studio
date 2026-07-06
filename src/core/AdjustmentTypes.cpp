#include "AdjustmentTypes.hpp"

#include "ColorBalanceData.hpp"
#include "CurvesData.hpp"
#include "HueSaturationData.hpp"
#include "SolidColorData.hpp"

#include <QColor>

#include <algorithm>
#include <array>
#include <cmath>

namespace adjustments {

const std::vector<AdjustmentInfo>& registry()
{
    // Order defines menu order. shaderId must match the `uAdjustmentType`
    // branches in the GPU adjustment shader (GPUViewport_Shaders.cpp).
    static const std::vector<AdjustmentInfo> kRegistry = {
        { QStringLiteral("grayscale"),    QStringLiteral("Grayscale"),     0 },
        { QStringLiteral("curves"),       QStringLiteral("Curves"),        1 },
        { QStringLiteral("colorbalance"), QStringLiteral("Color Balance"), 2 },
        { QStringLiteral("huesaturation"), QStringLiteral("Hue/Saturation"), 3 },
        { QStringLiteral("solidcolor"),   QStringLiteral("Solid Color"),   4 },
    };
    return kRegistry;
}

bool buildCurveLuts(const AdjustmentData& data,
                    std::array<unsigned char, 256>& rLut,
                    std::array<unsigned char, 256>& gLut,
                    std::array<unsigned char, 256>& bLut)
{
    if (data.type != QLatin1String("curves"))
        return false;
    const curves::CurvesData cd = curves::CurvesData::fromParams(data.params);
    cd.buildCombinedLuts(rLut, gLut, bLut);
    return true;
}

bool buildColorBalanceLuts(const AdjustmentData& data,
                           std::array<unsigned char, 256>& rLut,
                           std::array<unsigned char, 256>& gLut,
                           std::array<unsigned char, 256>& bLut,
                           bool& preserveLuminosity)
{
    if (data.type != QLatin1String("colorbalance"))
        return false;
    const colorbalance::ColorBalanceData cb =
        colorbalance::ColorBalanceData::fromParams(data.params);
    cb.buildTransferLuts(rLut, gLut, bLut);
    preserveLuminosity = cb.preserveLuminosity;
    return true;
}

bool isKnown(const QString& id)
{
    return shaderId(id) >= 0;
}

QString displayName(const QString& id)
{
    for (const auto& info : registry()) {
        if (info.id == id)
            return info.displayName;
    }
    return id;
}

int shaderId(const QString& id)
{
    for (const auto& info : registry()) {
        if (info.id == id)
            return info.shaderId;
    }
    return -1;
}

namespace {

inline uchar toByte(float v)
{
    return static_cast<uchar>(std::clamp(static_cast<int>(std::lround(v)), 0, 255));
}

inline float luma601(float r, float g, float b)
{
    return 0.299f * r + 0.587f * g + 0.114f * b;
}

// Re-fits an RGB triple (0..255) into gamut while preserving its luma, exactly
// like the GLSL clipColor() used by the blend/adjustment shaders. Used by the
// Color Balance "Preserve Luminosity" pass so the CPU projection matches the
// live GPU frame.
inline void clipColor255(float& r, float& g, float& b)
{
    const float l = luma601(r, g, b);
    const float n = std::min({ r, g, b });
    const float x = std::max({ r, g, b });
    if (n < 0.0f && l - n != 0.0f) {
        r = l + (r - l) * l / (l - n);
        g = l + (g - l) * l / (l - n);
        b = l + (b - l) * l / (l - n);
    }
    if (x > 255.0f && x - l != 0.0f) {
        r = l + (r - l) * (255.0f - l) / (x - l);
        g = l + (g - l) * (255.0f - l) / (x - l);
        b = l + (b - l) * (255.0f - l) / (x - l);
    }
}

// Shifts an RGB triple (0..255) so its luma matches `targetL`, clipping back
// into gamut — the setLum() of the W3C compositing math.
inline void setLuma255(float& r, float& g, float& b, float targetL)
{
    const float d = targetL - luma601(r, g, b);
    r += d; g += d; b += d;
    clipColor255(r, g, b);
}

} // namespace

void apply(QImage& image, const AdjustmentData& data, float opacity,
           const QImage& mask, const QPoint& maskTopLeft, float maskDensity)
{
    if (image.isNull() || opacity <= 0.0f)
        return;
    if (shaderId(data.type) < 0)
        return;

    // Channel byte offsets for the supported 4-byte formats.
    int rIdx, gIdx, bIdx, aIdx;
    switch (image.format()) {
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGB32:
        // Stored as 0xAARRGGBB — little-endian byte order B,G,R,A.
        bIdx = 0; gIdx = 1; rIdx = 2; aIdx = 3;
        break;
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
        rIdx = 0; gIdx = 1; bIdx = 2; aIdx = 3;
        break;
    default:
        image = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        if (image.isNull())
            return;
        bIdx = 0; gIdx = 1; rIdx = 2; aIdx = 3;
        break;
    }

    // Premultiplied content must be un-premultiplied before a non-linear
    // transform (curves) and re-premultiplied after, so semi-transparent edges
    // are colored correctly — exactly what the GPU shader does. Grayscale is a
    // linear combination, so it is correct in either space.
    const bool premultiplied =
        image.format() == QImage::Format_ARGB32_Premultiplied
        || image.format() == QImage::Format_RGBA8888_Premultiplied;

    const bool isCurves = data.type == QLatin1String("curves");
    const bool isColorBalance = data.type == QLatin1String("colorbalance");
    const bool isHueSat = data.type == QLatin1String("huesaturation");
    const bool isSolidColor = data.type == QLatin1String("solidcolor");
    // Curves and Color Balance both resolve to per-channel transfer LUTs; Color
    // Balance adds an optional luma-restoring pass on top (handled per pixel).
    const bool isLutTransform = isCurves || isColorBalance;
    bool preserveLuminosity = false;
    std::array<unsigned char, 256> rLut{}, gLut{}, bLut{};
    if (isLutTransform) {
        if (isCurves)
            buildCurveLuts(data, rLut, gLut, bLut);
        else
            buildColorBalanceLuts(data, rLut, gLut, bLut, preserveLuminosity);
        // Identity transform with no luma restoration: nothing to do.
        bool identity = true;
        for (int v = 0; v < 256 && identity; ++v)
            identity = rLut[v] == v && gLut[v] == v && bLut[v] == v;
        if (identity)
            return;
    }

    // Hue/Saturation can't be a per-channel LUT (band membership depends on the
    // pixel's hue), so it runs the shared per-pixel model directly.
    huesaturation::HueSaturationData hs;
    if (isHueSat) {
        hs = huesaturation::HueSaturationData::fromParams(data.params);
        if (hs.isIdentity())
            return;
    }
    // Solid Color in this (clipped / Single-Layer-Mode) path replaces the parent
    // layer's colour with a constant, keeping the parent's alpha so the fill stays
    // clipped to the layer's shape. The colour's own alpha modulates coverage
    // (folded into `f` below). The stack path is handled separately in the
    // DocumentCompositor / GPU adjustment pass as a real source-over fill.
    float scR = 0.0f, scG = 0.0f, scB = 0.0f, scAlpha = 1.0f;
    if (isSolidColor) {
        const QColor c = solidcolor::SolidColorData::fromParams(data.params).color;
        scR = c.red(); scG = c.green(); scB = c.blue();
        scAlpha = c.alpha() / 255.0f;
    }

    // Both LUT transforms and Hue/Saturation are non-linear, so semi-transparent
    // premultiplied pixels must be un-premultiplied before the transform; Solid
    // Color sets a straight colour and follows the same un/re-premultiply path.
    const bool isColorTransform = isLutTransform || isHueSat || isSolidColor;

    const bool hasMask = !mask.isNull() && mask.format() == QImage::Format_Grayscale8;
    const float density = std::clamp(maskDensity, 0.0f, 1.0f);
    const int w = image.width();
    const int h = image.height();

    for (int y = 0; y < h; ++y) {
        uchar* row = image.scanLine(y);
        const uchar* maskRow = nullptr;
        if (hasMask) {
            const int my = y - maskTopLeft.y();
            if (my >= 0 && my < mask.height())
                maskRow = mask.constScanLine(my);
        }
        for (int x = 0; x < w; ++x) {
            float f = opacity;
            if (hasMask) {
                // Outside the mask buffer counts as white (fully applied),
                // matching the layer-mask sampling convention.
                int m = 255;
                if (maskRow) {
                    const int mx = x - maskTopLeft.x();
                    if (mx >= 0 && mx < mask.width())
                        m = maskRow[mx];
                }
                const float maskAlpha = 1.0f + (m / 255.0f - 1.0f) * density;
                f *= maskAlpha;
            }
            if (isSolidColor)
                f *= scAlpha;   // colour's own alpha modulates the clipped fill
            if (f <= 0.0f)
                continue;

            uchar* px = row + x * 4;
            const float r = px[rIdx], g = px[gIdx], b = px[bIdx];
            float ar, ag, ab;

            if (isColorTransform) {
                const int a = px[aIdx];
                if (premultiplied && a == 0)
                    continue; // fully transparent: no color to adjust
                // Un-premultiply to straight color, run the transform, re-premultiply.
                float sr = r, sg = g, sb = b;
                if (premultiplied && a < 255) {
                    const float inv = 255.0f / static_cast<float>(a);
                    sr = std::min(255.0f, r * inv);
                    sg = std::min(255.0f, g * inv);
                    sb = std::min(255.0f, b * inv);
                }
                if (isSolidColor) {
                    // Constant fill colour (straight); the parent's alpha (a) is
                    // re-applied below so the fill stays clipped to the shape.
                    ar = scR; ag = scG; ab = scB;
                } else if (isHueSat) {
                    // Shared HSL model — straight color in [0,1].
                    float nr = sr / 255.0f, ng = sg / 255.0f, nb = sb / 255.0f;
                    hs.applyPixel(nr, ng, nb);
                    ar = nr * 255.0f; ag = ng * 255.0f; ab = nb * 255.0f;
                } else {
                    ar = rLut[static_cast<int>(sr)];
                    ag = gLut[static_cast<int>(sg)];
                    ab = bLut[static_cast<int>(sb)];
                    // Color Balance can restore the source luma so only the colour
                    // balance shifts, not the brightness (matches the GPU shader).
                    if (preserveLuminosity)
                        setLuma255(ar, ag, ab, luma601(sr, sg, sb));
                }
                if (premultiplied && a < 255) {
                    const float k = static_cast<float>(a) / 255.0f;
                    ar *= k; ag *= k; ab *= k;
                }
            } else {
                // grayscale — linear, premultiplied-safe.
                const float gray = 0.299f * r + 0.587f * g + 0.114f * b;
                ar = ag = ab = gray;
            }

            px[rIdx] = toByte(r + (ar - r) * f);
            px[gIdx] = toByte(g + (ag - g) * f);
            px[bIdx] = toByte(b + (ab - b) * f);
        }
    }
}

} // namespace adjustments
