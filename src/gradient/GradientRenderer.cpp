#include "GradientRenderer.hpp"

#include "renderer/BlendRules.hpp"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace {
constexpr double kPi = 3.14159265358979323846;

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double srgbToLinear(double value)
{
    value = clamp01(value);
    if (value <= 0.04045)
        return value / 12.92;
    return std::pow((value + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double value)
{
    value = clamp01(value);
    if (value <= 0.0031308)
        return value * 12.92;
    return 1.055 * std::pow(value, 1.0 / 2.4) - 0.055;
}

double smoothStep(double value)
{
    value = clamp01(value);
    return value * value * (3.0 - 2.0 * value);
}

double midpointProgress(double local, double midpoint)
{
    local = clamp01(local);
    midpoint = std::clamp(midpoint, 0.001, 0.999);
    if (local <= midpoint)
        return 0.5 * local / midpoint;
    return 0.5 + 0.5 * (local - midpoint) / (1.0 - midpoint);
}

template <typename Stop>
int segmentFor(const QVector<Stop>& stops, double position)
{
    if (position <= stops.first().position)
        return 0;
    for (int i = 0; i < stops.size() - 1; ++i) {
        if (position >= stops[i].position && position <= stops[i + 1].position)
            return i;
    }
    return std::max(0, static_cast<int>(stops.size()) - 2);
}

double maskAlphaAt(const QImage& mask, int x, int y, const QSize& targetSize)
{
    if (mask.isNull())
        return 1.0;

    int sx = x;
    int sy = y;
    if (mask.size() != targetSize) {
        sx = std::clamp(static_cast<int>(std::floor(
                            (static_cast<double>(x) + 0.5)
                            * static_cast<double>(mask.width())
                            / std::max(1, targetSize.width()))),
                        0, std::max(0, mask.width() - 1));
        sy = std::clamp(static_cast<int>(std::floor(
                            (static_cast<double>(y) + 0.5)
                            * static_cast<double>(mask.height())
                            / std::max(1, targetSize.height()))),
                        0, std::max(0, mask.height() - 1));
    }

    return qGray(mask.pixel(sx, sy)) / 255.0;
}

double ditherNoise(int x, int y)
{
    quint32 n = static_cast<quint32>(x) * 1973u
              + static_cast<quint32>(y) * 9277u
              + 0x68bc21ebu;
    n ^= n << 13;
    n ^= n >> 17;
    n ^= n << 5;
    return (static_cast<double>(n & 255u) / 255.0 - 0.5) / 255.0;
}

int toByte(double value, bool dither, int x, int y)
{
    if (dither)
        value += ditherNoise(x, y);
    return std::clamp(static_cast<int>(std::round(clamp01(value) * 255.0)), 0, 255);
}

double snapToColorStop(const GradientDefinition& definition,
                       double position,
                       double tolerance,
                       bool* snapped)
{
    for (const auto& stop : definition.colorStops) {
        const double visualPosition = definition.reverse ? 1.0 - stop.position : stop.position;
        if (std::abs(position - visualPosition) <= tolerance) {
            if (snapped)
                *snapped = true;
            return visualPosition;
        }
    }
    if (snapped)
        *snapped = false;
    return position;
}
}

QColor GradientRenderer::sampleGradientAt(const GradientDefinition& sourceDefinition,
                                          double position)
{
    GradientDefinition definition = sourceDefinition;
    definition.normalize();

    position = clamp01(definition.reverse ? 1.0 - position : position);
    const int colorSegment = segmentFor(definition.colorStops, position);
    const auto& c0 = definition.colorStops[colorSegment];
    const auto& c1 = definition.colorStops[colorSegment + 1];
    const double colorSpan = std::max(1e-9, c1.position - c0.position);
    double colorT = midpointProgress((position - c0.position) / colorSpan, c0.midpoint);
    colorT = colorT + (smoothStep(colorT) - colorT) * definition.smoothness;

    const int opacitySegment = segmentFor(definition.opacityStops, position);
    const auto& o0 = definition.opacityStops[opacitySegment];
    const auto& o1 = definition.opacityStops[opacitySegment + 1];
    const double opacitySpan = std::max(1e-9, o1.position - o0.position);
    double opacityT = midpointProgress((position - o0.position) / opacitySpan, o0.midpoint);
    opacityT = opacityT + (smoothStep(opacityT) - opacityT) * definition.smoothness;

    auto interpolateClassic = [colorT](double a, double b) {
        return a + (b - a) * colorT;
    };
    auto interpolateLinear = [colorT](double a, double b) {
        return linearToSrgb(srgbToLinear(a) + (srgbToLinear(b) - srgbToLinear(a)) * colorT);
    };

    QColor result;
    if (definition.interpolation == GradientInterpolationMethod::Classic) {
        result.setRgbF(interpolateClassic(c0.color.redF(), c1.color.redF()),
                       interpolateClassic(c0.color.greenF(), c1.color.greenF()),
                       interpolateClassic(c0.color.blueF(), c1.color.blueF()));
    } else {
        const double perceptualT = definition.interpolation == GradientInterpolationMethod::Perceptual
            ? smoothStep(colorT)
            : colorT;
        auto perceptual = [perceptualT](double a, double b) {
            return linearToSrgb(srgbToLinear(a) + (srgbToLinear(b) - srgbToLinear(a)) * perceptualT);
        };
        result.setRgbF(perceptual(c0.color.redF(), c1.color.redF()),
                       perceptual(c0.color.greenF(), c1.color.greenF()),
                       perceptual(c0.color.blueF(), c1.color.blueF()));
    }

    const double alpha = definition.transparency
        ? o0.opacity + (o1.opacity - o0.opacity) * opacityT
        : 1.0;
    result.setAlphaF(clamp01(alpha));
    return result;
}

double GradientRenderer::positionForPoint(const GradientDefinition& definition,
                                          QPointF point,
                                          QPointF start,
                                          QPointF end)
{
    const QPointF vector = end - start;
    const double len2 = vector.x() * vector.x() + vector.y() * vector.y();
    const double len = std::sqrt(len2);
    if (len < 1e-6)
        return 0.0;

    const QPointF rel = point - start;
    const double ux = vector.x() / len;
    const double uy = vector.y() / len;
    const double along = rel.x() * ux + rel.y() * uy;
    const double across = -rel.x() * uy + rel.y() * ux;

    switch (definition.kind) {
    case GradientKind::Linear:
        return clamp01(along / len);
    case GradientKind::Radial:
        return clamp01(std::sqrt(rel.x() * rel.x() + rel.y() * rel.y()) / len);
    case GradientKind::Angle: {
        double base = std::atan2(vector.y(), vector.x());
        double angle = std::atan2(rel.y(), rel.x()) - base;
        while (angle < 0.0)
            angle += 2.0 * kPi;
        while (angle >= 2.0 * kPi)
            angle -= 2.0 * kPi;
        return angle / (2.0 * kPi);
    }
    case GradientKind::Reflected:
        return clamp01(std::abs(along) / len);
    case GradientKind::Diamond:
        return clamp01((std::abs(along) + std::abs(across)) / len);
    }
    return 0.0;
}

QImage GradientRenderer::renderGradientToImage(const GradientRenderRequest& request)
{
    if (request.targetSize.width() <= 0 || request.targetSize.height() <= 0)
        return QImage();

    GradientDefinition definition = request.definition;
    definition.normalize();

    QImage image(request.targetSize, QImage::Format_RGBA8888);
    const double toolOpacity = clamp01(request.opacity);
    const double gradientLength = std::hypot(request.endPoint.x() - request.startPoint.x(),
                                            request.endPoint.y() - request.startPoint.y());
    const double stopSnapTolerance = 0.5 / std::max(1.0, gradientLength);

    for (int y = 0; y < image.height(); ++y) {
        auto* row = image.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const double t = positionForPoint(definition, QPointF(x + 0.5, y + 0.5),
                                              request.startPoint, request.endPoint);
            bool snappedToStop = false;
            const double samplePosition = snapToColorStop(definition, t, stopSnapTolerance, &snappedToStop);
            const QColor color = sampleGradientAt(definition, samplePosition);
            const double mask = maskAlphaAt(request.selectionMask, x, y, request.targetSize);
            const bool dither = definition.dither && !snappedToStop;
            const int r = toByte(color.redF(), dither, x, y);
            const int g = toByte(color.greenF(), dither, x + 17, y);
            const int b = toByte(color.blueF(), dither, x, y + 29);
            const int a = toByte(color.alphaF() * toolOpacity * mask, false, x, y);
            auto* pixel = row + x * 4;
            pixel[0] = static_cast<uchar>(r);
            pixel[1] = static_cast<uchar>(g);
            pixel[2] = static_cast<uchar>(b);
            pixel[3] = static_cast<uchar>(a);
        }
    }

    return image;
}

QImage GradientRenderer::compositeGradient(const GradientRenderRequest& request)
{
    if (request.baseImage.isNull())
        return renderGradientToImage(request);

    GradientRenderRequest adjusted = request;
    adjusted.targetSize = request.baseImage.size();

    QImage base = request.baseImage.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    const QImage source = renderGradientToImage(adjusted)
        .convertToFormat(QImage::Format_ARGB32_Premultiplied);
    if (source.isNull())
        return request.baseImage;

    QPainter painter(&base);
    painter.setCompositionMode(blend::painterHasNativeMode(request.blendMode)
        ? blend::painterMode(request.blendMode)
        : QPainter::CompositionMode_SourceOver);
    painter.drawImage(0, 0, source);
    painter.end();

    QImage result = base.convertToFormat(QImage::Format_RGBA8888);
    if (request.lockAlpha) {
        const QImage original = request.baseImage.convertToFormat(QImage::Format_RGBA8888);
        for (int y = 0; y < result.height(); ++y) {
            auto* dst = result.scanLine(y);
            const auto* src = original.constScanLine(y);
            for (int x = 0; x < result.width(); ++x)
                dst[x * 4 + 3] = src[x * 4 + 3];
        }
    }

    return result;
}

QImage GradientRenderer::generateThumbnail(const GradientDefinition& definition,
                                           const QSize& size)
{
    GradientRenderRequest request;
    request.definition = definition;
    request.targetSize = size;
    request.startPoint = QPointF(0.0, size.height() * 0.5);
    request.endPoint = QPointF(std::max(1, size.width()) - 1.0, size.height() * 0.5);
    request.opacity = 1.0;
    return renderGradientToImage(request);
}
