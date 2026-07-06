#pragma once

#include "color/ColorProfile.hpp"

#include <QColor>
#include <QImage>
#include <QString>

class ColorConversionEngine {
public:
    virtual ~ColorConversionEngine() = default;

    virtual QString name() const = 0;

    virtual bool canConvert(const ColorProfile& source,
                            const ColorProfile& destination) const = 0;

    virtual QImage convertImage(const QImage& sourcePixels,
                                const ColorProfile& sourceProfile,
                                const ColorProfile& destinationProfile,
                                const ColorConversionOptions& options) = 0;

    virtual QColor convertColor(const QColor& color,
                                const ColorProfile& sourceProfile,
                                const ColorProfile& destinationProfile,
                                const ColorConversionOptions& options) = 0;
};
