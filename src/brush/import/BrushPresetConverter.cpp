#include "BrushPresetConverter.hpp"
#include "brush/BrushTextureLibrary.hpp"
#include "brush/BrushTipLibrary.hpp"

#include <QBuffer>
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QtMath>
#include <algorithm>
#include <cmath>

QString BrushPresetConverter::sourceFolderName(BrushImportSourceType type)
{
    switch (type) {
    case BrushImportSourceType::KritaKpp:     return QStringLiteral("Krita");
    case BrushImportSourceType::KritaBundle:  return QStringLiteral("Krita");
    case BrushImportSourceType::GimpBrush:    return QStringLiteral("GIMP Brushes");
    case BrushImportSourceType::ImageBrushTip:return QStringLiteral("Images");
    case BrushImportSourceType::Unknown:
    default:                                  return QStringLiteral("Other");
    }
}

namespace {
QString brushImportSourceId(BrushImportSourceType type)
{
    switch (type) {
    case BrushImportSourceType::KritaKpp:     return QStringLiteral("krita-kpp");
    case BrushImportSourceType::KritaBundle:  return QStringLiteral("krita-bundle");
    case BrushImportSourceType::GimpBrush:    return QStringLiteral("gimp-brush");
    case BrushImportSourceType::ImageBrushTip:return QStringLiteral("image-brush-tip");
    case BrushImportSourceType::Unknown:
    default:                                  return QStringLiteral("unknown");
    }
}

bool propHasValue(float value, float defaultValue = 0.0f)
{
    return std::abs(value - defaultValue) > 0.0001f;
}

DualBrushConfig dualBrushFromImport(const ImportedBrushPreset& imported)
{
    DualBrushConfig dual;
    dual.tipType = std::clamp(imported.dualBrushTipType, 0, 1);
    dual.size = std::clamp(imported.dualBrushSize, 1.0f, 5000.0f);
    dual.spacing = std::clamp(imported.dualBrushSpacing, 0.01f, 10.0f);
    dual.scatter = std::clamp(imported.dualBrushScatter, 0.0f, 10.0f);
    dual.count = std::clamp(imported.dualBrushCount, 1, 16);
    dual.hardness = std::clamp(imported.dualBrushHardness, 0.0f, 1.0f);
    dual.enabled = imported.hasDualBrush
        || dual.tipType != 0
        || propHasValue(dual.size, 20.0f)
        || propHasValue(dual.spacing, 1.0f)
        || propHasValue(dual.scatter)
        || dual.count != 1
        || propHasValue(dual.hardness, 0.8f);
    return dual;
}

BrushBlendMode blendModeFromImport(const QString& name)
{
    const QString key = name.trimmed().toLower().remove(QLatin1Char(' '))
                            .remove(QLatin1Char('_')).remove(QLatin1Char('-'));
    if (key == QLatin1String("multiply")) return BrushBlendMode::Multiply;
    if (key == QLatin1String("screen")) return BrushBlendMode::Screen;
    if (key == QLatin1String("overlay")) return BrushBlendMode::Overlay;
    if (key == QLatin1String("darken")) return BrushBlendMode::Darken;
    if (key == QLatin1String("lighten")) return BrushBlendMode::Lighten;
    if (key == QLatin1String("colordodge")) return BrushBlendMode::ColorDodge;
    if (key == QLatin1String("colorburn")) return BrushBlendMode::ColorBurn;
    if (key == QLatin1String("hardlight")) return BrushBlendMode::HardLight;
    if (key == QLatin1String("softlight")) return BrushBlendMode::SoftLight;
    if (key == QLatin1String("difference")) return BrushBlendMode::Difference;
    if (key == QLatin1String("exclusion")) return BrushBlendMode::Exclusion;
    if (key == QLatin1String("hue")) return BrushBlendMode::Hue;
    if (key == QLatin1String("saturation")) return BrushBlendMode::Saturation;
    if (key == QLatin1String("color")) return BrushBlendMode::Color;
    if (key == QLatin1String("luminosity")) return BrushBlendMode::Luminosity;
    return BrushBlendMode::Normal;
}

// ── Structural mapping (lossless) ─────────────────────────────────────────────

SensorType sensorTypeFromKrita(const QString& id)
{
    const QString s = id.toLower();
    if (s.contains(QLatin1String("tiltdirection"))
        || s.contains(QLatin1String("ascension"))) return SensorType::TiltDirection;
    if (s.contains(QLatin1String("xtilt")))       return SensorType::TiltX;
    if (s.contains(QLatin1String("ytilt")))       return SensorType::TiltY;
    if (s.contains(QLatin1String("declination"))
        || s.contains(QLatin1String("elevation")))return SensorType::TiltElevation;
    if (s.contains(QLatin1String("speed")))       return SensorType::Speed;
    if (s.contains(QLatin1String("drawingangle")))return SensorType::DrawingAngle;
    if (s.contains(QLatin1String("rotation")))    return SensorType::Rotation;
    if (s.contains(QLatin1String("distance")))    return SensorType::Distance;
    if (s.contains(QLatin1String("time")))        return SensorType::Time;
    if (s.contains(QLatin1String("fade")))        return SensorType::Fade;
    // Two distinct fuzzy sensors: per-stroke (one value for the whole stroke)
    // and per-dab. Keep them apart so a "fuzzystroke" rotation stays steady
    // along the stroke instead of jittering every dab.
    if (s.contains(QLatin1String("fuzzystroke")))  return SensorType::FuzzyStroke;
    if (s.contains(QLatin1String("fuzzy"))
        || s.contains(QLatin1String("random")))   return SensorType::Fuzzy;
    return SensorType::Pressure;
}

// Build an engine CurveOption from the imported one, reproducing the source's
// strength, sensors and control points verbatim.
CurveOption curveOptionFromImport(const ImportedCurveOption& in)
{
    CurveOption opt;
    if (!in.present)
        return opt;                       // disabled → evaluator no-op
    opt.enabled = in.enabled;
    opt.minValue = std::clamp(in.minValue, 0.0f, 5.0f);
    opt.maxValue = std::clamp(in.maxValue, 0.0f, 5.0f);
    switch (in.combineMode) {             // source curveMode → engine Combine
    case 1:  opt.combine = CurveOption::Combine::Add; break;
    case 2:  opt.combine = CurveOption::Combine::Max; break;
    case 3:  opt.combine = CurveOption::Combine::Min; break;
    case 4:  opt.combine = CurveOption::Combine::Difference; break;
    default: opt.combine = CurveOption::Combine::Multiply; break;
    }
    // One engine Sensor per source sensor (the sensorslist), each with its own
    // verbatim curve and ramp parameters. An empty list is a flat option: the
    // engine then contributes exactly maxValue (the strength).
    for (const ImportedSensorCurve& sc : in.sensors) {
        Sensor sensor;
        sensor.type = sensorTypeFromKrita(sc.sensor);
        sensor.enabled = true;
        sensor.curve.points = sc.curve;   // verbatim
        sensor.length = std::max(0, sc.length);
        sensor.periodic = sc.periodic;
        sensor.angleOffset = sc.angleOffset;
        opt.sensors.append(sensor);
    }
    return opt;
}

void applyMaskGenerator(BrushSettings& s, const ImportedMaskGenerator& mg)
{
    if (!mg.present)
        return;
    s.type = mg.shape == 1 ? BrushType::Square : BrushType::Round;
    s.autoBrush.profile = std::clamp(mg.profile, 0, 2);
    s.autoBrush.spikes = std::max(2, mg.spikes);
    s.autoBrush.hFade = mg.hFade;
    s.autoBrush.vFade = mg.vFade;
    s.autoBrush.density = std::clamp(mg.density, 0.0f, 1.0f);
    s.autoBrush.randomness = std::clamp(mg.randomness, 0.0f, 1.0f);
    s.autoBrush.softnessCurve.points = mg.softnessCurve;
}

} // namespace

QString BrushPresetConverter::groupPathFor(const ImportedBrushPreset& imported,
                                           const BrushImportOptions& options)
{
    QStringList parts;
    if (!options.rootImportFolderName.isEmpty())
        parts << options.rootImportFolderName;
    if (options.createFolderFromSourceType)
        parts << sourceFolderName(imported.sourceType);

    if (!options.targetGroupName.isEmpty()) {
        parts << options.targetGroupName;
    } else if (options.createFolderFromFileName && !imported.sourceFilePath.isEmpty()) {
        parts << QFileInfo(imported.sourceFilePath).completeBaseName();
    }

    if (options.preserveExternalFolders && !imported.groupName.isEmpty()
        && imported.groupName != parts.value(parts.size() - 1))
        parts << imported.groupName;

    parts.removeAll(QString());
    return parts.join(QLatin1Char('/'));
}

BrushSettings BrushPresetConverter::convertSettings(const ImportedBrushPreset& imported,
                                                    const BrushImportOptions& options) const
{
    BrushSettings s;
    s.size = std::clamp(imported.size, 1.0f, m_maxSize);
    // Engine spacing is a fraction of the tip diameter; imported spacing is a
    // percentage. Keep it inside the engine's sane range.
    s.spacing = std::clamp(imported.spacing / 100.0f, 0.01f, 10.0f);
    s.hardness = std::clamp(imported.hardness, 0.0f, 1.0f);
    s.opacity = std::clamp(imported.opacity, 0.0f, 1.0f);
    s.flow = std::clamp(imported.flow, 0.0f, 1.0f);
    s.angle = qDegreesToRadians(imported.angle);          // engine angle is radians
    s.roundness = std::clamp(imported.roundness, 0.01f, 1.0f);
    s.flipX = imported.flipX;
    s.flipY = imported.flipY;
    s.dualBrushConfig = dualBrushFromImport(imported);

    // Verbatim per-axis curve options (strength + sensors + curves).
    s.sizeOption = curveOptionFromImport(imported.sizeCurve);
    s.opacityOption = curveOptionFromImport(imported.opacityCurve);
    s.flowOption = curveOptionFromImport(imported.flowCurve);
    s.rotationOption = curveOptionFromImport(imported.rotationCurve);
    s.ratioOption = curveOptionFromImport(imported.ratioCurve);
    s.scatterOption = curveOptionFromImport(imported.scatterCurve);
    s.scatter.axisX = imported.scatterAxisX;
    s.scatter.axisY = imported.scatterAxisY;
    // Simple sources with no structured dynamics can still ask for the natural
    // pressure→size response.
    if (imported.dynamics.pressureAffectsSize && !imported.sizeCurve.present)
        s.sizeOption.setPressureSensorEnabled(true);
    // Procedural tip (mask generator) and auto-spacing.
    applyMaskGenerator(s, imported.maskGenerator);
    s.useAutoSpacing = imported.useAutoSpacing;
    s.autoSpacingCoeff = std::max(0.01f, imported.autoSpacingCoeff);
    // Image-tip lightness remap (brightness/contrast/midpoint adjustments).
    if (imported.hasTipRemap) {
        s.tipBrightness = std::clamp(imported.tipBrightness, -1.0f, 1.0f);
        s.tipContrast = std::clamp(imported.tipContrast, -1.0f, 1.0f);
        s.tipMidpoint = std::clamp(imported.tipMidpoint, 0.0f, 1.0f);
    }
    if (!imported.blendModeName.isEmpty())
        s.blendMode = blendModeFromImport(imported.blendModeName);
    if (imported.smoothingMode >= 0)
        s.smoothingMode = static_cast<SmoothingMode>(std::clamp(imported.smoothingMode, 0, 3));
    s.smoothingRadius = std::clamp(imported.smoothingRadius, 0.0f, 500.0f);
    s.airbrush = imported.airbrush;
    s.airbrushRate = std::clamp(imported.airbrushRate, 1.0f, 200.0f);
    if (imported.paintMode >= 0)
        s.paintMode = imported.paintMode == 0 ? BrushPaintMode::BuildUp
                                              : BrushPaintMode::Wash;
    s.wetEdges = imported.wetEdges;

    // Brush tip: store as a self-contained base64 PNG. Resolve the application
    // mode FIRST (the explicit brushApplication attribute is authoritative), then
    // pick the tip representation that mode needs:
    //   ColorStamp   paints the tip's own RGBA      → keep the colour image
    //   LightnessMap modulates the ink by tip grey  → keep the colour image
    //   AlphaMask    paints the ink through coverage → use the alpha-only mask
    // Choosing the image independently of the mode (the old behaviour) collapsed
    // a LightnessMap/ColorStamp tip to the alpha-only mask (RGB = 0), so at paint
    // time the lightness/colour was derived from black and the stroke came out
    // flat/black instead of the tip's grain (e.g. textured-pencil brushes).
    // Default (no explicit application in the source): a colour tip stamps its
    // own colours unless the legacy colour-as-mask flag forces coverage.
    const bool coloredTip = !imported.tip.isAlphaOnly && !imported.tip.colorImage.isNull();
    BrushApplication application = (coloredTip && imported.colorAsMask != 1)
                                       ? BrushApplication::ColorStamp
                                       : BrushApplication::AlphaMask;
    const QString app = imported.brushApplication;
    if (app == QLatin1String("LIGHTNESSMAP"))   application = BrushApplication::LightnessMap;
    else if (app == QLatin1String("IMAGESTAMP"))application = BrushApplication::ColorStamp;
    else if (app == QLatin1String("ALPHAMASK")) application = BrushApplication::AlphaMask;

    const bool needsRgb = application == BrushApplication::ColorStamp
                       || application == BrushApplication::LightnessMap;
    QImage tip = (needsRgb && !imported.tip.colorImage.isNull())
                     ? imported.tip.colorImage
                     : imported.tip.alphaImage;
    if (tip.isNull()) // last resort (e.g. preset icon)
        tip = imported.tip.colorImage.isNull() ? imported.tip.alphaImage
                                               : imported.tip.colorImage;

    if (options.importBrushTips && !tip.isNull()) {
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        tip.save(&buf, "PNG");
        buf.close();
        s.tipSource = BrushTipSource::Image;
        s.tipImageData = QString::fromLatin1(bytes.toBase64());
        s.application = application;
        // Tips are global editor resources: register the imported one so it shows
        // up in the Brush Tip grid for any brush, and reference it by id.
        const QString tipName = imported.name.isEmpty()
            ? QStringLiteral("imported tip") : imported.name;
        s.tipId = BrushTipLibrary::instance()->add(tipName, tip);
    } else {
        s.tipSource = BrushTipSource::Circle;
    }

    // Texture, only when the engine can represent it and the user opted in.
    if (options.importTexturesWhenPossible && imported.hasTexture
        && !imported.textureImage.isNull()) {
        s.textureConfig.enabled = true;
        s.textureConfig.texture = imported.textureImage;
        // Textures are global editor resources: register the imported one so it
        // appears in the Texture panel for any brush, and reference it by id.
        const QString texName = imported.name.isEmpty()
            ? QStringLiteral("imported texture") : imported.name;
        s.textureConfig.textureId =
            BrushTextureLibrary::instance()->add(texName, imported.textureImage);
        s.textureConfig.depth = std::clamp(imported.textureDepth, 0.0f, 1.0f);
        s.textureConfig.scale = imported.textureScale > 0.0f ? imported.textureScale : 1.0f;
        // Brightness/contrast/neutralPoint carry the source format's native values,
        // and the engine's BrushDab applies the identical maths (brightness
        // subtracted, contrast pivoting at 0.5, neutral-point piecewise remap), so
        // pass them straight through. contrast 1.0 = identity.
        s.textureConfig.brightness = std::clamp(imported.textureBrightness, -1.0f, 1.0f);
        s.textureConfig.contrast = std::max(0.0f, imported.textureContrast);
        s.textureConfig.neutralPoint = std::clamp(imported.textureNeutralPoint, 0.0f, 1.0f);
        // Cutoff band + policy carried verbatim. Source policy 0=off, 1=cut to
        // transparent, 2=cut to opaque; the engine's pattern cutoff zeros the value
        // outside the band, so 1 and 2 both map to Pattern (the closest available).
        s.textureConfig.cutoffMin = std::clamp(imported.textureCutoff, 0.0f, 1.0f);
        s.textureConfig.cutoffMax = std::clamp(imported.textureCutoffMax, 0.0f, 1.0f);
        s.textureConfig.cutoffPolicy = imported.textureCutoffPolicy == 0
            ? TextureConfig::CutoffPolicy::Disabled
            : TextureConfig::CutoffPolicy::Pattern;
        s.textureConfig.randomHorizontalOffset = imported.textureRandomOffsetX;
        s.textureConfig.randomVerticalOffset = imported.textureRandomOffsetY;
        s.textureConfig.texturing =
            static_cast<TextureConfig::Texturing>(std::clamp(imported.textureTexturing, 0, 1));
        s.textureConfig.texturingMode =
            static_cast<TextureConfig::TexturingMode>(std::clamp(imported.textureTexturing, 0, 1));
        s.textureConfig.invert = imported.textureInvert;
    }

    return s;
}

BrushPreset BrushPresetConverter::convert(const ImportedBrushPreset& imported,
                                          const BrushImportOptions& options) const
{
    BrushPreset preset;
    preset.name = imported.name.isEmpty() ? QStringLiteral("Unnamed Brush") : imported.name;
    preset.group = groupPathFor(imported, options);
    preset.settings = convertSettings(imported, options);

    QJsonObject src;
    const QFileInfo sourceFile(imported.sourceFilePath);
    src["type"] = brushImportSourceId(imported.sourceType);
    src["fileName"] = sourceFile.completeBaseName();
    src["displayRoot"] = QStringLiteral("Imported Brushes");
    src["importedAt"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    src["originalPresetName"] = imported.name;
    if (!imported.groupName.isEmpty())
        src["externalGroup"] = imported.groupName;
    if (imported.sourceType == BrushImportSourceType::KritaKpp) {
        src["sourceApp"] = QStringLiteral("Krita");
        src["displayGroup"] = QStringLiteral("Individual Presets");
    } else if (imported.sourceType == BrushImportSourceType::KritaBundle) {
        src["sourceApp"] = QStringLiteral("Krita");
        src["bundleName"] = sourceFile.completeBaseName();
    } else if (imported.sourceType == BrushImportSourceType::ImageBrushTip) {
        src["sourceApp"] = QStringLiteral("Image");
    }
    if (!imported.originalEngineName.isEmpty())
        src["originalEngine"] = imported.originalEngineName;
    if (!imported.compatibilityLabel.isEmpty())
        src["compatibility"] = imported.compatibilityLabel;
    preset.sourceInfo = src;

    // Keep the source's own preview/icon as the brush-list thumbnail. Downscale to a
    // bounded size and store as base64 PNG so the preset stays self-contained and the
    // persisted JSON does not balloon with a full-resolution icon.
    if (!imported.previewImage.isNull()) {
        QImage preview = imported.previewImage;
        constexpr int kMaxPreview = 256;
        if (preview.width() > kMaxPreview || preview.height() > kMaxPreview)
            preview = preview.scaled(kMaxPreview, kMaxPreview,
                                     Qt::KeepAspectRatio, Qt::SmoothTransformation);
        QByteArray bytes;
        QBuffer buf(&bytes);
        buf.open(QIODevice::WriteOnly);
        if (preview.save(&buf, "PNG"))
            preset.previewImageData = QString::fromLatin1(bytes.toBase64());
        buf.close();
    }

    return preset;
}
