#include "LayerEffect.hpp"
#include "engine/ImageEngine.hpp"

#include <QColor>
#include <QLinearGradient>
#include <QPainter>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <opencv2/imgproc.hpp>

namespace {
QImage toRgba(const QImage& input)
{
    return input.convertToFormat(QImage::Format_RGBA8888);
}

QColor colorParam(const QVariantMap& params, const QString& key,
                  const QColor& fallback)
{
    const QVariant value = params.value(key);
    if (value.canConvert<QColor>()) {
        const QColor color = value.value<QColor>();
        if (color.isValid()) return color;
    }
    const QColor color(value.toString());
    return color.isValid() ? color : fallback;
}

float floatParam(const QVariantMap& params, const QString& key, float fallback)
{
    return static_cast<float>(params.value(key, fallback).toDouble());
}

int intParam(const QVariantMap& params, const QString& key, int fallback)
{
    return params.value(key, fallback).toInt();
}

QString stringParam(const QVariantMap& params, const QString& key,
                    const QString& fallback)
{
    return params.value(key, fallback).toString();
}

QPainter::CompositionMode painterBlendMode(const QString& mode)
{
    if (mode == QLatin1String("multiply")) return QPainter::CompositionMode_Multiply;
    if (mode == QLatin1String("screen"))   return QPainter::CompositionMode_Screen;
    if (mode == QLatin1String("overlay"))  return QPainter::CompositionMode_Overlay;
    if (mode == QLatin1String("darken"))   return QPainter::CompositionMode_Darken;
    if (mode == QLatin1String("lighten"))  return QPainter::CompositionMode_Lighten;
    return QPainter::CompositionMode_SourceOver;
}

cv::Mat alphaMask(const QImage& image)
{
    QImage rgba = toRgba(image);
    cv::Mat mask(rgba.height(), rgba.width(), CV_8UC1);
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* src = rgba.constScanLine(y);
        uchar* dst = mask.ptr<uchar>(y);
        for (int x = 0; x < rgba.width(); ++x)
            dst[x] = src[x * 4 + 3];
    }
    return mask;
}

cv::Mat dilateMask(const cv::Mat& mask, int amount)
{
    if (amount <= 0) return mask.clone();
    cv::Mat out;
    const int k = amount * 2 + 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    cv::dilate(mask, out, kernel);
    return out;
}

cv::Mat erodeMask(const cv::Mat& mask, int amount)
{
    if (amount <= 0) return mask.clone();
    cv::Mat out;
    const int k = amount * 2 + 1;
    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_ELLIPSE, cv::Size(k, k));
    cv::erode(mask, out, kernel);
    return out;
}

cv::Mat blurMask(const cv::Mat& mask, float radius)
{
    if (radius <= 0.0f) return mask.clone();
    cv::Mat out;
    int k = std::max(1, static_cast<int>(std::ceil(radius)) * 2 + 1);
    if (k % 2 == 0) ++k;
    cv::GaussianBlur(mask, out, cv::Size(k, k), radius * 0.5);
    return out;
}

cv::Mat blurPremultipliedBgra(const cv::Mat& input, float radius)
{
    if (radius <= 0.0f)
        return input.clone();

    cv::Mat src = input;
    if (src.channels() != 4)
        cv::cvtColor(src, src, cv::COLOR_BGR2BGRA);

    int k = std::max(1, static_cast<int>(std::ceil(radius)) * 2 + 1);
    if (k % 2 == 0) ++k;

    cv::Mat srcF;
    src.convertTo(srcF, CV_32FC4, 1.0 / 255.0);

    std::vector<cv::Mat> channels;
    cv::split(srcF, channels);
    for (int c = 0; c < 3; ++c)
        channels[c] = channels[c].mul(channels[3]);

    cv::Mat premultiplied;
    cv::merge(channels, premultiplied);

    cv::Mat blurred;
    cv::GaussianBlur(premultiplied, blurred, cv::Size(k, k), radius);

    for (int y = 0; y < blurred.rows; ++y) {
        auto* row = blurred.ptr<cv::Vec4f>(y);
        for (int x = 0; x < blurred.cols; ++x) {
            cv::Vec4f& px = row[x];
            const float alpha = std::clamp(px[3], 0.0f, 1.0f);
            if (alpha > 0.0001f) {
                px[0] = std::clamp(px[0] / alpha, 0.0f, 1.0f);
                px[1] = std::clamp(px[1] / alpha, 0.0f, 1.0f);
                px[2] = std::clamp(px[2] / alpha, 0.0f, 1.0f);
            } else {
                px[0] = 0.0f;
                px[1] = 0.0f;
                px[2] = 0.0f;
            }
            px[3] = alpha;
        }
    }

    cv::Mat out;
    blurred.convertTo(out, CV_8UC4, 255.0);
    return out;
}

cv::Mat offsetMask(const cv::Mat& mask, int dx, int dy)
{
    cv::Mat out(mask.size(), mask.type(), cv::Scalar(0));
    cv::Mat transform = (cv::Mat_<double>(2, 3) << 1, 0, dx, 0, 1, dy);
    cv::warpAffine(mask, out, transform, mask.size(),
                   cv::INTER_LINEAR, cv::BORDER_CONSTANT, cv::Scalar(0));
    return out;
}

QImage solidFromMask(const cv::Mat& mask, QColor color, float opacity)
{
    QImage out(mask.cols, mask.rows, QImage::Format_RGBA8888);
    const float colorAlpha = color.alphaF();
    opacity = std::clamp(opacity, 0.0f, 1.0f);
    for (int y = 0; y < mask.rows; ++y) {
        uchar* dst = out.scanLine(y);
        const uchar* src = mask.ptr<uchar>(y);
        for (int x = 0; x < mask.cols; ++x) {
            dst[x * 4 + 0] = static_cast<uchar>(color.red());
            dst[x * 4 + 1] = static_cast<uchar>(color.green());
            dst[x * 4 + 2] = static_cast<uchar>(color.blue());
            dst[x * 4 + 3] = static_cast<uchar>(
                std::clamp(src[x] * opacity * colorAlpha, 0.0f, 255.0f));
        }
    }
    return out;
}

QImage sourceOver(const QImage& below, const QImage& above,
                  QPainter::CompositionMode mode = QPainter::CompositionMode_SourceOver,
                  float opacity = 1.0f)
{
    QImage out = toRgba(below);
    QPainter p(&out);
    p.setCompositionMode(mode);
    p.setOpacity(std::clamp(opacity, 0.0f, 1.0f));
    p.drawImage(0, 0, toRgba(above));
    p.end();
    return out;
}

QImage clippedFill(const QImage& input, const QBrush& brush, float opacity,
                   QPainter::CompositionMode mode)
{
    QImage rgba = toRgba(input);
    QImage fill(rgba.size(), QImage::Format_RGBA8888);
    fill.fill(Qt::transparent);
    {
        QPainter p(&fill);
        p.fillRect(fill.rect(), brush);
        p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
        p.drawImage(0, 0, rgba);
    }
    return sourceOver(rgba, fill, mode, opacity);
}

QImage applyDropShadow(const QImage& input, const QVariantMap& params)
{
    QImage base = toRgba(input);
    QColor color = colorParam(params, QStringLiteral("color"), Qt::black);
    float opacity = floatParam(params, QStringLiteral("opacity"), 0.45f);
    float angle = floatParam(params, QStringLiteral("angle"), 120.0f) * float(M_PI) / 180.0f;
    int distance = intParam(params, QStringLiteral("distance"), 12);
    int spread = intParam(params, QStringLiteral("spread"), 0);
    float blur = floatParam(params, QStringLiteral("blur"), 18.0f);
    int dx = static_cast<int>(std::round(std::cos(angle) * distance));
    int dy = static_cast<int>(std::round(-std::sin(angle) * distance));

    cv::Mat mask = blurMask(dilateMask(alphaMask(base), spread), blur);
    mask = offsetMask(mask, dx, dy);
    QImage shadow = solidFromMask(mask, color, opacity);
    return sourceOver(shadow, base);
}

QImage applyOuterGlow(const QImage& input, const QVariantMap& params)
{
    QImage base = toRgba(input);
    QColor color = colorParam(params, QStringLiteral("color"), QColor("#4C8DFF"));
    float opacity = floatParam(params, QStringLiteral("opacity"), 0.5f);
    int spread = intParam(params, QStringLiteral("spread"), 0);
    float blur = floatParam(params, QStringLiteral("blur"), 20.0f);
    cv::Mat mask = blurMask(dilateMask(alphaMask(base), spread), blur);
    QImage glow = solidFromMask(mask, color, opacity);
    return sourceOver(glow, base);
}

QImage applyInnerGlow(const QImage& input, const QVariantMap& params)
{
    QImage base = toRgba(input);
    QColor color = colorParam(params, QStringLiteral("color"), QColor("#FFFFFF"));
    float opacity = floatParam(params, QStringLiteral("opacity"), 0.45f);
    float blur = floatParam(params, QStringLiteral("blur"), 12.0f);
    cv::Mat a = alphaMask(base);
    cv::Mat edge;
    cv::subtract(a, erodeMask(a, std::max(1, static_cast<int>(blur * 0.35f))), edge);
    edge = blurMask(edge, blur);
    cv::min(edge, a, edge);
    QImage glow = solidFromMask(edge, color, opacity);
    return sourceOver(base, glow, QPainter::CompositionMode_Screen);
}

QImage applyInnerShadow(const QImage& input, const QVariantMap& params)
{
    QImage base = toRgba(input);
    QColor color = colorParam(params, QStringLiteral("color"), Qt::black);
    float opacity = floatParam(params, QStringLiteral("opacity"), 0.35f);
    float angle = floatParam(params, QStringLiteral("angle"), 120.0f) * float(M_PI) / 180.0f;
    int distance = intParam(params, QStringLiteral("distance"), 8);
    float blur = floatParam(params, QStringLiteral("blur"), 10.0f);
    int dx = static_cast<int>(std::round(-std::cos(angle) * distance));
    int dy = static_cast<int>(std::round(std::sin(angle) * distance));

    cv::Mat a = alphaMask(base);
    cv::Mat shifted = offsetMask(a, dx, dy);
    cv::Mat inner;
    cv::subtract(a, shifted, inner);
    inner = blurMask(inner, blur);
    cv::min(inner, a, inner);
    QImage shadow = solidFromMask(inner, color, opacity);
    return sourceOver(base, shadow, QPainter::CompositionMode_Multiply);
}

QImage applyStroke(const QImage& input, const QVariantMap& params)
{
    QImage base = toRgba(input);
    QColor color = colorParam(params, QStringLiteral("color"), Qt::white);
    float opacity = floatParam(params, QStringLiteral("opacity"), 1.0f);
    int size = std::max(0, intParam(params, QStringLiteral("size"), 3));
    QString position = stringParam(params, QStringLiteral("position"), QStringLiteral("outside"));
    if (size <= 0) return base;

    cv::Mat a = alphaMask(base);
    cv::Mat mask;
    if (position == QLatin1String("inside")) {
        cv::subtract(a, erodeMask(a, size), mask);
    } else if (position == QLatin1String("center")) {
        cv::subtract(dilateMask(a, size / 2 + 1), erodeMask(a, size / 2 + 1), mask);
    } else {
        cv::subtract(dilateMask(a, size), a, mask);
    }

    QImage stroke = solidFromMask(mask, color, opacity);
    if (position == QLatin1String("outside"))
        return sourceOver(stroke, base);
    return sourceOver(base, stroke);
}

QImage applyColorOverlay(const QImage& input, const QVariantMap& params)
{
    QColor color = colorParam(params, QStringLiteral("color"), QColor("#4C8DFF"));
    float opacity = floatParam(params, QStringLiteral("opacity"), 1.0f);
    QString blend = stringParam(params, QStringLiteral("blendMode"), QStringLiteral("normal"));
    return clippedFill(input, QBrush(color), opacity, painterBlendMode(blend));
}

QImage applyGradientOverlay(const QImage& input, const QVariantMap& params)
{
    QImage base = toRgba(input);
    QColor start = colorParam(params, QStringLiteral("startColor"), QColor("#4C8DFF"));
    QColor end = colorParam(params, QStringLiteral("endColor"), QColor("#35D6FF"));
    float opacity = floatParam(params, QStringLiteral("opacity"), 1.0f);
    float angle = floatParam(params, QStringLiteral("angle"), 90.0f) * float(M_PI) / 180.0f;
    float scale = std::max(0.1f, floatParam(params, QStringLiteral("scale"), 1.0f));
    QPointF c(base.width() * 0.5, base.height() * 0.5);
    QPointF d(std::cos(angle) * base.width() * 0.5 / scale,
              -std::sin(angle) * base.height() * 0.5 / scale);
    QLinearGradient grad(c - d, c + d);
    grad.setColorAt(0.0, start);
    grad.setColorAt(1.0, end);
    QString blend = stringParam(params, QStringLiteral("blendMode"), QStringLiteral("normal"));
    return clippedFill(base, QBrush(grad), opacity, painterBlendMode(blend));
}
} // namespace

QImage LayerEffect::apply(const QImage& input) const
{
    if (!enabled)
        return input;
    return applyEffect(input, type, params);
}

QImage LayerEffect::applyEffect(const QImage& input, const QString& type,
                                 const QVariantMap& params)
{
    if (type == QLatin1String("drop_shadow"))      return applyDropShadow(input, params);
    if (type == QLatin1String("inner_shadow"))     return applyInnerShadow(input, params);
    if (type == QLatin1String("stroke"))           return applyStroke(input, params);
    if (type == QLatin1String("color_overlay"))    return applyColorOverlay(input, params);
    if (type == QLatin1String("gradient_overlay")) return applyGradientOverlay(input, params);
    if (type == QLatin1String("outer_glow"))       return applyOuterGlow(input, params);
    if (type == QLatin1String("inner_glow"))       return applyInnerGlow(input, params);

    cv::Mat cvImg = ImageEngine::toCvMat(input);
    cv::Mat result;

    auto get = [&](const QString& key, double def = 0.0) -> double {
        return params.value(key, def).toDouble();
    };

    auto getInt = [&](const QString& key, int def = 0) -> int {
        return params.value(key, def).toInt();
    };

    auto getBool = [&](const QString& key, bool def = false) -> bool {
        return params.value(key, def).toBool();
    };

    if (type == "adjust_color") {
        float b = static_cast<float>(get("brightness", 0.0));
        float c = static_cast<float>(get("contrast", 0.0));
        float s = static_cast<float>(get("saturation", 0.0));
        float h = static_cast<float>(get("hue", 0.0));
        bool ac = getBool("auto_contrast", false);

        result = cvImg.clone();
        if (b != 0.0f) result = ImageEngine::adjustBrightness(result, b);
        if (c != 0.0f) result = ImageEngine::adjustContrast(result, c);
        if (s != 0.0f) result = ImageEngine::adjustSaturation(result, s);
        if (h != 0.0f) result = ImageEngine::adjustHue(result, h);
        if (ac) result = ImageEngine::autoContrast(result);
    }
    else if (type == "adjust_brightness")
        result = ImageEngine::adjustBrightness(cvImg, static_cast<float>(get("value", 0.0)));
    else if (type == "adjust_contrast")
        result = ImageEngine::adjustContrast(cvImg, static_cast<float>(get("value", 0.0)));
    else if (type == "adjust_saturation")
        result = ImageEngine::adjustSaturation(cvImg, static_cast<float>(get("value", 0.0)));
    else if (type == "adjust_hue")
        result = ImageEngine::adjustHue(cvImg, static_cast<float>(get("value", 0.0)));
    else if (type == "gaussian_blur")
        result = blurPremultipliedBgra(cvImg, static_cast<float>(get("radius", 3.0)));
    else if (type == "sharpen")
        result = ImageEngine::sharpen(cvImg, static_cast<float>(get("strength", 1.0)));
    else if (type == "median_blur")
        result = ImageEngine::medianBlur(cvImg, getInt("kernel_size", 5));
    else if (type == "edge_detect")
        result = ImageEngine::edgeDetect(cvImg,
            static_cast<float>(get("threshold1", 50.0)),
            static_cast<float>(get("threshold2", 150.0)));
    else if (type == "grayscale")
        result = ImageEngine::grayscale(cvImg);
    else if (type == "invert_colors")
        result = ImageEngine::invertColors(cvImg);
    else if (type == "auto_contrast")
        result = ImageEngine::autoContrast(cvImg);
    else if (type == "noise_reduce")
        result = ImageEngine::noiseReduce(cvImg, static_cast<float>(get("strength", 2.0)));
    else if (type == "posterize")
        result = ImageEngine::posterize(cvImg, getInt("levels", 8));
    else if (type == "threshold")
        result = ImageEngine::threshold(cvImg, get("value", 128.0));
    else if (type == "remove_background")
        result = ImageEngine::removeBackground(cvImg);
    else
        return input;

    return result.empty() ? input : ImageEngine::toQImage(result);
}

bool LayerEffect::isLayerStyle(const QString& type)
{
    return type == QLatin1String("drop_shadow")
        || type == QLatin1String("inner_shadow")
        || type == QLatin1String("stroke")
        || type == QLatin1String("color_overlay")
        || type == QLatin1String("gradient_overlay")
        || type == QLatin1String("outer_glow")
        || type == QLatin1String("inner_glow");
}

QString LayerEffect::displayName(const QString& type)
{
    if (type == QLatin1String("drop_shadow")) return QStringLiteral("Drop Shadow");
    if (type == QLatin1String("inner_shadow")) return QStringLiteral("Inner Shadow");
    if (type == QLatin1String("stroke")) return QStringLiteral("Stroke");
    if (type == QLatin1String("color_overlay")) return QStringLiteral("Color Overlay");
    if (type == QLatin1String("gradient_overlay")) return QStringLiteral("Gradient Overlay");
    if (type == QLatin1String("outer_glow")) return QStringLiteral("Outer Glow");
    if (type == QLatin1String("inner_glow")) return QStringLiteral("Inner Glow");
    return type;
}

QVariantMap LayerEffect::defaultStyleParams(const QString& type)
{
    QVariantMap p;
    if (type == QLatin1String("drop_shadow")) {
        p["color"] = QColor(Qt::black);
        p["opacity"] = 0.45;
        p["angle"] = -70.0;
        p["distance"] = 12;
        p["spread"] = 0;
        p["blur"] = 18.0;
        p["blendMode"] = QStringLiteral("multiply");
    } else if (type == QLatin1String("inner_shadow")) {
        p["color"] = QColor(Qt::black);
        p["opacity"] = 0.35;
        p["angle"] = 120.0;
        p["distance"] = 8;
        p["blur"] = 10.0;
        p["blendMode"] = QStringLiteral("multiply");
    } else if (type == QLatin1String("stroke")) {
        p["color"] = QColor(Qt::black);
        p["opacity"] = 1.0;
        p["size"] = 3;
        p["position"] = QStringLiteral("outside");
        p["blendMode"] = QStringLiteral("normal");
    } else if (type == QLatin1String("color_overlay")) {
        p["color"] = QColor("#4C8DFF");
        p["opacity"] = 1.0;
        p["blendMode"] = QStringLiteral("normal");
    } else if (type == QLatin1String("gradient_overlay")) {
        p["startColor"] = QColor("#4C8DFF");
        p["endColor"] = QColor("#35D6FF");
        p["opacity"] = 1.0;
        p["angle"] = 90.0;
        p["scale"] = 1.0;
        p["blendMode"] = QStringLiteral("normal");
    } else if (type == QLatin1String("outer_glow")) {
        p["color"] = QColor("#4C8DFF");
        p["opacity"] = 0.5;
        p["spread"] = 0;
        p["blur"] = 20.0;
        p["blendMode"] = QStringLiteral("screen");
    } else if (type == QLatin1String("inner_glow")) {
        p["color"] = QColor(Qt::white);
        p["opacity"] = 0.45;
        p["blur"] = 12.0;
        p["blendMode"] = QStringLiteral("screen");
    }
    return p;
}

QMargins LayerEffect::stylePadding(const std::vector<LayerEffect>& effects)
{
    QMargins padding;

    for (const auto& effect : effects) {
        if (!effect.enabled)
            continue;

        const QVariantMap& p = effect.params;
        QMargins extra;
        if (effect.type == QLatin1String("drop_shadow")) {
            const float angle = floatParam(p, QStringLiteral("angle"), 120.0f)
                * float(M_PI) / 180.0f;
            const int distance = intParam(p, QStringLiteral("distance"), 12);
            const int spread = intParam(p, QStringLiteral("spread"), 0);
            const int blur = static_cast<int>(
                std::ceil(floatParam(p, QStringLiteral("blur"), 18.0f)));
            const int dx = static_cast<int>(std::round(std::cos(angle) * distance));
            const int dy = static_cast<int>(std::round(-std::sin(angle) * distance));
            const int soft = blur + spread + 2;
            extra = QMargins(soft + std::max(0, -dx),
                             soft + std::max(0, -dy),
                             soft + std::max(0, dx),
                             soft + std::max(0, dy));
        } else if (effect.type == QLatin1String("outer_glow")) {
            const int spread = intParam(p, QStringLiteral("spread"), 0);
            const int blur = static_cast<int>(
                std::ceil(floatParam(p, QStringLiteral("blur"), 20.0f)));
            const int soft = blur + spread + 2;
            extra = QMargins(soft, soft, soft, soft);
        } else if (effect.type == QLatin1String("stroke")) {
            const int size = std::max(0, intParam(p, QStringLiteral("size"), 3));
            const QString position = stringParam(p, QStringLiteral("position"),
                                                 QStringLiteral("outside"));
            int amount = 0;
            if (position == QLatin1String("outside"))
                amount = size + 1;
            else if (position == QLatin1String("center"))
                amount = static_cast<int>(std::ceil(size * 0.5)) + 1;
            extra = QMargins(amount, amount, amount, amount);
        }

        // Effects are applied as a stack. Later effects can see pixels created
        // by earlier effects, so summing keeps combinations like Stroke +
        // Drop Shadow from clipping.
        padding += extra;
    }

    return padding;
}

QMargins LayerEffect::effectPadding(const std::vector<LayerEffect>& effects)
{
    QMargins padding = stylePadding(effects);

    for (const auto& effect : effects) {
        if (!effect.enabled)
            continue;

        // Soft filters now blur the alpha channel too (premultiplied), so the
        // shape grows past its tight raster/dab bounds — pad the source by the
        // filter's reach to keep that growth from being clipped.
        int soft = 0;
        if (effect.type == QLatin1String("gaussian_blur")) {
            const int blur = static_cast<int>(
                std::ceil(floatParam(effect.params, QStringLiteral("radius"), 3.0f)));
            soft = std::max(0, blur) + 2;
        } else if (effect.type == QLatin1String("median_blur")) {
            const int k = intParam(effect.params, QStringLiteral("kernel_size"), 5);
            soft = std::max(0, k / 2) + 1;
        }
        if (soft > 0)
            padding += QMargins(soft, soft, soft, soft);
    }

    return padding;
}

LayerEffect::EffectCategory LayerEffect::effectCategory(const QString& type)
{
    static const QSet<QString> styles = {
        QStringLiteral("drop_shadow"), QStringLiteral("inner_shadow"),
        QStringLiteral("stroke"),      QStringLiteral("color_overlay"),
        QStringLiteral("gradient_overlay"), QStringLiteral("outer_glow"),
        QStringLiteral("inner_glow")
    };
    static const QSet<QString> filters = {
        QStringLiteral("gaussian_blur"),  QStringLiteral("sharpen"),
        QStringLiteral("median_blur"),    QStringLiteral("edge_detect"),
        QStringLiteral("noise_reduce"),   QStringLiteral("grayscale"),
        QStringLiteral("invert_colors"),  QStringLiteral("posterize"),
        QStringLiteral("threshold"),      QStringLiteral("remove_background")
    };
    if (styles.contains(type))  return EffectCategory::LayerStyle;
    if (filters.contains(type)) return EffectCategory::ImageFilter;
    return EffectCategory::Adjustment;
}
