#include "ProjectFileService.hpp"

#include "core/AdjustmentTypes.hpp"
#include "core/Layer.hpp"
#include "core/LayerEffect.hpp"
#include "core/LayerTreeNode.hpp"
#include "core/ShapeTypes.hpp"
#include "transform/TransformTypes.hpp"
#include "text/TextTypes.hpp"
#include "raw/RawLayerData.hpp"
#include "smartobject/DocumentContainer.hpp"
#include "smartobject/SmartObjectData.hpp"

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
#include <QDir>
#include <QSaveFile>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace {
constexpr quint32 kMagic = 0x485A5331; // "HZS1"
// Version 1: everything before the RAW Layer (Fase 4.9). Version 2 adds the
// "rawData" node field + its blobs. Version 3 adds Smart Objects: the
// "smartObjectData" node field, the root document's "docId" +
// "embeddedDocuments" block, and "docs/<docId>/"-prefixed blobs for embedded
// documents.
//
// The loader accepts 1..3; the saver writes the LOWEST version the document
// actually needs (see documentHasRawLayer()/documentHasSmartObject()), so a
// document with neither feature still opens in older Hazor builds.
//
// Compatibility is one-directional, and deliberately so: an older build
// rejects a v3 file at the header check below with "Unsupported project
// version." — it never reaches the node JSON, so there is no partial read and
// no cached-composite fallback. What v3 does guarantee is FUNCTIONAL
// round-tripping: this build opens v1/v2 unchanged, and a document without
// Smart Objects keeps saving in the older format byte-for-byte (same blob keys,
// same JSON shape).
constexpr quint32 kMaxFormatVersion = 3;

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

QJsonObject rawGeometryToJson(const RawGeometry& g)
{
    QJsonObject o;
    o["fullWidth"] = g.fullWidth;
    o["fullHeight"] = g.fullHeight;
    o["coarseRotationDegrees"] = g.coarseRotationDegrees;
    o["mirrorHorizontal"] = g.mirrorHorizontal;
    o["mirrorVertical"] = g.mirrorVertical;
    o["cropEnabled"] = g.cropEnabled;
    o["cropX"] = g.cropX;
    o["cropY"] = g.cropY;
    o["cropWidth"] = g.cropWidth;
    o["cropHeight"] = g.cropHeight;
    o["resizeScale"] = g.resizeScale;
    o["reframeExact"] = g.reframeExact;
    return o;
}

RawGeometry rawGeometryFromJson(const QJsonObject& o)
{
    RawGeometry g;
    g.fullWidth = o.value("fullWidth").toInt(0);
    g.fullHeight = o.value("fullHeight").toInt(0);
    g.coarseRotationDegrees = o.value("coarseRotationDegrees").toInt(0);
    g.mirrorHorizontal = o.value("mirrorHorizontal").toBool(false);
    g.mirrorVertical = o.value("mirrorVertical").toBool(false);
    g.cropEnabled = o.value("cropEnabled").toBool(false);
    g.cropX = o.value("cropX").toInt(0);
    g.cropY = o.value("cropY").toInt(0);
    g.cropWidth = o.value("cropWidth").toInt(0);
    g.cropHeight = o.value("cropHeight").toInt(0);
    g.resizeScale = o.value("resizeScale").toDouble(1.0);
    g.reframeExact = o.value("reframeExact").toBool(true);
    return g;
}

QJsonObject rawMetadataToJson(const RawMetadata& m)
{
    QJsonObject o;
    o["cameraMake"] = QString::fromStdString(m.cameraMake);
    o["cameraModel"] = QString::fromStdString(m.cameraModel);
    o["lens"] = QString::fromStdString(m.lens);
    o["isoSpeed"] = m.isoSpeed;
    o["fNumber"] = m.fNumber;
    o["focalLengthMm"] = m.focalLengthMm;
    o["shutterSpeedSeconds"] = m.shutterSpeedSeconds;
    o["exposureCompensationEv"] = m.exposureCompensationEv;
    o["orientationDegrees"] = m.orientationDegrees;
    o["orientationMirrored"] = m.orientationMirrored;
    o["width"] = m.width;
    o["height"] = m.height;
    return o;
}

RawMetadata rawMetadataFromJson(const QJsonObject& o)
{
    RawMetadata m;
    m.cameraMake = o.value("cameraMake").toString().toStdString();
    m.cameraModel = o.value("cameraModel").toString().toStdString();
    m.lens = o.value("lens").toString().toStdString();
    m.isoSpeed = o.value("isoSpeed").toInt(0);
    m.fNumber = o.value("fNumber").toDouble(0.0);
    m.focalLengthMm = o.value("focalLengthMm").toDouble(0.0);
    m.shutterSpeedSeconds = o.value("shutterSpeedSeconds").toDouble(0.0);
    m.exposureCompensationEv = o.value("exposureCompensationEv").toDouble(0.0);
    m.orientationDegrees = o.value("orientationDegrees").toInt(0);
    m.orientationMirrored = o.value("orientationMirrored").toBool(false);
    m.width = o.value("width").toInt(0);
    m.height = o.value("height").toInt(0);
    return m;
}

// RawLayerData is serialized as JSON (mode/state/source metadata); the
// Embedded source's bytes (when present) are a separate blob, same pattern
// as distortData's sourceImage. Linked mode stores only the path — the
// bytes are never duplicated into the project file.
QJsonObject rawDataToJson(const RawLayerData& d, const QString& embeddedBytesRef)
{
    QJsonObject o;
    o["mode"] = d.mode == RawStorageMode::Embedded
        ? QStringLiteral("embedded") : QStringLiteral("linked");
    o["path"] = d.source ? d.source->path : QString();
    if (d.source)
        o["metadata"] = rawMetadataToJson(d.source->meta);
    if (!embeddedBytesRef.isEmpty())
        o["embeddedBytes"] = embeddedBytesRef;
    o["pp3"] = d.state.pp3;
    o["developedWidth"] = d.state.developedSize.width();
    o["developedHeight"] = d.state.developedSize.height();
    o["geometry"] = rawGeometryToJson(d.state.geometry);
    o["revision"] = QString::number(d.state.revision); // quint64: keep full range as text
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

// `blobPrefix` namespaces this node's blob keys. Empty for a container's root
// document, so its keys stay exactly "layers/layer_0001.png" as every pre-v3
// file has them; "docs/<docId>/" for an embedded document, which is what keeps
// N documents in one archive from colliding on the shared `nextId` counter.
// The prefix is baked into the key *stored in the JSON* ("source",
// "maskSource", ...), so nodeFromJson needs no prefix of its own — a node
// always carries the full key it was saved under.
QJsonObject smartObjectDataToJson(const SmartObjectData& data)
{
    QJsonObject o;
    o["mode"] = data.mode == SmartObjectStorageMode::Embedded
        ? QStringLiteral("embedded") : QStringLiteral("linked");
    o["docId"] = data.docId;
    if (!data.linkedPath.isEmpty())
        o["linkedPath"] = data.linkedPath;
    o["sourceWidth"] = data.state.sourceSize.width();
    o["sourceHeight"] = data.state.sourceSize.height();
    o["sourceDpi"] = data.state.sourceDpi;
    // Both counters go out as strings: they are 64-bit and JSON numbers are
    // doubles, so a long-lived document would start losing the low bits.
    // Same reason RawDevelopState::revision is persisted this way.
    o["sourceGeneration"] = QString::number(data.state.sourceGeneration);
    o["revision"] = QString::number(data.state.revision);
    if (data.linkBroken)
        o["linkBroken"] = true;
    return o;
}

SmartObjectData smartObjectDataFromJson(const QJsonObject& o)
{
    SmartObjectData data;
    data.mode = o.value("mode").toString() == QLatin1String("linked")
        ? SmartObjectStorageMode::Linked : SmartObjectStorageMode::Embedded;
    data.docId = o.value("docId").toString();
    data.linkedPath = o.value("linkedPath").toString();
    data.state.sourceSize = QSize(o.value("sourceWidth").toInt(0),
                                  o.value("sourceHeight").toInt(0));
    data.state.sourceDpi = o.value("sourceDpi").toDouble(300.0);
    data.state.sourceGeneration = o.value("sourceGeneration").toString().toULongLong();
    data.state.revision = o.value("revision").toString().toULongLong();
    data.linkBroken = o.value("linkBroken").toBool(false);
    return data;
}

QJsonObject nodeToJson(const LayerTreeNode& node,
                       int& nextId,
                       QMap<QString, QByteArray>& blobs,
                       const QString& blobPrefix)
{
    QJsonObject o;
    const QString id = QStringLiteral("layer_%1").arg(nextId++, 4, 10, QLatin1Char('0'));
    o["id"] = id;
    o["name"] = node.name;
    o["type"] = nodeTypeToString(node.type);
    o["visible"] = node.visible;
    o["opacity"] = node.opacity;
    o["blendMode"] = static_cast<int>(node.blendMode);
    o["groupBlendMode"] = static_cast<int>(node.groupBlendMode);
    o["lockFlags"] = node.lockFlags;
    o["collapsed"] = node.collapsed;
    o["clipped"] = node.clipped;
    o["nameIsAuto"] = node.nameIsAuto;
    o["transform"] = transformToArray(node.transform);
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
                const QString maskSource =
                    blobPrefix + QStringLiteral("layers/%1_mask.png").arg(id);
                blobs.insert(maskSource, encodePng(node.layer->maskImage));
                o["maskSource"] = maskSource;
                o["maskOriginX"] = node.layer->maskOrigin.x();
                o["maskOriginY"] = node.layer->maskOrigin.y();
            }
        }
        QJsonArray adjChildren;
        for (const auto& c : node.children)
            adjChildren.push_back(nodeToJson(*c, nextId, blobs, blobPrefix));
        o["children"] = adjChildren;
        return o;
    }

    if (node.layer) {
        const QString source = blobPrefix + QStringLiteral("layers/%1.png").arg(id);
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
            const QString maskSource =
                blobPrefix + QStringLiteral("layers/%1_mask.png").arg(id);
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
            const QString distSource =
                blobPrefix + QStringLiteral("layers/%1_distort.png").arg(id);
            blobs.insert(distSource, encodePng(node.layer->distortData->sourceImage));
            o["distortData"] = distortDataToJson(*node.layer->distortData, distSource);
        }
        if (node.layer->rawData) {
            QString embeddedRef;
            if (node.layer->rawData->mode == RawStorageMode::Embedded
                && node.layer->rawData->source && !node.layer->rawData->source->bytes.isEmpty()) {
                embeddedRef = blobPrefix + QStringLiteral("layers/%1_raw.bin").arg(id);
                blobs.insert(embeddedRef, node.layer->rawData->source->bytes);
            }
            o["rawData"] = rawDataToJson(*node.layer->rawData, embeddedRef);
        }
        if (node.layer->smartObjectData) {
            // No blob of its own: the instance's render is the layer's ordinary
            // "source" PNG written above, which is what keeps an unresolvable
            // Linked reference displayable (see SmartObjectData.hpp / D9-A).
            o["smartObjectData"] = smartObjectDataToJson(*node.layer->smartObjectData);
        }
    }

    QJsonArray children;
    for (const auto& c : node.children) {
        children.push_back(nodeToJson(*c, nextId, blobs, blobPrefix));
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
    node->visible = o.value("visible").toBool(true);
    node->opacity = static_cast<float>(o.value("opacity").toDouble(1.0));
    node->blendMode = static_cast<BlendMode>(o.value("blendMode").toInt(0));
    // Default 0 == GroupBlendMode::Isolated so projects saved before group
    // blend modes existed load with the safe default.
    node->groupBlendMode =
        static_cast<LayerTreeNode::GroupBlendMode>(o.value("groupBlendMode").toInt(0));
    node->lockFlags = o.value("lockFlags").toInt(0);
    node->collapsed = o.value("collapsed").toBool(false);
    node->clipped = o.value("clipped").toBool(false);
    node->nameIsAuto = o.value("nameIsAuto").toBool(false);
    node->transform = transformFromArray(o.value("transform").toArray());
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
            node->layer->resetTransform = node->transform;
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
                    dd->quadTransform = node->transform; // legacy: assume in-sync
                node->layer->distortData = dd;
            }
        }
        if (o.contains("rawData") && o.value("rawData").isObject()) {
            const QJsonObject rj = o.value("rawData").toObject();
            const bool embedded = rj.value("mode").toString() == QLatin1String("embedded");
            const QString embeddedRef = rj.value("embeddedBytes").toString();
            // Embedded mode requires its bytes blob; a missing blob (file
            // trimmed, corrupted archive) can't be treated as Linked either
            // — there is no on-disk file to fall back to — so the layer
            // loses its RAW payload rather than silently mis-reporting its
            // storage mode. The layer still loads and renders from the
            // cached cpuImage; re-reveal is simply unavailable for it.
            if (!embedded || blobs.contains(embeddedRef)) {
                auto source = std::make_shared<RawSource>();
                source->path = rj.value("path").toString();
                source->meta = rawMetadataFromJson(rj.value("metadata").toObject());
                if (embedded)
                    source->bytes = blobs.value(embeddedRef);

                auto rd = std::make_shared<RawLayerData>();
                rd->source = source;
                rd->mode = embedded ? RawStorageMode::Embedded : RawStorageMode::Linked;
                rd->state.pp3 = rj.value("pp3").toString();
                rd->state.developedSize = QSize(rj.value("developedWidth").toInt(0),
                                                rj.value("developedHeight").toInt(0));
                rd->state.geometry = rawGeometryFromJson(rj.value("geometry").toObject());
                rd->state.revision = rj.value("revision").toString().toULongLong();
                node->layer->rawData = rd;
            }
        }
        if (o.contains("smartObjectData") && o.value("smartObjectData").isObject()) {
            const SmartObjectData data =
                smartObjectDataFromJson(o.value("smartObjectData").toObject());
            // An instance with no docId is unresolvable noise; drop the payload
            // and keep the pixels, exactly as the RAW loader does for a missing
            // embedded blob. The layer still renders from the cached cpuImage.
            if (!data.docId.isEmpty() || !data.linkedPath.isEmpty()) {
                node->layer->smartObjectData =
                    std::make_shared<const SmartObjectData>(data);
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

// Gates the format-version bump (see kMaxFormatVersion): a document with no
// RAW Layer still saves as version 1 so it keeps opening in older builds.
bool documentHasRawLayer(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes)
{
    for (const auto& n : nodes) {
        if (n->layer && n->layer->rawData)
            return true;
        if (documentHasRawLayer(n->children))
            return true;
    }
    return false;
}

// Same gate, one version up: a document with no Smart Object still saves as
// version 1 or 2, so adding this feature did not break a single existing file.
bool documentHasSmartObject(const std::vector<std::unique_ptr<LayerTreeNode>>& nodes)
{
    for (const auto& n : nodes) {
        if (n->layer && n->layer->smartObjectData)
            return true;
        if (documentHasSmartObject(n->children))
            return true;
    }
    return false;
}

// ─────────────────────────────────────────────────────────────────────────
// One document, in and out — the reentrant unit.
//
// Everything above serializes *nodes*; these two serialize a whole Document
// and nothing else (no manifest, no header, no file). That reentrancy is the
// whole point: a .hazor container holds the root document plus N embedded ones
// (Smart Objects), and each is written/read by the same pair.
//
// `nextId` is shared across every document in one save pass, and `blobPrefix`
// is what makes that safe — see nodeToJson(). Keeping the counter shared (and
// starting the root at 1 with an empty prefix) is also what keeps a
// Smart-Object-free document's blob keys byte-identical to pre-v3 files.
//
// What documentFromJson deliberately does NOT restore, because saving never
// wrote it: `selection` (only resized + cleared here), `channels`,
// `perfConfig`, `scheduler`, and the mask-overlay view state
// (maskOverlayVisible/maskOverlayOpacity — explicitly non-persisted, see
// Document.hpp). For an embedded document that is not a gap but the correct
// behaviour: it has no view of its own until a tab opens it.
QJsonObject documentToJson(const Document& doc,
                           int& nextId,
                           QMap<QString, QByteArray>& blobs,
                           const QString& blobPrefix)
{
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

    QJsonArray roots;
    for (const auto& n : doc.roots)
        roots.push_back(nodeToJson(*n, nextId, blobs, blobPrefix));
    root["roots"] = roots;
    return root;
}

bool documentFromJson(const QJsonObject& root,
                      const QMap<QString, QByteArray>& blobs,
                      Document& out,
                      QString* error)
{
    const QJsonObject canvas = root.value("canvas").toObject();
    const int w = canvas.value("width").toInt(0);
    const int h = canvas.value("height").toInt(0);
    if (w <= 0 || h <= 0) {
        if (error) *error = QStringLiteral("Invalid canvas size.");
        return false;
    }

    // Defaults come from `out`, not from literals: the caller seeds the name
    // (loadProject uses the file's base name) and we only override what the
    // JSON actually carries.
    out.name = root.value("name").toString(out.name);
    out.size = QSize(w, h);
    out.resolutionDpi = root.value("resolutionDpi").toDouble(300.0);
    out.colorMode = root.value("colorMode").toString(QStringLiteral("RGB Color"));
    out.bitDepth = root.value("bitDepth").toInt(8);
    out.zoom = static_cast<float>(root.value("zoom").toDouble(1.0));
    const QJsonObject pan = root.value("pan").toObject();
    out.panOffset = QPointF(pan.value("x").toDouble(0.0), pan.value("y").toDouble(0.0));
    if (root.value("documentColor").isObject()) {
        ColorProfileSource source = ColorProfileSource::GeneratedDefault;
        out.setColorProfile(colorProfileFromJson(
            root.value("documentColor").toObject(), &source));
        out.setProfileSource(source);
    } else {
        out.setColorProfile(ColorProfile::sRgb());
        out.setProfileSource(ColorProfileSource::GeneratedDefault);
    }

    std::vector<Guide> loadedGuides;
    for (const auto value : root.value("guides").toArray()) {
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
        loadedGuides.push_back(guide);
    }
    out.guideManager.setGuides(loadedGuides);

    out.roots.clear();
    for (const auto v : root.value("roots").toArray()) {
        if (!v.isObject()) continue;
        QString err;
        auto node = nodeFromJson(v.toObject(), blobs, &err);
        if (!node) {
            if (error) {
                *error = err.isEmpty()
                    ? QStringLiteral("Could not deserialize layer tree.") : err;
            }
            return false;
        }
        out.roots.push_back(std::move(node));
    }

    out.activeFlatIndex = std::clamp(root.value("activeFlatIndex").toInt(0),
                                     0, std::max(0, out.flatCount() - 1));
    out.selection.resize(w, h);
    out.selection.clear();
    out.selection.setActive(false);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────
// The archive layer: magic + version + manifest + document JSON + blob map.
// Extracted so the single-document and container save/load paths share one
// definition of the file format instead of two copies that can drift.
// ─────────────────────────────────────────────────────────────────────────
struct Archive {
    quint32 version = 0;
    QJsonObject manifest;
    QJsonObject document;
    QMap<QString, QByteArray> blobs;
};

bool writeArchive(const QString& path,
                  quint32 formatVersion,
                  const QJsonObject& document,
                  const QMap<QString, QByteArray>& blobs,
                  QString* error)
{
    QJsonObject manifest;
    manifest["format"] = QStringLiteral("UPS_PROJECT");
    manifest["formatVersion"] = static_cast<int>(formatVersion);
    manifest["appName"] = QStringLiteral("Hazor Studio");
    manifest["appVersion"] = QCoreApplication::applicationVersion();
    // Both stamps are "now" on every save, which is why a byte diff of two
    // saves of the same document never matches — compare the document JSON and
    // the blob map instead (see docs/SMART-OBJECTS.md).
    const QString now = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    manifest["createdAt"] = now;
    manifest["modifiedAt"] = now;

    const QByteArray manifestBytes = QJsonDocument(manifest).toJson(QJsonDocument::Compact);
    const QByteArray documentBytes = QJsonDocument(document).toJson(QJsonDocument::Compact);

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("Could not open file for writing.");
        return false;
    }

    QDataStream out(&file);
    out.setVersion(QDataStream::Qt_5_15);
    out << kMagic;
    out << formatVersion;
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

bool readArchive(const QString& path, Archive& out, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Could not open project file.");
        return false;
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
        if (error) *error = QStringLiteral("Invalid or corrupted project file.");
        return false;
    }
    if (magic != kMagic) {
        if (error) *error = QStringLiteral("Invalid project format.");
        return false;
    }
    if (version < 1 || version > kMaxFormatVersion) {
        if (error) *error = QStringLiteral("Unsupported project version.");
        return false;
    }

    const QJsonDocument manifestDoc = QJsonDocument::fromJson(manifestBytes);
    if (!manifestDoc.isObject()) {
        if (error) *error = QStringLiteral("Invalid manifest.");
        return false;
    }
    out.manifest = manifestDoc.object();
    if (out.manifest.value("format").toString() != QLatin1String("UPS_PROJECT")) {
        if (error) *error = QStringLiteral("Invalid project manifest format.");
        return false;
    }

    for (quint32 i = 0; i < blobCount; ++i) {
        QString key;
        QByteArray data;
        in >> key;
        in >> data;
        if (in.status() != QDataStream::Ok) {
            if (error) *error = QStringLiteral("Corrupted project resources.");
            return false;
        }
        out.blobs.insert(key, data);
    }

    const QJsonDocument documentDoc = QJsonDocument::fromJson(documentBytes);
    if (!documentDoc.isObject()) {
        if (error) *error = QStringLiteral("Invalid document.json payload.");
        return false;
    }
    out.document = documentDoc.object();
    out.version = version;
    return true;
}
} // namespace

// Single-document form, kept for callers that have a Document and no container.
// It writes exactly one document, so a Smart Object instance in `doc` survives
// as a payload + cached render but its EMBEDDED document does not get written —
// only saveContainer() can do that. Every path that owns a container must use
// saveContainer().
bool ProjectFileService::saveProject(const Document& doc, const QString& path, QString* error)
{
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid path.");
        return false;
    }

    const quint32 formatVersion = documentHasSmartObject(doc.roots)
        ? 3u : (documentHasRawLayer(doc.roots) ? 2u : 1u);

    QMap<QString, QByteArray> blobs;
    int nextId = 1;
    // Empty prefix: a single-document project keeps the exact blob keys every
    // pre-container file already uses.
    const QJsonObject root = documentToJson(doc, nextId, blobs, QString());

    return writeArchive(path, formatVersion, root, blobs, error);
}

ProjectLoadResult ProjectFileService::loadProject(const QString& path)
{
    ProjectLoadResult result;

    Archive archive;
    if (!readArchive(path, archive, &result.error))
        return result;

    const QJsonObject root = archive.document;

    Document doc;
    doc.name = QFileInfo(path).baseName();
    if (!documentFromJson(root, archive.blobs, doc, &result.error)) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("Invalid document.json payload.");
        return result;
    }

    // ProjectLoadResult stays the loose-field shape its callers already expect;
    // documentFromJson is the single place that knows the JSON layout.
    result.projectName = doc.name;
    result.canvasSize = doc.size;
    result.resolutionDpi = doc.resolutionDpi;
    result.colorMode = doc.colorMode;
    result.bitDepth = doc.bitDepth;
    result.activeFlatIndex = doc.activeFlatIndex;
    result.zoom = doc.zoom;
    result.panOffset = doc.panOffset;
    result.colorProfile = doc.colorProfile();
    result.profileSource = doc.profileSource();
    result.guides = doc.guideManager.guides();
    result.roots = std::move(doc.roots);
    result.ok = true;
    return result;
}

bool ProjectFileService::saveContainer(const DocumentContainer& container,
                                       const QString& path, QString* error)
{
    if (path.isEmpty()) {
        if (error) *error = QStringLiteral("Invalid path.");
        return false;
    }
    const auto rootDoc = container.rootDocument();
    if (!rootDoc) {
        if (error) *error = QStringLiteral("The project has no root document.");
        return false;
    }

    // Persistence set = reachability from the root, computed now. A document
    // the user orphaned (converted to Linked, deleted the last instance) stays
    // alive in memory for undo but is not written: that is the single rule that
    // keeps the file from growing dead subgraphs, and it needs no deletion
    // anywhere else in the codebase.
    const std::vector<SmartObjectDocId> persisted = container.reachableFromRoot();

    QMap<QString, QByteArray> blobs;
    // One counter across the whole pass. Safe because every non-root document
    // namespaces its blob keys (see nodeToJson), and starting the root at 1
    // with an empty prefix is what keeps a Smart-Object-free project's keys
    // identical to what every pre-v3 build wrote.
    int nextId = 1;
    QJsonObject root = documentToJson(*rootDoc, nextId, blobs, QString());
    root["docId"] = container.rootId();

    bool hasSmartObject = documentHasSmartObject(rootDoc->roots);
    bool hasRaw = documentHasRawLayer(rootDoc->roots);

    QJsonArray embedded;
    for (const SmartObjectDocId& id : persisted) {
        if (id == container.rootId())
            continue;
        const auto doc = container.document(id);
        if (!doc)
            continue;
        QJsonObject entry;
        entry["docId"] = id;
        entry["document"] = documentToJson(
            *doc, nextId, blobs, QStringLiteral("docs/%1/").arg(id));
        embedded.push_back(entry);
        hasSmartObject = hasSmartObject || documentHasSmartObject(doc->roots);
        hasRaw = hasRaw || documentHasRawLayer(doc->roots);
    }
    if (!embedded.isEmpty())
        root["embeddedDocuments"] = embedded;

    // An embedded document can only be reached through an instance, so
    // `embedded` being non-empty already implies a Smart Object somewhere; the
    // explicit check also catches a Linked-only project, which has no embedded
    // documents at all but still needs v3 to carry its payload.
    const quint32 formatVersion = hasSmartObject ? 3u : (hasRaw ? 2u : 1u);
    return writeArchive(path, formatVersion, root, blobs, error);
}

ContainerLoadResult ProjectFileService::loadContainer(const QString& path)
{
    ContainerLoadResult result;

    Archive archive;
    if (!readArchive(path, archive, &result.error))
        return result;

    auto rootDoc = std::make_shared<Document>();
    rootDoc->name = QFileInfo(path).baseName();
    if (!documentFromJson(archive.document, archive.blobs, *rootDoc, &result.error)) {
        if (result.error.isEmpty())
            result.error = QStringLiteral("Invalid document.json payload.");
        return result;
    }

    // Pre-v3 files carry no docId; a fresh one is correct there because nothing
    // can reference the root (a reference to the root would be a cycle).
    auto container = std::make_shared<DocumentContainer>(
        rootDoc, archive.document.value("docId").toString());
    container->setPath(QFileInfo(path).absoluteFilePath());

    for (const auto value : archive.document.value("embeddedDocuments").toArray()) {
        if (!value.isObject())
            continue;
        const QJsonObject entry = value.toObject();
        const SmartObjectDocId id = entry.value("docId").toString();
        if (id.isEmpty())
            continue;
        auto doc = std::make_shared<Document>();
        doc->name = entry.value("document").toObject().value("name")
                        .toString(QStringLiteral("Smart Object"));
        QString err;
        if (!documentFromJson(entry.value("document").toObject(), archive.blobs, *doc, &err)) {
            // One corrupt embedded document must not cost the user the whole
            // project: its instances degrade to the cached render they already
            // have (and report a broken reference), which is the same policy
            // the RAW loader applies to a missing embedded blob.
            qWarning("Smart Object document %s could not be loaded: %s",
                     qUtf8Printable(id), qUtf8Printable(err));
            continue;
        }
        container->addDocumentWithId(id, std::move(doc));
    }

    result.container = std::move(container);
    result.ok = true;
    return result;
}
