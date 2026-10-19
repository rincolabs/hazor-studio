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

    // Two-input form used when Fill Opacity has separated the layer's content
    // from its silhouette: `content` is what the style composites over (already
    // faded by Fill), `shape` the full-strength image the style's geometry and
    // alpha are generated from. apply() is this with both halves equal, which
    // is the Fill-100% case and therefore every pre-existing caller.
    QImage applyOver(const QImage& content, const QImage& shape) const;

    static QImage applyEffect(const QImage& input, const QString& type,
                               const QVariantMap& params);
    static QImage applyEffectOver(const QImage& content, const QImage& shape,
                                   const QString& type, const QVariantMap& params);

    // Multiplies a straight-alpha image's alpha by `factor`, leaving colour
    // alone — how Fill Opacity fades a layer's own content inside the style
    // bake (LayerTreeNode::applyEffectStack). Returns the input unchanged at
    // factor 1, so the default path copies nothing.
    static QImage scaleAlpha(const QImage& image, float factor);
    static bool isLayerStyle(const QString& type);
    static QString displayName(const QString& type);
    static QVariantMap defaultStyleParams(const QString& type);
    static QMargins stylePadding(const std::vector<LayerEffect>& effects);
    static QMargins effectPadding(const std::vector<LayerEffect>& effects);
    static EffectCategory effectCategory(const QString& type);
};
