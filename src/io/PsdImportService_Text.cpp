// Text layer (`TySh` + EngineData) -> Hazor's TextLayerData.
//
// `TySh` layout and the EngineData key names/semantics follow the documented
// PSD/PSB text-engine format.
//
// ─── The two coordinate spaces, because everything here depends on them ───
// EngineData holds the text in its own "text space": font sizes, leading,
// tracking and the `bounds` box are all in text-space units, and `TySh`'s
// 6-double affine maps that space into document pixels. A non-identity scale
// is written there for ANY type layer that was ever free-transformed,
// which in practice is most of them — reading the sizes without it renders
// every such layer at the wrong size (a 0.42 scale is a 2.4x error).
//
// The scale is baked into the text metrics rather than left on the node
// transform, because that is what Hazor itself does when a text layer is
// scaled (ImageController's resize path multiplies fontSize / letterSpacing /
// kerning / baselineShift / box / paragraph spacing and re-renders, keeping
// lineHeight as a multiplier). The leftover rotation/skew/non-uniform part —
// the only thing text metrics cannot absorb — stays on the node transform.

#include "PsdImportService_Internal.hpp"
#include "PsdEngineData.hpp"

#include "core/LayerTreeNode.hpp"
#include "text/TextLayoutEngine.hpp"
#include "text/TextRenderer.hpp"
#include "text/TextTypes.hpp"

#include <QCoreApplication>
#include <QFontDatabase>
#include <QImage>
#include <QPointF>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <span>
#include <utility>
#include <vector>

namespace psdimport {

namespace {

using psdtext::EngineValue;

// ─────────────────────────────────────────────────────────────────────────
// Sparse-dictionary lookup
// ─────────────────────────────────────────────────────────────────────────

// A style/paragraph sheet resolved against its fallbacks: the run's own
// dictionary first, then the run array's DefaultRunData, then the document's
// "normal" sheet from the ResourceDict. A complete sheet per run is the norm,
// but a sparse one is legal — and taking Hazor's struct
// defaults for the missing keys would silently invent a style the file never
// asked for.
class SheetLookup {
public:
    void push(const EngineValue& d)
    {
        if (d.kind() == EngineValue::Kind::Dict)
            m_dicts.push_back(&d);
    }

    const EngineValue& get(const char* name) const
    {
        static const EngineValue kInvalid;
        const QString key = QString::fromLatin1(name);
        for (const EngineValue* d : m_dicts) {
            const EngineValue& v = d->get(key);
            if (v.isValid())
                return v;
        }
        return kInvalid;
    }

    double real(const char* name, double fallback) const { return get(name).asReal(fallback); }
    qint64 integer(const char* name, qint64 fallback) const { return get(name).asInt(fallback); }
    bool boolean(const char* name, bool fallback) const { return get(name).asBool(fallback); }

private:
    std::vector<const EngineValue*> m_dicts;
};

// ─────────────────────────────────────────────────────────────────────────
// Fonts
// ─────────────────────────────────────────────────────────────────────────

// PostScript style suffixes, longest first so "-BoldItalic" is matched before
// "-Bold". The `bold`/`italic` columns say what the suffix itself encodes:
// a name's style has to come from its style token, not from a substring search
// over the whole name ("BoldonseSquare" is not a bold font).
struct FontSuffix { const char* text; bool bold; bool italic; };
constexpr FontSuffix kFontSuffixes[] = {
    {"-BoldItalic", true, true},   {"-BoldOblique", true, true},
    {"-SemiboldItalic", true, true}, {"-BlackItalic", true, true},
    {"-Bold", true, false},        {"-Semibold", true, false},
    {"-Black", true, false},       {"-Heavy", true, false},
    {"-Italic", false, true},      {"-Oblique", false, true},
    {"-Regular", false, false},    {"-Roman", false, false},
    {"-Book", false, false},       {"-Medium", false, false},
    {"-Light", false, false},      {"-Thin", false, false},
    {"BoldItalic", true, true},    {"Bold", true, false},
    {"Italic", false, true},       {"Oblique", false, true},
};

// "MyriadPro" -> "Myriad Pro": the family name the file stores is the
// PostScript one (no spaces), while the family Qt knows is the human one.
QString splitCamelCase(const QString& s)
{
    QString out;
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s[i];
        const bool boundary = i > 0 && c.isUpper()
            && (!s[i - 1].isUpper() || (i + 1 < s.size() && s[i + 1].isLower()));
        if (boundary && !out.endsWith(QLatin1Char(' ')))
            out.append(QLatin1Char(' '));
        out.append(c);
    }
    return out.simplified();
}

// Case-insensitive on purpose: the PostScript names in the file and the
// family names fontconfig reports disagree on case often enough ("DejaVuSans" vs
// "DejaVu Sans") that an exact test would reject families that are present.
bool familyInstalled(const QStringList& installed, const QString& family)
{
    if (family.isEmpty())
        return false;
    for (const QString& f : installed) {
        if (f.compare(family, Qt::CaseInsensitive) == 0)
            return true;
    }
    return false;
}

// The resolved family for one FontSet entry, plus whether the PostScript name
// encoded bold/italic and whether an installed family was actually found.
struct ResolvedFont {
    QString family;
    bool bold = false;
    bool italic = false;
    bool installed = false;
    QString postScriptName;
};

ResolvedFont resolveFont(const QString& psName, const QStringList& installed)
{
    ResolvedFont out;
    out.postScriptName = psName;
    if (psName.isEmpty()) {
        out.family = QStringLiteral("Sans Serif");
        out.installed = true;
        return out;
    }

    QString stem = psName;
    for (const FontSuffix& suffix : kFontSuffixes) {
        const QString text = QString::fromLatin1(suffix.text);
        if (stem.size() > text.size() && stem.endsWith(text, Qt::CaseInsensitive)) {
            stem.chop(text.size());
            out.bold = suffix.bold;
            out.italic = suffix.italic;
            break;
        }
    }
    // Trailing foundry/format tags PostScript names carry but that never
    // appear in the family ("TimesNewRomanPSMT" -> "TimesNewRoman").
    for (const char* tag : {"PSMT", "MT", "PS"}) {
        const QString text = QString::fromLatin1(tag);
        if (stem.size() > text.size() && stem.endsWith(text)) {
            stem.chop(text.size());
            break;
        }
    }
    while (stem.endsWith(QLatin1Char('-')))
        stem.chop(1);

    // Most specific candidate first: a family that really is called
    // "MyriadPro" must win over the camel-split guess.
    const QString candidates[] = {psName, stem, splitCamelCase(stem), splitCamelCase(psName)};
    for (const QString& candidate : candidates) {
        if (familyInstalled(installed, candidate)) {
            out.family = candidate;
            out.installed = true;
            return out;
        }
    }
    // Nothing installed: hand Qt the most human-readable spelling, which is
    // what its own substitution table is keyed on.
    out.family = splitCamelCase(stem);
    if (out.family.isEmpty())
        out.family = psName;
    return out;
}

// ─────────────────────────────────────────────────────────────────────────
// Colours
// ─────────────────────────────────────────────────────────────────────────

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

// ─────────────────────────────────────────────────────────────────────────
// Per-layer conversion state
// ─────────────────────────────────────────────────────────────────────────

// Everything about one text layer that cannot be reproduced, collected while
// the runs are walked so the compatibility report gets one line per feature
// instead of one per character run.
struct TextGaps {
    bool allCaps = false;
    bool superSubscript = false;
    bool baselineShift = false;
    bool kerning = false;
    bool glyphScaling = false;
    bool stroked = false;
    bool noFill = false;
    bool indents = false;
    bool hyphenation = false;
    bool mixedLeading = false;
    bool lastLineJustify = false;
    bool vertical = false;
    bool warped = false;
    QStringList missingFonts;
};

// A character run's style, plus the leading it asks for (text-space units,
// before the TySh scale) — leading is per-run in PSD but per-paragraph in
// Hazor, so it travels alongside the span instead of inside it.
struct RunStyle {
    TextSpan span;
    double leadingTextUnits = 0.0;
    bool autoLeading = true;
    double fontSizeTextUnits = 12.0;
};

RunStyle runStyleFromSheet(const SheetLookup& sheet, const std::vector<ResolvedFont>& fonts,
                           double scale, TextGaps& gaps)
{
    RunStyle out;
    TextSpan& s = out.span;

    const qint64 fontIndex = sheet.integer("Font", -1);
    if (fontIndex >= 0 && fontIndex < static_cast<qint64>(fonts.size())) {
        const ResolvedFont& font = fonts[static_cast<size_t>(fontIndex)];
        s.fontFamily = font.family;
        s.bold = font.bold;
        s.italic = font.italic;
        if (!font.installed && !gaps.missingFonts.contains(font.postScriptName))
            gaps.missingFonts.push_back(font.postScriptName);
    }

    out.fontSizeTextUnits = std::max(0.01, sheet.real("FontSize", 12.0));
    s.fontSize = static_cast<float>(std::max(1.0, out.fontSizeTextUnits * scale));

    s.bold = s.bold || sheet.boolean("FauxBold", false);
    s.italic = s.italic || sheet.boolean("FauxItalic", false);
    s.underline = sheet.boolean("Underline", false);
    s.strikethrough = sheet.boolean("Strikethrough", false);

    // Tracking and kerning are both 1/1000 em, so they scale with the already
    // scaled font size rather than with `scale` a second time.
    s.letterSpacing = static_cast<float>(sheet.real("Tracking", 0.0) / 1000.0 * s.fontSize);
    const double kerning = sheet.real("Kerning", 0.0);
    s.kerning = static_cast<float>(kerning / 1000.0 * s.fontSize);
    if (std::abs(kerning) > 0.5)
        gaps.kerning = true;

    const double baselineShift = sheet.real("BaselineShift", 0.0);
    s.baselineShift = static_cast<float>(baselineShift * scale);
    if (std::abs(baselineShift) > 0.01)
        gaps.baselineShift = true;

    switch (sheet.integer("FontCaps", 0)) {
    case 1: s.smallCaps = true; break;          // rendered: QFont::SmallCaps
    case 2: gaps.allCaps = true; break;         // no Hazor equivalent
    default: break;
    }
    switch (sheet.integer("FontBaseline", 0)) {
    case 1: s.superscript = true; gaps.superSubscript = true; break;
    case 2: s.subscript = true; gaps.superSubscript = true; break;
    default: break;
    }

    if (std::abs(sheet.real("HorizontalScale", 1.0) - 1.0) > 0.001
        || std::abs(sheet.real("VerticalScale", 1.0) - 1.0) > 0.001) {
        gaps.glyphScaling = true;
    }
    if (sheet.boolean("StrokeFlag", false))
        gaps.stroked = true;
    if (!sheet.boolean("FillFlag", true))
        gaps.noFill = true;
    // The format's default is ligatures on, which is also Qt's; only an
    // explicit opt-out is worth recording in the model.
    if (!sheet.boolean("Ligatures", true))
        s.openTypeFeatures = QStringLiteral("-liga");

    if (const EngineValue& fill = sheet.get("FillColor"); fill.isValid())
        s.color = colorFromFillColor(fill);

    out.autoLeading = sheet.boolean("AutoLeading", true);
    out.leadingTextUnits = sheet.real("Leading", 0.0);
    return out;
}

// A paragraph's style, plus the auto-leading factor (the "Auto Leading"
// percentage) that the character runs' AutoLeading flag refers to.
struct ParaStyle {
    ParagraphStyle style;
    double autoLeadingFactor = 1.2;
};

ParaStyle paraStyleFromSheet(const SheetLookup& sheet, double scale, TextGaps& gaps)
{
    ParaStyle out;
    ParagraphStyle& p = out.style;

    switch (sheet.integer("Justification", 0)) {
    case 1: p.alignment = TextAlign::Right; break;
    case 2: p.alignment = TextAlign::Center; break;
    case 3: p.alignment = TextAlign::Justify; break;  // justify, last line left == Qt
    case 4: case 5: case 6:
        p.alignment = TextAlign::Justify;
        gaps.lastLineJustify = true;                  // last-line variants Qt has no mode for
        break;
    default: p.alignment = TextAlign::Left; break;
    }

    p.spaceBefore = static_cast<float>(sheet.real("SpaceBefore", 0.0) * scale);
    p.spaceAfter = static_cast<float>(sheet.real("SpaceAfter", 0.0) * scale);
    p.firstLineIndent = static_cast<float>(sheet.real("FirstLineIndent", 0.0) * scale);
    p.leftIndent = static_cast<float>(sheet.real("StartIndent", 0.0) * scale);
    p.rightIndent = static_cast<float>(sheet.real("EndIndent", 0.0) * scale);
    if (std::abs(p.firstLineIndent) > 0.01 || std::abs(p.leftIndent) > 0.01
        || std::abs(p.rightIndent) > 0.01) {
        gaps.indents = true;
    }
    p.hyphenation = sheet.boolean("AutoHyphenate", false);
    if (p.hyphenation)
        gaps.hyphenation = true;

    out.autoLeadingFactor = sheet.real("AutoLeading", 1.2);
    if (out.autoLeadingFactor <= 0.0)
        out.autoLeadingFactor = 1.2;
    return out;
}

// ─────────────────────────────────────────────────────────────────────────
// TySh placement
// ─────────────────────────────────────────────────────────────────────────

// The `TySh` affine, split into the uniform scale that gets baked into the
// text metrics and the residual map from the (already scaled) rendered raster
// into document pixels. For the overwhelmingly common pure-scale placement
// the residual is a plain translation.
struct Placement {
    double scale = 1.0;
    QTransform residual;   // scaled-text-space pixels -> document pixels
};

Placement decomposeTySh(const double m[6])
{
    Placement out;
    const QTransform textToDoc(m[0], m[1], m[2], m[3], m[4], m[5]);
    // sqrt(|det|) is the geometric mean of the two axis scales: it is the one
    // scalar a uniform font-size multiplication can absorb, and it is exactly
    // the axis scale whenever the placement is uniform (which it is unless
    // the layer was stretched non-proportionally).
    const double det = std::abs(textToDoc.determinant());
    out.scale = det > 1e-12 ? std::sqrt(det) : 1.0;
    out.residual = QTransform::fromScale(1.0 / out.scale, 1.0 / out.scale) * textToDoc;
    return out;
}

bool isPureTranslation(const QTransform& t)
{
    return std::abs(t.m11() - 1.0) < 1e-6 && std::abs(t.m22() - 1.0) < 1e-6
        && std::abs(t.m12()) < 1e-6 && std::abs(t.m21()) < 1e-6
        && std::abs(t.m13()) < 1e-12 && std::abs(t.m23()) < 1e-12;
}

// The text-space layout box from the text descriptor's `bounds` (the line
// boxes) — the anchor that ties the file's own layout to Hazor's, with no
// font metric involved on either side. `boundingBox` (the ink box) is the
// fallback; it is tighter, so it anchors a little lower.
struct TextBounds {
    double left = 0.0;
    double top = 0.0;
    double right = 0.0;
    bool valid = false;
};

TextBounds readTextBounds(const psd::Descriptor& textDesc)
{
    auto num = [](const psd::Descriptor& d, const char* k) -> std::optional<double> {
        const psd::Value* v = d.get(k);
        if (!v) return std::nullopt;
        switch (v->kind()) {
        case psd::Value::Kind::Double: return v->asDouble();
        case psd::Value::Kind::UnitFloat: return v->asUnitFloat().value;
        case psd::Value::Kind::Integer: return static_cast<double>(v->asInteger());
        default: return std::nullopt;
        }
    };
    for (const char* key : {"bounds", "boundingBox"}) {
        const psd::Value* v = textDesc.get(key);
        if (!v || v->kind() != psd::Value::Kind::DescriptorKind)
            continue;
        const psd::Descriptor& d = v->asDescriptor();
        const auto left = num(d, "Left");
        const auto top = num(d, "Top ");
        const auto right = num(d, "Rght");
        if (!left || !top || !right)
            continue;
        TextBounds out;
        out.left = *left;
        out.top = *top;
        out.right = *right;
        out.valid = true;
        return out;
    }
    return {};
}

// ─────────────────────────────────────────────────────────────────────────
// Leading
// ─────────────────────────────────────────────────────────────────────────

// Hazor's ParagraphStyle::lineHeight is a multiplier over the font's own
// natural line height, while PSD stores leading as an absolute baseline-to-
// baseline distance. Converting one into the other needs the natural height
// Hazor's layout engine would use, so it is measured through that very
// engine: lay the text out with every multiplier pinned to 1.0, and each
// line's reported height IS its natural height.
//
// `desiredPx[p] <= 0` means "this paragraph has no usable leading"; the
// multiplier is then left at 1.0, which makes the line box exactly as tall as
// the font needs — the same box the file's own `bounds` reports for a
// single-line paragraph.
void applyAbsoluteLeading(TextLayerData& td, const std::vector<double>& desiredPx)
{
    for (ParagraphStyle& p : td.paragraphs)
        p.lineHeight = 1.0f;

    TextLayoutEngine engine;
    engine.setData(&td);
    const TextLayout& layout = engine.layout(false);

    const int paraCount = static_cast<int>(td.paragraphs.size());
    for (int p = 0; p < paraCount; ++p) {
        if (p >= static_cast<int>(desiredPx.size()) || desiredPx[static_cast<size_t>(p)] <= 0.0)
            continue;
        double natural = 0.0;
        for (const TextLine& line : layout.lines) {
            if (line.paragraph == p) {
                natural = line.bounds.height(); // measured at lineHeight 1.0
                break;
            }
        }
        if (natural <= 0.0)
            continue;
        td.paragraphs[static_cast<size_t>(p)].lineHeight =
            static_cast<float>(std::clamp(desiredPx[static_cast<size_t>(p)] / natural, 0.01, 100.0));
    }
    if (!td.paragraphs.empty())
        td.lineSpacing = td.paragraphs.front().lineHeight;
    td.dirty = true;
}

// ─────────────────────────────────────────────────────────────────────────
// Reporting
// ─────────────────────────────────────────────────────────────────────────

void reportGaps(const TextGaps& gaps, Context& ctx, const QString& layerName)
{
    auto warn = [&](const QString& what) {
        ctx.warn(QCoreApplication::translate("PsdImportService", "Layer \"%1\": %2").arg(layerName, what));
    };
    if (!gaps.missingFonts.isEmpty()) {
        warn(QCoreApplication::translate("PsdImportService",
            "the font(s) %1 are not installed, so the text was rendered with a substitute; "
            "its line breaks and width differ from the original.")
            .arg(gaps.missingFonts.join(QStringLiteral(", "))));
    }
    if (gaps.vertical) {
        warn(QCoreApplication::translate("PsdImportService",
            "its text runs vertically, which Hazor does not support; it was imported as "
            "horizontal text."));
    }
    if (gaps.warped) {
        warn(QCoreApplication::translate("PsdImportService",
            "its text warp has no equivalent in Hazor and was not imported."));
    }
    if (gaps.allCaps) {
        warn(QCoreApplication::translate("PsdImportService",
            "part of its text uses All Caps, which Hazor does not support; that text was "
            "imported in its original case."));
    }
    if (gaps.superSubscript) {
        warn(QCoreApplication::translate("PsdImportService",
            "part of its text is superscript or subscript; Hazor stores that but does not "
            "render it, so that text appears at full size on the baseline."));
    }
    if (gaps.baselineShift) {
        warn(QCoreApplication::translate("PsdImportService",
            "part of its text has a baseline shift; Hazor stores it but does not render it, "
            "so that text sits on the normal baseline."));
    }
    if (gaps.kerning) {
        warn(QCoreApplication::translate("PsdImportService",
            "its text uses manual pair kerning; Hazor stores it but does not render it."));
    }
    if (gaps.glyphScaling) {
        warn(QCoreApplication::translate("PsdImportService",
            "its text uses horizontal/vertical glyph scaling, which Hazor does not support; "
            "the glyphs were rendered unscaled."));
    }
    if (gaps.stroked) {
        warn(QCoreApplication::translate("PsdImportService",
            "its text has a character stroke, which Hazor's text model does not have; only "
            "the fill was imported."));
    }
    if (gaps.noFill) {
        warn(QCoreApplication::translate("PsdImportService",
            "part of its text is set to no fill (outline only), which Hazor does not support; "
            "it was filled."));
    }
    if (gaps.indents) {
        warn(QCoreApplication::translate("PsdImportService",
            "its paragraphs use indents; Hazor stores them but does not lay them out, so the "
            "text starts flush with the box."));
    }
    if (gaps.hyphenation) {
        warn(QCoreApplication::translate("PsdImportService",
            "its paragraphs enable automatic hyphenation, which Hazor does not perform; line "
            "breaks differ from the original."));
    }
    if (gaps.mixedLeading) {
        warn(QCoreApplication::translate("PsdImportService",
            "a paragraph mixes several leading values; Hazor has one line height per "
            "paragraph, so the largest was used."));
    }
    if (gaps.lastLineJustify) {
        warn(QCoreApplication::translate("PsdImportService",
            "its justified paragraphs align the last line in a way Hazor does not support; "
            "the last line was left-aligned."));
    }
}

} // namespace

bool tryBuildText(const psd::LayerRecord& rec, Context& ctx, const QString& layerName, LayerTreeNode& node,
                  bool& maskApplied)
{
    maskApplied = false;

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

    TextGaps gaps;
    if (const psd::Value* orient = textDesc.get("Ornt");
        orient && orient->kind() == psd::Value::Kind::Enumerated
        && orient->asEnumerated().value.is("Vrtc")) {
        gaps.vertical = true;
    }
    if (textDescLen + 2 <= rest.size()) {
        const std::span<const uint8_t> warpRest(rest.data() + textDescLen + 2, rest.size() - textDescLen - 2);
        if (const auto warpParsed = psd::VersionedDescriptor::parsePrefix(warpRest); warpParsed.hasValue()) {
            if (const psd::Value* style = warpParsed.value().first.descriptor.get("warpStyle");
                style && style->kind() == psd::Value::Kind::Enumerated && !style->asEnumerated().value.is("warpNone")) {
                gaps.warped = true;
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
    const EngineValue& resourceDict = engine.get(QStringLiteral("ResourceDict"));

    // The document's "normal" sheets, the last fallback behind every run.
    const EngineValue& normalStyleSheet = [&]() -> const EngineValue& {
        const EngineValue& set = resourceDict.get(QStringLiteral("StyleSheetSet"));
        const qint64 index = resourceDict.get(QStringLiteral("TheNormalStyleSheet")).asInt(0);
        if (set.kind() == EngineValue::Kind::Array && index >= 0
            && index < static_cast<qint64>(set.asArray().size())) {
            return set.asArray()[static_cast<size_t>(index)].get(QStringLiteral("StyleSheetData"));
        }
        static const EngineValue kInvalid;
        return kInvalid;
    }();
    const EngineValue& normalParagraphSheet = [&]() -> const EngineValue& {
        const EngineValue& set = resourceDict.get(QStringLiteral("ParagraphSheetSet"));
        const qint64 index = resourceDict.get(QStringLiteral("TheNormalParagraphSheet")).asInt(0);
        if (set.kind() == EngineValue::Kind::Array && index >= 0
            && index < static_cast<qint64>(set.asArray().size())) {
            return set.asArray()[static_cast<size_t>(index)].get(QStringLiteral("Properties"));
        }
        static const EngineValue kInvalid;
        return kInvalid;
    }();

    // Queried once per layer: the list is long, and resolveFont() scans it for
    // up to four spellings of every font in the layer's FontSet.
    const QStringList installedFamilies = QFontDatabase::families();
    std::vector<ResolvedFont> fonts;
    for (const auto& f : resourceDict.get(QStringLiteral("FontSet")).asArray())
        fonts.push_back(resolveFont(f.get(QStringLiteral("Name")).asString(), installedFamilies));

    const Placement placement = decomposeTySh(matrix);

    auto td = std::make_shared<TextLayerData>();
    QString text = engineDict.path({"Editor", "Text"}).asString();
    text.replace(QChar(u'\r'), QChar(u'\n'));
    if (text.endsWith(QChar(u'\n')))
        text.chop(1);
    td->text = text;
    const int textLen = static_cast<int>(text.size());

    // ── Character runs (StyleRun) -> TextSpan list ───────────────────────
    const EngineValue& styleRun = engineDict.get(QStringLiteral("StyleRun"));
    const EngineValue& styleRunArray = styleRun.get(QStringLiteral("RunArray"));
    const EngineValue& styleRunLengths = styleRun.get(QStringLiteral("RunLengthArray"));
    const EngineValue& styleDefaultRun =
        styleRun.path({"DefaultRunData", "StyleSheet", "StyleSheetData"});

    // [start, end) of each run plus the leading it asks for, kept so the
    // paragraph pass can find the runs covering each paragraph.
    struct PlacedRun { int start; int end; RunStyle style; };
    std::vector<PlacedRun> runs;
    if (styleRunArray.kind() == EngineValue::Kind::Array
        && styleRunLengths.kind() == EngineValue::Kind::Array) {
        const auto& array = styleRunArray.asArray();
        const auto& lengths = styleRunLengths.asArray();
        const size_t count = std::min(array.size(), lengths.size());
        int pos = 0;
        for (size_t i = 0; i < count; ++i) {
            SheetLookup sheet;
            sheet.push(array[i].path({"StyleSheet", "StyleSheetData"}));
            sheet.push(styleDefaultRun);
            sheet.push(normalStyleSheet);
            const int len = static_cast<int>(std::max<qint64>(0, lengths[i].asInt(0)));
            PlacedRun run{std::min(pos, textLen), std::min(pos + len, textLen),
                          runStyleFromSheet(sheet, fonts, placement.scale, gaps)};
            pos += len;
            if (run.end > run.start)
                runs.push_back(std::move(run));
        }
    }
    if (runs.empty()) {
        // No StyleRun at all: still honour the document's normal style sheet
        // instead of falling back to Hazor's struct defaults.
        SheetLookup sheet;
        sheet.push(styleDefaultRun);
        sheet.push(normalStyleSheet);
        runs.push_back({0, textLen, runStyleFromSheet(sheet, fonts, placement.scale, gaps)});
    }
    for (const PlacedRun& run : runs) {
        TextSpan span = run.style.span;
        span.start = run.start;
        span.end = run.end;
        td->spans.push_back(span);
    }

    // ── Paragraph runs (ParagraphRun) -> ParagraphStyle list ─────────────
    const EngineValue& paraRun = engineDict.get(QStringLiteral("ParagraphRun"));
    const EngineValue& paraRunArray = paraRun.get(QStringLiteral("RunArray"));
    const EngineValue& paraDefaultRun =
        paraRun.path({"DefaultRunData", "ParagraphSheet", "Properties"});

    std::vector<ParaStyle> paraStyles;
    if (paraRunArray.kind() == EngineValue::Kind::Array) {
        for (const EngineValue& item : paraRunArray.asArray()) {
            SheetLookup sheet;
            sheet.push(item.path({"ParagraphSheet", "Properties"}));
            sheet.push(paraDefaultRun);
            sheet.push(normalParagraphSheet);
            paraStyles.push_back(paraStyleFromSheet(sheet, placement.scale, gaps));
        }
    }
    if (paraStyles.empty()) {
        SheetLookup sheet;
        sheet.push(paraDefaultRun);
        sheet.push(normalParagraphSheet);
        paraStyles.push_back(paraStyleFromSheet(sheet, placement.scale, gaps));
    }
    for (const ParaStyle& p : paraStyles)
        td->paragraphs.push_back(p.style);
    td->align = td->paragraphs.front().alignment;
    normalizeParagraphs(*td);
    // normalizeParagraphs may have padded/truncated the list against the real
    // '\n' count; keep the auto-leading factors in step so the leading pass
    // below indexes both by the same paragraph number. The fill value is a
    // copy, not a reference into the vector being resized.
    {
        const ParaStyle pad = paraStyles.back();
        paraStyles.resize(td->paragraphs.size(), pad);
    }

    // The absolute leading each paragraph asks for, in destination pixels.
    // PSD stores leading per character run and applies the largest one on a
    // line; Hazor has one line height per paragraph, so the largest leading
    // among the runs covering the paragraph is the faithful choice.
    std::vector<double> desiredLeading(td->paragraphs.size(), 0.0);
    for (size_t p = 0; p < td->paragraphs.size(); ++p) {
        int paraStart = 0, paraEnd = 0;
        paragraphCharRange(td->text, static_cast<int>(p), paraStart, paraEnd);
        double best = 0.0;
        for (const PlacedRun& run : runs) {
            // A run touching the paragraph, or the run that ends exactly where
            // an empty paragraph begins (so a blank line still gets a height).
            const bool overlaps = run.start < std::max(paraEnd, paraStart + 1) && run.end > paraStart;
            if (!overlaps)
                continue;
            double leading = run.style.autoLeading
                ? paraStyles[p].autoLeadingFactor * run.style.span.fontSize
                : run.style.leadingTextUnits * placement.scale;
            // A manual leading far below the font size is a value that was
            // never actually laid out: the Leading field keeps whatever was
            // last typed into it even on a single-line layer, where the
            // leading has no baseline to measure to (its own `bounds` for such
            // a layer is ascent+descent, not the leading). Honouring it would
            // collapse the line box to a few pixels and crop the glyphs, so it
            // is dropped in favour of the font's natural height.
            if (leading < 0.5 * run.style.span.fontSize)
                leading = 0.0;
            if (leading > 0.0 && best > 0.0 && std::abs(leading - best) > 0.5)
                gaps.mixedLeading = true;
            best = std::max(best, leading);
        }
        desiredLeading[p] = best;
    }

    // ── Area (paragraph) text box, if the engine recorded one ────────────
    const EngineValue& box = engineDict.path({"Rendered", "Shapes", "Children"});
    if (box.kind() == EngineValue::Kind::Array && !box.asArray().empty()) {
        const EngineValue& shape = box.asArray().front();
        if (shape.get(QStringLiteral("ShapeType")).asInt(-1) == 1) {
            const EngineValue& bb = shape.path({"Cookie", "Photoshop", "BoxBounds"});
            if (bb.kind() == EngineValue::Kind::Array && bb.asArray().size() == 4) {
                const auto& a = bb.asArray();
                const double w = (a[2].asReal(0.0) - a[0].asReal(0.0)) * placement.scale;
                const double h = (a[3].asReal(0.0) - a[1].asReal(0.0)) * placement.scale;
                if (w > 0.0 && h > 0.0) {
                    td->box.width = static_cast<float>(w);
                    td->box.height = static_cast<float>(h);
                    td->flowMode = TextFlowMode::Paragraph;
                }
            }
        }
    }

    applyAbsoluteLeading(*td, desiredLeading);

    node.layer->textData = td;
    TextRenderer renderer;
    renderer.render(*td, node.layer->cpuImage);
    if (node.layer->cpuImage.isNull()) {
        node.layer->textData.reset();
        return false;
    }

    // ── Placement ────────────────────────────────────────────────────────
    // Tie the top-left (or top-centre / top-right, per alignment) of Hazor's
    // layout box to the same corner of the file's, both taken from each
    // engine's own layout rather than from glyph metrics — that is what makes
    // this survive the font substitution that changes the rendered width.
    const QSize imageSize = node.layer->cpuImage.size();
    const double contentW = std::max(0.0, imageSize.width() - 2.0 * TextLayoutEngine::kPad);
    const TextBounds bounds = readTextBounds(textDesc);
    if (bounds.valid) {
        double anchorFraction = 0.0;
        double anchorTextX = bounds.left;
        if (td->align == TextAlign::Center) {
            anchorFraction = 0.5;
            anchorTextX = (bounds.left + bounds.right) / 2.0;
        } else if (td->align == TextAlign::Right) {
            anchorFraction = 1.0;
            anchorTextX = bounds.right;
        }
        const QPointF inScaledText(anchorTextX * placement.scale, bounds.top * placement.scale);
        const QPointF inImage(TextLayoutEngine::kPad + anchorFraction * contentW, TextLayoutEngine::kPad);
        const QTransform imageToDocPx =
            QTransform::fromTranslate(inScaledText.x() - inImage.x(), inScaledText.y() - inImage.y())
            * placement.residual;

        if (isPureTranslation(imageToDocPx)) {
            node.transform = ndcTransformForPixelRect(
                QRectF(QPointF(imageToDocPx.dx(), imageToDocPx.dy()), QSizeF(imageSize)), ctx.docSize);
        } else {
            // Rotated/skewed/non-uniformly scaled type: the placement cannot be
            // an axis-aligned rect, so the mask has to be resampled through the
            // same map instead of blitted into one.
            node.transform = ndcTransformForImageMap(imageToDocPx, imageSize, ctx.docSize);
            applyLayerMaskThroughTransform(rec, ctx, layerName, *node.layer, imageSize, imageToDocPx);
            maskApplied = true;
        }
    } else {
        // No `bounds` to anchor to: fall back to the record's own stored rect,
        // which is the ink box of the render stored in the file.
        const QPointF anchor = rec.rect.isEmpty() ? QPointF(matrix[4], matrix[5])
                                                   : QPointF(rec.rect.left, rec.rect.top);
        node.transform = ndcTransformForPixelRect(QRectF(anchor, QSizeF(imageSize)), ctx.docSize);
    }

    reportGaps(gaps, ctx, layerName);
    return true;
}

} // namespace psdimport
