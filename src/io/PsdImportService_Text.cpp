// Text layer (`TySh` + EngineData) -> Hazor's TextLayerData.
//
// `TySh` layout and the EngineData key names/semantics follow the
// documented PSD/PSB text-engine format. Scope is bounded to the common
// case: plain text, per-run font/size/color/bold/italic/underline/
// strikethrough/tracking, and per-paragraph alignment — not warp, vertical
// text, optical kerning or OpenType feature toggles, none of which Hazor's
// TextLayerData models.

#include "PsdImportService_Internal.hpp"
#include "PsdEngineData.hpp"

#include "core/LayerTreeNode.hpp"
#include "text/TextRenderer.hpp"
#include "text/TextTypes.hpp"

#include <QCoreApplication>
#include <QImage>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace psdimport {

namespace {

using psdtext::EngineValue;

// Trims common PostScript-name suffixes/decorations to approximate a family
// name QFont can match (or gracefully substitute for).
QString cleanFontFamily(const QString& ps)
{
    QString s = ps;
    static const QStringList kSuffixes = {
        QStringLiteral("-BoldItalic"), QStringLiteral("-BoldOblique"), QStringLiteral("-Bold"),
        QStringLiteral("-Italic"), QStringLiteral("-Oblique"), QStringLiteral("-Regular"),
        QStringLiteral("-Roman"), QStringLiteral("MT"),
    };
    for (const QString& suf : kSuffixes) {
        if (s.endsWith(suf, Qt::CaseInsensitive)) {
            s.chop(suf.size());
            break;
        }
    }
    return s.isEmpty() ? ps : s;
}

QColor colorFromFillColor(const EngineValue& fillColor)
{
    const EngineValue& values = fillColor.get(QStringLiteral("Values"));
    if (values.kind() != EngineValue::Kind::Array)
        return QColor(Qt::black);
    const auto& arr = values.asArray();
    auto at = [&](size_t i) { return i < arr.size() ? arr[i].asReal(0.0) : 0.0; };
    const float a = static_cast<float>(std::clamp(at(0), 0.0, 1.0));
    switch (arr.size()) {
    case 2: { // alpha, gray
        const float g = static_cast<float>(std::clamp(at(1), 0.0, 1.0));
        return QColor::fromRgbF(g, g, g, a);
    }
    case 4: // alpha, r, g, b
        return QColor::fromRgbF(static_cast<float>(std::clamp(at(1), 0.0, 1.0)),
                                 static_cast<float>(std::clamp(at(2), 0.0, 1.0)),
                                 static_cast<float>(std::clamp(at(3), 0.0, 1.0)), a);
    case 5: { // alpha, c, m, y, k
        const double c = std::clamp(at(1), 0.0, 1.0), m = std::clamp(at(2), 0.0, 1.0);
        const double y = std::clamp(at(3), 0.0, 1.0), k = std::clamp(at(4), 0.0, 1.0);
        return QColor::fromRgbF(static_cast<float>((1.0 - c) * (1.0 - k)), static_cast<float>((1.0 - m) * (1.0 - k)),
                                 static_cast<float>((1.0 - y) * (1.0 - k)), a);
    }
    default:
        return QColor(Qt::black);
    }
}

TextSpan spanFromStyleSheetData(const EngineValue& d, const QStringList& fonts)
{
    TextSpan s;
    if (const EngineValue& fi = d.get(QStringLiteral("Font")); fi.isValid()) {
        const qint64 idx = fi.asInt(-1);
        if (idx >= 0 && idx < fonts.size()) {
            const QString ps = fonts[static_cast<int>(idx)];
            s.fontFamily = cleanFontFamily(ps);
            if (ps.contains(QStringLiteral("Bold"), Qt::CaseInsensitive))
                s.bold = true;
            if (ps.contains(QStringLiteral("Italic"), Qt::CaseInsensitive) || ps.contains(QStringLiteral("Oblique"), Qt::CaseInsensitive))
                s.italic = true;
        }
    }
    if (const EngineValue& v = d.get(QStringLiteral("FontSize")); v.isValid())
        s.fontSize = static_cast<float>(std::max(1.0, v.asReal(s.fontSize)));
    if (const EngineValue& v = d.get(QStringLiteral("FauxBold")); v.isValid())
        s.bold = s.bold || v.asBool();
    if (const EngineValue& v = d.get(QStringLiteral("FauxItalic")); v.isValid())
        s.italic = s.italic || v.asBool();
    if (const EngineValue& v = d.get(QStringLiteral("Underline")); v.isValid())
        s.underline = v.asBool();
    if (const EngineValue& v = d.get(QStringLiteral("Strikethrough")); v.isValid())
        s.strikethrough = v.asBool();
    if (const EngineValue& v = d.get(QStringLiteral("Tracking")); v.isValid())
        s.letterSpacing = static_cast<float>(v.asReal(0.0) / 1000.0 * s.fontSize);
    if (const EngineValue& v = d.get(QStringLiteral("FillColor")); v.isValid())
        s.color = colorFromFillColor(v);
    return s;
}

ParagraphStyle paragraphStyleFromProperties(const EngineValue& d)
{
    ParagraphStyle p;
    const qint64 j = d.get(QStringLiteral("Justification")).asInt(0);
    switch (j) {
    case 1: p.alignment = TextAlign::Right; break;
    case 2: p.alignment = TextAlign::Center; break;
    case 3: case 4: case 5: case 6: p.alignment = TextAlign::Justify; break;
    default: p.alignment = TextAlign::Left; break;
    }
    if (const EngineValue& v = d.get(QStringLiteral("SpaceBefore")); v.isValid())
        p.spaceBefore = static_cast<float>(v.asReal(0.0));
    if (const EngineValue& v = d.get(QStringLiteral("SpaceAfter")); v.isValid())
        p.spaceAfter = static_cast<float>(v.asReal(0.0));
    return p;
}

// Applies run lengths (UTF-16 code units == QString indices) over `runs`,
// calling `make(sheetValue)` for each run's style/paragraph payload.
template <typename T, typename MakeFn>
std::vector<std::pair<int, T>> expandRuns(const EngineValue& runArray, const EngineValue& lengths, const char* sheetKey, MakeFn make)
{
    std::vector<std::pair<int, T>> out;
    if (runArray.kind() != EngineValue::Kind::Array || lengths.kind() != EngineValue::Kind::Array)
        return out;
    const auto& runs = runArray.asArray();
    const auto& lens = lengths.asArray();
    const size_t n = std::min(runs.size(), lens.size());
    for (size_t i = 0; i < n; ++i) {
        const EngineValue& sheet = runs[i].get(QString::fromLatin1(sheetKey));
        const int len = static_cast<int>(std::max<qint64>(0, lens[i].asInt(0)));
        out.emplace_back(len, make(sheet));
    }
    return out;
}

} // namespace

bool tryBuildText(const psd::LayerRecord& rec, Context& ctx, const QString& layerName, LayerTreeNode& node)
{
    const psd::TaggedBlock* block = rec.block(psdKey("TySh"));
    if (!block || block->data.size() < 52)
        return false;
    const auto& d = block->data;
    auto beF64 = [&](size_t at) -> double {
        uint64_t bits = 0;
        for (int i = 0; i < 8; ++i)
            bits = (bits << 8) | d[at + static_cast<size_t>(i)];
        double v;
        std::memcpy(&v, &bits, 8);
        return v;
    };
    double matrix[6];
    for (int i = 0; i < 6; ++i)
        matrix[i] = beF64(2 + static_cast<size_t>(i) * 8);

    const std::span<const uint8_t> rest(d.data() + 52, d.size() - 52);
    const auto parsed = psd::VersionedDescriptor::parsePrefix(rest);
    if (!parsed.hasValue())
        return false;
    const psd::Descriptor& textDesc = parsed.value().first.descriptor;
    const size_t textDescLen = parsed.value().second;

    bool warped = false;
    if (textDescLen + 2 <= rest.size()) {
        const std::span<const uint8_t> warpRest(rest.data() + textDescLen + 2, rest.size() - textDescLen - 2);
        if (const auto warpParsed = psd::VersionedDescriptor::parsePrefix(warpRest); warpParsed.hasValue()) {
            if (const psd::Value* style = warpParsed.value().first.descriptor.get("warpStyle");
                style && style->kind() == psd::Value::Kind::Enumerated && !style->asEnumerated().value.is("warpNone")) {
                warped = true;
            }
        }
    }

    const psd::Value* engineRaw = textDesc.get("EngineData");
    if (!engineRaw || engineRaw->kind() != psd::Value::Kind::RawData) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its text engine data could not be read; the layer was imported as pixels.").arg(layerName));
        return false;
    }
    const EngineValue engine = psdtext::parseEngineData(engineRaw->asBytes());
    if (!engine.isValid()) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its text engine data could not be parsed; the layer was imported as pixels.").arg(layerName));
        return false;
    }

    const EngineValue& engineDict = engine.get(QStringLiteral("EngineDict"));
    const QString editorText = engineDict.path({"Editor", "Text"}).asString();

    const EngineValue& resourceDict = engine.get(QStringLiteral("ResourceDict"));
    QStringList fonts;
    for (const auto& f : resourceDict.get(QStringLiteral("FontSet")).asArray())
        fonts.push_back(f.get(QStringLiteral("Name")).asString());

    auto td = std::make_shared<TextLayerData>();
    QString text = editorText;
    text.replace(QChar(u'\r'), QChar(u'\n'));
    if (text.endsWith(QChar(u'\n')))
        text.chop(1);
    td->text = text;

    // Character runs (StyleRun) -> TextSpan list.
    const EngineValue& styleRun = engineDict.get(QStringLiteral("StyleRun"));
    const auto spans = expandRuns<TextSpan>(styleRun.get(QStringLiteral("RunArray")), styleRun.get(QStringLiteral("RunLengthArray")),
                                            "StyleSheet", [&](const EngineValue& sheet) {
        return spanFromStyleSheetData(sheet.get(QStringLiteral("StyleSheetData")), fonts);
    });
    int multipleStyles = 0;
    {
        int pos = 0;
        const int textLen = static_cast<int>(text.size());
        for (const auto& [len, span] : spans) {
            TextSpan s = span;
            s.start = std::min(pos, textLen);
            s.end = std::min(pos + len, textLen);
            if (s.end > s.start)
                td->spans.push_back(s);
            pos += len;
        }
        multipleStyles = static_cast<int>(spans.size());
    }
    if (td->spans.empty()) {
        TextSpan s;
        s.start = 0;
        s.end = static_cast<int>(text.size());
        td->spans.push_back(s);
    }

    // Paragraph runs (ParagraphRun) -> ParagraphStyle list.
    const EngineValue& paraRun = engineDict.get(QStringLiteral("ParagraphRun"));
    const auto paras = expandRuns<ParagraphStyle>(paraRun.get(QStringLiteral("RunArray")), paraRun.get(QStringLiteral("RunLengthArray")),
                                                  "ParagraphSheet", [&](const EngineValue& sheet) {
        return paragraphStyleFromProperties(sheet.get(QStringLiteral("Properties")));
    });
    for (const auto& [len, style] : paras) {
        Q_UNUSED(len);
        td->paragraphs.push_back(style);
    }
    if (!td->paragraphs.empty()) {
        td->align = td->paragraphs.front().alignment;
    }
    normalizeParagraphs(*td);

    // Area (paragraph) text box, if the engine recorded one.
    const EngineValue& box = engineDict.path({"Rendered", "Shapes", "Children"});
    if (box.kind() == EngineValue::Kind::Array && !box.asArray().empty()) {
        const EngineValue& shape = box.asArray().front();
        if (shape.get(QStringLiteral("ShapeType")).asInt(-1) == 1) {
            const EngineValue& bb = shape.path({"Cookie", "Photoshop", "BoxBounds"});
            if (bb.kind() == EngineValue::Kind::Array && bb.asArray().size() == 4) {
                const auto& a = bb.asArray();
                const float w = static_cast<float>(a[2].asReal(0.0) - a[0].asReal(0.0));
                const float h = static_cast<float>(a[3].asReal(0.0) - a[1].asReal(0.0));
                if (w > 0.0f && h > 0.0f) {
                    td->box.width = w;
                    td->box.height = h;
                    td->flowMode = TextFlowMode::Paragraph;
                }
            }
        }
    }
    td->dirty = true;

    node.layer->textData = td;
    TextRenderer renderer;
    renderer.render(*td, node.layer->cpuImage);
    if (node.layer->cpuImage.isNull()) {
        node.layer->textData.reset();
        return false;
    }

    const QPointF anchor = rec.rect.isEmpty() ? QPointF(matrix[4], matrix[5]) : QPointF(rec.rect.left, rec.rect.top);
    const QRectF rectPx(anchor, node.layer->cpuImage.size());
    node.transform = ndcTransformForPixelRect(rectPx, ctx.docSize);

    if (multipleStyles > 1) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its text uses multiple character styles; only an approximation of each "
            "run's font, size and color was preserved.").arg(layerName));
    }
    if (warped) {
        ctx.warn(QCoreApplication::translate("PsdImportService",
            "Layer \"%1\": its text warp has no equivalent in Hazor and was not imported.").arg(layerName));
    }
    return true;
}

} // namespace psdimport
