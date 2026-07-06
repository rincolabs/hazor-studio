#pragma once

#include "color/ColorEngine.hpp"

class AdvancedIccColorEngine final : public ColorConversionEngine {
public:
    QString name() const override { return QStringLiteral("Advanced ICC Color Engine"); }

    bool canConvert(const ColorProfile& source,
                    const ColorProfile& destination) const override
    {
        Q_UNUSED(source);
        Q_UNUSED(destination);
        return false;
    }

    QImage convertImage(const QImage& sourcePixels,
                        const ColorProfile& sourceProfile,
                        const ColorProfile& destinationProfile,
                        const ColorConversionOptions& options) override
    {
        Q_UNUSED(sourceProfile);
        Q_UNUSED(destinationProfile);
        Q_UNUSED(options);
        return sourcePixels;
    }

    QColor convertColor(const QColor& color,
                        const ColorProfile& sourceProfile,
                        const ColorProfile& destinationProfile,
                        const ColorConversionOptions& options) override
    {
        Q_UNUSED(sourceProfile);
        Q_UNUSED(destinationProfile);
        Q_UNUSED(options);
        return color;
    }
};
