#pragma once

#include <QString>
#include <QColor>
#include <QPointF>
#include <QRectF>
#include <QFont>
#include <vector>
#include <memory>

enum class TextToolState {
    Idle,
    Creating,
    Editing,
    Transforming
};

enum class TextAlign {
    Left,
    Center,
    Right,
    Justify
};

// ──────────────────────────────────────────────────────────────────────────
// Character Style (per-character / per-run formatting).
//
// A TextSpan is a "character run": a contiguous [start, end) range of the
// layer text that shares one CharacterStyle. A run is NOT tied to a word or a
// line — it can be a single letter, part of a word, a whole word, several
// words, or span multiple lines/paragraphs. Applying a style to a partial
// selection splits the affected runs (see TextEditorController::applyToRange).
//
// MVP fields are exposed in the UI; the remaining fields below are kept in the
// data model so future typography features (kerning, baseline shift, small
// caps, super/subscript, OpenType features) can be enabled without a data
// migration.
// ──────────────────────────────────────────────────────────────────────────
struct TextSpan {
    int start = 0;
    int end = 0;

    // MVP character style
    QString fontFamily = "Sans Serif";
    float fontSize = 32.0f;
    QColor color = Qt::black;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    bool strikethrough = false;
    float letterSpacing = 0.0f;   // tracking (absolute px)

    // Future-ready character style (architecture only — not yet in the UI)
    float kerning = 0.0f;          // additional pair kerning (px)
    float baselineShift = 0.0f;    // baseline offset (px, +up)
    bool smallCaps = false;
    bool superscript = false;
    bool subscript = false;
    QString openTypeFeatures;      // e.g. "liga,kern,smcp"

    // Compares only the styling attributes, ignoring start/end. Used to merge
    // adjacent runs and to detect "no-op" style applications.
    bool sameStyle(const TextSpan& o) const {
        return fontFamily == o.fontFamily
            && qFuzzyCompare(fontSize, o.fontSize)
            && color == o.color
            && bold == o.bold
            && italic == o.italic
            && underline == o.underline
            && strikethrough == o.strikethrough
            && qFuzzyCompare(letterSpacing + 1.0f, o.letterSpacing + 1.0f)
            && qFuzzyCompare(kerning + 1.0f, o.kerning + 1.0f)
            && qFuzzyCompare(baselineShift + 1.0f, o.baselineShift + 1.0f)
            && smallCaps == o.smallCaps
            && superscript == o.superscript
            && subscript == o.subscript
            && openTypeFeatures == o.openTypeFeatures;
    }

    bool operator==(const TextSpan& o) const {
        return start == o.start && end == o.end && sameStyle(o);
    }
    bool operator!=(const TextSpan& o) const { return !(*this == o); }
};

// ──────────────────────────────────────────────────────────────────────────
// Paragraph Style (per-paragraph formatting).
//
// Paragraphs are delimited by hard line breaks ('\n'). There is conceptually
// one ParagraphStyle per paragraph (TextLayerData::paragraphs). Alignment and
// spacing live here — separated from character styling — so that a single text
// layer can mix left/centered/justified paragraphs with independent leading
// and paragraph spacing, matching the standard professional text layout model.
// ──────────────────────────────────────────────────────────────────────────
struct ParagraphStyle {
    // MVP paragraph style
    TextAlign alignment = TextAlign::Left;
    float lineHeight = 1.2f;       // leading multiplier (× natural line height)
    float spaceBefore = 0.0f;      // px above the paragraph
    float spaceAfter = 0.0f;       // px below the paragraph

    // Future-ready paragraph style (architecture only — not yet in the UI)
    float firstLineIndent = 0.0f;  // px
    float leftIndent = 0.0f;       // px
    float rightIndent = 0.0f;      // px
    bool hyphenation = false;

    bool operator==(const ParagraphStyle& o) const {
        return alignment == o.alignment
            && qFuzzyCompare(lineHeight, o.lineHeight)
            && qFuzzyCompare(spaceBefore + 1.0f, o.spaceBefore + 1.0f)
            && qFuzzyCompare(spaceAfter + 1.0f, o.spaceAfter + 1.0f)
            && qFuzzyCompare(firstLineIndent + 1.0f, o.firstLineIndent + 1.0f)
            && qFuzzyCompare(leftIndent + 1.0f, o.leftIndent + 1.0f)
            && qFuzzyCompare(rightIndent + 1.0f, o.rightIndent + 1.0f)
            && hyphenation == o.hyphenation;
    }
    bool operator!=(const ParagraphStyle& o) const { return !(*this == o); }
};

struct TextBox {
    float width = 0.0f;
    float height = 0.0f;
};

enum class TextFlowMode {
    Point,        // grows horizontally from a click; no fixed width
    Paragraph,    // area text: fixed-width box, automatic word wrap
    SingleLine    // single line; only manual breaks, no wrap
};

struct TextLine {
    QRectF bounds;
    int startChar = 0;
    int endChar = 0;
    int paragraph = 0;
};

struct TextLayout {
    std::vector<TextLine> lines;
    QSizeF contentSize;
    bool valid = false;
};

// ──────────────────────────────────────────────────────────────────────────
// Text Layer data model.
//
// Conceptual separation (per the professional architecture):
//   • text       — the textual content
//   • spans      — per-character formatting (character runs / CharacterStyle)
//   • paragraphs — per-paragraph formatting (ParagraphStyle)
//
// `align` / `lineSpacing` are retained as the layer-wide DEFAULT paragraph
// style: they seed new paragraphs and act as a fallback for legacy documents
// saved before per-paragraph styling existed (see paragraphStyleAt()).
// ──────────────────────────────────────────────────────────────────────────
struct TextLayerData {
    QString text;
    std::vector<TextSpan> spans;              // character runs
    std::vector<ParagraphStyle> paragraphs;   // one per '\n'-delimited paragraph
    TextBox box;
    TextAlign align = TextAlign::Left;        // default alignment (fallback/seed)
    float lineSpacing = 1.2f;                 // default leading (fallback/seed)
    TextFlowMode flowMode = TextFlowMode::Point;
    bool dirty = true;
};

// ── Paragraph helpers ─────────────────────────────────────────────────────

// Number of '\n'-delimited paragraphs in `text` (always ≥ 1).
inline int textParagraphCount(const QString& text)
{
    return static_cast<int>(text.count(QLatin1Char('\n'))) + 1;
}

// 0-based paragraph index containing character position `pos`.
inline int paragraphIndexForChar(const QString& text, int pos)
{
    if (pos < 0) return 0;
    int hi = qMin(pos, static_cast<int>(text.size()));
    int para = 0;
    for (int i = 0; i < hi; ++i)
        if (text[i] == QLatin1Char('\n')) ++para;
    return para;
}

// [start, end) character range of paragraph `index` (excluding the trailing
// '\n'). Returns an empty range past the end.
inline void paragraphCharRange(const QString& text, int index, int& start, int& end)
{
    start = 0;
    int para = 0;
    int i = 0;
    const int n = static_cast<int>(text.size());
    while (para < index && i < n) {
        if (text[i] == QLatin1Char('\n')) { ++para; start = i + 1; }
        ++i;
    }
    end = start;
    while (end < n && text[end] != QLatin1Char('\n')) ++end;
    if (para < index) { start = end = n; }
}

// Returns the effective ParagraphStyle for paragraph `index`. Falls back to a
// style derived from the layer-wide defaults when the paragraphs vector is not
// populated (legacy documents) or out of range.
inline ParagraphStyle paragraphStyleAt(const TextLayerData& d, int index)
{
    if (index >= 0 && index < static_cast<int>(d.paragraphs.size()))
        return d.paragraphs[index];
    ParagraphStyle ps;
    ps.alignment = d.align;
    ps.lineHeight = d.lineSpacing;
    return ps;
}

// Ensures `d.paragraphs` has exactly one entry per paragraph. New entries are
// seeded from the previous paragraph (or the layer defaults). Call after any
// edit that changes the number of '\n' in the text.
inline void normalizeParagraphs(TextLayerData& d)
{
    const int n = textParagraphCount(d.text);
    if (static_cast<int>(d.paragraphs.size()) == n) return;

    ParagraphStyle seed;
    seed.alignment = d.align;
    seed.lineHeight = d.lineSpacing;
    if (!d.paragraphs.empty())
        seed = d.paragraphs.back();

    if (static_cast<int>(d.paragraphs.size()) < n)
        d.paragraphs.resize(n, seed);
    else
        d.paragraphs.resize(n);
}
