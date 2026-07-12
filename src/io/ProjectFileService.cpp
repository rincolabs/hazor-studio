#include "ProjectFileService.hpp"

#include "core/AdjustmentTypes.hpp"
#include "core/Layer.hpp"
#include "core/LayerEffect.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/ShapeTypes.hpp"
#include "transform/TransformTypes.hpp"
#include "text/TextTypes.hpp"

#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QDateTime>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QUuid>

#include "animation/AnimationModel.hpp"
#include "animation/AnimationTrack.hpp"
#include "animation/AnimationTypes.hpp"
#include "animation/AnimationValue.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace {
constexpr quint32 kMagic = 0x485A5331; // "HZS1"
constexpr quint32 kFormatVersion = 1;

QString nodeTypeToString(LayerTreeNode::Type type)
{
    switch (type) {
    case LayerTreeNode::Type::Layer: return QStringLiteral("layer");
    case LayerTreeNode::Type::Group: return QStringLiteral("group");
    case LayerTreeNode::Type::Adjustment: return QStringLiteral("adjustment");
    case LayerTreeNode::Type::Effect: return QStringLiteral("effect");
    }
    return QStringLiteral("layer");
}

LayerTreeNode::Type nodeTypeFromString(const QString& type)
{
    if (type == QLatin1String("group")) return LayerTreeNode::Type::Group;
    if (type == QLatin1String("adjustment")) return LayerTreeNode::Type::Adjustment;
    if (type == QLatin1String("effect")) return LayerTreeNode::Type::Effect;
    return LayerTreeNode::Type::Layer;
}

void logImagePixels(const QString& tag, const QImage& img)
{
    if (img.isNull()) { return; }
    QImage rgba = img.convertToFormat(QImage::Format_RGBA8888);
    int total = 0;
    bool foundFirst = false;
    QPoint firstPx;
    QColor firstColor;
    for (int y = 0; y < rgba.height(); ++y) {
        const uchar* row = rgba.constScanLine(y);
        for (int x = 0; x < rgba.width(); ++x) {
            if (row[x * 4 + 3] > 0) {
                if (!foundFirst) {
                    firstPx = QPoint(x, y);
                    firstColor = QColor(row[x*4], row[x*4+1], row[x*4+2], row[x*4+3]);
                    foundFirst = true;
                }
                ++total;
            }
        }
    }
}

QByteArray encodePng(const QImage& img)
{
    QByteArray out;
    QBuffer buf(&out);
    buf.open(QIODevice::WriteOnly);
    img.save(&buf, "PNG");
    return out;
}

QImage decodePng(const QByteArray& bytes)
{
    QImage img;
    img.loadFromData(bytes, "PNG");
    return img;
}

QJsonArray transformToArray(const QTransform& t)
{
    QJsonArray a;
    a.push_back(t.m11()); a.push_back(t.m12()); a.push_back(t.m13());
    a.push_back(t.m21()); a.push_back(t.m22()); a.push_back(t.m23());
    a.push_back(t.m31()); a.push_back(t.m32()); a.push_back(t.m33());
    return a;
}

QTransform transformFromArray(const QJsonArray& a)
{
    if (a.size() != 9) return QTransform();
    return QTransform(
        a[0].toDouble(), a[1].toDouble(), a[2].toDouble(),
        a[3].toDouble(), a[4].toDouble(), a[5].toDouble(),
        a[6].toDouble(), a[7].toDouble(), a[8].toDouble());
}

// ── Animation section (Etapa 10) ────────────────────────────────────────────
// Optional "animation" block. Only base values are persisted (the model is
// independent of the current frame); documents without the block load as plain
// static documents. Loading validates every track and ignores (with a warning)
// anything malformed rather than failing the whole document.
QString interpToString(anim::Interpolation i)
{
    switch (i) {
    case anim::Interpolation::Hold:   return QStringLiteral("hold");
    case anim::Interpolation::Bezier: return QStringLiteral("bezier");
    case anim::Interpolation::Linear: break;
    }
    return QStringLiteral("linear");
}

anim::Interpolation interpFromString(const QString& s)
{
    if (s == QLatin1String("hold"))   return anim::Interpolation::Hold;
    if (s == QLatin1String("bezier")) return anim::Interpolation::Bezier;
    return anim::Interpolation::Linear;
}

QJsonValue keyValueToJson(const anim::AnimationValue& v, anim::ValueType t)
{
    switch (t) {
    case anim::ValueType::Bool: return v.asBool();
    case anim::ValueType::Enum: return v.asEnum();
    case anim::ValueType::Float: break;
    }
    return static_cast<double>(v.asFloat());
}

anim::AnimationValue jsonToKeyValue(const QJsonValue& jv, anim::ValueType t)
{
    switch (t) {
    case anim::ValueType::Bool: return anim::AnimationValue(jv.toBool());
    case anim::ValueType::Enum: return anim::AnimationValue::fromEnum(jv.toInt());
    case anim::ValueType::Float: break;
    }
    return anim::AnimationValue(static_cast<float>(jv.toDouble()));
}

QJsonObject animationToJson(const anim::AnimationModel& m,
                            QMap<QString, QByteArray>& blobs)
{
    QJsonObject o;
    o["version"] = 1;
    o["fps"] = m.fps();
    o["frameRange"] = QJsonArray{ m.startFrame(), m.endFrame() };
    o["playbackRange"] = QJsonArray{ m.playbackStart(), m.playbackEnd() };

    QJsonArray tracks;
    for (const LayerId& id : m.animatedLayers()) {
        const auto* layerTracks = m.tracksFor(id);
        if (!layerTracks) continue;
        for (const auto& [prop, track] : *layerTracks) {
            const anim::ValueType vt = anim::valueType(prop);
            QJsonObject to;
            to["layerId"] = id.toString(QUuid::WithoutBraces);
            to["property"] = QString(anim::propertyId(prop));
            to["defaultInterp"] = interpToString(track.defaultInterpolation());
            QJsonArray keys;
            for (const anim::Keyframe& k : track.keyframes()) {
                QJsonObject ko;
                ko["frame"] = k.frame;
                ko["value"] = keyValueToJson(k.value, vt);
                ko["interp"] = interpToString(k.interpolation);
                if (k.interpolation == anim::Interpolation::Bezier) {
                    ko["inTanX"] = k.inTangentX;   ko["inTanY"] = k.inTangentY;
                    ko["outTanX"] = k.outTangentX; ko["outTanY"] = k.outTangentY;
                }
                keys.push_back(ko);
            }
            to["keyframes"] = keys;
            tracks.push_back(to);
        }
    }
    o["tracks"] = tracks;

    QJsonArray cels;
    for (const anim::CelId& celId : m.celStorage().ids()) {
        const auto* content = m.celStorage().content(celId);
        if (!content) continue;
        QJsonObject co;
        const QString id = celId.toString(QUuid::WithoutBraces);
        co["id"] = id;
        co["bounds"] = QJsonArray{ content->bounds.x(), content->bounds.y(),
                                    content->bounds.width(), content->bounds.height() };
        co["metadata"] = QJsonObject::fromVariantMap(content->metadata);
        if (!content->cpuImage.isNull()) {
            const QString key = QStringLiteral("animation/cels/%1_base.png").arg(id);
            blobs.insert(key, encodePng(content->cpuImage));
            co["baseSource"] = key;
        }
        if (content->rasterStorage.isEnabled()) {
            QRect tileBounds;
            const QImage tiles = content->rasterStorage.toImage(&tileBounds);
            co["tileSize"] = content->rasterStorage.tileSize();
            co["baseWidth"] = content->rasterStorage.baseSize().width();
            co["baseHeight"] = content->rasterStorage.baseSize().height();
            co["tileOriginX"] = tileBounds.x();
            co["tileOriginY"] = tileBounds.y();
            if (!tiles.isNull()) {
                const QString key = QStringLiteral("animation/cels/%1_tiles.png").arg(id);
                blobs.insert(key, encodePng(tiles));
                co["tilesSource"] = key;
            }
        }
        cels.push_back(co);
    }
    o["cels"] = cels;

    QJsonArray rasterTracks;
    for (const LayerId& layerId : m.rasterAnimatedLayers()) {
        const auto* track = m.rasterTrack(layerId);
        if (!track || track->isEmpty()) continue;
        QJsonObject to;
        to["layerId"] = layerId.toString(QUuid::WithoutBraces);
        QJsonArray keys;
        for (const auto& [frame, celId] : track->keyframes()) {
            QJsonObject ko;
            ko["frame"] = frame;
            if (celId)
                ko["celId"] = celId->toString(QUuid::WithoutBraces);
            else
                ko["empty"] = true;
            keys.push_back(ko);
        }
        to["keyframes"] = keys;
        rasterTracks.push_back(to);
    }
    o["rasterTracks"] = rasterTracks;
    return o;
}

void animationFromJson(const QJsonObject& o, anim::AnimationModel& m,
                       const QSet<QUuid>& validIds,
                       const QMap<QString, QByteArray>& blobs)
{
    if (o.isEmpty())
        return;  // old document: no animation section, stays static

    m.setFps(o.value("fps").toDouble(24.0));
    const QJsonArray fr = o.value("frameRange").toArray();
    if (fr.size() == 2) m.setFrameRange(fr[0].toInt(), fr[1].toInt());
    const QJsonArray pr = o.value("playbackRange").toArray();
    if (pr.size() == 2) m.setPlaybackRange(pr[0].toInt(), pr[1].toInt());

    for (const QJsonValue& cv : o.value("cels").toArray()) {
        if (!cv.isObject()) continue;
        const QJsonObject co = cv.toObject();
        const anim::CelId id = QUuid::fromString(co.value("id").toString());
        if (id.isNull()) continue;
        anim::RasterCelContent content;
        const QJsonArray bounds = co.value("bounds").toArray();
        if (bounds.size() == 4)
            content.bounds = QRect(bounds[0].toInt(), bounds[1].toInt(),
                                   bounds[2].toInt(), bounds[3].toInt());
        content.metadata = co.value("metadata").toObject().toVariantMap();
        const QString baseSource = co.value("baseSource").toString();
        if (!baseSource.isEmpty() && blobs.contains(baseSource))
            content.cpuImage = decodePng(blobs.value(baseSource))
                .convertToFormat(QImage::Format_RGBA8888);
        const QString tilesSource = co.value("tilesSource").toString();
        if (!tilesSource.isEmpty() && blobs.contains(tilesSource)) {
            const QImage tiles = decodePng(blobs.value(tilesSource))
                .convertToFormat(QImage::Format_RGBA8888);
            content.rasterStorage.replaceWithImage(
                tiles,
                QPoint(co.value("tileOriginX").toInt(), co.value("tileOriginY").toInt()),
                co.value("tileSize").toInt(256));
            const QSize baseSize(co.value("baseWidth").toInt(),
                                 co.value("baseHeight").toInt());
            if (baseSize.isValid() && !baseSize.isEmpty())
                content.rasterStorage.setBaseSize(baseSize);
        }
        if (!m.celStorage().insertCel(id, std::move(content)))
            qWarning() << "animation: skipping duplicate cel" << id;
    }

    for (const QJsonValue& tv : o.value("tracks").toArray()) {
        if (!tv.isObject()) continue;
        const QJsonObject to = tv.toObject();

        const QUuid id = QUuid::fromString(to.value("layerId").toString());
        if (id.isNull()) {
            qWarning() << "animation: skipping track with invalid layerId";
            continue;
        }
        if (!validIds.isEmpty() && !validIds.contains(id)) {
            qWarning() << "animation: skipping track for unknown layer" << id.toString();
            continue;
        }
        anim::Property prop;
        if (!anim::propertyFromId(to.value("property").toString(), prop)) {
            qWarning() << "animation: skipping unsupported property"
                       << to.value("property").toString();
            continue;
        }

        const anim::ValueType vt = anim::valueType(prop);
        anim::AnimationTrack track(prop);
        track.setDefaultInterpolation(interpFromString(to.value("defaultInterp").toString()));
        for (const QJsonValue& kv : to.value("keyframes").toArray()) {
            if (!kv.isObject()) continue;
            const QJsonObject ko = kv.toObject();
            if (!ko.contains("frame") || !ko.contains("value")) {
                qWarning() << "animation: skipping malformed keyframe";
                continue;
            }
            anim::Keyframe k(ko.value("frame").toInt(),
                             jsonToKeyValue(ko.value("value"), vt),
                             interpFromString(ko.value("interp").toString()));
            if (ko.contains("inTanX")) {
                k.inTangentX = static_cast<float>(ko.value("inTanX").toDouble());
                k.inTangentY = static_cast<float>(ko.value("inTanY").toDouble());
                k.outTangentX = static_cast<float>(ko.value("outTanX").toDouble());
                k.outTangentY = static_cast<float>(ko.value("outTanY").toDouble());
            }
            track.setKeyframe(k);  // duplicate frames collapse (last wins)
        }
        if (!track.isEmpty())
            m.ensureTrack(id, prop) = track;
    }

    for (const QJsonValue& tv : o.value("rasterTracks").toArray()) {
        if (!tv.isObject()) continue;
        const QJsonObject to = tv.toObject();
        const LayerId layerId = QUuid::fromString(to.value("layerId").toString());
        if (layerId.isNull() || (!validIds.isEmpty() && !validIds.contains(layerId))) {
            qWarning() << "animation: skipping raster track for invalid layer";
            continue;
        }
        anim::RasterCelTrack track;
        for (const QJsonValue& kv : to.value("keyframes").toArray()) {
            if (!kv.isObject()) continue;
            const QJsonObject ko = kv.toObject();
            if (!ko.contains("frame")) continue;
            const int frame = ko.value("frame").toInt();
            if (ko.value("empty").toBool(false)) {
                track.setCel(frame, std::nullopt);
                continue;
            }
            const anim::CelId celId = QUuid::fromString(ko.value("celId").toString());
            if (celId.isNull() || !m.celStorage().contains(celId)) {
                qWarning() << "animation: raster key references unknown cel";
                continue;
            }
            track.setCel(frame, celId);
        }
        if (!track.isEmpty())
            m.ensureRasterTrack(layerId) = std::move(track);
    }
    m.pruneUnusedCels();
}

// Collect the stable ids of every node in a loaded tree, for validating that
// animation tracks reference layers that actually exist.
void collectNodeIds(const std::vector<std::unique_ptr<LayerTreeNode>>& roots,
                    QSet<QUuid>& out)
{
    for (const auto& n : roots) {
        if (!n) continue;
        out.insert(n->id);
        collectNodeIds(n->children, out);
    }
}

QJsonObject colorProfileToJson(const ColorProfile& profile, ColorProfileSource source)
{
    QJsonObject o;
    o["profileName"] = profile.displayName();
    o["profileKind"] = colorProfileKindToString(profile.kind());
    o["profileSource"] = colorProfileSourceToString(source);
    o["workingSpaceName"] = profile.displayName();
    if (!profile.iccBytes().isEmpty())
        o["iccBase64"] = QString::fromLatin1(profile.iccBytes().toBase64());
    return o;
}

ColorProfile colorProfileFromJson(const QJsonObject& o, ColorProfileSource* source)
{
    if (source) {
        *source = colorProfileSourceFromString(
            o.value("profileSource").toString(QStringLiteral("GeneratedDefault")));
    }

    const QByteArray icc = QByteArray::fromBase64(
        o.value("iccBase64").toString().toLatin1());
    if (!icc.isEmpty()) {
        ColorProfile profile = ColorProfile::fromIccBytes(icc);
        if (profile.isValid()) {
            if (source)
                profile.setSource(*source);
            return profile;
        }
    }

    ColorProfile profile = ColorProfile::fromKind(
        colorProfileKindFromString(o.value("profileKind").toString()));
    if (!profile.isValid())
        profile = ColorProfile::sRgb();
    if (source)
        profile.setSource(*source);
    return profile;
}

QJsonObject spanToJson(const TextSpan& s)
{
    QJsonObject o;
    o["start"] = s.start;
    o["end"] = s.end;
    o["fontFamily"] = s.fontFamily;
    o["fontSize"] = s.fontSize;
    o["color"] = s.color.name(QColor::HexArgb);
    o["bold"] = s.bold;
    o["italic"] = s.italic;
    o["underline"] = s.underline;
    o["strikethrough"] = s.strikethrough;
    o["letterSpacing"] = s.letterSpacing;
    // Future-ready character style (stored even though not yet in the UI).
    o["kerning"] = s.kerning;
    o["baselineShift"] = s.baselineShift;
    o["smallCaps"] = s.smallCaps;
    o["superscript"] = s.superscript;
    o["subscript"] = s.subscript;
    o["openTypeFeatures"] = s.openTypeFeatures;
    return o;
}

TextSpan spanFromJson(const QJsonObject& o)
{
    TextSpan s;
    s.start = o.value("start").toInt();
    s.end = o.value("end").toInt();
    s.fontFamily = o.value("fontFamily").toString("Sans Serif");
    s.fontSize = static_cast<float>(o.value("fontSize").toDouble(32.0));
    s.color = QColor(o.value("color").toString("#FF000000"));
    s.bold = o.value("bold").toBool(false);
    s.italic = o.value("italic").toBool(false);
    s.underline = o.value("underline").toBool(false);
    s.strikethrough = o.value("strikethrough").toBool(false);
    s.letterSpacing = static_cast<float>(o.value("letterSpacing").toDouble(0.0));
    s.kerning = static_cast<float>(o.value("kerning").toDouble(0.0));
    s.baselineShift = static_cast<float>(o.value("baselineShift").toDouble(0.0));
    s.smallCaps = o.value("smallCaps").toBool(false);
    s.superscript = o.value("superscript").toBool(false);
    s.subscript = o.value("subscript").toBool(false);
    s.openTypeFeatures = o.value("openTypeFeatures").toString();
    return s;
}

QJsonObject paragraphToJson(const ParagraphStyle& p)
{
    QJsonObject o;
    o["alignment"] = static_cast<int>(p.alignment);
    o["lineHeight"] = p.lineHeight;
    o["spaceBefore"] = p.spaceBefore;
    o["spaceAfter"] = p.spaceAfter;
    o["firstLineIndent"] = p.firstLineIndent;
    o["leftIndent"] = p.leftIndent;
    o["rightIndent"] = p.rightIndent;
    o["hyphenation"] = p.hyphenation;
    return o;
}

ParagraphStyle paragraphFromJson(const QJsonObject& o)
{
    ParagraphStyle p;
    p.alignment = static_cast<TextAlign>(o.value("alignment").toInt(0));
    p.lineHeight = static_cast<float>(o.value("lineHeight").toDouble(1.2));
    p.spaceBefore = static_cast<float>(o.value("spaceBefore").toDouble(0.0));
    p.spaceAfter = static_cast<float>(o.value("spaceAfter").toDouble(0.0));
    p.firstLineIndent = static_cast<float>(o.value("firstLineIndent").toDouble(0.0));
    p.leftIndent = static_cast<float>(o.value("leftIndent").toDouble(0.0));
    p.rightIndent = static_cast<float>(o.value("rightIndent").toDouble(0.0));
    p.hyphenation = o.value("hyphenation").toBool(false);
    return p;
}

QJsonObject textDataToJson(const TextLayerData& t)
{
    QJsonObject o;
    o["text"] = t.text;
    o["align"] = static_cast<int>(t.align);
    o["lineSpacing"] = t.lineSpacing;
    o["flowMode"] = static_cast<int>(t.flowMode);
    QJsonObject box;
    box["width"] = t.box.width;
    box["height"] = t.box.height;
    o["box"] = box;
    QJsonArray spans;
    for (const auto& s : t.spans)
        spans.push_back(spanToJson(s));
    o["spans"] = spans;
    QJsonArray paragraphs;
    for (const auto& p : t.paragraphs)
        paragraphs.push_back(paragraphToJson(p));
    o["paragraphs"] = paragraphs;
    return o;
}

TextLayerData textDataFromJson(const QJsonObject& o)
{
    TextLayerData t;
    t.text = o.value("text").toString();
    t.align = static_cast<TextAlign>(o.value("align").toInt(0));
    t.lineSpacing = static_cast<float>(o.value("lineSpacing").toDouble(1.2));
    t.flowMode = static_cast<TextFlowMode>(o.value("flowMode").toInt(0));
    const QJsonObject box = o.value("box").toObject();
    t.box.width = static_cast<float>(box.value("width").toDouble(0.0));
    t.box.height = static_cast<float>(box.value("height").toDouble(0.0));
    for (const auto v : o.value("spans").toArray()) {
        if (v.isObject()) t.spans.push_back(spanFromJson(v.toObject()));
    }
    for (const auto v : o.value("paragraphs").toArray()) {
        if (v.isObject()) t.paragraphs.push_back(paragraphFromJson(v.toObject()));
    }
    normalizeParagraphs(t);   // backfill for legacy projects without paragraph data
    t.dirty = true;
    return t;
}

QJsonObject colorToJson(const QColor& color)
{
    QJsonObject o;
    o["value"] = color.name(QColor::HexArgb);
    return o;
}

QColor colorFromJson(const QJsonValue& value, const QColor& fallback)
{
    if (!value.isObject())
        return fallback;
    QColor color(value.toObject().value("value").toString(fallback.name(QColor::HexArgb)));
    return color.isValid() ? color : fallback;
}

QJsonObject pointToJson(const QPointF& p)
{
    QJsonObject o;
    o["x"] = p.x();
    o["y"] = p.y();
    return o;
}

QPointF pointFromJson(const QJsonValue& value, const QPointF& fallback)
{
    if (!value.isObject())
        return fallback;
    const QJsonObject o = value.toObject();
    return QPointF(o.value("x").toDouble(fallback.x()),
                   o.value("y").toDouble(fallback.y()));
}

QJsonObject sizeToJson(const QSizeF& s)
{
    QJsonObject o;
    o["width"] = s.width();
    o["height"] = s.height();
    return o;
}

QSizeF sizeFromJson(const QJsonValue& value, const QSizeF& fallback)
{
    if (!value.isObject())
        return fallback;
    const QJsonObject o = value.toObject();
    return QSizeF(o.value("width").toDouble(fallback.width()),
                  o.value("height").toDouble(fallback.height()));
}

QJsonArray rectToArray(const QRectF& r)
{
    QJsonArray a;
    a.push_back(r.x());
    a.push_back(r.y());
    a.push_back(r.width());
    a.push_back(r.height());
    return a;
}

QRectF rectFromArray(const QJsonValue& value, const QRectF& fallback)
{
    if (!value.isArray())
        return fallback;
    const QJsonArray a = value.toArray();
    if (a.size() != 4)
        return fallback;
    return QRectF(a[0].toDouble(), a[1].toDouble(),
                  a[2].toDouble(), a[3].toDouble());
}

QString pathCommandTypeToString(PathCommandType type)
{
    switch (type) {
    case PathCommandType::MoveTo: return QStringLiteral("moveTo");
    case PathCommandType::LineTo: return QStringLiteral("lineTo");
    case PathCommandType::QuadTo: return QStringLiteral("quadTo");
    case PathCommandType::CubicTo: return QStringLiteral("cubicTo");
    case PathCommandType::ClosePath: return QStringLiteral("closePath");
    }
    return QStringLiteral("moveTo");
}

PathCommandType pathCommandTypeFromString(const QString& value)
{
    if (value == QLatin1String("lineTo")) return PathCommandType::LineTo;
    if (value == QLatin1String("quadTo")) return PathCommandType::QuadTo;
    if (value == QLatin1String("cubicTo")) return PathCommandType::CubicTo;
    if (value == QLatin1String("closePath")) return PathCommandType::ClosePath;
    return PathCommandType::MoveTo;
}

QJsonObject pathCommandToJson(const PathCommand& command)
{
    QJsonObject o;
    o["type"] = pathCommandTypeToString(command.type);
    if (command.type == PathCommandType::MoveTo
        || command.type == PathCommandType::LineTo
        || command.type == PathCommandType::QuadTo
        || command.type == PathCommandType::CubicTo)
        o["p1"] = pointToJson(command.p1);
    if (command.type == PathCommandType::QuadTo
        || command.type == PathCommandType::CubicTo)
        o["p2"] = pointToJson(command.p2);
    if (command.type == PathCommandType::CubicTo)
        o["p3"] = pointToJson(command.p3);
    return o;
}

PathCommand pathCommandFromJson(const QJsonObject& o)
{
    PathCommand command;
    command.type = pathCommandTypeFromString(o.value("type").toString());
    command.p1 = pointFromJson(o.value("p1"), command.p1);
    command.p2 = pointFromJson(o.value("p2"), command.p2);
    command.p3 = pointFromJson(o.value("p3"), command.p3);
    return command;
}

QJsonObject shapeDataToJson(const ShapeData& s)
{
    QJsonObject o;
    o["version"] = 2;

    QJsonObject path;
    path["fillRule"] = s.path.fillRule == Qt::OddEvenFill ? QStringLiteral("oddEven") : QStringLiteral("winding");
    path["closed"] = s.path.closed;
    path["localBounds"] = rectToArray(s.path.localBounds);
    QJsonArray commands;
    for (const auto& command : s.path.commands)
        commands.push_back(pathCommandToJson(command));
    path["commands"] = commands;
    o["path"] = path;

    QJsonObject style;
    style["fillEnabled"] = s.style.fillEnabled;
    style["fillColor"] = colorToJson(s.style.fillColor);
    style["strokeEnabled"] = s.style.strokeEnabled;
    style["strokeColor"] = colorToJson(s.style.strokeColor);
    style["strokeWidth"] = s.style.strokeWidth;
    style["antiAlias"] = s.style.antiAlias;
    o["style"] = style;

    QJsonObject transform;
    transform["localToCanvas"] = transformToArray(s.transform.localToCanvas);
    o["transform"] = transform;

    QJsonObject metadata;
    metadata["presetId"] = s.metadata.presetId;
    metadata["parameters"] = QJsonObject::fromVariantMap(s.metadata.parameters);
    metadata["parametricEditable"] = s.metadata.parametricEditable;
    o["metadata"] = metadata;
    return o;
}

ShapeData shapeDataFromJson(const QJsonObject& o)
{
    ShapeData s;
    const QJsonObject path = o.value("path").toObject();
    s.path.fillRule = path.value("fillRule").toString() == QLatin1String("oddEven")
        ? Qt::OddEvenFill
        : Qt::WindingFill;
    s.path.closed = path.value("closed").toBool(true);
    s.path.localBounds = rectFromArray(path.value("localBounds"), QRectF());
    for (const auto value : path.value("commands").toArray()) {
        if (value.isObject())
            s.path.commands.push_back(pathCommandFromJson(value.toObject()));
    }

    const QJsonObject style = o.value("style").toObject();
    s.style.fillEnabled = style.value("fillEnabled").toBool(s.style.fillEnabled);
    s.style.fillColor = colorFromJson(style.value("fillColor"), s.style.fillColor);
    s.style.strokeEnabled = style.value("strokeEnabled").toBool(s.style.strokeEnabled);
    s.style.strokeColor = colorFromJson(style.value("strokeColor"), s.style.strokeColor);
    s.style.strokeWidth = std::max(0.0, style.value("strokeWidth").toDouble(s.style.strokeWidth));
    s.style.antiAlias = style.value("antiAlias").toBool(s.style.antiAlias);

    const QJsonObject transform = o.value("transform").toObject();
    s.transform.localToCanvas = transformFromArray(transform.value("localToCanvas").toArray());

    const QJsonObject metadata = o.value("metadata").toObject();
    s.metadata.presetId = metadata.value("presetId").toString();
    s.metadata.parameters = metadata.value("parameters").toObject().toVariantMap();
    s.metadata.parametricEditable = metadata.value("parametricEditable").toBool(false);
    return s;
}

QJsonObject quadToJson(const TransformQuad& q)
{
    QJsonObject o;
    o["tl"] = pointToJson(q.topLeft);
    o["tr"] = pointToJson(q.topRight);
    o["br"] = pointToJson(q.bottomRight);
    o["bl"] = pointToJson(q.bottomLeft);
    return o;
}

TransformQuad quadFromJson(const QJsonObject& o)
{
    TransformQuad q;
    q.topLeft     = pointFromJson(o.value("tl"), q.topLeft);
    q.topRight    = pointFromJson(o.value("tr"), q.topRight);
    q.bottomRight = pointFromJson(o.value("br"), q.bottomRight);
    q.bottomLeft  = pointFromJson(o.value("bl"), q.bottomLeft);
    return q;
}

// Distort metadata is serialized as JSON (quad/mode/origin); the sourceImage is
// stored as a separate PNG blob (see source/load wiring), like the layer pixels.
QJsonObject distortDataToJson(const DistortData& d, const QString& sourceImageRef)
{
    QJsonObject o;
    o["mode"] = static_cast<int>(d.mode);
    o["sourceQuad"] = quadToJson(d.sourceQuad);
    o["quad"] = quadToJson(d.quad);
    o["resultOriginX"] = d.resultOrigin.x();
    o["resultOriginY"] = d.resultOrigin.y();
    o["quadTransform"] = transformToArray(d.quadTransform);
    o["sourceImage"] = sourceImageRef;
    return o;
}

QJsonValue variantToJson(const QVariant& value)
{
    if (value.canConvert<QColor>()) {
        QColor color = value.value<QColor>();
        if (color.isValid()) return color.name(QColor::HexArgb);
    }
    switch (value.metaType().id()) {
    case QMetaType::Bool: return value.toBool();
    case QMetaType::Int: return value.toInt();
    case QMetaType::Double:
    case QMetaType::Float: return value.toDouble();
    case QMetaType::QString: return value.toString();
    default: return value.toString();
    }
}

QVariant jsonToVariant(const QJsonValue& value)
{
    if (value.isBool()) return value.toBool();
    if (value.isDouble()) return value.toDouble();
    if (value.isString()) {
        const QString s = value.toString();
        const QColor color(s);
        if (color.isValid() && (s.startsWith(QLatin1Char('#')) || s.startsWith(QLatin1String("rgba"))))
            return color;
        return s;
    }
    return {};
}

QJsonObject variantMapToJson(const QVariantMap& map)
{
    QJsonObject o;
    for (auto it = map.begin(); it != map.end(); ++it)
        o[it.key()] = variantToJson(it.value());
    return o;
}

QVariantMap variantMapFromJson(const QJsonObject& o)
{
    QVariantMap map;
    for (auto it = o.begin(); it != o.end(); ++it)
        map[it.key()] = jsonToVariant(it.value());
    return map;
}

QJsonObject effectToJson(const LayerEffect& effect)
{
    QJsonObject o;
    o["type"] = effect.type;
    o["enabled"] = effect.enabled;
    o["params"] = variantMapToJson(effect.params);
    o["defaultParams"] = variantMapToJson(effect.defaultParams);
    return o;
}

LayerEffect effectFromJson(const QJsonObject& o)
{
    LayerEffect effect;
    effect.type = o.value("type").toString();
    effect.enabled = o.value("enabled").toBool(true);
    effect.params = variantMapFromJson(o.value("params").toObject());
    effect.defaultParams = variantMapFromJson(o.value("defaultParams").toObject());
    if (effect.params.isEmpty() && LayerEffect::isLayerStyle(effect.type))
        effect.params = LayerEffect::defaultStyleParams(effect.type);
    return effect;
}

QJsonObject nodeToJson(const LayerTreeNode& node,
                       int& nextId,
                       QMap<QString, QByteArray>& blobs)
{
    QJsonObject o;
    // Ephemeral, per-save key used only to name this node's blobs inside the
    // file (layers/<id>.png). NOT the node's identity.
    const QString id = QStringLiteral("layer_%1").arg(nextId++, 4, 10, QLatin1Char('0'));
    o["id"] = id;
    // Stable persistent identity — survives reload and reorganization, and is
    // what the animation model keys tracks by.
    o["nodeId"] = node.id.toString(QUuid::WithoutBraces);
    o["name"] = node.name;
    o["type"] = nodeTypeToString(node.type);
    // Persist the BASE state — the static values, never the current frame's
    // evaluated result (evaluatedState == baseState in a static document).
    o["visible"] = node.baseVisible();
    o["opacity"] = node.baseOpacity();
    o["blendMode"] = static_cast<int>(node.baseBlendMode());
    o["groupBlendMode"] = static_cast<int>(node.groupBlendMode);
    o["lockFlags"] = node.lockFlags;
    o["collapsed"] = node.collapsed;
    o["clipped"] = node.clipped;
    o["nameIsAuto"] = node.nameIsAuto;
    o["transform"] = transformToArray(node.baseTransform());
    QJsonArray effects;
    for (const auto& effect : node.effects)
        effects.push_back(effectToJson(effect));
    o["effects"] = effects;

    // Adjustment layers persist their type/params + mask, but never a color
    // PNG (their cpuImage is just a transparent coordinate-space buffer,
    // rebuilt on load).
    if (node.type == LayerTreeNode::Type::Adjustment && node.adjustment) {
        o["adjustmentType"] = node.adjustment->type;
        o["adjustmentParams"] = variantMapToJson(node.adjustment->params);
        if (node.layer) {
            o["maskVisible"] = node.layer->maskVisible;
            o["maskDensity"] = node.layer->maskDensity;
            o["maskFeather"] = node.layer->maskFeather;
            if (!node.layer->maskImage.isNull()) {
                const QString maskSource = QStringLiteral("layers/%1_mask.png").arg(id);
                blobs.insert(maskSource, encodePng(node.layer->maskImage));
                o["maskSource"] = maskSource;
                o["maskOriginX"] = node.layer->maskOrigin.x();
                o["maskOriginY"] = node.layer->maskOrigin.y();
            }
        }
        QJsonArray adjChildren;
        for (const auto& c : node.children)
            adjChildren.push_back(nodeToJson(*c, nextId, blobs));
        o["children"] = adjChildren;
        return o;
    }

    if (node.layer) {
        const QString source = QStringLiteral("layers/%1.png").arg(id);
        if (node.layer->rasterStorage.isEnabled()) {
            QRect bounds;
            QImage rasterImg = node.layer->rasterStorage.toImage(&bounds);
            const QImage& imgToSave = rasterImg.isNull() ? node.layer->cpuImage : rasterImg;
            blobs.insert(source, encodePng(imgToSave));
            logImagePixels("[Save]   imgToSave", imgToSave);
            if (!bounds.isNull() && bounds.topLeft() != QPoint(0, 0)) {
                o["rasterOriginX"] = bounds.left();
                o["rasterOriginY"] = bounds.top();
            }
            o["rasterTileSize"] = node.layer->rasterStorage.tileSize();
            const QSize baseSize = node.layer->rasterBaseSize();
            o["rasterBaseWidth"]  = baseSize.width();
            o["rasterBaseHeight"] = baseSize.height();
        } else {
            blobs.insert(source, encodePng(node.layer->cpuImage));
        }
        o["source"] = source;
        o["maskVisible"] = node.layer->maskVisible;
        o["maskDensity"] = node.layer->maskDensity;
        o["maskFeather"] = node.layer->maskFeather;
        o["hasResetTransform"] = node.layer->hasResetTransform;
        o["resetTransform"] = transformToArray(node.layer->resetTransform);
        if (!node.layer->maskImage.isNull()) {
            const QString maskSource = QStringLiteral("layers/%1_mask.png").arg(id);
            blobs.insert(maskSource, encodePng(node.layer->maskImage));
            o["maskSource"] = maskSource;
            // The mask can cover expanded bounds (origin != 0,0); without it the
            // reload renders the mask shifted/cropped against the layer pixels.
            o["maskOriginX"] = node.layer->maskOrigin.x();
            o["maskOriginY"] = node.layer->maskOrigin.y();
        }
        if (node.layer->textData) {
            o["textData"] = textDataToJson(*node.layer->textData);
        }
        if (node.layer->shapeData) {
            o["shapeData"] = shapeDataToJson(*node.layer->shapeData);
        }
        if (node.layer->distortData && !node.layer->distortData->sourceImage.isNull()) {
            const QString distSource = QStringLiteral("layers/%1_distort.png").arg(id);
            blobs.insert(distSource, encodePng(node.layer->distortData->sourceImage));
            o["distortData"] = distortDataToJson(*node.layer->distortData, distSource);
        }
    }

    QJsonArray children;
    for (const auto& c : node.children) {
        children.push_back(nodeToJson(*c, nextId, blobs));
    }
    o["children"] = children;
    return o;
}

std::unique_ptr<LayerTreeNode> nodeFromJson(const QJsonObject& o,
                                            const QMap<QString, QByteArray>& blobs,
                                            QString* error)
{
    auto node = std::make_unique<LayerTreeNode>();
    node->name = o.value("name").toString("Layer");
    node->type = nodeTypeFromString(o.value("type").toString("layer"));
    // Restore the stable identity. Documents saved before nodeId existed keep
    // the fresh id generated in the constructor (persisted on the next save).
    const QUuid nid = QUuid::fromString(o.value("nodeId").toString());
    if (!nid.isNull())
        node->id = nid;
    node->setBaseVisible(o.value("visible").toBool(true));
    node->setBaseOpacity(static_cast<float>(o.value("opacity").toDouble(1.0)));
    node->setBaseBlendMode(static_cast<BlendMode>(o.value("blendMode").toInt(0)));
    // Default 0 == GroupBlendMode::Isolated so projects saved before group
    // blend modes existed load with the safe default.
    node->groupBlendMode =
        static_cast<LayerTreeNode::GroupBlendMode>(o.value("groupBlendMode").toInt(0));
    node->lockFlags = o.value("lockFlags").toInt(0);
    node->collapsed = o.value("collapsed").toBool(false);
    node->clipped = o.value("clipped").toBool(false);
    node->nameIsAuto = o.value("nameIsAuto").toBool(false);
    node->setBaseTransform(transformFromArray(o.value("transform").toArray()));
    for (const auto v : o.value("effects").toArray()) {
        if (v.isObject())
            node->effects.push_back(effectFromJson(v.toObject()));
    }
    node->invalidateEffects();

    // Adjustment layers: rebuild the payload + mask (their cpuImage is a
    // transparent coordinate-space buffer, recreated to cover the mask).
    if (node->type == LayerTreeNode::Type::Adjustment) {
        node->adjustment = std::make_shared<AdjustmentData>();
        node->adjustment->type = o.value("adjustmentType").toString();
        node->adjustment->params = variantMapFromJson(o.value("adjustmentParams").toObject());

        node->layer = std::make_shared<Layer>();
        node->layer->name = node->name;
        node->layer->owner = node.get();
        node->layer->maskVisible = o.value("maskVisible").toBool(true);
        node->layer->maskDensity = static_cast<float>(o.value("maskDensity").toDouble(1.0));
        node->layer->maskFeather = static_cast<float>(o.value("maskFeather").toDouble(0.0));

        const QString maskSource = o.value("maskSource").toString();
        QSize coverage(1, 1);
        if (!maskSource.isEmpty() && blobs.contains(maskSource)) {
            node->layer->maskImage =
                decodePng(blobs.value(maskSource)).convertToFormat(QImage::Format_Grayscale8);
            node->layer->maskOrigin = QPoint(o.value("maskOriginX").toInt(0),
                                             o.value("maskOriginY").toInt(0));
            coverage = QSize(node->layer->maskOrigin.x() + node->layer->maskImage.width(),
                             node->layer->maskOrigin.y() + node->layer->maskImage.height());
        }
        node->layer->cpuImage = QImage(coverage.expandedTo(QSize(1, 1)),
                                       QImage::Format_RGBA8888);
        node->layer->cpuImage.fill(Qt::transparent);
        node->layer->maskThumbDirty = true;

        for (const auto v : o.value("children").toArray()) {
            if (!v.isObject()) continue;
            auto child = nodeFromJson(v.toObject(), blobs, error);
            if (!child) return nullptr;
            child->parent = node.get();
            node->children.push_back(std::move(child));
        }
        return node;
    }

    const QString source = o.value("source").toString();
    if (!source.isEmpty()) {
        if (!blobs.contains(source)) {
            if (error) *error = QStringLiteral("Layer image missing: %1").arg(source);
            return nullptr;
        }
        node->layer = std::make_shared<Layer>();
        node->layer->name = node->name;
        node->layer->cpuImage = decodePng(blobs.value(source)).convertToFormat(QImage::Format_RGBA8888);
        if (node->layer->cpuImage.isNull()) {
            if (error) *error = QStringLiteral("Invalid layer image: %1").arg(source);
            return nullptr;
        }
        if (o.contains("rasterTileSize")) {
            const int tileSize = o.value("rasterTileSize").toInt(256);
            const QPoint origin(o.value("rasterOriginX").toInt(0),
                                o.value("rasterOriginY").toInt(0));
            logImagePixels("[Load]   cpuImage before restore", node->layer->cpuImage);
            node->layer->replaceRasterStorageWithImage(node->layer->cpuImage, origin, tileSize);
            if (o.contains("rasterBaseWidth")) {
                const QSize baseSize(o.value("rasterBaseWidth").toInt(0),
                                     o.value("rasterBaseHeight").toInt(0));
                if (baseSize.isValid() && !baseSize.isEmpty())
                    node->layer->rasterStorage.setBaseSize(baseSize);
            }
            {
                QRect lb;
                QImage reconstructed = node->layer->rasterStorage.toImage(&lb);
                logImagePixels("[Load]   reconstructed tile", reconstructed);
            }
        }
        node->layer->owner = node.get();
        node->layer->maskVisible = o.value("maskVisible").toBool(true);
        node->layer->maskDensity = static_cast<float>(o.value("maskDensity").toDouble(1.0));
        node->layer->maskFeather = static_cast<float>(o.value("maskFeather").toDouble(0.0));
        const bool hasResetKey = o.contains("hasResetTransform");
        node->layer->hasResetTransform = hasResetKey
            ? o.value("hasResetTransform").toBool(false)
            : true;
        if (o.contains("resetTransform") && o.value("resetTransform").isArray())
            node->layer->resetTransform = transformFromArray(o.value("resetTransform").toArray());
        else
            node->layer->resetTransform = node->baseTransform();
        const QString maskSource = o.value("maskSource").toString();
        if (!maskSource.isEmpty()) {
            if (!blobs.contains(maskSource)) {
                if (error) *error = QStringLiteral("Layer mask image missing: %1").arg(maskSource);
                return nullptr;
            }
            node->layer->maskImage = decodePng(blobs.value(maskSource)).convertToFormat(QImage::Format_Grayscale8);
            node->layer->maskOrigin = QPoint(o.value("maskOriginX").toInt(0),
                                             o.value("maskOriginY").toInt(0));
        }
        if (o.contains("textData") && o.value("textData").isObject()) {
            node->layer->textData = std::make_shared<TextLayerData>(textDataFromJson(o.value("textData").toObject()));
        }
        if (o.contains("shapeData") && o.value("shapeData").isObject()) {
            node->layer->shapeData = std::make_shared<ShapeData>(shapeDataFromJson(o.value("shapeData").toObject()));
        }
        if (o.contains("distortData") && o.value("distortData").isObject()) {
            const QJsonObject dj = o.value("distortData").toObject();
            const QString distSource = dj.value("sourceImage").toString();
            if (!distSource.isEmpty() && blobs.contains(distSource)) {
                auto dd = std::make_shared<DistortData>();
                dd->sourceImage = decodePng(blobs.value(distSource))
                                      .convertToFormat(QImage::Format_RGBA8888);
                dd->mode = static_cast<TransformMode>(
                    dj.value("mode").toInt(static_cast<int>(TransformMode::Distort)));
                dd->sourceQuad = quadFromJson(dj.value("sourceQuad").toObject());
                dd->quad = quadFromJson(dj.value("quad").toObject());
                dd->resultOrigin = QPoint(dj.value("resultOriginX").toInt(0),
                                          dj.value("resultOriginY").toInt(0));
                if (dj.value("quadTransform").isArray())
                    dd->quadTransform = transformFromArray(dj.value("quadTransform").toArray());
                else
                    dd->quadTransform = node->baseTransform(); // legacy: assume in-sync
                node->layer->distortData = dd;
            }
        }
    }

    for (const auto v : o.value("children").toArray()) {
        if (!v.isObject()) continue;
        auto child = nodeFromJson(v.toObject(), blobs, error);
        if (!child) return nullptr;
        child->parent = node.get();
        node->children.push_back(std::move(child));
    }
    return node;
}
} // namespace

bool ProjectFileService::saveProject(const Document& doc, const QString& path, QString* error)
{
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid path.");
        return false;
    }

    QMap<QString, QByteArray> blobs;
    QJsonObject manifest;
    manifest["format"] = QStringLiteral("UPS_PROJECT");
    manifest["formatVersion"] = static_cast<int>(kFormatVersion);
    manifest["appName"] = QStringLiteral("Hazor Studio");
    manifest["appVersion"] = QCoreApplication::applicationVersion();
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    manifest["createdAt"] = now;
    manifest["modifiedAt"] = now;

    QJsonObject root;
    root["name"] = doc.name;
    QJsonObject canvas;
    canvas["width"] = doc.size.width();
    canvas["height"] = doc.size.height();
    root["canvas"] = canvas;
    root["resolutionDpi"] = doc.resolutionDpi;
    root["colorMode"] = doc.colorMode;
    root["bitDepth"] = doc.bitDepth;
    root["activeFlatIndex"] = doc.activeFlatIndex;
    root["zoom"] = doc.zoom;
    QJsonObject pan;
    pan["x"] = doc.panOffset.x();
    pan["y"] = doc.panOffset.y();
    root["pan"] = pan;
    root["documentColor"] = colorProfileToJson(doc.colorProfile(), doc.profileSource());

    QJsonArray guides;
    for (const Guide& guide : doc.guideManager.guides()) {
        QJsonObject g;
        g["orientation"] = guide.orientation == GuideOrientation::Vertical
            ? QStringLiteral("vertical") : QStringLiteral("horizontal");
        g["position"] = guide.position;
        g["id"] = guide.id;
        guides.push_back(g);
    }
    root["guides"] = guides;

    int nextId = 1;
    QJsonArray roots;
    for (const auto& n : doc.roots) {
        roots.push_back(nodeToJson(*n, nextId, blobs));
    }
    root["roots"] = roots;

    // Optional animation section — omitted entirely for static documents so
    // their files stay byte-identical to before.
    if (doc.animation.hasAnyTracks()
        || doc.animation.endFrame() > doc.animation.startFrame())
        root["animation"] = animationToJson(doc.animation, blobs);

    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const QByteArray documentBytes = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not open file for writing.");
        return false;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);
    out << kMagic;
    out << kFormatVersion;
    out << manifestBytes;
    out << documentBytes;
    out << static_cast<quint32>(blobs.size());
    for (auto it = blobs.constBegin(); it != blobs.constEnd(); ++it) {
        out << it.key();
        out << it.value();
    }

    if (out.status() != QDataStream::Ok) {
        if (error) *error = QStringLiteral("Could not write project stream.");
        file.cancelWriting();
        return false;
    }
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Could not finalize project file.");
        return false;
    }
    return true;
}

ProjectLoadResult ProjectFileService::loadProject(const QString& path)
{
    ProjectLoadResult result;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("Could not open project file.");
        return result;
    }

    QDataStream in(&file);
    in.setVersion(QDataStream::Qt_5_15);

    quint32 magic = 0;
    quint32 version = 0;
    QByteArray manifestBytes;
    QByteArray documentBytes;
    quint32 blobCount = 0;
    in >> magic;
    in >> version;
    in >> manifestBytes;
    in >> documentBytes;
    in >> blobCount;

    if (in.status() != QDataStream::Ok) {
        result.error = QStringLiteral("Invalid or corrupted project file.");
        return result;
    }
    if (magic != kMagic) {
        result.error = QStringLiteral("Invalid project format.");
        return result;
    }
    if (version != kFormatVersion) {
        result.error = QStringLiteral("Unsupported project version.");
        return result;
    }

    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes);
    if (!manifestDoc.isObject()) {
        result.error = QStringLiteral("Invalid manifest.");
        return result;
    }
    const QJsonObject manifest = manifestDoc.object();
    if (manifest.value("format").toString() != QLatin1String("UPS_PROJECT")) {
        result.error = QStringLiteral("Invalid project manifest format.");
        return result;
    }

    QMap<QString, QByteArray> blobs;
    for (quint32 i = 0; i < blobCount; ++i) {
        QString key;
        QByteArray data;
        in >> key;
        in >> data;
        if (in.status() != QDataStream::Ok) {
            result.error = QStringLiteral("Corrupted project resources.");
            return result;
        }
        blobs.insert(key, data);
    }

    const QJsonDocument documentDoc = QJsonDocument::fromJson(documentBytes);
    if (!documentDoc.isObject()) {
        result.error = QStringLiteral("Invalid document.json payload.");
        return result;
    }
    const QJsonObject root = documentDoc.object();
    const QJsonObject canvas = root.value("canvas").toObject();
    const int w = canvas.value("width").toInt(0);
    const int h = canvas.value("height").toInt(0);
    if (w <= 0 || h <= 0) {
        result.error = QStringLiteral("Invalid canvas size.");
        return result;
    }

    result.projectName = root.value("name").toString(QFileInfo(path).baseName());
    result.resolutionDpi = root.value("resolutionDpi").toDouble(300.0);
    result.colorMode = root.value("colorMode").toString(QStringLiteral("RGB Color"));
    result.bitDepth = root.value("bitDepth").toInt(8);
    result.canvasSize = QSize(w, h);
    result.activeFlatIndex = root.value("activeFlatIndex").toInt(0);
    result.zoom = static_cast<float>(root.value("zoom").toDouble(1.0));
    const QJsonObject pan = root.value("pan").toObject();
    result.panOffset = QPointF(pan.value("x").toDouble(0.0), pan.value("y").toDouble(0.0));
    if (root.value("documentColor").isObject()) {
        result.colorProfile = colorProfileFromJson(
            root.value("documentColor").toObject(), &result.profileSource);
    } else {
        result.colorProfile = ColorProfile::sRgb();
        result.profileSource = ColorProfileSource::GeneratedDefault;
    }

    const QJsonArray guides = root.value("guides").toArray();
    for (const auto value : guides) {
        if (!value.isObject())
            continue;
        const QJsonObject g = value.toObject();
        Guide guide;
        guide.orientation = g.value("orientation").toString() == QLatin1String("horizontal")
            ? GuideOrientation::Horizontal : GuideOrientation::Vertical;
        guide.position = g.value("position").toDouble(0.0);
        guide.id = g.value("id").toInt(0);
        if (guide.orientation == GuideOrientation::Vertical)
            guide.position = std::clamp<qreal>(guide.position, 0.0, w);
        else
            guide.position = std::clamp<qreal>(guide.position, 0.0, h);
        result.guides.push_back(guide);
    }

    const QJsonArray roots = root.value("roots").toArray();
    for (const auto v : roots) {
        if (!v.isObject()) continue;
        QString err;
        auto node = nodeFromJson(v.toObject(), blobs, &err);
        if (!node) {
            result.error = err.isEmpty() ? QStringLiteral("Could not deserialize layer tree.") : err;
            return result;
        }
        result.roots.push_back(std::move(node));
    }

    // Animation section (optional). Validate tracks against the ids that
    // actually loaded so a stale/renamed reference is dropped, not fatal.
    const QJsonObject animationObj = root.value("animation").toObject();
    if (!animationObj.isEmpty()) {
        QSet<QUuid> validIds;
        collectNodeIds(result.roots, validIds);
        animationFromJson(animationObj, result.animation, validIds, blobs);
    }

    result.ok = true;
    return result;
}
