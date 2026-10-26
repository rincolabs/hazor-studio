// Layer styles (`lfx2`/`lmfx`, groups' `lfxs`) -> Hazor's LayerEffect list.
// Field names follow the documented `Lefx` (layer effects) descriptor
// layout used by the PSD/PSB format.
//
// Hazor's layer-style vocabulary is drop_shadow, inner_shadow, outer_glow,
// inner_glow, stroke, color_overlay and gradient_overlay (see
// LayerEffect.cpp): Bevel & Emboss and Satin have no Hazor equivalent and
// are reported instead of silently dropped.
//
// Two asymmetries in Hazor's own effect renderer shape what is worth writing
// into the params map (see LayerEffect.cpp's apply* functions):
//   * only color_overlay and gradient_overlay read `blendMode`; the shadows,
//     glows and stroke composite with a fixed mode, so a non-default PSD mode
//     for those is reported instead of stored as a value nothing would read.
//   * drop_shadow's `angle` is the direction the shadow is OFFSET towards
//     (dx = cos a, dy = -sin a), while inner_shadow's is the direction the
//     LIGHT comes from (dx = -cos a, dy = sin a). PSD stores the light angle
//     in `lagl` for both, so only the drop shadow needs the 180 degree flip.

#include "PsdImportService_Internal.hpp"

#include "core/LayerEffect.hpp"
#include "psd/Descriptor.hpp"
#include "psd/Layer.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace psdimport {

namespace {

double numOf(const psd::Descriptor& d, const char* key, double fallback)
{
    const psd::Value* v = d.get(key);
    if (!v) return fallback;
    switch (v->kind()) {
    case psd::Value::Kind::Double: return v->asDouble();
    case psd::Value::Kind::Integer: return static_cast<double>(v->asInteger());
    case psd::Value::Kind::UnitFloat: return v->asUnitFloat().value;
    default: return fallback;
    }
}
float pctOf(const psd::Descriptor& d, const char* key, float fallbackFraction)
{
    return static_cast<float>(numOf(d, key, static_cast<double>(fallbackFraction) * 100.0) / 100.0);
}
bool boolOf(const psd::Descriptor& d, const char* key, bool fallback)
{
    const psd::Value* v = d.get(key);
    return v && v->kind() == psd::Value::Kind::Boolean ? v->asBoolean() : fallback;
}
bool enabledOf(const psd::Descriptor& d)
{
    const psd::Value* v = d.get("enab");
    return !(v && v->kind() == psd::Value::Kind::Boolean && !v->asBoolean());
}
QColor colorOf(const psd::Descriptor& d, const char* key)
{
    const psd::Value* v = d.get(key);
    return (v && v->kind() == psd::Value::Kind::DescriptorKind) ? colorFromDescriptor(v->asDescriptor()) : QColor(Qt::black);
}
// The PSD mode key of an effect, as the four raw bytes ("Mltp", "Nrml", ...);
// empty when the descriptor does not record one.
QString modeKeyOf(const psd::Descriptor& d)
{
    const psd::Value* v = d.get("Md  ");
    if (!v || v->kind() != psd::Value::Kind::Enumerated)
        return QString();
    QString out;
    for (uint8_t c : v->asEnumerated().value.asBytes())
        out.append(QChar(static_cast<ushort>(c)));
    return out.trimmed();
}

// The effect blend mode as one of the five names Hazor's effect renderer
// understands (painterBlendMode in LayerEffect.cpp). Returns nullopt when the
// file asks for a mode that has no QPainter composition mode, so the caller
// can report it rather than quietly compositing Normal.
std::optional<QString> blendModeOf(const psd::Descriptor& d)
{
    const psd::Value* v = d.get("Md  ");
    if (!v || v->kind() != psd::Value::Kind::Enumerated)
        return QStringLiteral("normal");
    const auto& e = v->asEnumerated();
    if (e.value.is("Nrml")) return QStringLiteral("normal");
    if (e.value.is("Mltp")) return QStringLiteral("multiply");
    if (e.value.is("Scrn")) return QStringLiteral("screen");
    if (e.value.is("Ovrl")) return QStringLiteral("overlay");
    if (e.value.is("Drkn")) return QStringLiteral("darken");
    if (e.value.is("Lghn")) return QStringLiteral("lighten");
    return std::nullopt;
}

// The mode Hazor's renderer composites each effect with no matter what the
// params say — the baseline a PSD mode has to match to be reproduced.
const char* fixedRenderedMode(const QString& hazorType)
{
    if (hazorType == QLatin1String("drop_shadow")) return "normal";
    if (hazorType == QLatin1String("outer_glow")) return "normal";
    if (hazorType == QLatin1String("inner_shadow")) return "multiply";
    if (hazorType == QLatin1String("inner_glow")) return "screen";
    if (hazorType == QLatin1String("stroke")) return "normal";
    return nullptr; // color_overlay / gradient_overlay honour the param
}

const psd::Descriptor* nestedOf(const psd::Descriptor& d, const char* key)
{
    const psd::Value* v = d.get(key);
    return v && v->kind() == psd::Value::Kind::DescriptorKind ? &v->asDescriptor() : nullptr;
}

// First/last colour stop of a "Grad" gradient descriptor (location 0..4096),
// already swapped when the style asks for a reversed ramp. Hazor's
// gradient_overlay only supports a straight two-colour ramp.
std::pair<QColor, QColor> gradientEndpoints(const psd::Descriptor& d, bool& hadMoreStops)
{
    QColor start = QColor(0, 0, 0);
    QColor end = QColor(255, 255, 255);
    hadMoreStops = false;
    const bool reversed = boolOf(d, "Rvrs", false);
    auto finish = [&]() -> std::pair<QColor, QColor> {
        return reversed ? std::pair<QColor, QColor>{end, start} : std::pair<QColor, QColor>{start, end};
    };
    const psd::Descriptor* grad = nestedOf(d, "Grad");
    if (!grad) return finish();
    const psd::Value* clrs = grad->get("Clrs");
    if (!clrs || clrs->kind() != psd::Value::Kind::List) return finish();
    std::vector<std::pair<double, QColor>> stops;
    for (const auto& item : clrs->asList()) {
        if (item.kind() != psd::Value::Kind::DescriptorKind) continue;
        const auto& sd = item.asDescriptor();
        const auto* clrDesc = nestedOf(sd, "Clr ");
        if (!clrDesc) continue;
        stops.emplace_back(numOf(sd, "Lctn", 0.0), colorFromDescriptor(*clrDesc));
    }
    if (stops.empty()) return finish();
    std::sort(stops.begin(), stops.end(), [](auto& a, auto& b) { return a.first < b.first; });
    start = stops.front().second;
    end = stops.back().second;
    hadMoreStops = stops.size() > 2;
    return finish();
}

// One effect type's single-instance key and (optional) multi-instance list
// key, and the Hazor LayerEffect::type it maps to ("" when unsupported).
struct FxKind { const char* single; const char* multi; const char* hazorType; };
constexpr FxKind kFxKinds[] = {
    {"DrSh", "dropShadowMulti", "drop_shadow"},
    {"IrSh", "innerShadowMulti", "inner_shadow"},
    {"OrGl", "", "outer_glow"},
    {"IrGl", "", "inner_glow"},
    {"SoFi", "solidFillMulti", "color_overlay"},
    {"GrFl", "gradientFillMulti", "gradient_overlay"},
    {"FrFX", "frameFXMulti", "stroke"},
    {"ebbl", "", ""},
    {"ChFX", "", ""},
    {"patternFill", "", ""},
};

// `sizeScale`: the style's own "Scale Effects" factor (`Scl ` on the Lefx
// descriptor), which the format multiplies every distance/size/blur by at
// render time. Hazor has no such layer-wide factor, so it is folded into the
// individual values here instead of being dropped.
void appendEffect(const psd::Descriptor& d, const char* hazorType, Context& ctx, const QString& layerName,
                  double sizeScale, std::vector<LayerEffect>& out)
{
    const bool enabled = enabledOf(d);
    QVariantMap p;
    const QString type = QString::fromUtf8(hazorType);
    const auto scaled = [sizeScale](double v) { return v * sizeScale; };

    // A mode Hazor's renderer will not honour for this effect type: report it
    // rather than store a value nothing reads (see the file comment).
    if (const char* fixed = fixedRenderedMode(type)) {
        const std::optional<QString> mode = blendModeOf(d);
        if (!mode || *mode != QLatin1String(fixed)) {
            const QString name = mode ? *mode : modeKeyOf(d);
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its %2 blends with \"%3\"; Hazor always composites that effect "
                "as \"%4\", so it looks different.")
                .arg(layerName, LayerEffect::displayName(type), name, QString::fromUtf8(fixed)));
        }
    }

    if (type == QLatin1String("drop_shadow") || type == QLatin1String("inner_shadow")) {
        const bool inner = type == QLatin1String("inner_shadow");
        p[QStringLiteral("color")] = colorOf(d, "Clr ");
        p[QStringLiteral("opacity")] = pctOf(d, "Opct", inner ? 0.35f : 0.45f);
        // `lagl` is the light angle. inner_shadow's param means the same
        // thing; drop_shadow's means the offset direction, 180 degrees away.
        double angle = numOf(d, "lagl", 120.0);
        if (!inner) {
            angle += 180.0;
            while (angle > 180.0) angle -= 360.0;
            while (angle < -180.0) angle += 360.0;
        }
        p[QStringLiteral("angle")] = static_cast<float>(angle);
        p[QStringLiteral("distance")] = static_cast<int>(std::lround(scaled(numOf(d, "Dstn", inner ? 8.0 : 12.0))));
        const float blurPx = static_cast<float>(scaled(numOf(d, "blur", inner ? 10.0 : 18.0)));
        p[QStringLiteral("blur")] = blurPx;
        p[QStringLiteral("spread")] = static_cast<int>(std::lround(pctOf(d, "Ckmt", 0.0f) * blurPx));
    } else if (type == QLatin1String("outer_glow") || type == QLatin1String("inner_glow")) {
        const bool inner = type == QLatin1String("inner_glow");
        if (d.get("Grad")) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its %2 uses a gradient, which Hazor's glow effect does not support; "
                "a solid color was used instead.").arg(layerName, inner ? QStringLiteral("Inner Glow") : QStringLiteral("Outer Glow")));
        }
        p[QStringLiteral("color")] = colorOf(d, "Clr ");
        p[QStringLiteral("opacity")] = pctOf(d, "Opct", 0.5f);
        const float blurPx = static_cast<float>(scaled(numOf(d, "blur", inner ? 12.0 : 20.0)));
        p[QStringLiteral("blur")] = blurPx;
        if (!inner) {
            p[QStringLiteral("spread")] = static_cast<int>(std::lround(pctOf(d, "Ckmt", 0.0f) * blurPx));
        } else if (pctOf(d, "Ckmt", 0.0f) > 0.01f) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Inner Glow uses a choke/spread, which Hazor's inner glow does "
                "not have; only its size was imported.").arg(layerName));
        }
    } else if (type == QLatin1String("stroke")) {
        const psd::Value* pntT = d.get("PntT");
        if (pntT && pntT->kind() == psd::Value::Kind::Enumerated
            && (pntT->asEnumerated().value.is("GrFl") || pntT->asEnumerated().value.is("Ptrn"))) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Stroke style uses a gradient or pattern, which Hazor's stroke "
                "effect does not support; a solid color was used instead.").arg(layerName));
        }
        p[QStringLiteral("color")] = colorOf(d, "Clr ");
        p[QStringLiteral("opacity")] = pctOf(d, "Opct", 1.0f);
        p[QStringLiteral("size")] = static_cast<int>(std::lround(scaled(numOf(d, "Sz  ", 3.0))));
        const psd::Value* styl = d.get("Styl");
        QString position = QStringLiteral("outside");
        if (styl && styl->kind() == psd::Value::Kind::Enumerated) {
            if (styl->asEnumerated().value.is("InsF")) position = QStringLiteral("inside");
            else if (styl->asEnumerated().value.is("CtrF")) position = QStringLiteral("center");
        }
        p[QStringLiteral("position")] = position;
    } else if (type == QLatin1String("color_overlay")) {
        p[QStringLiteral("color")] = colorOf(d, "Clr ");
        p[QStringLiteral("opacity")] = pctOf(d, "Opct", 1.0f);
        p[QStringLiteral("blendMode")] = blendModeOf(d).value_or(QStringLiteral("normal"));
        if (!blendModeOf(d)) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Color Overlay blends with \"%2\", which Hazor does not "
                "support; Normal was used instead.").arg(layerName, modeKeyOf(d)));
        }
    } else if (type == QLatin1String("gradient_overlay")) {
        bool hadMoreStops = false;
        const auto [start, end] = gradientEndpoints(d, hadMoreStops);
        if (hadMoreStops) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Gradient Overlay has more than two color stops; Hazor only "
                "supports a two-color gradient, so it was simplified.").arg(layerName));
        }
        if (const psd::Value* gradType = d.get("Type"); gradType && gradType->kind() == psd::Value::Kind::Enumerated
            && !gradType->asEnumerated().value.is("Lnr ")) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Gradient Overlay is not a linear gradient; Hazor only has "
                "linear gradient overlays, so a linear one was used.").arg(layerName));
        }
        p[QStringLiteral("startColor")] = start;
        p[QStringLiteral("endColor")] = end;
        p[QStringLiteral("opacity")] = pctOf(d, "Opct", 1.0f);
        p[QStringLiteral("angle")] = static_cast<float>(numOf(d, "Angl", 90.0));
        // PSD's `Scl ` is how long the ramp is as a fraction of the layer box
        // (150% spreads the ramp past the edges, 10% squeezes it into a narrow
        // band). Hazor's `scale` DIVIDES the ramp's half-extent
        // (LayerEffect::applyGradientOverlay), so it is the reciprocal.
        const float psdScale = std::max(0.01f, pctOf(d, "Scl ", 1.0f));
        p[QStringLiteral("scale")] = 1.0f / psdScale;
        p[QStringLiteral("blendMode")] = blendModeOf(d).value_or(QStringLiteral("normal"));
        if (!blendModeOf(d)) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Gradient Overlay blends with \"%2\", which Hazor does not "
                "support; Normal was used instead.").arg(layerName, modeKeyOf(d)));
        }
    }
    LayerEffect effect(type, p, LayerEffect::defaultStyleParams(type));
    effect.enabled = enabled;
    out.push_back(std::move(effect));
}

} // namespace

void convertLayerEffects(const psd::LayerRecord& rec, Context& ctx, const QString& layerName,
                         std::vector<LayerEffect>& outEffects)
{
    const psd::TaggedBlock* block = rec.block(psdKey("lmfx"));
    if (!block) block = rec.block(psdKey("lfx2"));
    if (!block) block = rec.block(psdKey("lfxs"));
    if (!block) {
        // `lrFX` is the first-generation layer-style record: a fixed-field binary
        // layout, not a descriptor, and nothing here can read it. Saying so
        // beats letting the styles vanish without a trace.
        if (rec.block(psdKey("lrFX"))) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its layer styles are stored in the legacy layer-style format, "
                "which Hazor cannot read; they were not imported.").arg(layerName));
        }
        return;
    }
    if (block->data.size() < 4)
        return;
    const std::span<const uint8_t> rest(block->data.data() + 4, block->data.size() - 4);
    const auto parsed = psd::VersionedDescriptor::parsePrefix(rest);
    if (!parsed.hasValue())
        return;
    const psd::Descriptor& d = parsed.value().first.descriptor;

    // The style's master switch: with it off no effect renders at all,
    // whatever each effect's own `enab` says.
    const bool masterEnabled = boolOf(d, "masterFXSwitch", true);
    // "Scale Effects...": a percentage every size, distance and blur is
    // multiplied by. 0 is not a meaningful scale, so treat it as 100%.
    const double sizeScale = [&]() {
        const double pct = numOf(d, "Scl ", 100.0);
        return pct > 0.0 ? pct / 100.0 : 1.0;
    }();

    for (const auto& fx : kFxKinds) {
        const psd::Value* multiList = (fx.multi[0] != '\0') ? d.get(fx.multi) : nullptr;
        std::vector<const psd::Descriptor*> instances;
        if (multiList && multiList->kind() == psd::Value::Kind::List) {
            for (const auto& item : multiList->asList()) {
                if (item.kind() == psd::Value::Kind::DescriptorKind)
                    instances.push_back(&item.asDescriptor());
            }
        } else if (const psd::Value* single = d.get(fx.single); single && single->kind() == psd::Value::Kind::DescriptorKind) {
            instances.push_back(&single->asDescriptor());
        }
        for (const psd::Descriptor* inst : instances) {
            const psd::Value* present = inst->get("present");
            if (present && present->kind() == psd::Value::Kind::Boolean && !present->asBoolean())
                continue;
            if (fx.hazorType[0] == '\0') {
                if (enabledOf(*inst)) {
                    const QString label = QString::fromUtf8(fx.single) == QLatin1String("ebbl") ? QStringLiteral("Bevel & Emboss")
                        : QString::fromUtf8(fx.single) == QLatin1String("ChFX") ? QStringLiteral("Satin")
                                                                                 : QStringLiteral("Pattern Overlay");
                    ctx.warn(QCoreApplication::translate("PsdImportService",
                        "Layer \"%1\": its %2 layer style has no equivalent in Hazor and was not imported.")
                        .arg(layerName, label));
                }
                continue;
            }
            appendEffect(*inst, fx.hazorType, ctx, layerName, sizeScale, outEffects);
            if (!masterEnabled)
                outEffects.back().enabled = false;
        }
    }
}

} // namespace psdimport
