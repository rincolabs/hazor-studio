#pragma once

#include "TextTypes.hpp"
#include <QImage>

class TextLayoutEngine;

class TextRenderer {
public:
    void render(TextLayerData& data, QImage& outImage);

    void setEditing(bool editing) { m_editing = editing; }
    void setCursorPos(int pos) { m_cursorPos = pos; }
    void setSelection(int start, int end) { m_selStart = start; m_selEnd = end; }
    void setCaretVisible(bool visible) { m_caretVisible = visible; }

private:
    bool m_editing = false;
    int m_cursorPos = 0;
    int m_selStart = -1;
    int m_selEnd = -1;
    bool m_caretVisible = false;
};
