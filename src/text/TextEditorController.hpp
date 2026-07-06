#pragma once

#include "TextTypes.hpp"
#include <QObject>
#include <QTimer>
#include <QClipboard>
#include <QMimeData>
#include <functional>
#include <algorithm>

class TextEditorController : public QObject {
    Q_OBJECT

public:
    explicit TextEditorController(QObject* parent = nullptr);

    void beginEdit(TextLayerData* data);
    void endEdit();
    bool isEditing() const { return m_editing; }
    TextLayerData* data() const { return m_data; }

    int cursorPos() const { return m_cursorPos; }
    void setCursorPos(int pos);

    bool hasSelection() const { return m_selStart >= 0 && m_selEnd >= 0 && m_selStart != m_selEnd; }
    int selStart() const { return m_selStart; }
    int selEnd() const { return m_selEnd; }
    int selLow() const { return std::min(m_selStart, m_selEnd); }
    int selHigh() const { return std::max(m_selStart, m_selEnd); }
    QString selectedText() const;
    void clearSelection();
    void selectAll();
    void setSelection(int start, int end);
    void deleteSelection();

    void insertText(const QString& text);
    void backspace();
    void deleteChar();
    void newLine();

    void moveLeft(bool shift = false, bool word = false);
    void moveRight(bool shift = false, bool word = false);
    void moveUp(bool shift = false);
    void moveDown(bool shift = false);
    void moveHome(bool shift = false);
    void moveEnd(bool shift = false);

    void copy();
    void cut();
    void paste();

    // ── Character styling ─────────────────────────────────────────────────
    // Applies `fn` to every run intersecting [lo, hi), splitting runs at the
    // boundaries so only the affected range changes (other attributes of each
    // run are preserved). Re-merges adjacent runs with identical style.
    void applyToRange(int lo, int hi, const std::function<void(TextSpan&)>& fn);

    // Selection-aware attribute change: applies `fn` to the current selection,
    // or — when there is no selection — records it as the pending style for the
    // next inserted characters (insertion-point formatting).
    void modifyCharStyle(const std::function<void(TextSpan&)>& fn);

    // The character style at the current caret / selection start (or the
    // pending style if one is staged). Used to populate the options bar.
    TextSpan currentStyle() const;

    bool hasPendingStyle() const { return m_hasPending; }

    // ── Paragraph styling ─────────────────────────────────────────────────
    // Applies `fn` to every paragraph touched by the selection (or the
    // paragraph at the caret when there is no selection).
    void applyToParagraphs(const std::function<void(ParagraphStyle&)>& fn);

    bool caretVisible() const { return m_caretVisible; }
    void setCaretVisible(bool v) { m_caretVisible = v; }
    QTimer* caretTimer() { return m_caretTimer; }

    bool isDirty() const { return m_dirty; }
    void clearDirty() { m_dirty = false; }

signals:
    void textChanged();
    void caretChanged();
    void editFinished();

private:
    void clampCursor();
    void mergeSpans();
    void blinkCaret();
    void clearPending();

    TextLayerData* m_data = nullptr;
    int m_cursorPos = 0;
    int m_selStart = -1;
    int m_selEnd = -1;
    bool m_editing = false;
    bool m_dirty = false;
    bool m_caretVisible = true;
    bool m_hasPending = false;
    TextSpan m_pendingStyle;
    QTimer* m_caretTimer = nullptr;
};
