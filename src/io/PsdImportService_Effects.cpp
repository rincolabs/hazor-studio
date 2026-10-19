// Layer styles (`lfx2`/`lmfx`, groups' `lfxs`) -> Hazor's LayerEffect list.
// Field names follow the documented `Lefx` (layer effects) descriptor
// layout used by the PSD/PSB format.
//
// Hazor's layer-style vocabulary is drop_shadow, inner_shadow, outer_glow,
// inner_glow, stroke, color_overlay and gradient_overlay (see
// LayerEffect.cpp): Bevel & Emboss and Satin have no Hazor equivalent and
// are reported instead of silently dropped.

#include "PsdImportService_Internal.hpp"

#include "core/LayerEffect.hpp"
#include "psd/Descriptor.hpp"
#include "psd/Layer.hpp"

#include <QCoreApplication>

#include <algorithm>
#include <cmath>
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
QString blendModeOf(const psd::Descriptor& d)
{
    const psd::Value* v = d.get("Md  ");
    if (!v || v->kind() != psd::Value::Kind::Enumerated)
        return QStringLiteral("normal");
    const auto& e = v->asEnumerated();
    if (e.value.is("Mltp")) return QStringLiteral("multiply");
    if (e.value.is("Scrn")) return QStringLiteral("screen");
    if (e.value.is("Ovrl")) return QStringLiteral("overlay");
    if (e.value.is("Drkn")) return QStringLiteral("darken");
    if (e.value.is("Lghn")) return QStringLiteral("lighten");
    return QStringLiteral("normal");
}

const psd::Descriptor* nestedOf(const psd::Descriptor& d, const char* key)
{
    const psd::Value* v = d.get(key);
    return v && v->kind() == psd::Value::Kind::DescriptorKind ? &v->asDescriptor() : nullptr;
}

// First/last colour stop of a "Grad" gradient descriptor (location 0..4096).
// Hazor's gradient_overlay only supports a straight two-colour ramp.
std::pair<QColor, QColor> gradientEndpoints(const psd::Descriptor& d, bool& hadMoreStops)
{
    QColor start = QColor(0, 0, 0);
    QColor end = QColor(255, 255, 255);
    hadMoreStops = false;
    const psd::Descriptor* grad = nestedOf(d, "Grad");
    if (!grad) return {start, end};
    const psd::Value* clrs = grad->get("Clrs");
    if (!clrs || clrs->kind() != psd::Value::Kind::List) return {start, end};
    std::vector<std::pair<double, QColor>> stops;
    for (const auto& item : clrs->asList()) {
        if (item.kind() != psd::Value::Kind::DescriptorKind) continue;
        const auto& sd = item.asDescriptor();
        const auto* clrDesc = nestedOf(sd, "Clr ");
        if (!clrDesc) continue;
        stops.emplace_back(numOf(sd, "Lctn", 0.0), colorFromDescriptor(*clrDesc));
    }
    if (stops.empty()) return {start, end};
    std::sort(stops.begin(), stops.end(), [](auto& a, auto& b) { return a.first < b.first; });
    start = stops.front().second;
    end = stops.back().second;
    hadMoreStops = stops.size() > 2;
    return {start, end};
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

void appendEffect(const psd::Descriptor& d, const char* hazorType, Context& ctx, const QString& layerName,
                  std::vector<LayerEffect>& out)
{
    const bool enabled = enabledOf(d);
    QVariantMap p;
    const QString type = QString::fromUtf8(hazorType);
    if (type == QLatin1String("drop_shadow") || type == QLatin1String("inner_shadow")) {
        const bool inner = type == QLatin1String("inner_shadow");
        p[QStringLiteral("color")] = colorOf(d, "Clr ");
        p[QStringLiteral("opacity")] = pctOf(d, "Opct", inner ? 0.35f : 0.45f);
        p[QStringLiteral("angle")] = static_cast<float>(numOf(d, "lagl", 120.0));
        p[QStringLiteral("distance")] = static_cast<int>(numOf(d, "Dstn", inner ? 8.0 : 12.0));
        const float blurPx = static_cast<float>(numOf(d, "blur", inner ? 10.0 : 18.0));
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
        const float blurPx = static_cast<float>(numOf(d, "blur", inner ? 12.0 : 20.0));
        p[QStringLiteral("blur")] = blurPx;
        if (!inner)
            p[QStringLiteral("spread")] = static_cast<int>(std::lround(pctOf(d, "Ckmt", 0.0f) * blurPx));
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
        p[QStringLiteral("size")] = static_cast<int>(numOf(d, "Sz  ", 3.0));
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
        p[QStringLiteral("blendMode")] = blendModeOf(d);
    } else if (type == QLatin1String("gradient_overlay")) {
        bool hadMoreStops = false;
        const auto [start, end] = gradientEndpoints(d, hadMoreStops);
        if (hadMoreStops) {
            ctx.warn(QCoreApplication::translate("PsdImportService",
                "Layer \"%1\": its Gradient Overlay has more than two color stops; Hazor only "
                "supports a two-color gradient, so it was simplified.").arg(layerName));
        }
        p[QStringLiteral("startColor")] = start;
        p[QStringLiteral("endColor")] = end;
        p[QStringLiteral("opacity")] = pctOf(d, "Opct", 1.0f);
        p[QStringLiteral("angle")] = static_cast<float>(numOf(d, "Angl", 90.0));
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
    if (!block)
        return;
    if (block->data.size() < 4)
        return;
    const std::span<const uint8_t> rest(block->data.data() + 4, block->data.size() - 4);
    const auto parsed = psd::VersionedDescriptor::parsePrefix(rest);
    if (!parsed.hasValue())
        return;
    const psd::Descriptor& d = parsed.value().first.descriptor;

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
            appendEffect(*inst, fx.hazorType, ctx, layerName, outEffects);
        }
    }
}

} // namespace psdimport
