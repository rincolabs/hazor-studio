#include "KritaKppImportAdapter.hpp"
#include "BrushImportArchive.hpp"
#include "GimpBrushDecoder.hpp"

#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QByteArray>
#include <QXmlStreamReader>
#include <QMap>
#include <QRegularExpression>
#include <QStringList>
#include <QtGlobal>
#include <QtMath>
#include <algorithm>
#include <cstring>
#include <initializer_list>

namespace {

const char kPngSig[8] = { char(0x89), 'P', 'N', 'G', '\r', '\n', char(0x1A), '\n' };

bool hasPngSignature(const QByteArray& d)
{
    return d.size() >= 8 && memcmp(d.constData(), kPngSig, 8) == 0;
}

quint32 beU32(const QByteArray& d, int off)
{
    if (off < 0 || off + 4 > d.size()) return 0;
    return (quint32(quint8(d[off])) << 24) | (quint32(quint8(d[off + 1])) << 16)
         | (quint32(quint8(d[off + 2])) << 8) | quint32(quint8(d[off + 3]));
}

// Pull every textual chunk (tEXt / zTXt / iTXt) out of a PNG as keyword→text.
QMap<QString, QString> extractPngTexts(const QByteArray& d)
{
    QMap<QString, QString> out;
    if (!hasPngSignature(d)) return out;
    int pos = 8;
    int guard = 0;
    while (pos + 8 <= d.size() && guard++ < 100000) {
        const quint32 len = beU32(d, pos);
        const QByteArray type = d.mid(pos + 4, 4);
        const int dataStart = pos + 8;
        if (qint64(dataStart) + len + 4 > d.size()) break;
        const QByteArray chunk = d.mid(dataStart, int(len));

        auto splitKeyword = [](const QByteArray& c, int from, int& keyEnd) -> QString {
            keyEnd = c.indexOf('\0', from);
            if (keyEnd < 0) keyEnd = c.size();
            return QString::fromLatin1(c.mid(from, keyEnd - from));
        };

        if (type == QByteArray("tEXt")) {
            int keyEnd = 0;
            const QString key = splitKeyword(chunk, 0, keyEnd);
            const QString text = QString::fromLatin1(chunk.mid(keyEnd + 1));
            if (!key.isEmpty()) out.insert(key, text);
        } else if (type == QByteArray("zTXt")) {
            int keyEnd = 0;
            const QString key = splitKeyword(chunk, 0, keyEnd);
            // keyEnd points at NUL; next byte is the compression method.
            const QByteArray comp = chunk.mid(keyEnd + 2);
            const QByteArray text = BrushImportArchive::inflateZlib(comp);
            if (!key.isEmpty() && !text.isEmpty())
                out.insert(key, QString::fromUtf8(text));
        } else if (type == QByteArray("iTXt")) {
            int keyEnd = 0;
            const QString key = splitKeyword(chunk, 0, keyEnd);
            int p = keyEnd + 1;
            if (p + 2 <= chunk.size()) {
                const int compFlag = quint8(chunk[p]);
                p += 2; // compression flag + method
                // skip language tag and translated keyword (two NUL-terminated)
                int lt = chunk.indexOf('\0', p); if (lt < 0) lt = chunk.size();
                p = lt + 1;
                int tk = chunk.indexOf('\0', p); if (tk < 0) tk = chunk.size();
                p = tk + 1;
                const QByteArray body = chunk.mid(p);
                const QByteArray text = compFlag ? BrushImportArchive::inflateZlib(body) : body;
                if (!key.isEmpty() && !text.isEmpty())
                    out.insert(key, QString::fromUtf8(text));
            }
        }
        pos = dataStart + int(len) + 4; // skip data + CRC
        if (type == QByteArray("IEND")) break;
    }
    return out;
}

float toFloat(const QString& s, float fallback)
{
    bool ok = false;
    const float v = s.toFloat(&ok);
    return ok ? v : fallback;
}

QString normalizeLookupKey(QString s)
{
    s = s.toLower();
    s.remove(QLatin1Char(' '));
    s.remove(QLatin1Char('_'));
    s.remove(QLatin1Char('-'));
    s.remove(QLatin1Char('/'));
    return s;
}

void appendParamValue(QMap<QString, QString>& params, const QString& key, const QString& value)
{
    const QString trimmed = value.trimmed();
    if (key.isEmpty() || trimmed.isEmpty())
        return;
    QString& slot = params[key];
    if (!slot.isEmpty())
        slot += QLatin1Char(' ');
    slot += trimmed;
}

void collectDeepParam(QXmlStreamReader& xml, const QString& paramName,
                      QMap<QString, QString>& params)
{
    QStringList context;
    QString summary;
    QString plainText;
    int depth = 1;

    while (!xml.atEnd() && depth > 0) {
        const auto tok = xml.readNext();
        if (tok == QXmlStreamReader::StartElement) {
            ++depth;
            const QString elem = xml.name().toString();
            const auto attrs = xml.attributes();
            QString identity = elem;
            const QString named = attrs.value(QLatin1String("name")).toString();
            const QString typed = attrs.value(QLatin1String("type")).toString();
            const QString id = attrs.value(QLatin1String("id")).toString();
            if (!named.isEmpty())
                identity += QLatin1Char(':') + named;
            else if (!typed.isEmpty())
                identity += QLatin1Char(':') + typed;
            else if (!id.isEmpty())
                identity += QLatin1Char(':') + id;
            context.append(identity);

            appendParamValue(params, paramName, elem);
            summary += QLatin1Char(' ') + elem;

            const QString path = paramName + QLatin1Char('.') + context.join(QLatin1Char('.'));
            appendParamValue(params, paramName + QLatin1Char('.') + elem, identity);
            appendParamValue(params, path, identity);
            for (int i = 1; i <= context.size(); ++i) {
                const QString prefix = paramName + QLatin1Char('.')
                    + context.mid(0, i).join(QLatin1Char('.'));
                appendParamValue(params, prefix, identity);
            }

            for (const QXmlStreamAttribute& attr : attrs) {
                const QString attrName = attr.name().toString();
                const QString attrValue = attr.value().toString();
                appendParamValue(params, paramName, attrName + QLatin1Char('=') + attrValue);
                appendParamValue(params, path + QLatin1Char('.') + attrName, attrValue);
                for (int i = 1; i <= context.size(); ++i) {
                    const QString prefix = paramName + QLatin1Char('.')
                        + context.mid(0, i).join(QLatin1Char('.'));
                    appendParamValue(params, prefix, attrName + QLatin1Char('=') + attrValue);
                }
                appendParamValue(params, paramName + QLatin1Char('.') + elem + QLatin1Char('.') + attrName,
                                 attrValue);
                if (!named.isEmpty()) {
                    appendParamValue(params, paramName + QLatin1Char('.') + named + QLatin1Char('.') + attrName,
                                     attrValue);
                }
                if (!typed.isEmpty()) {
                    appendParamValue(params, paramName + QLatin1Char('.') + typed + QLatin1Char('.') + attrName,
                                     attrValue);
                }
                summary += QLatin1Char(' ') + attrName + QLatin1Char('=') + attrValue;
            }
        } else if (tok == QXmlStreamReader::Characters) {
            if (!xml.isWhitespace()) {
                const QString text = xml.text().toString();
                if (!plainText.isEmpty())
                    plainText += QLatin1Char(' ');
                plainText += text.trimmed();
                appendParamValue(params, paramName, text);
                if (!context.isEmpty()) {
                    const QString path = paramName + QLatin1Char('.') + context.join(QLatin1Char('.'));
                    appendParamValue(params, path, text);
                    for (int i = 1; i <= context.size(); ++i) {
                        const QString prefix = paramName + QLatin1Char('.')
                            + context.mid(0, i).join(QLatin1Char('.'));
                        appendParamValue(params, prefix, text);
                    }
                }
                summary += QLatin1Char(' ') + text;
            }
        } else if (tok == QXmlStreamReader::EndElement) {
            --depth;
            if (depth > 0 && !context.isEmpty())
                context.removeLast();
        }
    }

    appendParamValue(params, paramName + QStringLiteral(".deep"), summary);
    appendParamValue(params, paramName + QStringLiteral(".text"), plainText);
}

float normalizeUnit(float v, float fallback)
{
    if (!qIsFinite(v))
        return fallback;
    if (v > 1.0f)
        v /= 100.0f;
    return qBound(0.0f, v, 1.0f);
}

float firstNumberInText(const QString& text, float fallback)
{
    static const QRegularExpression re(QStringLiteral(R"([-+]?\d*\.?\d+)"));
    const QRegularExpressionMatch match = re.match(text);
    if (!match.hasMatch())
        return fallback;
    return toFloat(match.captured(0), fallback);
}

QString firstDynamicsParam(const QMap<QString, QString>& params,
                           std::initializer_list<const char*> keys)
{
    for (const char* k : keys) {
        const QString key = QString::fromLatin1(k);
        if (params.contains(key))
            return params.value(key);
    }

    for (const char* k : keys) {
        const QString needle = normalizeLookupKey(QString::fromLatin1(k));
        for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
            if (normalizeLookupKey(it.key()).contains(needle))
                return it.value();
        }
    }
    return QString();
}

float firstNumericParam(const QMap<QString, QString>& params,
                        std::initializer_list<const char*> keys,
                        float fallback)
{
    const QString text = firstDynamicsParam(params, keys);
    if (text.isEmpty())
        return fallback;
    const float direct = toFloat(text, fallback);
    if (direct != fallback || text.trimmed() == QString::number(fallback))
        return direct;
    return firstNumberInText(text, fallback);
}

float firstUnitParam(const QMap<QString, QString>& params,
                     std::initializer_list<const char*> keys,
                     float fallback)
{
    return normalizeUnit(firstNumericParam(params, keys, fallback), fallback);
}

bool textLooksEnabled(const QString& text, bool fallback = false)
{
    const QString lower = text.trimmed().toLower();
    if (lower.isEmpty())
        return fallback;
    if (lower == QLatin1String("true") || lower == QLatin1String("yes")
        || lower == QLatin1String("on") || lower == QLatin1String("1"))
        return true;
    if (lower == QLatin1String("false") || lower == QLatin1String("no")
        || lower == QLatin1String("off") || lower == QLatin1String("0"))
        return false;
    if (lower.contains(QLatin1String("enabled=\"false\""))
        || lower.contains(QLatin1String("enabled=\"0\""))
        || lower.contains(QLatin1String("checked=\"false\""))
        || lower.contains(QLatin1String("checked=\"0\"")))
        return false;
    if (lower.contains(QLatin1String("enabled=\"true\""))
        || lower.contains(QLatin1String("enabled=\"1\""))
        || lower.contains(QLatin1String("checked=\"true\""))
        || lower.contains(QLatin1String("checked=\"1\"")))
        return true;
    return fallback;
}

bool firstBoolParam(const QMap<QString, QString>& params,
                    std::initializer_list<const char*> keys,
                    bool fallback = false)
{
    const QString text = firstDynamicsParam(params, keys);
    return textLooksEnabled(text, fallback);
}

float optionNumberForNames(const QString& text,
                           std::initializer_list<const char*> names,
                           float fallback)
{
    for (const char* name : names) {
        const QString pattern = QStringLiteral(R"(\b%1\b[^0-9+-]*([-+]?\d*\.?\d+))")
                                    .arg(QString::fromLatin1(name));
        const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch())
            return toFloat(match.captured(1), fallback);
    }
    return fallback;
}

float optionUnitForNames(const QString& text,
                         std::initializer_list<const char*> names,
                         float fallback)
{
    return normalizeUnit(optionNumberForNames(text, names, fallback), fallback);
}

bool optionBoolForNames(const QString& text,
                        std::initializer_list<const char*> names,
                        bool fallback = false)
{
    for (const char* name : names) {
        const QString pattern = QStringLiteral(R"(\b%1\b[^A-Za-z0-9_-]*(true|false|0|1|yes|no|on|off))")
                                    .arg(QString::fromLatin1(name));
        const QRegularExpression re(pattern, QRegularExpression::CaseInsensitiveOption);
        const QRegularExpressionMatch match = re.match(text);
        if (match.hasMatch())
            return textLooksEnabled(match.captured(1), fallback);
    }
    return fallback;
}

QString resourceNameFromText(const QString& text)
{
    QXmlStreamReader xml(text);
    while (!xml.atEnd()) {
        if (xml.readNext() == QXmlStreamReader::StartElement) {
            const auto attrs = xml.attributes();
            for (const char* attr : { "filename", "brushFileName", "pattern", "texture", "file", "name" }) {
                const QString key = QString::fromLatin1(attr);
                if (attrs.hasAttribute(key)) {
                    const QString value = attrs.value(key).toString().trimmed();
                    if (!value.isEmpty())
                        return value;
                }
            }
        }
    }

    static const QRegularExpression fileRe(
        QStringLiteral(R"(([^<>"'\s]+\.(?:png|jpg|jpeg|bmp|webp)))"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch match = fileRe.match(text);
    if (match.hasMatch())
        return match.captured(1);
    return text.trimmed();
}

QString firstResourceName(const QMap<QString, QString>& params,
                          std::initializer_list<const char*> keys)
{
    const QString direct = firstDynamicsParam(params, keys);
    if (!direct.isEmpty())
        return resourceNameFromText(direct);

    for (auto it = params.constBegin(); it != params.constEnd(); ++it) {
        const QString key = it.key().toLower();
        if (key.contains(QLatin1String("texture")) || key.contains(QLatin1String("pattern")))
            return resourceNameFromText(it.value());
    }
    return QString();
}

// Build an engine tip (coverage in alpha) from an arbitrary image. If the source
// already has alpha, keep it; otherwise derive coverage from inverted luminance
// (dark → opaque), matching the editor's tip convention.
QImage imageToTip(const QImage& srcIn)
{
    if (srcIn.isNull()) return {};
    const QImage src = srcIn.convertToFormat(QImage::Format_ARGB32);
    bool hasAlpha = src.hasAlphaChannel();
    if (hasAlpha) {
        // Confirm the alpha actually varies; flat-opaque means treat as luminance.
        bool varies = false;
        for (int y = 0; y < src.height() && !varies; ++y) {
            const QRgb* r = reinterpret_cast<const QRgb*>(src.constScanLine(y));
            for (int x = 0; x < src.width(); ++x)
                if (qAlpha(r[x]) < 255) { varies = true; break; }
        }
        hasAlpha = varies;
    }
    QImage tip(src.size(), QImage::Format_ARGB32);
    for (int y = 0; y < src.height(); ++y) {
        const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
        QRgb* d = reinterpret_cast<QRgb*>(tip.scanLine(y));
        for (int x = 0; x < src.width(); ++x) {
            int cov;
            if (hasAlpha) {
                cov = qAlpha(s[x]);
            } else {
                const int lum = qGray(s[x]);
                cov = 255 - lum;
            }
            d[x] = qRgba(0, 0, 0, cov);
        }
    }
    return tip;
}

// ── Structural curve-option parsing (Krita 1:1) ───────────────────────────────

// Krita curves are "x,y;x,y;..." pairs in 0..1. Parse them verbatim.
QVector<QPointF> parseKritaCurvePoints(const QString& s)
{
    QVector<QPointF> pts;
    const QStringList pairs = s.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString& pair : pairs) {
        const QStringList xy = pair.split(QLatin1Char(','), Qt::SkipEmptyParts);
        if (xy.size() != 2)
            continue;
        bool ok1 = false, ok2 = false;
        const float x = xy[0].trimmed().toFloat(&ok1);
        const float y = xy[1].trimmed().toFloat(&ok2);
        if (ok1 && ok2)
            pts.append(QPointF(qBound(0.0f, x, 1.0f), qBound(0.0f, y, 1.0f)));
    }
    return pts;
}

// Parse one sensor's extra attributes (ramp length/periodic for fade/distance/
// time, angle offset for the drawing-angle sensor) off its XML element.
void readSensorAttributes(ImportedSensorCurve& sc, const QXmlStreamAttributes& a)
{
    for (const char* lengthKey : { "length", "duration" }) {
        if (a.hasAttribute(QLatin1String(lengthKey)))
            sc.length = qMax(0, qRound(toFloat(
                a.value(QLatin1String(lengthKey)).toString(), 0.0f)));
    }
    if (a.hasAttribute(QLatin1String("periodic")))
        sc.periodic = a.value(QLatin1String("periodic")).toString().toInt() != 0;
    if (a.hasAttribute(QLatin1String("angleOffset")))
        sc.angleOffset = toFloat(a.value(QLatin1String("angleOffset")).toString(), 0.0f);
}

// Parse a sensor-option value (the "<Axis>Sensor" param, raw sensor XML) into
// every sensor it declares with that sensor's own curve. Handles both the
// single-sensor form (<params id="pressure"><curve/></params>) and the stacked
// form (<params id="sensorslist"><ChildSensor id=".."><curve/></ChildSensor>…>).
QVector<ImportedSensorCurve> parseKritaSensors(const QString& value)
{
    QVector<ImportedSensorCurve> out;
    if (!value.contains(QLatin1Char('<')))
        return out;
    QXmlStreamReader r(value);
    while (!r.atEnd()) {
        if (r.readNext() != QXmlStreamReader::StartElement)
            continue;
        const QString nm = r.name().toString().toLower();
        const auto a = r.attributes();
        if (nm == QLatin1String("params")) {
            // A non-list <params id="X"> is itself the single sensor.
            const QString id = a.value(QLatin1String("id")).toString();
            if (id.compare(QLatin1String("sensorslist"), Qt::CaseInsensitive) != 0) {
                ImportedSensorCurve sc;
                sc.sensor = id;
                readSensorAttributes(sc, a);
                out.append(sc);
            }
        } else if (nm == QLatin1String("childsensor") || nm == QLatin1String("sensor")
                   || nm == QLatin1String("paramsensor")) {
            ImportedSensorCurve sc;
            sc.sensor = a.value(QLatin1String("id")).toString();
            if (sc.sensor.isEmpty())
                sc.sensor = a.value(QLatin1String("type")).toString();
            readSensorAttributes(sc, a);
            if (a.hasAttribute(QLatin1String("curve")))
                sc.curve = parseKritaCurvePoints(a.value(QLatin1String("curve")).toString());
            out.append(sc);
        } else if (nm == QLatin1String("curve") && !out.isEmpty()) {
            // <curve> belongs to the sensor it sits inside (the last one opened).
            const QVector<QPointF> c = parseKritaCurvePoints(r.readElementText());
            if (!c.isEmpty())
                out.last().curve = c;
        }
    }
    return out;
}

// Build the curve option for one axis from its named params, faithfully:
//   Pressure<Axis>     — the option's on/off switch (checkable axes only)
//   <Axis>Value        — the option strength (flat multiplier over the sensors)
//   <Axis>Sensor       — which sensor(s) drive the axis (+ per-sensor curves)
//   <Axis>UseCurve     — whether the response curve is applied at all; when off
//                        the option is a flat strength multiplier
//   <Axis>UseSameCurve — all sensors share <Axis>commonCurve vs their own
//   <Axis>commonCurve  — the shared response curve
//   <Axis>curveMode    — sensor combination: 0=multiply..4=difference
// `checkable` axes (size/rotation/ratio/scatter) are absent unless their
// Pressure<Axis> switch is on; opacity/flow are always applied when described.
ImportedCurveOption extractKritaAxisCurve(const QMap<QString, QString>& params,
                                          const QString& axisPrefix,
                                          bool checkable,
                                          float maxStrength = 1.0f)
{
    ImportedCurveOption opt;
    const QString sensorXml = params.value(axisPrefix + QLatin1String("Sensor"));
    const bool described = params.contains(axisPrefix + QLatin1String("Value"))
        || !sensorXml.isEmpty();
    if (!described)
        return opt;                       // axis not described → absent
    if (checkable && !textLooksEnabled(
            params.value(QLatin1String("Pressure") + axisPrefix), false))
        return opt;                       // switched off → absent

    const float strength = qBound(0.0f,
        toFloat(params.value(axisPrefix + QLatin1String("Value")), 1.0f), maxStrength);
    const bool useCurve = textLooksEnabled(
        params.value(axisPrefix + QLatin1String("UseCurve")), true);

    QVector<ImportedSensorCurve> sensors;
    if (useCurve) {
        const bool useSameCurve = textLooksEnabled(
            params.value(axisPrefix + QLatin1String("UseSameCurve")), true);
        const QVector<QPointF> commonCurve = parseKritaCurvePoints(
            params.value(axisPrefix + QLatin1String("commonCurve")));

        sensors = parseKritaSensors(sensorXml);
        // When all sensors share one curve, override each with the common curve;
        // otherwise keep each sensor's own (already parsed above).
        for (ImportedSensorCurve& sc : sensors) {
            if (sc.sensor.isEmpty())
                sc.sensor = QStringLiteral("pressure");
            sc.sensor = sc.sensor.toLower();
            if (useSameCurve)
                sc.curve = commonCurve;
        }
    }
    // useCurve off (or no sensors) → flat strength multiplier: the empty sensor
    // list makes the engine option contribute exactly maxValue.

    opt.present = true;
    opt.enabled = true;
    opt.sensors = sensors;
    opt.minValue = 0.0f;
    opt.maxValue = strength;
    bool modeOk = false;
    const int mode = params.value(axisPrefix + QLatin1String("curveMode")).toInt(&modeOk);
    opt.combineMode = (modeOk && mode >= 0 && mode <= 4) ? mode : 0;
    return opt;
}

struct EngineCompat { QString label; bool basicFallback; };

EngineCompat classifyEngine(const QString& paintop)
{
    const QString p = paintop.toLower();
    if (p == QLatin1String("paintbrush") || p == QLatin1String("pixelbrush")
        || p == QLatin1String("defaultpaintop"))
        return { QStringLiteral("Compatible"), false };
    if (p == QLatin1String("colorsmudge") || p == QLatin1String("smudge")
        || p == QLatin1String("deformbrush") || p == QLatin1String("hairy")
        || p == QLatin1String("sketchbrush") || p == QLatin1String("curvebrush")
        || p == QLatin1String("chalkbrush") || p == QLatin1String("dynabrush"))
        return { QStringLiteral("Partial"), true };
    // particlebrush, filter, clone, gridbrush, spraybrush, etc.
    return { QStringLiteral("Unsupported"), true };
}

} // namespace

bool KritaKppImportAdapter::canImport(const QString& filePath) const
{
    const QString suffix = QFileInfo(filePath).suffix().toLower();
    if (suffix != QLatin1String("kpp") && suffix != QLatin1String("paintoppreset"))
        return false;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    return hasPngSignature(f.read(8));
}

ImportedBrushPreset KritaKppImportAdapter::parsePreset(const QByteArray& pngBytes,
                                                       const QString& sourceFile,
                                                       BrushImportSourceType sourceType,
                                                       bool scanOnly,
                                                       const ResourceResolver& resolver)
{
    ImportedBrushPreset preset;
    preset.sourceType = sourceType;
    preset.sourceFilePath = sourceFile;
    preset.name = QFileInfo(sourceFile).completeBaseName();

    auto diag = [&](BrushImportSeverity sev, const QString& code, const QString& msg) {
        BrushImportDiagnostic d;
        d.severity = sev; d.code = code; d.message = msg;
        d.sourceFile = sourceFile; d.presetName = preset.name;
        preset.diagnostics.append(d);
    };

    if (!hasPngSignature(pngBytes)) {
        diag(BrushImportSeverity::Error, QStringLiteral("corrupted_file"),
             QStringLiteral("This Krita preset could not be read (not a valid preset file)."));
        preset.compatibilityLabel = QStringLiteral("Unsupported");
        return preset;
    }

    // The PNG pixels are the preset icon — a fine thumbnail/preview source.
    QImage icon;
    icon.loadFromData(pngBytes, "PNG");

    const QMap<QString, QString> texts = extractPngTexts(pngBytes);
    QString presetXml;
    for (auto it = texts.constBegin(); it != texts.constEnd(); ++it) {
        if (it.value().contains(QLatin1String("<Preset"))
            || it.key().compare(QLatin1String("preset"), Qt::CaseInsensitive) == 0) {
            presetXml = it.value();
            break;
        }
    }

    QString paintop;
    QString brushDef;
    QMap<QString, QString> params;
    // Krita 5 embeds referenced resources (brush tip / pattern) as base64 inside
    // the preset's <resources> section, keyed by filename / name / md5sum. A
    // standalone .kpp carries no external resolver, so this is the only source.
    QMap<QString, QString> embeddedResourceB64;
    // First embedded pattern resource (type="patterns"), keyed by filename/name/md5
    // (hex). Used as the texture fallback when the preset names its grain only by a
    // base64 MD5 the resource map can't match directly (the common Krita-5 case).
    QString embeddedPatternKey;
    if (!presetXml.isEmpty()) {
        QXmlStreamReader xml(presetXml);
        while (!xml.atEnd()) {
            const auto tok = xml.readNext();
            if (tok == QXmlStreamReader::StartElement) {
                const QString elem = xml.name().toString();
                if (elem == QLatin1String("Preset")) {
                    const auto a = xml.attributes();
                    if (a.hasAttribute(QLatin1String("name")))
                        preset.name = a.value(QLatin1String("name")).toString();
                    if (a.hasAttribute(QLatin1String("paintopid")))
                        paintop = a.value(QLatin1String("paintopid")).toString();
                } else if (elem == QLatin1String("param")) {
                    const QString curParam = xml.attributes().value(QLatin1String("name")).toString();
                    collectDeepParam(xml, curParam, params);
                } else if (elem == QLatin1String("resource")) {
                    // <resource md5sum=".." type="brushes|patterns" filename=".."
                    //   name=".."><![CDATA[ base64 image bytes ]]></resource>
                    const auto a = xml.attributes();
                    const QString fn = a.value(QLatin1String("filename")).toString().trimmed();
                    const QString nm = a.value(QLatin1String("name")).toString().trimmed();
                    const QString md5 = a.value(QLatin1String("md5sum")).toString().trimmed();
                    const QString ty = a.value(QLatin1String("type")).toString().trimmed();
                    const QString b64 =
                        xml.readElementText(QXmlStreamReader::SkipChildElements).trimmed();
                    if (!b64.isEmpty()) {
                        if (!fn.isEmpty())  embeddedResourceB64.insert(fn, b64);
                        if (!nm.isEmpty())  embeddedResourceB64.insert(nm, b64);
                        if (!md5.isEmpty()) embeddedResourceB64.insert(md5, b64);
                        if (embeddedPatternKey.isEmpty()
                            && ty.compare(QLatin1String("patterns"), Qt::CaseInsensitive) == 0)
                            embeddedPatternKey = !fn.isEmpty() ? fn
                                               : (!nm.isEmpty() ? nm : md5);
                    }
                }
            }
        }
        brushDef = params.value(QStringLiteral("brush_definition.text"));
        if (brushDef.isEmpty())
            brushDef = params.value(QStringLiteral("brush_definition"));
    } else {
        diag(BrushImportSeverity::Warning, QStringLiteral("partial_import"),
             QStringLiteral("This Krita preset has no embedded settings; only its icon was imported."));
    }

    // Unified resource lookup: the preset's own embedded base64 first (decoded on
    // demand so unused resources and scan-only passes cost nothing), then any
    // archive-backed resolver the caller supplied (the bundle adapter). Truthy
    // whenever either source can serve bytes, so the resolution sites below no
    // longer need a non-null external resolver to load an embedded tip/pattern.
    ResourceResolver resolveResource;
    if (!embeddedResourceB64.isEmpty() || resolver) {
        resolveResource = [&embeddedResourceB64, &resolver](const QString& name) -> QByteArray {
            const QString key = name.trimmed();
            if (!key.isEmpty() && !embeddedResourceB64.isEmpty()) {
                auto it = embeddedResourceB64.constFind(key);
                if (it == embeddedResourceB64.constEnd()) {
                    for (auto i = embeddedResourceB64.constBegin();
                         i != embeddedResourceB64.constEnd(); ++i)
                        if (i.key().compare(key, Qt::CaseInsensitive) == 0) { it = i; break; }
                }
                if (it != embeddedResourceB64.constEnd()) {
                    const QByteArray raw = QByteArray::fromBase64(it.value().toLatin1());
                    if (!raw.isEmpty())
                        return raw;
                }
            }
            return resolver ? resolver(name) : QByteArray();
        };
    }

    preset.originalEngineName = paintop.isEmpty() ? QStringLiteral("Krita") : paintop;
    const EngineCompat compat = classifyEngine(paintop);
    preset.compatibilityLabel = compat.label;

    // Common paintop params (names vary across Krita versions; try the usual set).
    auto firstParam = [&](std::initializer_list<const char*> keys, float fb) {
        for (const char* k : keys) {
            const QString key = QString::fromLatin1(k);
            if (params.contains(key))
                return toFloat(params.value(key), fb);
        }
        return fb;
    };
    // NOTE: "SizeValue" is the *size curve-option strength* (a 0..1 dynamics
    // base, ~1.0 = "100% of the brush size"), NOT a diameter. The painted
    // diameter comes from the tip — a predefined brush's native size x its
    // <Brush scale=...>, or a procedural <MaskGenerator diameter=...> — and is
    // resolved further below. Treating SizeValue as pixels made every imported
    // pixel-brush a ~1px brush that barely painted, so it is deliberately
    // excluded here; BrushSize/diameter/size remain as genuine-pixel fallbacks
    // for presets authored by other tools. Likewise RotationValue/RatioValue are
    // option strengths (handled by the curve options below), NOT the static tip
    // angle/ratio, which come from the brush definition.
    preset.size = qBound(1.0f,
                         firstParam({ "BrushSize", "diameter", "size" }, preset.size),
                         5000.0f);
    const float spacingValue = firstParam({ "SpacingValue", "spacing" }, preset.spacing);
    preset.spacing = qBound(1.0f,
                            spacingValue <= 10.0f ? spacingValue * 100.0f : spacingValue,
                            1000.0f);
    preset.opacity = firstUnitParam(params, { "OpacityValue", "opacity" }, 1.0f);
    preset.flow = firstUnitParam(params, { "FlowValue", "flow" }, 1.0f);
    preset.angle = firstParam({ "AngleValue" }, preset.angle);
    preset.hardness = firstUnitParam(params, { "HardnessValue", "hardness" }, preset.hardness);

    // Brush-tip mirror. The mirror-option enable flags map to the engine's
    // static dab flip. The dynamic per-stroke mirror (MirrorSensor/PressureMirror)
    // has no equivalent, so only the steady horizontal/vertical flip is carried.
    preset.flipX = firstBoolParam(params, { "HorizontalMirrorEnabled", "HorizontalMirror" }, false);
    preset.flipY = firstBoolParam(params, { "VerticalMirrorEnabled", "VerticalMirror" }, false);

    // Per-axis curve options, parsed structurally (enable switch, strength,
    // sensors, per-sensor curves, combination mode). Size/Rotation/Ratio/Scatter
    // are gated on their Pressure<Axis> switch; Opacity/Flow are always applied
    // when described. The Opacity/Flow strengths are the preset's base
    // opacity/flow (already read above), so their options keep strength 1 to
    // avoid applying the base twice.
    preset.sizeCurve = extractKritaAxisCurve(params, QStringLiteral("Size"), true);
    preset.opacityCurve = extractKritaAxisCurve(params, QStringLiteral("Opacity"), false);
    preset.opacityCurve.maxValue = 1.0f;
    preset.flowCurve = extractKritaAxisCurve(params, QStringLiteral("Flow"), false);
    preset.flowCurve.maxValue = 1.0f;
    preset.rotationCurve = extractKritaAxisCurve(params, QStringLiteral("Rotation"), true);
    preset.ratioCurve = extractKritaAxisCurve(params, QStringLiteral("Ratio"), true);
    // Scatter strength spans 0..5 (a multiple of the dab diameter).
    preset.scatterCurve = extractKritaAxisCurve(params, QStringLiteral("Scatter"), true, 5.0f);
    preset.scatterAxisX = firstBoolParam(params, { "Scattering/AxisX", "ScatterAxisX" }, true);
    preset.scatterAxisY = firstBoolParam(params, { "Scattering/AxisY", "ScatterAxisY" }, true);

    // Krita stores the texture under the "Texture/Pattern/..." param group; the
    // referenced pattern is matched by filename first, then its MD5 (how Krita
    // bundles cross-reference resources). The legacy flat keys stay as a fallback
    // for presets authored by other tools.
    const QString textureName = firstResourceName(
        params, { "Texture/Pattern/PatternFileName", "Texture/Pattern/Name",
                  "Texture/Pattern", "TextureOption", "PatternOption",
                  "TextureName", "PatternName" });
    const QString textureMd5 = firstDynamicsParam(
        params, { "Texture/Pattern/PatternMD5Sum", "Texture/Pattern/PatternMD5" }).trimmed();
    // Only honour the texture when the preset actually enables it; the pattern may
    // stay embedded while switched off. Absent flag → assume on (back-compat).
    const bool textureEnabled = firstBoolParam(
        params, { "Texture/Pattern/Enabled", "TextureEnabled", "Texture/Enabled" }, true);
    if (!scanOnly && textureEnabled && resolveResource
        && (!textureName.isEmpty() || !textureMd5.isEmpty() || !embeddedPatternKey.isEmpty())) {
        QImage texture;
        // Resolve the grain pattern, most specific key first:
        //  1) the named file (when the preset fills PatternFileName/Name),
        //  2) the MD5 — Krita 5 writes Texture/Pattern/PatternMD5 as the *raw* md5 in
        //     base64, while embedded <resource> entries are keyed by the md5 in hex,
        //     so try the value verbatim (bundles) and then its hex transcoding,
        //  3) the single embedded pattern resource (the preset references its own
        //     grain but left the filename/name blank — the common Krita-5 case).
        QByteArray bytes = textureName.isEmpty() ? QByteArray() : resolveResource(textureName);
        if (bytes.isEmpty() && !textureMd5.isEmpty()) {
            bytes = resolveResource(textureMd5);
            if (bytes.isEmpty()) {
                const QByteArray rawMd5 = QByteArray::fromBase64(textureMd5.toLatin1());
                if (rawMd5.size() == 16)
                    bytes = resolveResource(QString::fromLatin1(rawMd5.toHex()));
            }
        }
        if (bytes.isEmpty() && !embeddedPatternKey.isEmpty())
            bytes = resolveResource(embeddedPatternKey);
        if (!bytes.isEmpty())
            texture.loadFromData(bytes);
        if (!texture.isNull()) {
            preset.hasTexture = true;
            // Grayscale with Krita's luminance weights ((11R+16G+5B)/32) and the same
            // alpha-against-white premultiply as KisTextureMaskInfo, instead of Qt's
            // qGray, so the grain tone matches. Brightness/contrast/neutral are still
            // applied later by the dab (same order as Krita).
            const QImage src = texture.convertToFormat(QImage::Format_ARGB32);
            QImage gray(src.size(), QImage::Format_Grayscale8);
            for (int y = 0; y < src.height(); ++y) {
                const QRgb* s = reinterpret_cast<const QRgb*>(src.constScanLine(y));
                uchar* d = gray.scanLine(y);
                for (int x = 0; x < src.width(); ++x) {
                    const QRgb px = s[x];
                    const int g = (qRed(px) * 11 + qGreen(px) * 16 + qBlue(px) * 5) / 32;
                    const float a = qAlpha(px) / 255.0f;
                    const float v = (g / 255.0f) * a + (1.0f - a);
                    d[x] = static_cast<uchar>(qBound(0.0f, v * 255.0f, 255.0f));
                }
            }
            preset.textureImage = gray;
            // Krita's texture strength lives in the "Texture/Strength/Value" option.
            preset.textureDepth = firstUnitParam(
                params, { "Texture/Strength/Value", "TextureDepth", "TextureStrength",
                          "PatternDepth" }, 1.0f);
            preset.textureScale = qMax(
                0.01f,
                firstNumericParam(params, { "Texture/Pattern/Scale", "TextureScale",
                                            "PatternScale", "texture_scale" }, 1.0f));
            preset.textureBrightness = qBound(-1.0f,
                firstNumericParam(params, { "Texture/Pattern/Brightness", "TextureBrightness",
                                            "Texture/Brightness" }, 0.0f), 1.0f);
            // Contrast is a direct multiplier (1.0 = identity, pivoting at 0.5), the
            // same convention the engine now uses, so store it raw — the old code
            // clamped it into a -1..1 scale and inflated it to a near-binary grain
            // that shattered the stroke.
            preset.textureContrast = qBound(0.0f,
                firstNumericParam(params, { "Texture/Pattern/Contrast", "TextureContrast",
                                            "Texture/Contrast" }, 1.0f), 10.0f);
            // Neutral point (piecewise-remap pivot). 0..1, default 0.5.
            preset.textureNeutralPoint = qBound(0.0f,
                firstNumericParam(params, { "Texture/Pattern/NeutralPoint", "TextureNeutralPoint",
                                            "Texture/NeutralPoint" }, 0.5f), 1.0f);
            // Krita cutoff is a [left, right] band in 0..255 with a policy
            // (0=off, 1=cut to transparent, 2=cut to opaque). Normalize to 0..1 and
            // carry the real policy and right bound instead of guessing from left>0.
            auto norm255 = [](float v) {
                return v > 1.0f ? qBound(0.0f, v / 255.0f, 1.0f) : qBound(0.0f, v, 1.0f);
            };
            preset.textureCutoff = norm255(firstNumericParam(
                params, { "Texture/Pattern/CutoffLeft", "TextureCutoffLeft",
                          "Texture/CutoffLeft", "CutoffLeft" }, 0.0f));
            preset.textureCutoffMax = norm255(firstNumericParam(
                params, { "Texture/Pattern/CutoffRight", "TextureCutoffRight",
                          "Texture/CutoffRight", "CutoffRight" }, 255.0f));
            preset.textureCutoffPolicy = qBound(0, qRound(firstNumericParam(
                params, { "Texture/Pattern/CutoffPolicy", "TextureCutoffPolicy",
                          "CutoffPolicy" }, 0.0f)), 2);
            // TexturingMode: 0 = Multiply (grain), 1 = Subtract (knock-out).
            preset.textureTexturing = qRound(firstNumericParam(
                params, { "Texture/Pattern/TexturingMode", "TexturingMode" }, 0.0f)) == 1 ? 1 : 0;
            preset.textureInvert = firstBoolParam(
                params, { "Texture/Pattern/Invert", "TextureInvert" }, false);
            // Random pattern offset per axis, driving the engine's
            // randomHorizontalOffset/randomVerticalOffset directly.
            preset.textureRandomOffsetX =
                firstBoolParam(params, { "Texture/Pattern/isRandomOffsetX" }, false);
            preset.textureRandomOffsetY =
                firstBoolParam(params, { "Texture/Pattern/isRandomOffsetY" }, false);
        }
    }

    const QString dualOption = firstDynamicsParam(
        params, { "DualBrushOption", "DualBrush", "dual_brush", "DualBrushSettings" });
    preset.hasDualBrush = firstBoolParam(
        params, { "DualBrushEnabled", "DualBrush", "dual_brush_enabled" },
        textLooksEnabled(dualOption, false));
    if (preset.hasDualBrush || !dualOption.isEmpty()) {
        preset.dualBrushSize = qMax(
            1.0f,
            firstNumericParam(params, { "DualBrushSize", "dual_size" },
                              optionNumberForNames(dualOption, { "size", "diameter" }, preset.dualBrushSize)));
        preset.dualBrushSpacing = qMax(
            0.01f,
            firstNumericParam(params, { "DualBrushSpacing", "dual_spacing" },
                              optionNumberForNames(dualOption, { "spacing" }, preset.dualBrushSpacing)));
        if (preset.dualBrushSpacing > 10.0f)
            preset.dualBrushSpacing /= 100.0f;
        preset.dualBrushScatter =
            firstUnitParam(params, { "DualBrushScatter", "dual_scatter" },
                           optionUnitForNames(dualOption, { "scatter" }, preset.dualBrushScatter));
        preset.dualBrushCount = qBound(
            1,
            qRound(firstNumericParam(params, { "DualBrushCount", "dual_count" },
                                     optionNumberForNames(dualOption, { "count" }, preset.dualBrushCount))),
            16);
        preset.dualBrushHardness =
            firstUnitParam(params, { "DualBrushHardness", "dual_hardness" },
                           optionUnitForNames(dualOption, { "hardness" }, preset.dualBrushHardness));
        preset.dualBrushTipType = firstBoolParam(
            params, { "DualBrushSquare", "dual_square" },
            dualOption.toLower().contains(QLatin1String("square"))) ? 1 : 0;
    }

    preset.blendModeName = firstDynamicsParam(
        params, { "CompositeOp", "CompositeOpId", "compositeop", "BlendMode", "blend_mode" });
    preset.airbrush = firstBoolParam(
        params, { "PaintOpSettings/isAirbrushing", "AirbrushOption", "Airbrush", "airbrush" },
        false);
    preset.airbrushRate = qBound(1.0f,
        firstNumericParam(params, { "PaintOpSettings/rate", "AirbrushRate" }, 20.0f),
        200.0f);
    // Painting mode: 1 = build-up (dabs composite straight onto the layer),
    // 2 = wash (per-stroke buffer capped at the stroke opacity).
    if (params.contains(QStringLiteral("PaintOpAction"))) {
        const int action = qRound(firstNumericParam(params, { "PaintOpAction" }, 2.0f));
        preset.paintMode = (action == 1) ? 0 : 1;
    }
    preset.wetEdges = firstBoolParam(
        params, { "WetEdges", "WetEdgesOption", "wet_edges" }, false);
    if (firstBoolParam(params, { "Stabilizer", "StabilizerOption", "stabilizer" }, false)) {
        preset.smoothingMode = 2; // PulledString, the closest existing engine mode.
        preset.smoothingRadius =
            qMax(1.0f, firstNumericParam(params, { "StabilizerDistance", "SmoothingRadius" },
                                         preset.smoothingRadius));
    } else if (firstBoolParam(params, { "Smoothing", "SmoothingOption", "BasicSmoothing" }, false)) {
        preset.smoothingMode = 0; // Basic.
        preset.smoothingRadius =
            qMax(1.0f, firstNumericParam(params, { "SmoothingRadius", "smoothing_radius" },
                                         preset.smoothingRadius));
    }

    // brush_definition holds the tip: an <MaskGenerator> (auto/procedural) or a
    // predefined <Brush> (embedded base64 / referenced file).
    QImage tipImage;
    float brushScale = 1.0f;   // used by the predefined-tip size fallback below
    if (!brushDef.isEmpty()) {
        QXmlStreamReader bx(brushDef);
        QString brushType;
        QString brushFileName;
        QString embeddedB64;
        bool sawMaskGen = false;
        while (!bx.atEnd()) {
            const auto tok = bx.readNext();
            if (tok == QXmlStreamReader::StartElement) {
                const auto a = bx.attributes();
                const QString elem = bx.name().toString();
                if (elem == QLatin1String("Brush") || elem == QLatin1String("brush")) {
                    brushType = a.value(QLatin1String("type")).toString();
                    if (a.hasAttribute(QLatin1String("spacing")))
                        preset.spacing = qBound(1.0f,
                            toFloat(a.value(QLatin1String("spacing")).toString(), 0.1f) * 100.0f, 1000.0f);
                    if (a.hasAttribute(QLatin1String("angle")))
                        // Krita stores the brush-tip angle in radians; the importer's
                        // angle field is degrees (the converter re-applies radians).
                        preset.angle = qRadiansToDegrees(
                            toFloat(a.value(QLatin1String("angle")).toString(), 0.0f));
                    if (a.hasAttribute(QLatin1String("scale")))
                        brushScale = toFloat(a.value(QLatin1String("scale")).toString(), 1.0f);
                    if (a.hasAttribute(QLatin1String("BrushVersion")) || a.hasAttribute(QLatin1String("filename")))
                        brushFileName = a.value(QLatin1String("filename")).toString();
                    if (a.hasAttribute(QLatin1String("brushFileName")))
                        brushFileName = a.value(QLatin1String("brushFileName")).toString();
                    // Auto-spacing (Layer C).
                    for (const char* k : { "AutoSpacingActive", "autoSpacingActive", "useAutoSpacing" })
                        if (a.hasAttribute(QLatin1String(k)))
                            preset.useAutoSpacing = textLooksEnabled(a.value(QLatin1String(k)).toString(), false);
                    for (const char* k : { "AutoSpacingCoeff", "autoSpacingCoeff" })
                        if (a.hasAttribute(QLatin1String(k)))
                            preset.autoSpacingCoeff = qMax(0.01f,
                                toFloat(a.value(QLatin1String(k)).toString(), 1.0f));
                    // How the tip's pixels become paint. Newer presets store a
                    // numeric enum (0=ALPHAMASK, 1=IMAGESTAMP, 2=LIGHTNESSMAP,
                    // 3=GRADIENTMAP); older ones store preserveLightness and/or
                    // ColorAsMask flags. Resolution order mirrors the source
                    // format: preserveLightness wins, then the explicit
                    // application, then the legacy flag (kept for the converter
                    // to combine with tip-colour detection).
                    if (a.hasAttribute(QLatin1String("brushApplication"))) {
                        const QString appRaw =
                            a.value(QLatin1String("brushApplication")).toString().trimmed();
                        bool isNum = false;
                        const int appNum = appRaw.toInt(&isNum);
                        if (isNum) {
                            switch (appNum) {
                            case 1:  preset.brushApplication = QStringLiteral("IMAGESTAMP"); break;
                            case 2:
                            case 3:  preset.brushApplication = QStringLiteral("LIGHTNESSMAP"); break;
                            case 0:
                            default: preset.brushApplication = QStringLiteral("ALPHAMASK"); break;
                            }
                        } else {
                            preset.brushApplication = appRaw.toUpper();
                        }
                    }
                    if (a.hasAttribute(QLatin1String("preserveLightness"))
                        && a.value(QLatin1String("preserveLightness")).toString().toInt() != 0)
                        preset.brushApplication = QStringLiteral("LIGHTNESSMAP");
                    if (a.hasAttribute(QLatin1String("ColorAsMask")))
                        preset.colorAsMask =
                            a.value(QLatin1String("ColorAsMask")).toString().toInt() != 0 ? 1 : 0;
                    if (a.hasAttribute(QLatin1String("AdjustmentMidPoint"))) {
                        preset.hasTipRemap = true;
                        preset.tipMidpoint = qBound(0.0f,
                            toFloat(a.value(QLatin1String("AdjustmentMidPoint")).toString(), 127.0f) / 255.0f, 1.0f);
                    }
                    if (a.hasAttribute(QLatin1String("BrightnessAdjustment"))) {
                        preset.hasTipRemap = true;
                        preset.tipBrightness = qBound(-1.0f,
                            toFloat(a.value(QLatin1String("BrightnessAdjustment")).toString(), 0.0f), 1.0f);
                    }
                    if (a.hasAttribute(QLatin1String("ContrastAdjustment"))) {
                        preset.hasTipRemap = true;
                        preset.tipContrast = qBound(-1.0f,
                            toFloat(a.value(QLatin1String("ContrastAdjustment")).toString(), 0.0f), 1.0f);
                    }
                } else if (elem == QLatin1String("MaskGenerator")) {
                    sawMaskGen = true;
                    preset.maskGenerator.present = true;
                    if (a.hasAttribute(QLatin1String("diameter")))
                        preset.size = qBound(1.0f,
                            toFloat(a.value(QLatin1String("diameter")).toString(), preset.size), 5000.0f);
                    if (a.hasAttribute(QLatin1String("ratio")))
                        preset.roundness = qBound(0.01f,
                            toFloat(a.value(QLatin1String("ratio")).toString(), 1.0f), 1.0f);
                    // hfade/vfade: 0 = hard, 1 = soft → hardness = 1 - fade.
                    const float hfade = toFloat(a.value(QLatin1String("hfade")).toString(), 0.0f);
                    const float vfade = toFloat(a.value(QLatin1String("vfade")).toString(), hfade);
                    preset.hardness = qBound(0.0f, 1.0f - std::max(hfade, vfade), 1.0f);
                    // type = shape (circle/rectangle); id = profile (default/soft/gauss).
                    const QString mgType = a.value(QLatin1String("type")).toString().toLower();
                    preset.maskGenerator.shape = mgType.contains(QLatin1String("rect")) ? 1 : 0;
                    const QString mgId = a.value(QLatin1String("id")).toString().toLower();
                    preset.maskGenerator.profile = mgId.contains(QLatin1String("gauss")) ? 2
                        : (mgId.contains(QLatin1String("soft")) ? 1 : 0);
                    preset.maskGenerator.hFade = qBound(0.0f, hfade, 1.0f);
                    preset.maskGenerator.vFade = qBound(0.0f, vfade, 1.0f);
                    if (a.hasAttribute(QLatin1String("spikes")))
                        preset.maskGenerator.spikes = qMax(2,
                            qRound(toFloat(a.value(QLatin1String("spikes")).toString(), 2.0f)));
                    if (a.hasAttribute(QLatin1String("density")))
                        preset.maskGenerator.density =
                            normalizeUnit(toFloat(a.value(QLatin1String("density")).toString(), 1.0f), 1.0f);
                    if (a.hasAttribute(QLatin1String("randomness")))
                        preset.maskGenerator.randomness =
                            normalizeUnit(toFloat(a.value(QLatin1String("randomness")).toString(), 0.0f), 0.0f);
                    // Optional falloff remap curve applied over the profile.
                    if (a.hasAttribute(QLatin1String("softness_curve")))
                        preset.maskGenerator.softnessCurve = parseKritaCurvePoints(
                            a.value(QLatin1String("softness_curve")).toString());
                }
            } else if (tok == QXmlStreamReader::Characters) {
                if (!bx.isWhitespace())
                    embeddedB64 += bx.text().toString();
            }
        }

        // Predefined brush: try embedded base64, then a referenced resource.
        if (!scanOnly && brushType.compare(QLatin1String("auto"), Qt::CaseInsensitive) != 0) {
            QByteArray raw;
            if (embeddedB64.trimmed().isEmpty() && !brushDef.trimmed().startsWith(QLatin1Char('<')))
                embeddedB64 = brushDef.trimmed();
            const QByteArray b64 = embeddedB64.trimmed().toLatin1();
            if (b64.size() > 64)
                raw = QByteArray::fromBase64(b64);
            // The tip may be a plain image Qt can read, or a GIMP/Krita .gbr/.gih
            // that only GimpBrushDecoder understands (these are common in Krita
            // bundles and newer presets that embed the tip as base64).
            auto decodeTip = [](const QByteArray& bytes) -> QImage {
                if (bytes.isEmpty()) return {};
                QImage img;
                if (img.loadFromData(bytes) && !img.isNull())
                    return img;
                if (GimpBrushDecoder::looksLikeGimpBrush(bytes))
                    return GimpBrushDecoder::decode(bytes).firstImage();
                return {};
            };
            QImage decoded = decodeTip(raw);
            if (decoded.isNull() && !brushFileName.isEmpty() && resolveResource)
                decoded = decodeTip(resolveResource(brushFileName));
            if (!decoded.isNull())
                tipImage = decoded;
            else if (!sawMaskGen)
                diag(BrushImportSeverity::Warning, QStringLiteral("missing_brush_tip"),
                     QStringLiteral("The preset references a brush tip that could not be found. "
                                    "A basic brush was created from the supported settings."));
        }
    }

    const float deepBrushSpacing = firstNumericParam(
        params, { "brush_definition.Brush.spacing", "brush_definition.brush.spacing" },
        -1.0f);
    if (deepBrushSpacing >= 0.0f)
        preset.spacing = qBound(1.0f,
                                deepBrushSpacing <= 10.0f ? deepBrushSpacing * 100.0f : deepBrushSpacing,
                                1000.0f);
    // brush_definition angle is radians (Krita); convert to the degrees the
    // importer carries. Sentinel keeps the current value when the deep param is
    // absent (0 is a valid angle, so it can't double as "not found").
    const float deepBrushAngleRad = firstNumericParam(
        params, { "brush_definition.Brush.angle", "brush_definition.brush.angle" },
        -1000.0f);
    if (deepBrushAngleRad > -999.0f)
        preset.angle = qRadiansToDegrees(deepBrushAngleRad);

    const float deepDiameter = firstNumericParam(
        params, { "brush_definition.MaskGenerator.diameter", "brush_definition.maskgenerator.diameter" },
        -1.0f);
    if (deepDiameter > 0.0f)
        preset.size = qBound(1.0f, deepDiameter, 5000.0f);
    const float deepRatio = firstUnitParam(
        params, { "brush_definition.MaskGenerator.ratio", "brush_definition.maskgenerator.ratio" },
        preset.roundness);
    preset.roundness = qBound(0.01f, deepRatio, 1.0f);
    float hfade = firstNumericParam(
        params, { "brush_definition.MaskGenerator.hfade", "brush_definition.maskgenerator.hfade" },
        -1.0f);
    if (hfade >= 0.0f)
        hfade = normalizeUnit(hfade, hfade);
    float vfade = firstNumericParam(
        params, { "brush_definition.MaskGenerator.vfade", "brush_definition.maskgenerator.vfade" },
        -1.0f);
    if (vfade >= 0.0f)
        vfade = normalizeUnit(vfade, vfade);
    const float fade = std::max(hfade, vfade);
    if (fade >= 0.0f)
        preset.hardness = qBound(0.0f, 1.0f - fade, 1.0f);

    if (compat.label == QLatin1String("Partial"))
        diag(BrushImportSeverity::Warning, QStringLiteral("unsupported_parameter"),
             QStringLiteral("Krita-specific engine options (%1) are not available in this editor; "
                            "a compatible brush was created.").arg(preset.originalEngineName));
    else if (compat.label == QLatin1String("Unsupported"))
        diag(BrushImportSeverity::Warning, QStringLiteral("unsupported_brush_engine"),
             QStringLiteral("The Krita '%1' engine is not supported; the preset was imported as a "
                            "basic brush using its tip.").arg(preset.originalEngineName));

    if (!scanOnly && !tipImage.isNull()) {
        // Predefined Krita tip. Auto/procedural brushes have no bitmap and import
        // as a basic round brush (Circle tip) carrying the hardness/roundness
        // derived from the mask generator above. A coloured tip (RGBA .gbr or a
        // colour PNG) keeps its colours so it paints as a colour stamp; a
        // grayscale tip stays a coverage mask.
        const QImage tipArgb = tipImage.convertToFormat(QImage::Format_ARGB32);
        const bool coloredTip = !tipArgb.allGray();
        preset.tip.alphaImage = imageToTip(tipImage);
        preset.tip.originalSize = tipImage.size();
        preset.tip.name = preset.name;
        preset.tip.isAlphaOnly = !coloredTip;
        // Always keep the original RGBA tip, even for a grey one: a LightnessMap
        // brush derives its stroke from the tip's grey/lightness (and alpha), so
        // the converter must be able to recover those pixels rather than fall back
        // to the alpha-only coverage mask (which would zero the lightness). For a
        // truly chromatic tip this is also the ColorStamp source. isAlphaOnly still
        // flags "no chroma" so the default (non-LightnessMap) path stays AlphaMask.
        preset.tip.colorImage = tipArgb;
        // A predefined Krita tip's painted diameter is its native size × the
        // <Brush scale=...> (Krita's baseSize·scale) — e.g. a 300px tip at scale
        // 0.1 paints at 30px. Only an explicit pixel diameter (BrushSize/diameter/
        // size, used by other tools) overrides it; SizeValue is the size curve
        // value, not a diameter, so it must not count as "authored" here.
        const bool sizeAuthored = params.contains(QStringLiteral("BrushSize"))
            || params.contains(QStringLiteral("diameter"))
            || params.contains(QStringLiteral("size"));
        if (!sizeAuthored) {
            const float base = float(qMax(tipImage.width(), tipImage.height()));
            preset.size = qBound(1.0f, base * (brushScale > 0.0f ? brushScale : 1.0f), 5000.0f);
        }
    }

    // The .kpp's own PNG pixels are the preset's authored thumbnail; keep them as
    // the preview so the brush list can show the real brush look (tip render is the
    // fallback when there is no preview).
    if (!scanOnly)
        preset.previewImage = icon;
    return preset;
}

ImportedBrushPackage KritaKppImportAdapter::scan(const QString& filePath)
{
    return importFile(filePath, BrushImportOptions{});
}

ImportedBrushPackage KritaKppImportAdapter::importFile(const QString& filePath,
                                                       const BrushImportOptions&)
{
    ImportedBrushPackage pkg;
    pkg.sourceFilePath = filePath;
    pkg.sourceType = BrushImportSourceType::KritaKpp;
    pkg.vendorName = QStringLiteral("Krita");
    pkg.packageName = QFileInfo(filePath).completeBaseName();

    QFileInfo fi(filePath);
    if (fi.size() > BrushImportLimits::MaxFileSize || fi.size() <= 0) {
        BrushImportDiagnostic d;
        d.severity = BrushImportSeverity::Error;
        d.code = QStringLiteral("corrupted_file");
        d.message = QStringLiteral("The Krita preset could not be read.");
        d.sourceFile = filePath;
        pkg.diagnostics.append(d);
        return pkg;
    }

    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        BrushImportDiagnostic d;
        d.severity = BrushImportSeverity::Error;
        d.code = QStringLiteral("corrupted_file");
        d.message = QStringLiteral("The Krita preset could not be opened.");
        d.sourceFile = filePath;
        pkg.diagnostics.append(d);
        return pkg;
    }
    const QByteArray bytes = f.readAll();
    f.close();

    ImportedBrushPreset preset = parsePreset(bytes, filePath, BrushImportSourceType::KritaKpp,
                                             /*scanOnly=*/false, nullptr);
    pkg.presets.append(preset);
    return pkg;
}
