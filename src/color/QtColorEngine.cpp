#include "color/QtColorEngine.hpp"

QString QtColorEngine::name() const
{
    return QStringLiteral("Qt Color Engine");
}

bool QtColorEngine::canConvert(const ColorProfile& source,
                               const ColorProfile& destination) const
{
    return source.isValid()
        && destination.isValid()
        && source.toQColorSpace().isValid()
        && destination.toQColorSpace().isValid();
}

QImage QtColorEngine::convertImage(const QImage& sourcePixels,
                                   const ColorProfile& sourceProfile,
                                   const ColorProfile& destinationProfile,
                                   const ColorConversionOptions& options)
{
    Q_UNUSED(options);

    if (sourcePixels.isNull())
        return {};

    QImage image = sourcePixels.copy();
    if (sourceProfile.equivalentTo(destinationProfile)) {
        image.setColorSpace(destinationProfile.toQColorSpace());
        return image;
    }

    if (!canConvert(sourceProfile, destinationProfile))
        return {};

    image.setColorSpace(sourceProfile.toQColorSpace());
    return image.convertedToColorSpace(destinationProfile.toQColorSpace());
}

QColor QtColorEngine::convertColor(const QColor& color,
                                   const ColorProfile& sourceProfile,
                                   const ColorProfile& destinationProfile,
                                   const ColorConversionOptions& options)
{
    if (!canConvert(sourceProfile, destinationProfile)
        || sourceProfile.equivalentTo(destinationProfile)) {
        return color;
    }

    QImage pixel(1, 1, QImage::Format_RGBA8888);
    pixel.fill(color);
    pixel.setColorSpace(sourceProfile.toQColorSpace());

    const QImage converted = convertImage(pixel, sourceProfile, destinationProfile, options);
    return converted.isNull() ? color : converted.pixelColor(0, 0);
}
