#include "BrushPresetManager.hpp"
#include "BrushTextureLibrary.hpp"
#include "BrushTipLibrary.hpp"
#include "core/AppPaths.hpp"
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <algorithm>
#include <utility>

// ── BrushPreset serialization ─────────────────────────────────

// One nested jitter amount from the pre-curve-option format ({amount, source}).
static float legacyJitterAmount(const QJsonObject& parent, const char* key)
{
    return static_cast<float>(parent[key].toObject()["amount"].toDouble(0.0));
}

static QJsonObject colorToJson(const ColorDynamics& d)
{
    QJsonObject o;
    o["enabled"] = d.enabled;
    o["fgColor"] = d.fgColor.name(QColor::HexArgb);
    o["bgColor"] = d.bgColor.name(QColor::HexArgb);
    o["hue"] = static_cast<double>(d.hueAmount);
    o["saturation"] = static_cast<double>(d.saturationAmount);
    o["brightness"] = static_cast<double>(d.brightnessAmount);
    o["purity"] = static_cast<double>(d.purityAmount);
    return o;
}

static ColorDynamics colorFromJson(const QJsonObject& o)
{
    ColorDynamics d;
    d.enabled = o["enabled"].toBool(false);
    if (o.contains("fgColor")) d.fgColor = QColor(o["fgColor"].toString());
    if (o.contains("bgColor")) d.bgColor = QColor(o["bgColor"].toString());
    if (o.contains("hue")) {
        d.hueAmount = static_cast<float>(o["hue"].toDouble(0.0));
        d.saturationAmount = static_cast<float>(o["saturation"].toDouble(0.0));
        d.brightnessAmount = static_cast<float>(o["brightness"].toDouble(0.0));
        d.purityAmount = static_cast<float>(o["purity"].toDouble(0.0));
    } else {
        // Pre-curve-option presets stored each amount as a nested jitter object.
        d.hueAmount = legacyJitterAmount(o, "hueJitter");
        d.saturationAmount = legacyJitterAmount(o, "saturationJitter");
        d.brightnessAmount = legacyJitterAmount(o, "brightnessJitter");
        d.purityAmount = legacyJitterAmount(o, "purityJitter");
    }
    return d;
}

static QJsonObject textureToJson(const TextureConfig& d)
{
    QJsonObject o;
    o["enabled"] = d.enabled;
    o["textureId"] = d.textureId;
    o["scale"] = static_cast<double>(d.scale);
    o["brightness"] = static_cast<double>(d.brightness);
    o["contrast"] = static_cast<double>(d.contrast);
    o["invert"] = d.invert;
    o["depth"] = static_cast<double>(d.depth);
    o["texturing"] = static_cast<int>(d.texturing);
    o["texturingMode"] = static_cast<int>(d.texturingMode);
    o["softTexturing"] = d.softTexturing;
    o["neutralPoint"] = static_cast<double>(d.neutralPoint);
    o["cutoffPolicy"] = static_cast<int>(d.cutoffPolicy);
    o["cutoffMin"] = static_cast<double>(d.cutoffMin);
    o["cutoffMax"] = static_cast<double>(d.cutoffMax);
    o["horizontalOffset"] = d.horizontalOffset;
    o["verticalOffset"] = d.verticalOffset;
    o["randomHorizontalOffset"] = d.randomHorizontalOffset;
    o["randomVerticalOffset"] = d.randomVerticalOffset;
    o["autoInvertForEraser"] = d.autoInvertForEraser;
    // Embed the pixels only when they cannot be re-resolved from a bundled
    // resource. A built-in kit texture is referenced by id and re-loaded from the
    // Qt resource system (:/...) on every launch, so we keep the JSON lean and
    // store just the id (no duplication). Imported textures (session-only, not in
    // the bundle) still embed their pixels so they survive a restart.
    bool bundled = false;
    if (!d.textureId.isEmpty()) {
        if (const BrushTexture* tex = BrushTextureLibrary::instance()->find(d.textureId))
            bundled = tex->path.startsWith(QLatin1String(":/"));
    }
    if (!d.texture.isNull() && !bundled) {
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        d.texture.save(&buf, "PNG");
        buf.close();
        o["imageData"] = QString::fromLatin1(bytes.toBase64());
    }
    return o;
}

static TextureConfig textureFromJson(const QJsonObject& o, const QString& fallbackName)
{
    TextureConfig d;
    d.enabled = o["enabled"].toBool(false);
    d.textureId = o["textureId"].toString();
    d.scale = static_cast<float>(o["scale"].toDouble(1.0));
    d.brightness = static_cast<float>(o["brightness"].toDouble(0.0));
    d.invert = o["invert"].toBool(false);
    d.depth = static_cast<float>(o["depth"].toDouble(1.0));
    d.texturing = static_cast<TextureConfig::Texturing>(o["texturing"].toInt(0));

    const bool legacy = !o.contains("neutralPoint");
    if (legacy) {
        // Old contrast was a pivot-at-0.5 boost: factor = (tex-0.5)*(1+contrast*2)+0.5.
        // The engine uses (tex-neutral)*contrast+neutral, so map to preserve look.
        const float oldContrast = static_cast<float>(o["contrast"].toDouble(0.0));
        const float oldCutoff = static_cast<float>(o["cutoff"].toDouble(0.0));
        d.contrast = 1.0f + oldContrast * 2.0f;
        d.neutralPoint = 0.5f;
        d.texturingMode = static_cast<TextureConfig::TexturingMode>(o["texturing"].toInt(0));
        d.cutoffMin = oldCutoff;
        d.cutoffMax = 1.0f;
        d.cutoffPolicy = oldCutoff > 0.0f ? TextureConfig::CutoffPolicy::Pattern
                                          : TextureConfig::CutoffPolicy::Disabled;
    } else {
        d.contrast = static_cast<float>(o["contrast"].toDouble(1.0));
        d.neutralPoint = static_cast<float>(o["neutralPoint"].toDouble(0.5));
        d.texturingMode = static_cast<TextureConfig::TexturingMode>(o["texturingMode"].toInt(0));
        d.softTexturing = o["softTexturing"].toBool(false);
        d.cutoffPolicy = static_cast<TextureConfig::CutoffPolicy>(o["cutoffPolicy"].toInt(0));
        d.cutoffMin = static_cast<float>(o["cutoffMin"].toDouble(0.0));
        d.cutoffMax = static_cast<float>(o["cutoffMax"].toDouble(1.0));
        d.horizontalOffset = o["horizontalOffset"].toInt(0);
        d.verticalOffset = o["verticalOffset"].toInt(0);
        d.randomHorizontalOffset = o["randomHorizontalOffset"].toBool(false);
        d.randomVerticalOffset = o["randomVerticalOffset"].toBool(false);
        d.autoInvertForEraser = o["autoInvertForEraser"].toBool(false);
    }

    if (o.contains("imageData")) {
        QImage img;
        if (img.loadFromData(QByteArray::fromBase64(o["imageData"].toString().toLatin1()))) {
            d.texture = img.format() == QImage::Format_Grayscale8
                ? img
                : img.convertToFormat(QImage::Format_Grayscale8);
        }
    }
    // Make the texture a reusable library resource: register it (so it shows up in
    // the Texture panel) and keep the preset's reference id pointing at it. The
    // name falls back to the preset's so distinct textures stay distinct (dedup is
    // by pixel content inside the library, not by name).
    if (!d.texture.isNull()) {
        QString name = d.textureId;
        if (name.isEmpty()) name = fallbackName;
        if (name.isEmpty()) name = QStringLiteral("texture");
        d.textureId = BrushTextureLibrary::instance()->add(name, d.texture);
    } else if (!d.textureId.isEmpty()) {
        // Built-in presets reference a bundled texture by id only (no embedded
        // pixels — never duplicated per preset). Resolve the pixels from the global
        // library so the brush actually carries its texture once loaded.
        BrushTextureLibrary::instance()->ensureLoaded();
        if (const BrushTexture* tex = BrushTextureLibrary::instance()->find(d.textureId))
            d.texture = tex->image;
    }
    return d;
}

static QJsonObject dualToJson(const DualBrushConfig& d)
{
    QJsonObject o;
    o["enabled"] = d.enabled;
    o["tipType"] = d.tipType;
    o["size"] = static_cast<double>(d.size);
    o["spacing"] = static_cast<double>(d.spacing);
    o["scatter"] = static_cast<double>(d.scatter);
    o["count"] = d.count;
    o["hardness"] = static_cast<double>(d.hardness);
    return o;
}

static DualBrushConfig dualFromJson(const QJsonObject& o)
{
    DualBrushConfig d;
    d.enabled = o["enabled"].toBool(false);
    d.tipType = o["tipType"].toInt(0);
    d.size = static_cast<float>(o["size"].toDouble(20.0));
    d.spacing = static_cast<float>(o["spacing"].toDouble(1.0));
    d.scatter = static_cast<float>(o["scatter"].toDouble(0.0));
    d.count = o["count"].toInt(1);
    d.hardness = static_cast<float>(o["hardness"].toDouble(0.8));
    return d;
}

// ── Engine serialization (procedural tip config + curve options) ──────────────

static QJsonObject responseCurveToJson(const ResponseCurve& c)
{
    QJsonArray pts;
    for (const QPointF& p : c.points) {
        QJsonArray xy;
        xy.append(static_cast<double>(p.x()));
        xy.append(static_cast<double>(p.y()));
        pts.append(xy);
    }
    QJsonObject o;
    o["points"] = pts;
    return o;
}

static ResponseCurve responseCurveFromJson(const QJsonObject& o)
{
    ResponseCurve c;
    const QJsonArray pts = o["points"].toArray();
    for (const auto& v : pts) {
        const QJsonArray xy = v.toArray();
        if (xy.size() == 2)
            c.points.append(QPointF(xy[0].toDouble(), xy[1].toDouble()));
    }
    return c;
}

static QJsonObject sensorToJson(const Sensor& s)
{
    QJsonObject o;
    o["type"] = static_cast<int>(s.type);
    o["enabled"] = s.enabled;
    o["curve"] = responseCurveToJson(s.curve);
    if (s.length > 0)
        o["length"] = s.length;
    if (s.periodic)
        o["periodic"] = s.periodic;
    if (s.angleOffset != 0.0f)
        o["angleOffset"] = static_cast<double>(s.angleOffset);
    return o;
}

static Sensor sensorFromJson(const QJsonObject& o)
{
    Sensor s;
    s.type = static_cast<SensorType>(o["type"].toInt(0));
    s.enabled = o["enabled"].toBool(true);
    s.curve = responseCurveFromJson(o["curve"].toObject());
    s.length = o["length"].toInt(0);
    s.periodic = o["periodic"].toBool(false);
    s.angleOffset = static_cast<float>(o["angleOffset"].toDouble(0.0));
    return s;
}

static QJsonObject curveOptionToJson(const CurveOption& c)
{
    QJsonObject o;
    o["enabled"] = c.enabled;
    o["combine"] = static_cast<int>(c.combine);
    o["minValue"] = static_cast<double>(c.minValue);
    o["maxValue"] = static_cast<double>(c.maxValue);
    QJsonArray arr;
    for (const Sensor& s : c.sensors)
        arr.append(sensorToJson(s));
    o["sensors"] = arr;
    return o;
}

static CurveOption curveOptionFromJson(const QJsonObject& o)
{
    CurveOption c;
    c.enabled = o["enabled"].toBool(false);
    c.combine = static_cast<CurveOption::Combine>(o["combine"].toInt(0));
    c.minValue = static_cast<float>(o["minValue"].toDouble(0.0));
    c.maxValue = static_cast<float>(o["maxValue"].toDouble(1.0));
    const QJsonArray arr = o["sensors"].toArray();
    for (const auto& v : arr)
        c.sensors.append(sensorFromJson(v.toObject()));
    return c;
}

static QJsonObject autoBrushToJson(const AutoBrushConfig& a)
{
    QJsonObject o;
    o["profile"] = a.profile;
    o["spikes"] = a.spikes;
    o["hFade"] = static_cast<double>(a.hFade);
    o["vFade"] = static_cast<double>(a.vFade);
    o["density"] = static_cast<double>(a.density);
    o["randomness"] = static_cast<double>(a.randomness);
    o["softnessCurve"] = responseCurveToJson(a.softnessCurve);
    return o;
}

static AutoBrushConfig autoBrushFromJson(const QJsonObject& o)
{
    AutoBrushConfig a;
    a.profile = o["profile"].toInt(0);
    a.spikes = o["spikes"].toInt(2);
    a.hFade = static_cast<float>(o["hFade"].toDouble(-1.0));
    a.vFade = static_cast<float>(o["vFade"].toDouble(-1.0));
    a.density = static_cast<float>(o["density"].toDouble(1.0));
    a.randomness = static_cast<float>(o["randomness"].toDouble(0.0));
    if (o.contains("softnessCurve"))
        a.softnessCurve = responseCurveFromJson(o["softnessCurve"].toObject());
    return a;
}

QJsonObject BrushPreset::toJson() const
{
    QJsonObject o;
    o["name"] = name;
    o["size"] = static_cast<double>(settings.size);
    o["hardness"] = static_cast<double>(settings.hardness);
    o["opacity"] = static_cast<double>(settings.opacity);
    o["flow"] = static_cast<double>(settings.flow);
    o["color"] = settings.color.name();
    o["type"] = static_cast<int>(settings.type);
    o["mode"] = static_cast<int>(settings.mode);
    o["angle"] = static_cast<double>(settings.angle);
    o["roundness"] = static_cast<double>(settings.roundness);
    o["flipX"] = settings.flipX;
    o["flipY"] = settings.flipY;
    o["colorDynamics"] = colorToJson(settings.colorDynamics);
    o["textureConfig"] = textureToJson(settings.textureConfig);
    o["dualBrushConfig"] = dualToJson(settings.dualBrushConfig);
    o["blendMode"] = static_cast<int>(settings.blendMode);
    o["smoothingMode"] = static_cast<int>(settings.smoothingMode);
    o["smoothingRadius"] = static_cast<double>(settings.smoothingRadius);
    o["spacing"] = static_cast<double>(settings.spacing);
    o["airbrush"] = settings.airbrush;
    o["airbrushRate"] = static_cast<double>(settings.airbrushRate);
    o["wetEdges"] = settings.wetEdges;
    o["scatterAxisX"] = settings.scatter.axisX;
    o["scatterAxisY"] = settings.scatter.axisY;
    o["tipSource"] = static_cast<int>(settings.tipSource);
    o["application"] = static_cast<int>(settings.application);
    o["paintMode"] = static_cast<int>(settings.paintMode);
    // Layer A/B/C additions (extended engine fields). Back-compat on read.
    o["tipBrightness"] = static_cast<double>(settings.tipBrightness);
    o["tipContrast"] = static_cast<double>(settings.tipContrast);
    o["tipMidpoint"] = static_cast<double>(settings.tipMidpoint);
    o["useAutoSpacing"] = settings.useAutoSpacing;
    o["autoSpacingCoeff"] = static_cast<double>(settings.autoSpacingCoeff);
    o["autoBrush"] = autoBrushToJson(settings.autoBrush);
    o["sizeOption"] = curveOptionToJson(settings.sizeOption);
    o["opacityOption"] = curveOptionToJson(settings.opacityOption);
    o["flowOption"] = curveOptionToJson(settings.flowOption);
    o["rotationOption"] = curveOptionToJson(settings.rotationOption);
    o["ratioOption"] = curveOptionToJson(settings.ratioOption);
    o["scatterOption"] = curveOptionToJson(settings.scatterOption);
    if (!settings.tipImagePath.isEmpty())
        o["tipImagePath"] = settings.tipImagePath;
    if (!settings.tipImageData.isEmpty())
        o["tipImageData"] = settings.tipImageData;
    if (!settings.tipId.isEmpty())
        o["tipId"] = settings.tipId;
    if (!group.isEmpty())
        o["group"] = group;
    if (!sourceInfo.isEmpty())
        o["source"] = sourceInfo;
    if (!previewImageData.isEmpty())
        o["previewImage"] = previewImageData;
    return o;
}

BrushPreset BrushPreset::fromJson(const QJsonObject& o)
{
    BrushPreset p;
    p.name = o["name"].toString();
    if (o.contains("size"))
        p.settings.size = static_cast<float>(o["size"].toDouble(20.0));
    if (o.contains("hardness"))
        p.settings.hardness = static_cast<float>(o["hardness"].toDouble(0.8));
    if (o.contains("opacity"))
        p.settings.opacity = static_cast<float>(o["opacity"].toDouble(1.0));
    if (o.contains("flow"))
        p.settings.flow = static_cast<float>(o["flow"].toDouble(1.0));
    if (o.contains("color"))
        p.settings.color = QColor(o["color"].toString());
    if (o.contains("type"))
        p.settings.type = static_cast<BrushType>(o["type"].toInt(0));
    if (o.contains("mode"))
        p.settings.mode = static_cast<BrushMode>(o["mode"].toInt(0));
    // Static dab shape (back-compat: absent → neutral round dab).
    p.settings.angle = static_cast<float>(o["angle"].toDouble(0.0));
    p.settings.roundness = static_cast<float>(o["roundness"].toDouble(1.0));
    p.settings.flipX = o["flipX"].toBool(false);
    p.settings.flipY = o["flipY"].toBool(false);
    if (o.contains("colorDynamics"))
        p.settings.colorDynamics = colorFromJson(o["colorDynamics"].toObject());
    if (o.contains("textureConfig"))
        p.settings.textureConfig = textureFromJson(o["textureConfig"].toObject(), p.name);
    if (o.contains("dualBrushConfig"))
        p.settings.dualBrushConfig = dualFromJson(o["dualBrushConfig"].toObject());
    if (o.contains("blendMode"))
        p.settings.blendMode = static_cast<BrushBlendMode>(o["blendMode"].toInt(0));
    if (o.contains("smoothingMode"))
        p.settings.smoothingMode = static_cast<SmoothingMode>(o["smoothingMode"].toInt(0));
    if (o.contains("smoothingRadius"))
        p.settings.smoothingRadius = static_cast<float>(o["smoothingRadius"].toDouble(10.0));
    if (o.contains("spacing"))
        p.settings.spacing = static_cast<float>(o["spacing"].toDouble(0.1));
    if (o.contains("airbrush"))
        p.settings.airbrush = o["airbrush"].toBool(false);
    if (o.contains("airbrushRate"))
        p.settings.airbrushRate = static_cast<float>(o["airbrushRate"].toDouble(20.0));
    if (o.contains("wetEdges"))
        p.settings.wetEdges = o["wetEdges"].toBool(false);
    p.settings.scatter.axisX = o["scatterAxisX"].toBool(true);
    p.settings.scatter.axisY = o["scatterAxisY"].toBool(true);
    if (o.contains("tipSource"))
        p.settings.tipSource = static_cast<BrushTipSource>(o["tipSource"].toInt(0));
    // Application mode is optional: legacy presets default to AlphaMask (0), so
    // they keep painting the brush colour through the tip exactly as before.
    if (o.contains("application"))
        p.settings.application = static_cast<BrushApplication>(o["application"].toInt(0));
    // Paint mode optional: absent → BuildUp (0), preserving legacy brush feel.
    if (o.contains("paintMode"))
        p.settings.paintMode = static_cast<BrushPaintMode>(o["paintMode"].toInt(0));
    // Layer A/B/C additions — all optional, defaults reproduce the legacy dab.
    if (o.contains("tipBrightness"))
        p.settings.tipBrightness = static_cast<float>(o["tipBrightness"].toDouble(0.0));
    if (o.contains("tipContrast"))
        p.settings.tipContrast = static_cast<float>(o["tipContrast"].toDouble(0.0));
    if (o.contains("tipMidpoint"))
        p.settings.tipMidpoint = static_cast<float>(o["tipMidpoint"].toDouble(0.5));
    if (o.contains("useAutoSpacing"))
        p.settings.useAutoSpacing = o["useAutoSpacing"].toBool(false);
    if (o.contains("autoSpacingCoeff"))
        p.settings.autoSpacingCoeff = static_cast<float>(o["autoSpacingCoeff"].toDouble(1.0));
    if (o.contains("autoBrush"))
        p.settings.autoBrush = autoBrushFromJson(o["autoBrush"].toObject());
    if (o.contains("sizeOption"))
        p.settings.sizeOption = curveOptionFromJson(o["sizeOption"].toObject());
    if (o.contains("opacityOption"))
        p.settings.opacityOption = curveOptionFromJson(o["opacityOption"].toObject());
    if (o.contains("flowOption"))
        p.settings.flowOption = curveOptionFromJson(o["flowOption"].toObject());
    if (o.contains("rotationOption"))
        p.settings.rotationOption = curveOptionFromJson(o["rotationOption"].toObject());
    if (o.contains("ratioOption"))
        p.settings.ratioOption = curveOptionFromJson(o["ratioOption"].toObject());
    if (o.contains("scatterOption"))
        p.settings.scatterOption = curveOptionFromJson(o["scatterOption"].toObject());

    // ── Migration of pre-curve-option presets ──
    // The old dedicated pressure block becomes a Pressure sensor on the matching
    // axis options (only when the preset has no curve option for that axis, so
    // nothing stacks). minDiameter/minOpacity/minFlow map onto the option's
    // minValue.
    if (o.contains("pressure")) {
        const QJsonObject pr = o["pressure"].toObject();
        if (pr["enabled"].toBool(true)) {
            auto migrateAxis = [&pr](CurveOption& opt, const char* affectsKey,
                                     const char* minKey, bool affectsDefault) {
                if (opt.enabled || !pr[affectsKey].toBool(affectsDefault))
                    return;
                opt.setPressureSensorEnabled(true);
                opt.minValue = std::clamp(
                    static_cast<float>(pr[minKey].toDouble(0.0)), 0.0f, 1.0f);
                const QString curve = pr["curve"].toString("linear");
                ResponseCurve::Preset preset = ResponseCurve::Preset::Linear;
                if (curve == QLatin1String("soft")) preset = ResponseCurve::Preset::Soft;
                else if (curve == QLatin1String("hard")) preset = ResponseCurve::Preset::Hard;
                for (Sensor& s : opt.sensors)
                    if (s.type == SensorType::Pressure)
                        s.curve = ResponseCurve::linePreset(preset);
            };
            migrateAxis(p.settings.sizeOption, "affectsSize", "minDiameter", true);
            migrateAxis(p.settings.opacityOption, "affectsOpacity", "minOpacity", false);
            migrateAxis(p.settings.flowOption, "affectsFlow", "minFlow", false);
        }
    }
    // The old scatter block (amount as a multiple of the dab RADIUS, optional
    // rectangular spread) becomes the scatter option: strength is halved (the
    // offset now scales by the dab DIAMETER) and both axes stay on (the closest
    // match for both the old radial and rectangular spreads).
    if (!o.contains("scatterOption") && o.contains("scatterDynamics")) {
        const QJsonObject sc = o["scatterDynamics"].toObject();
        const float amount = static_cast<float>(sc["amount"].toDouble(0.0));
        if (sc["enabled"].toBool(false) && amount > 0.0f) {
            p.settings.scatterOption.enabled = true;
            p.settings.scatterOption.minValue = 0.0f;
            p.settings.scatterOption.maxValue = std::clamp(amount * 0.5f, 0.0f, 5.0f);
            p.settings.scatter.axisX = true;
            p.settings.scatter.axisY = true;
        }
    }
    // The old shape-dynamics random jitters become Fuzzy sensors on the matching
    // options (angle jitter was authored in degrees; a rotation strength of 1
    // spans a half turn each way).
    if (o.contains("shapeDynamics")) {
        const QJsonObject sd = o["shapeDynamics"].toObject();
        if (sd["enabled"].toBool(false)) {
            const float angleJitter = legacyJitterAmount(sd, "angleJitter");
            if (angleJitter > 0.0f && !p.settings.rotationOption.enabled) {
                p.settings.rotationOption.enabled = true;
                p.settings.rotationOption.maxValue =
                    std::clamp(angleJitter / 180.0f, 0.0f, 1.0f);
                Sensor s;
                s.type = SensorType::Fuzzy;
                p.settings.rotationOption.sensors.append(s);
            }
        }
    }

    if (o.contains("tipImagePath"))
        p.settings.tipImagePath = o["tipImagePath"].toString();
    if (o.contains("tipImageData"))
        p.settings.tipImageData = o["tipImageData"].toString();
    if (o.contains("tipId"))
        p.settings.tipId = o["tipId"].toString();
    // Tips are global editor resources, like textures: register this preset's tip
    // into the global library at LOAD time so the Brush Tip grid lists every
    // brush's tip up front (rather than only as presets get selected), and keep
    // the preset's reference id pointing at it. Dedup inside the library is by
    // pixel content, so brushes that share a tip collapse to one entry.
    if (p.settings.tipSource == BrushTipSource::Image) {
        QImage tip;
        if (!p.settings.tipImageData.isEmpty())
            tip.loadFromData(QByteArray::fromBase64(p.settings.tipImageData.toLatin1()));
        else if (!p.settings.tipImagePath.isEmpty())
            tip.load(p.settings.tipImagePath);
        if (!tip.isNull()) {
            QString name = p.name.isEmpty() ? p.settings.tipId : p.name;
            if (name.isEmpty()) name = QStringLiteral("tip");
            p.settings.tipId = BrushTipLibrary::instance()->add(name, tip, p.settings.tipImagePath);
            p.settings.tipImage = tip;
        }
    }
    // Optional library folder + import provenance (absent for legacy presets).
    p.group = o["group"].toString();
    p.sourceInfo = o["source"].toObject();
    p.previewImageData = o["previewImage"].toString();
    return p;
}

// ── BrushPresetManager ────────────────────────────────────────

BrushPresetManager::BrushPresetManager(QObject* parent)
    : QObject(parent)
{
}

QString BrushPresetManager::configPath() const
{
    return AppPaths::dataFile(QStringLiteral("brush_presets.json"));
}

namespace {

// Root path for the bundled default-brush resources: an index.json manifest plus
// one JSON file per preset. The manifest defines BOTH the folder tree and the
// LOAD ORDER of the presets. Order matters: BrushPreset::fromJson() registers each
// preset's tip into the global BrushTipLibrary, which dedupes tips shared between
// presets by pixel content and assigns the tipId from the FIRST preset that uses
// a given tip — so the order here is what makes the resolved tipIds deterministic.
//
// Adding a brush later: drop a new <name>.json in :/brushes/presets/, register it
// in resources.qrc, and insert its file name at the desired position in
// index.json's "presets" list. No existing file needs to be renamed.
static const QString kDefaultPresetsRoot = QStringLiteral(":/brushes/presets");

QJsonObject readJsonObject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    return doc.object();
}

} // namespace

void BrushPresetManager::seedDefaultPresets()
{
    // Load the manifest (folder tree + preset order).
    const QJsonObject manifest =
        readJsonObject(kDefaultPresetsRoot + QStringLiteral("/index.json"));

    // Folders, in manifest order (so the panel lists them sensibly).
    for (const QJsonValue& v : manifest[QStringLiteral("folders")].toArray()) {
        const QString path = v.toString();
        if (!path.isEmpty() && !m_folders.contains(path))
            m_folders << path;
    }

    // Presets, in manifest order. Each file is a full preset object in exactly the
    // format BrushPreset::toJson() writes, so fromJson() round-trips it and the
    // shared tips/textures resolve into the global libraries the same way they do
    // when the persisted library is read back from disk.
    const QJsonArray presets = manifest[QStringLiteral("presets")].toArray();
    m_presets.reserve(presets.size());
    for (const QJsonValue& v : presets) {
        const QString file = v.toString();
        if (file.isEmpty())
            continue;
        const QJsonObject obj =
            readJsonObject(kDefaultPresetsRoot + QLatin1Char('/') + file);
        if (obj.isEmpty())
            continue;
        m_presets.push_back(BrushPreset::fromJson(obj));
    }
}

void BrushPresetManager::loadPresets()
{
    m_presets.clear();
    m_folders.clear();

    QFile file(configPath());
    if (file.open(QIODevice::ReadOnly)) {
        QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
        file.close();

        // New format: an object { presets:[…], folders:[…] }. Legacy format: a bare
        // array of presets (no persisted folders).
        if (doc.isObject()) {
            const QJsonObject obj = doc.object();
            for (const auto& val : obj["presets"].toArray())
                m_presets.push_back(BrushPreset::fromJson(val.toObject()));
            for (const auto& f : obj["folders"].toArray()) {
                const QString path = f.toString();
                if (!path.isEmpty() && !m_folders.contains(path))
                    m_folders << path;
            }
        } else {
            for (const auto& val : doc.array())
                m_presets.push_back(BrushPreset::fromJson(val.toObject()));
        }
    }

    // ── Default presets on first run ──
    if (!m_presets.empty()) return;

    seedDefaultPresets();
    savePresets();
}

void BrushPresetManager::reloadFromDisk()
{
    loadPresets();
    emit presetsChanged();
}

void BrushPresetManager::resetToDefaults()
{
    // Wipe the on-disk library, then rebuild the built-in kit from C++ and
    // persist it. Irreversible: any user presets/folders are gone.
    QFile::remove(configPath());
    m_presets.clear();
    m_folders.clear();
    seedDefaultPresets();
    savePresets();
    emit presetsChanged();
}

void BrushPresetManager::savePresets()
{
    QJsonArray presetArr;
    for (const auto& p : m_presets)
        presetArr.append(p.toJson());

    QJsonArray folderArr;
    for (const QString& f : m_folders)
        folderArr.append(f);

    QJsonObject root;
    root["presets"] = presetArr;
    root["folders"] = folderArr;

    QString path = configPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return;

    file.write(QJsonDocument(root).toJson());
    file.close();
}

void BrushPresetManager::addPreset(const BrushPreset& preset)
{
    // Replace if exists with same name
    for (auto& p : m_presets) {
        if (p.name == preset.name) {
            p = preset;
            savePresets();
            emit presetsChanged();
            return;
        }
    }
    m_presets.push_back(preset);
    savePresets();
    emit presetsChanged();
}

void BrushPresetManager::addPresets(const std::vector<BrushPreset>& presets, bool persist)
{
    if (presets.empty())
        return;
    for (const BrushPreset& preset : presets) {
        bool replaced = false;
        for (auto& p : m_presets) {
            if (p.name == preset.name) { p = preset; replaced = true; break; }
        }
        if (!replaced)
            m_presets.push_back(preset);
    }
    if (persist)
        savePresets();
    emit presetsChanged();
}

bool BrushPresetManager::contains(const QString& name) const
{
    for (const auto& p : m_presets)
        if (p.name == name) return true;
    return false;
}

QString BrushPresetManager::uniqueName(const QString& base) const
{
    if (!contains(base))
        return base;
    for (int i = 2; i < 100000; ++i) {
        const QString candidate = QStringLiteral("%1 %2").arg(base).arg(i);
        if (!contains(candidate))
            return candidate;
    }
    return base;
}

void BrushPresetManager::removePreset(const QString& name)
{
    m_presets.erase(
        std::remove_if(m_presets.begin(), m_presets.end(),
            [&](const BrushPreset& p) { return p.name == name; }),
        m_presets.end());
    savePresets();
    emit presetsChanged();
}

BrushPreset* BrushPresetManager::findPreset(const QString& name)
{
    for (auto& p : m_presets) {
        if (p.name == name)
            return &p;
    }
    return nullptr;
}

bool BrushPresetManager::overwritePreset(const QString& name, const BrushSettings& settings)
{
    BrushPreset* p = findPreset(name);
    if (!p)
        return false;
    p->settings = settings;
    savePresets();
    emit presetsChanged();
    return true;
}

bool BrushPresetManager::saveAsNewPreset(const QString& name, const QString& group,
                                         const BrushSettings& settings)
{
    if (name.trimmed().isEmpty())
        return false;
    BrushPreset preset;
    preset.name = name.trimmed();
    preset.group = group;
    preset.settings = settings;
    // addPreset replaces an existing same-name preset (the "substituir" path) or
    // appends a new one, then persists + emits.
    addPreset(preset);
    return true;
}

QString BrushPresetManager::duplicatePreset(const QString& name)
{
    const BrushPreset* src = nullptr;
    for (const auto& p : m_presets)
        if (p.name == name) { src = &p; break; }
    if (!src)
        return {};

    // Strip an existing "(copy)"/"(copy N)" suffix so repeated duplicates read
    // "Soft Pencil (copy)", "Soft Pencil (copy 2)"… rather than nesting suffixes.
    QString stem = src->name;
    static const QRegularExpression copyRe(
        QStringLiteral("\\s*\\(copy(?:\\s+\\d+)?\\)\\s*$"),
        QRegularExpression::CaseInsensitiveOption);
    stem.remove(copyRe);
    stem = stem.trimmed();
    if (stem.isEmpty())
        stem = src->name;

    QString candidate = QStringLiteral("%1 (copy)").arg(stem);
    for (int i = 2; contains(candidate) && i < 100000; ++i)
        candidate = QStringLiteral("%1 (copy %2)").arg(stem).arg(i);

    BrushPreset copy = *src;          // same settings/group/source provenance
    copy.name = candidate;
    m_presets.push_back(copy);
    savePresets();
    emit presetsChanged();
    return candidate;
}

QString BrushPresetManager::createDefaultPreset(const QString& group)
{
    BrushPreset preset;
    preset.name = uniqueName(QStringLiteral("New Brush Preset"));
    preset.group = group;
    preset.settings = BrushSettings{};   // Brush Engine defaults
    m_presets.push_back(preset);
    savePresets();
    emit presetsChanged();
    return preset.name;
}

bool BrushPresetManager::renamePreset(const QString& oldName, const QString& newName)
{
    const QString trimmed = newName.trimmed();
    if (trimmed.isEmpty() || oldName == trimmed)
        return false;
    if (contains(trimmed))            // names are globally unique
        return false;
    BrushPreset* p = findPreset(oldName);
    if (!p)
        return false;
    p->name = trimmed;
    savePresets();
    emit presetsChanged();
    return true;
}

bool BrushPresetManager::movePresetToGroup(const QString& name, const QString& newGroup)
{
    BrushPreset* p = findPreset(name);
    if (!p)
        return false;
    if (p->group == newGroup)
        return false;
    p->group = newGroup;
    savePresets();
    emit presetsChanged();
    return true;
}

void BrushPresetManager::addFolder(const QString& fullPath)
{
    const QStringList parts = fullPath.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    if (parts.isEmpty())
        return;
    bool changed = false;
    QStringList prefix;
    for (const QString& part : parts) {        // record the folder and its ancestors
        prefix << part.trimmed();
        const QString path = prefix.join(QLatin1Char('/'));
        if (!m_folders.contains(path)) { m_folders << path; changed = true; }
    }
    if (!changed)
        return;
    savePresets();
    emit presetsChanged();
}

void BrushPresetManager::relocateFolderData(
    const QString& oldFull, const QString& newFull,
    const std::vector<std::pair<QString, QString>>& presetGroupChanges)
{
    bool changed = false;

    // Rewrite folder entries: oldFull → newFull and oldFull/... → newFull/...
    const QString oldPrefix = oldFull + QLatin1Char('/');
    const QString newPrefix = newFull + QLatin1Char('/');
    for (QString& f : m_folders) {
        if (f == oldFull) { f = newFull; changed = true; }
        else if (f.startsWith(oldPrefix)) {
            f = newPrefix + f.mid(oldPrefix.size());
            changed = true;
        }
    }
    // Ensure the destination ancestors exist as entries.
    QStringList prefix;
    for (const QString& part : newFull.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        prefix << part;
        const QString path = prefix.join(QLatin1Char('/'));
        if (!m_folders.contains(path)) { m_folders << path; changed = true; }
    }
    m_folders.removeDuplicates();

    for (const auto& [name, group] : presetGroupChanges) {
        if (BrushPreset* p = findPreset(name)) {
            if (p->group != group) { p->group = group; changed = true; }
        }
    }

    if (!changed)
        return;
    savePresets();
    emit presetsChanged();
}

void BrushPresetManager::removeFolderTree(const QString& fullPath, const QStringList& presetNames)
{
    const auto beforeFolders = m_folders.size();
    const QString prefix = fullPath + QLatin1Char('/');
    m_folders.erase(
        std::remove_if(m_folders.begin(), m_folders.end(),
            [&](const QString& f) { return f == fullPath || f.startsWith(prefix); }),
        m_folders.end());

    bool changed = (m_folders.size() != beforeFolders);

    if (!presetNames.isEmpty()) {
        const QSet<QString> drop(presetNames.begin(), presetNames.end());
        const auto beforePresets = m_presets.size();
        m_presets.erase(
            std::remove_if(m_presets.begin(), m_presets.end(),
                [&](const BrushPreset& p) { return drop.contains(p.name); }),
            m_presets.end());
        changed = changed || (m_presets.size() != beforePresets);
    }

    if (!changed)
        return;
    savePresets();
    emit presetsChanged();
}

QStringList BrushPresetManager::presetNames() const
{
    QStringList names;
    for (const auto& p : m_presets)
        names << p.name;
    return names;
}
