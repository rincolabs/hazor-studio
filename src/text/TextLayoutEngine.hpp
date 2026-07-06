#pragma once

#include "TextTypes.hpp"
#include <QTextLayout>
#include <QFont>
#include <QSizeF>
#include <QPointF>
#include <QRectF>
#include <vector>

// Lays out a TextLayerData paragraph-by-paragraph. Each paragraph is rendered
// with its own alignment, leading (lineHeight) and paragraph spacing
// (spaceBefore / spaceAfter); character runs (TextSpan) supply per-range font,
// size, colour and style. Point/Single-line text aligns every line relative to
// the widest line in the layer; Area text aligns within the fixed box width and
// word-wraps.
class TextLayoutEngine {
public:
    // Padding (px) added around the text inside the layer image so glyph
    // overhangs and the caret are not clipped. Mirrored by TextRenderer.
    static constexpr qreal kPad = 6.0;

    void setData(const TextLayerData* data);
    void invalidate() const;

    const TextLayout& layout(bool editing = false) const;
    QSizeF contentSize(bool editing = false) const;

    int hitTest(QPointF pixelPos, bool editing = false) const;
    QRectF cursorRect(int charIndex) const;
    void drawText(QPainter& painter, const TextLayerData& data,
                  bool editing, int cursorPos,
                  int selStart, int selEnd, bool caretVisible) const;

    int wordStart(int index) const;
    int wordEnd(int index) const;

private:
    struct ParaInfo {
        int charStart = 0;     // absolute char index of paragraph start
        int charEnd = 0;       // absolute char index of paragraph end (excl '\n')
        float top = 0.0f;      // y (px) of the paragraph's first line top
        int firstLine = 0;     // index into TextLayout::lines
        int lineCount = 0;
    };

    void doLayout(bool editing = false) const;

    // Configures and lays out a single paragraph's QTextLayout using the shared
    // layout width. Line positions are relative to the paragraph top (y starts
    // at 0). Returns the paragraph's total content height.
    float buildParagraph(QTextLayout& layout, int paraIndex,
                         float layoutWidth, bool editing) const;

    float computeLayoutWidth(bool editing) const;

    const TextLayerData* m_data = nullptr;
    mutable TextLayout m_layout;
    mutable std::vector<ParaInfo> m_paras;
    mutable float m_layoutWidth = 0.0f;
    mutable bool m_dirty = true;
};
