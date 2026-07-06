#pragma once

#include <QString>
#include <QVariantMap>
#include <QImage>
#include <QColor>
#include <QMargins>
#include <vector>

class LayerEffect {
public:
    enum class EffectCategory {
        LayerStyle,  // drop_shadow, inner_shadow, stroke, glow, overlay
        ImageFilter, // gaussian_blur, sharpen, median_blur, edge_detect, noise_reduce, ...
        Adjustment   // brightness, contrast, saturation, hue, auto_contrast
    };

    QString type;
    QVariantMap params;
    QVariantMap defaultParams;
    bool enabled = true;

    LayerEffect() = default;

    LayerEffect(const QString& type, const QVariantMap& params,
                const QVariantMap& defaults = {})
        : type(type), params(params), defaultParams(defaults)
    {}

    QImage apply(const QImage& input) const;

    static QImage applyEffect(const QImage& input, const QString& type,
                               const QVariantMap& params);
    static bool isLayerStyle(const QString& type);
    static QString displayName(const QString& type);
    static QVariantMap defaultStyleParams(const QString& type);
    static QMargins stylePadding(const std::vector<LayerEffect>& effects);
    static QMargins effectPadding(const std::vector<LayerEffect>& effects);
    static EffectCategory effectCategory(const QString& type);
};
