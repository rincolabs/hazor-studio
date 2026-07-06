#pragma once

#include "color/ColorEngine.hpp"

class QtColorEngine final : public ColorConversionEngine {
public:
    QString name() const override;

    bool canConvert(const ColorProfile& source,
                    const ColorProfile& destination) const override;

    QImage convertImage(const QImage& sourcePixels,
                        const ColorProfile& sourceProfile,
                        const ColorProfile& destinationProfile,
                        const ColorConversionOptions& options) override;

    QColor convertColor(const QColor& color,
                        const ColorProfile& sourceProfile,
                        const ColorProfile& destinationProfile,
                        const ColorConversionOptions& options) override;
};
