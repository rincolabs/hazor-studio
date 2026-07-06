#include "TextLayoutEngine.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"
#include <QTextLine>
#include <QPainter>
#include <QTextLayout>
#include <algorithm>
#include <cmath>

static QFont buildFont(const TextSpan& span)
{
    QFont font(span.fontFamily);
    font.setPixelSize(std::max(1, static_cast<int>(std::round(span.fontSize))));
    font.setBold(span.bold);
    font.setItalic(span.italic);
    font.setUnderline(span.underline);
    font.setStrikeOut(span.strikethrough);
    font.setLetterSpacing(QFont::AbsoluteSpacing, span.letterSpacing);
    font.setCapitalization(span.smallCaps ? QFont::SmallCaps : QFont::MixedCase);
    return font;
}

// Style of the run covering (or ending at) `pos`. Used to seed the default font
// for empty paragraphs / gaps and to derive insertion-point inheritance.
static TextSpan spanStyleAt(const TextLayerData& data, int pos)
{
    for (const auto& s : data.spans) {
        if (pos >= s.start && pos < s.end)
            return s;
    }
    // Prefer the run ending exactly at pos (cursor sitting at a run boundary).
    for (const auto& s : data.spans) {
        if (s.end == pos)
            return s;
    }
    if (!data.spans.empty())
        return data.spans.back();
    return TextSpan{};
}

// Appends QTextLayout formats for the runs intersecting [charStart, charEnd),
// remapped to paragraph-local indices.
static void appendParagraphFormats(QList<QTextLayout::FormatRange>& formats,
                                   const TextLayerData& data,
                                   int charStart, int charEnd)
{
    for (const auto& span : data.spans) {
        const int s = std::max(span.start, charStart);
        const int e = std::min(span.end, charEnd);
        if (s >= e) continue;
        QTextLayout::FormatRange fr;
        fr.start = s - charStart;
        fr.length = e - s;
        fr.format.setFont(buildFont(span));
        fr.format.setForeground(span.color);
        formats.append(fr);
    }
}

void TextLayoutEngine::setData(const TextLayerData* data)
{
    m_data = data;
    m_dirty = true;
}

void TextLayoutEngine::invalidate() const
{
    m_dirty = true;
}

float TextLayoutEngine::buildParagraph(QTextLayout& layout, int paraIndex,
                                       float layoutWidth, bool editing) const
{
    if (!m_data) return 0.0f;

    int charStart = 0, charEnd = 0;
    paragraphCharRange(m_data->text, paraIndex, charStart, charEnd);

    QString paraText = m_data->text.mid(charStart, charEnd - charStart);
    const bool wholeLayerEmpty = m_data->text.isEmpty();
    if (paraText.isEmpty() && wholeLayerEmpty && editing)
        paraText = QStringLiteral(" ");

    const ParagraphStyle style = paragraphStyleAt(*m_data, paraIndex);

    layout.clearLayout();
    layout.setText(paraText);
    layout.setCacheEnabled(true);
    layout.setFont(buildFont(spanStyleAt(*m_data, charStart)));

    QTextOption opt;
    const bool area = m_data->flowMode == TextFlowMode::Paragraph
                      && m_data->box.width > 0.0f;
    opt.setWrapMode(area ? QTextOption::WordWrap : QTextOption::NoWrap);
    switch (style.alignment) {
    case TextAlign::Center:  opt.setAlignment(Qt::AlignHCenter); break;
    case TextAlign::Right:   opt.setAlignment(Qt::AlignRight); break;
    case TextAlign::Justify: opt.setAlignment(Qt::AlignJustify); break;
    default:                 opt.setAlignment(Qt::AlignLeft); break;
    }
    layout.setTextOption(opt);

    QList<QTextLayout::FormatRange> formats;
    appendParagraphFormats(formats, *m_data, charStart, charEnd);
    layout.setFormats(formats);

    const float lineMul = std::max(0.1f, style.lineHeight);
    const float width = std::max(1.0f, layoutWidth);

    layout.beginLayout();
    float y = 0.0f;
    while (true) {
        QTextLine line = layout.createLine();
        if (!line.isValid()) break;
        line.setLineWidth(width);
        line.setPosition(QPointF(0, y));
        y += line.height() * lineMul;
    }
    layout.endLayout();
    return y;
}

float TextLayoutEngine::computeLayoutWidth(bool editing) const
{
    if (!m_data) return 1.0f;

    if (m_data->flowMode == TextFlowMode::Paragraph && m_data->box.width > 0.0f)
        return m_data->box.width;

    const int count = textParagraphCount(m_data->text);
    float maxWidth = 0.0f;
    for (int p = 0; p < count; ++p) {
        int cs = 0, ce = 0;
        paragraphCharRange(m_data->text, p, cs, ce);
        QString paraText = m_data->text.mid(cs, ce - cs);
        if (paraText.isEmpty() && m_data->text.isEmpty() && editing)
            paraText = QStringLiteral(" ");
        if (paraText.isEmpty()) continue;

        QTextLayout tl;
        tl.setText(paraText);
        tl.setFont(buildFont(spanStyleAt(*m_data, cs)));
        QList<QTextLayout::FormatRange> formats;
        appendParagraphFormats(formats, *m_data, cs, ce);
        tl.setFormats(formats);
        QTextOption opt;
        opt.setWrapMode(QTextOption::NoWrap);
        tl.setTextOption(opt);
        tl.beginLayout();
        QTextLine line = tl.createLine();
        if (line.isValid()) {
            line.setLineWidth(1.0e7f);
            maxWidth = std::max(maxWidth, static_cast<float>(line.naturalTextWidth()));
        }
        tl.endLayout();
    }
    return std::max(1.0f, maxWidth);
}

void TextLayoutEngine::doLayout(bool editing) const
{
    m_layout = TextLayout{};
    m_paras.clear();
    m_layoutWidth = 0.0f;

    if (!m_data) {
        m_layout.valid = true;
        m_layout.contentSize = QSizeF(0, 0);
        m_dirty = false;
        return;
    }

    m_layoutWidth = computeLayoutWidth(editing);

    const bool area = m_data->flowMode == TextFlowMode::Paragraph
                      && m_data->box.width > 0.0f;
    const int count = textParagraphCount(m_data->text);

    float y = 0.0f;
    float maxLineWidth = 0.0f;
    for (int p = 0; p < count; ++p) {
        const ParagraphStyle style = paragraphStyleAt(*m_data, p);
        int cs = 0, ce = 0;
        paragraphCharRange(m_data->text, p, cs, ce);

        y += style.spaceBefore;
        const float paraTop = y;

        QTextLayout tl;
        const float paraHeight = buildParagraph(tl, p, m_layoutWidth, editing);

        ParaInfo info;
        info.charStart = cs;
        info.charEnd = ce;
        info.top = paraTop;
        info.firstLine = static_cast<int>(m_layout.lines.size());
        info.lineCount = tl.lineCount();

        const float lineMul = std::max(0.1f, style.lineHeight);
        for (int i = 0; i < tl.lineCount(); ++i) {
            QTextLine line = tl.lineAt(i);
            QRectF ntr = line.naturalTextRect();
            const float lineH = line.height() * lineMul;
            TextLine rec;
            rec.paragraph = p;
            rec.startChar = cs + line.textStart();
            rec.endChar = cs + line.textStart() + line.textLength();
            rec.bounds = QRectF(kPad + ntr.x(),
                                kPad + paraTop + line.y(),
                                ntr.width(), lineH);
            maxLineWidth = std::max(maxLineWidth,
                                    static_cast<float>(ntr.x() + ntr.width()));
            m_layout.lines.push_back(rec);
        }

        m_paras.push_back(info);
        y = paraTop + paraHeight + style.spaceAfter;
    }

    float contentW = area ? m_data->box.width : std::max(m_layoutWidth, maxLineWidth);
    float contentH = y;
    if (area && m_data->box.height > 0.0f)
        contentH = std::max(contentH, m_data->box.height);

    m_layout.contentSize = QSizeF(contentW, contentH);
    m_layout.valid = true;
    m_dirty = false;
}

const TextLayout& TextLayoutEngine::layout(bool editing) const
{
    if (m_dirty) doLayout(editing);
    return m_layout;
}

QSizeF TextLayoutEngine::contentSize(bool editing) const
{
    if (m_dirty) doLayout(editing);
    return m_layout.contentSize;
}

int TextLayoutEngine::hitTest(QPointF pixelPos, bool editing) const
{
    if (!m_data) return 0;
    doLayout(editing);
    const int textLen = static_cast<int>(m_data->text.size());
    if (m_layout.lines.empty()) return 0;

    const qreal y = pixelPos.y();

    // Locate the visual line nearest the pointer vertically.
    int li = -1;
    for (int i = 0; i < static_cast<int>(m_layout.lines.size()); ++i) {
        const QRectF& b = m_layout.lines[i].bounds;
        if (y >= b.top() && y < b.bottom()) { li = i; break; }
    }
    if (li < 0) {
        if (y < m_layout.lines.front().bounds.top()) li = 0;
        else li = static_cast<int>(m_layout.lines.size()) - 1;
    }

    const TextLine& tlRec = m_layout.lines[li];
    const int p = tlRec.paragraph;
    if (p < 0 || p >= static_cast<int>(m_paras.size())) return 0;
    const ParaInfo& info = m_paras[p];

    QTextLayout tl;
    buildParagraph(tl, p, m_layoutWidth, editing);
    const int local = tlRec.startChar - info.charStart;
    QTextLine line = tl.lineForTextPosition(std::max(0, local));
    if (!line.isValid()) {
        if (tl.lineCount() > 0) line = tl.lineAt(tl.lineCount() - 1);
        else return std::clamp(tlRec.startChar, 0, textLen);
    }
    const qreal x = pixelPos.x() - kPad;
    int cursor = line.xToCursor(x);
    return std::clamp(info.charStart + cursor, 0, textLen);
}

QRectF TextLayoutEngine::cursorRect(int charIndex) const
{
    if (!m_data) return {};
    doLayout(true);
    charIndex = std::clamp(charIndex, 0, static_cast<int>(m_data->text.size()));

    const int p = paragraphIndexForChar(m_data->text, charIndex);
    if (p < 0 || p >= static_cast<int>(m_paras.size())) return {};
    const ParaInfo& info = m_paras[p];

    QTextLayout tl;
    buildParagraph(tl, p, m_layoutWidth, true);
    const int local = std::clamp(charIndex - info.charStart, 0, info.charEnd - info.charStart);

    QTextLine line = tl.lineForTextPosition(local);
    if (!line.isValid()) {
        if (tl.lineCount() == 0) return {};
        line = tl.lineAt(tl.lineCount() - 1);
    }

    const qreal x = kPad + line.cursorToX(local);
    const qreal yTop = kPad + info.top + line.y();
    const qreal h = line.height();
    return QRectF(x, yTop, 1.0, h);
}

void TextLayoutEngine::drawText(QPainter& painter, const TextLayerData& data,
                                 bool editing, int cursorPos,
                                 int selStart, int selEnd,
                                 bool caretVisible) const
{
    Q_UNUSED(cursorPos)
    Q_UNUSED(caretVisible)

    // Use `data` as the active source (drawText may be called on a freshly
    // configured engine).
    if (m_data != &data) {
        const_cast<TextLayoutEngine*>(this)->setData(&data);
    }
    doLayout(editing);
    if (m_layout.lines.empty() && data.text.isEmpty()) return;

    const QColor selColor = ThemeManager::instance()->current()->colorTextSelection;
    const bool hasSel = editing && selStart >= 0 && selEnd >= 0 && selStart != selEnd;
    const int selLo = hasSel ? std::min(selStart, selEnd) : 0;
    const int selHi = hasSel ? std::max(selStart, selEnd) : 0;

    painter.save();
    painter.translate(kPad, kPad);

    for (int p = 0; p < static_cast<int>(m_paras.size()); ++p) {
        const ParaInfo& info = m_paras[p];
        QTextLayout tl;
        buildParagraph(tl, p, m_layoutWidth, editing);

        QVector<QTextLayout::FormatRange> sel;
        if (hasSel) {
            const int lo = std::max(selLo, info.charStart) - info.charStart;
            const int hi = std::min(selHi, info.charEnd) - info.charStart;
            if (hi > lo) {
                QTextLayout::FormatRange fr;
                fr.start = lo;
                fr.length = hi - lo;
                fr.format.setBackground(selColor);
                fr.format.setForeground(Qt::white);
                sel.append(fr);
            }
        }
        tl.draw(&painter, QPointF(0, info.top), sel);
    }

    painter.restore();
}

int TextLayoutEngine::wordStart(int index) const
{
    if (!m_data) return 0;
    int i = std::clamp(index, 0, static_cast<int>(m_data->text.size()));
    while (i > 0 && !m_data->text[i - 1].isSpace()) --i;
    return i;
}

int TextLayoutEngine::wordEnd(int index) const
{
    if (!m_data) return 0;
    int len = static_cast<int>(m_data->text.size());
    int i = std::clamp(index, 0, len);
    while (i < len && !m_data->text[i].isSpace()) ++i;
    return i;
}
