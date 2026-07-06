#pragma once

#include <QColor>
#include <QVariantMap>

#include <algorithm>

// Reusable model for a Solid Color fill/adjustment layer. Unlike Curves /
// Color Balance / Hue-Saturation, a Solid Color layer does not transform the
// pixels below it: it generates a single RGBA colour on demand that is
// composited over the stack with the node's own blend mode, opacity and mask
// (in the stack) or clipped to the parent layer's alpha (Single Layer Mode).
//
// The model is deliberately decoupled from any widget: SolidColorAdjustmentWidget
// edits it, AdjustmentTypes::apply() bakes it for the clipped path, and the
// DocumentCompositor / GPU adjustment pass read it for the stack path — so the
// live preview, the cached projection and the export stay in sync.
//
// Alpha is preserved end-to-end even though the current internal colour picker
// only edits opaque colours (it always reports alpha 255). Storing RGBA keeps
// the format forward-compatible with an alpha-aware picker without breaking
// existing projects.
namespace solidcolor {

struct SolidColorData {
    QColor color = QColor(255, 255, 255, 255); // default: opaque white

    SolidColorData() = default;
    explicit SolidColorData(const QColor& c) : color(c) {}

    static SolidColorData fromParams(const QVariantMap& params)
    {
        SolidColorData d;
        const int r = std::clamp(params.value(QStringLiteral("r"), 255).toInt(), 0, 255);
        const int g = std::clamp(params.value(QStringLiteral("g"), 255).toInt(), 0, 255);
        const int b = std::clamp(params.value(QStringLiteral("b"), 255).toInt(), 0, 255);
        const int a = std::clamp(params.value(QStringLiteral("a"), 255).toInt(), 0, 255);
        d.color = QColor(r, g, b, a);
        return d;
    }

    QVariantMap toParams() const
    {
        QVariantMap m;
        m[QStringLiteral("r")] = color.red();
        m[QStringLiteral("g")] = color.green();
        m[QStringLiteral("b")] = color.blue();
        m[QStringLiteral("a")] = color.alpha();
        return m;
    }

    static QVariantMap paramsFromColor(const QColor& c)
    {
        return SolidColorData(c).toParams();
    }
};

} // namespace solidcolor
