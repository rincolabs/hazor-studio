#include "TextEditorController.hpp"
#include "TextLayoutEngine.hpp"
#include <QGuiApplication>
#include <algorithm>

// Copies only the styling attributes of `src` onto `dst`, leaving the
// run boundaries (start/end) untouched.
static void assignStyle(TextSpan& dst, const TextSpan& src)
{
    const int s = dst.start, e = dst.end;
    dst = src;
    dst.start = s;
    dst.end = e;
}

static TextSpan spanStyleAt(const TextLayerData& data, int pos)
{
    const int len = static_cast<int>(data.text.size());
    pos = std::clamp(pos, 0, len);
    for (const auto& s : data.spans) {
        if (pos >= s.start && pos < s.end)
            return s;
    }
    for (const auto& s : data.spans) {
        if (s.end == pos)
            return s;
    }
    if (!data.spans.empty())
        return data.spans.back();
    return {};
}

static void appendMergedSpan(std::vector<TextSpan>& spans, TextSpan span)
{
    if (span.start >= span.end)
        return;
    if (!spans.empty() && spans.back().end == span.start && spans.back().sameStyle(span)) {
        spans.back().end = span.end;
        return;
    }
    spans.push_back(std::move(span));
}

static void normalizeSpanCoverage(TextLayerData& data)
{
    const int len = static_cast<int>(data.text.size());
    if (len <= 0) {
        TextSpan style = data.spans.empty() ? TextSpan{} : data.spans.front();
        style.start = 0;
        style.end = 0;
        data.spans = {style};
        return;
    }

    std::sort(data.spans.begin(), data.spans.end(),
              [](const TextSpan& a, const TextSpan& b) {
                  if (a.start == b.start)
                      return a.end < b.end;
                  return a.start < b.start;
              });

    const auto sourceSpans = data.spans;
    std::vector<TextSpan> covered;
    covered.reserve(sourceSpans.size() + 2);

    int cursor = 0;
    for (auto span : sourceSpans) {
        span.start = std::clamp(span.start, 0, len);
        span.end = std::clamp(span.end, 0, len);
        if (span.start >= span.end)
            continue;
        if (span.end <= cursor)
            continue;

        if (cursor < span.start) {
            TextSpan gap = spanStyleAt(data, cursor);
            gap.start = cursor;
            gap.end = span.start;
            appendMergedSpan(covered, gap);
        }

        if (span.start < cursor)
            span.start = cursor;
        appendMergedSpan(covered, span);
        cursor = span.end;
    }

    if (cursor < len) {
        TextSpan gap = spanStyleAt(data, cursor);
        gap.start = cursor;
        gap.end = len;
        appendMergedSpan(covered, gap);
    }

    if (covered.empty()) {
        TextSpan span;
        span.start = 0;
        span.end = len;
        covered.push_back(span);
    }

    data.spans = std::move(covered);
}

TextEditorController::TextEditorController(QObject* parent)
    : QObject(parent)
{
    m_caretTimer = new QTimer(this);
    m_caretTimer->setInterval(530);
    connect(m_caretTimer, &QTimer::timeout, this, &TextEditorController::blinkCaret);
}

void TextEditorController::beginEdit(TextLayerData* data)
{
    m_data = data;
    m_cursorPos = m_data ? m_data->text.size() : 0;
    m_selStart = -1;
    m_selEnd = -1;
    m_editing = true;
    m_dirty = false;
    m_caretVisible = true;
    clearPending();
    if (m_data) {
        normalizeParagraphs(*m_data);
        normalizeSpanCoverage(*m_data);
    }
    m_caretTimer->start();
}

void TextEditorController::endEdit()
{
    m_editing = false;
    m_caretTimer->stop();
    if (m_dirty && m_data)
        m_data->dirty = true;
    m_caretVisible = false;
    m_selStart = -1;
    m_selEnd = -1;
    clearPending();
    emit editFinished();
}

void TextEditorController::clearPending()
{
    m_hasPending = false;
}

void TextEditorController::setCursorPos(int pos)
{
    m_cursorPos = std::clamp(pos, 0, static_cast<int>(m_data ? m_data->text.size() : 0));
    m_caretVisible = true;
    clearPending();
    m_caretTimer->start();
    emit caretChanged();
}

void TextEditorController::clampCursor()
{
    if (m_data)
        m_cursorPos = std::clamp(m_cursorPos, 0, static_cast<int>(m_data->text.size()));
}

void TextEditorController::clearSelection()
{
    m_selStart = -1;
    m_selEnd = -1;
    clearPending();
    emit caretChanged();
}

void TextEditorController::selectAll()
{
    if (!m_data) return;
    m_selStart = 0;
    m_selEnd = m_data->text.size();
    clearPending();
    emit caretChanged();
}

void TextEditorController::setSelection(int start, int end)
{
    if (!m_data) return;
    int len = static_cast<int>(m_data->text.size());
    m_selStart = std::clamp(start, 0, len);
    m_selEnd = std::clamp(end, 0, len);
    m_caretVisible = true;
    clearPending();
    m_caretTimer->start();
    emit caretChanged();
}

QString TextEditorController::selectedText() const
{
    if (!hasSelection() || !m_data) return {};
    int lo = std::min(m_selStart, m_selEnd);
    int hi = std::max(m_selStart, m_selEnd);
    return m_data->text.mid(lo, hi - lo);
}

void TextEditorController::deleteSelection()
{
    if (!hasSelection() || !m_data) return;
    int lo = std::min(m_selStart, m_selEnd);
    int hi = std::max(m_selStart, m_selEnd);

    const int removedNl = m_data->text.mid(lo, hi - lo).count(QLatin1Char('\n'));
    const int firstPara = paragraphIndexForChar(m_data->text, lo);

    m_data->text.remove(lo, hi - lo);

    // Map every span boundary through the deletion of [lo, hi).
    auto remap = [lo, hi](int idx) {
        if (idx <= lo) return idx;
        if (idx >= hi) return idx - (hi - lo);
        return lo;
    };
    for (auto& span : m_data->spans) {
        span.start = remap(span.start);
        span.end = remap(span.end);
    }
    normalizeSpanCoverage(*m_data);

    if (removedNl > 0 && firstPara + 1 < static_cast<int>(m_data->paragraphs.size())) {
        const int eraseCount = std::min(removedNl,
            static_cast<int>(m_data->paragraphs.size()) - (firstPara + 1));
        m_data->paragraphs.erase(m_data->paragraphs.begin() + firstPara + 1,
                                 m_data->paragraphs.begin() + firstPara + 1 + eraseCount);
    }
    normalizeParagraphs(*m_data);

    m_cursorPos = lo;
    clearSelection();
    m_dirty = true;
}

void TextEditorController::insertText(const QString& text)
{
    if (!m_data || text.isEmpty()) return;
    if (hasSelection()) deleteSelection();

    const bool applyPending = m_hasPending;
    const TextSpan pending = m_pendingStyle;

    normalizeParagraphs(*m_data);
    const int para = paragraphIndexForChar(m_data->text, m_cursorPos);
    const int insAt = m_cursorPos;
    const int n = text.size();
    TextSpan insertedStyle = applyPending ? pending : spanStyleAt(*m_data, insAt);
    insertedStyle.start = insAt;
    insertedStyle.end = insAt + n;

    m_data->text.insert(insAt, text);

    std::vector<TextSpan> shifted;
    shifted.reserve(m_data->spans.size() + 2);
    for (auto span : m_data->spans) {
        if (span.end <= insAt) {
            shifted.push_back(span);
        } else if (span.start >= insAt) {
            span.start += n;
            span.end += n;
            shifted.push_back(span);
        } else {
            TextSpan left = span;
            left.end = insAt;
            shifted.push_back(left);

            TextSpan right = span;
            right.start = insAt + n;
            right.end += n;
            shifted.push_back(right);
        }
    }
    shifted.push_back(insertedStyle);
    m_data->spans = std::move(shifted);
    normalizeSpanCoverage(*m_data);

    // Split paragraph styling: each inserted '\n' creates a new paragraph that
    // inherits the style of the paragraph being split.
    const int nl = text.count(QLatin1Char('\n'));
    if (nl > 0) {
        ParagraphStyle base = paragraphStyleAt(*m_data, para);
        if (para + 1 <= static_cast<int>(m_data->paragraphs.size()))
            m_data->paragraphs.insert(m_data->paragraphs.begin() + para + 1, nl, base);
    }
    normalizeParagraphs(*m_data);

    m_cursorPos += n;

    clearPending();

    m_dirty = true;
    emit textChanged();
}

void TextEditorController::backspace()
{
    if (!m_data) return;
    if (hasSelection()) { deleteSelection(); return; }
    if (m_cursorPos <= 0) return;

    const bool wasNewline = (m_data->text[m_cursorPos - 1] == QLatin1Char('\n'));
    const int para = wasNewline ? paragraphIndexForChar(m_data->text, m_cursorPos - 1) : -1;

    m_cursorPos--;
    m_data->text.remove(m_cursorPos, 1);

    for (auto& span : m_data->spans) {
        if (span.start > m_cursorPos) {
            span.start--;
            span.end--;
        } else if (span.end > m_cursorPos) {
            span.end--;
        }
    }
    normalizeSpanCoverage(*m_data);

    if (wasNewline && para + 1 < static_cast<int>(m_data->paragraphs.size()))
        m_data->paragraphs.erase(m_data->paragraphs.begin() + para + 1);
    normalizeParagraphs(*m_data);

    m_dirty = true;
    emit textChanged();
}

void TextEditorController::deleteChar()
{
    if (!m_data) return;
    if (hasSelection()) { deleteSelection(); return; }
    if (m_cursorPos >= m_data->text.size()) return;

    const bool isNewline = (m_data->text[m_cursorPos] == QLatin1Char('\n'));
    const int para = isNewline ? paragraphIndexForChar(m_data->text, m_cursorPos) : -1;

    m_data->text.remove(m_cursorPos, 1);

    for (auto& span : m_data->spans) {
        if (span.start > m_cursorPos) {
            span.start--;
            span.end--;
        } else if (span.end > m_cursorPos) {
            span.end--;
        }
    }
    normalizeSpanCoverage(*m_data);

    if (isNewline && para + 1 < static_cast<int>(m_data->paragraphs.size()))
        m_data->paragraphs.erase(m_data->paragraphs.begin() + para + 1);
    normalizeParagraphs(*m_data);

    m_dirty = true;
    emit textChanged();
}

void TextEditorController::newLine()
{
    insertText("\n");
}

void TextEditorController::moveLeft(bool shift, bool word)
{
    if (!m_data) return;
    clearPending();
    if (!shift && hasSelection()) {
        m_cursorPos = std::min(m_selStart, m_selEnd);
        clearSelection();
        return;
    }

    int newPos = m_cursorPos;
    if (word) {
        while (newPos > 0 && m_data->text[newPos - 1].isSpace()) --newPos;
        while (newPos > 0 && !m_data->text[newPos - 1].isSpace()) --newPos;
    } else {
        newPos = std::max(0, m_cursorPos - 1);
    }

    if (shift) {
        if (m_selStart < 0) { m_selStart = m_cursorPos; m_selEnd = m_cursorPos; }
        m_selEnd = newPos;
    } else {
        clearSelection();
    }
    m_cursorPos = newPos;
    m_caretVisible = true;
}

void TextEditorController::moveRight(bool shift, bool word)
{
    if (!m_data) return;
    clearPending();
    int len = m_data->text.size();
    if (!shift && hasSelection()) {
        m_cursorPos = std::max(m_selStart, m_selEnd);
        clearSelection();
        return;
    }

    int newPos = m_cursorPos;
    if (word) {
        while (newPos < len && m_data->text[newPos].isSpace()) ++newPos;
        while (newPos < len && !m_data->text[newPos].isSpace()) ++newPos;
    } else {
        newPos = std::min(len, m_cursorPos + 1);
    }

    if (shift) {
        if (m_selStart < 0) { m_selStart = m_cursorPos; m_selEnd = m_cursorPos; }
        m_selEnd = newPos;
    } else {
        clearSelection();
    }
    m_cursorPos = newPos;
    m_caretVisible = true;
}

void TextEditorController::moveUp(bool shift)
{
    if (!m_data) return;
    clearPending();
    if (!shift && hasSelection()) {
        m_cursorPos = std::min(m_selStart, m_selEnd);
        clearSelection();
        return;
    }

    TextLayoutEngine engine;
    engine.setData(m_data);
    QRectF cr = engine.cursorRect(m_cursorPos);
    if (cr.isEmpty()) { m_cursorPos = 0; return; }

    int newPos = engine.hitTest(
        QPointF(cr.center().x(), cr.top() - 2.0), true);

    if (shift) {
        if (m_selStart < 0) { m_selStart = m_cursorPos; m_selEnd = m_cursorPos; }
        m_selEnd = newPos;
    } else {
        clearSelection();
    }
    m_cursorPos = newPos;
    m_caretVisible = true;
}

void TextEditorController::moveDown(bool shift)
{
    if (!m_data) return;
    clearPending();
    if (!shift && hasSelection()) {
        m_cursorPos = std::max(m_selStart, m_selEnd);
        clearSelection();
        return;
    }

    TextLayoutEngine engine;
    engine.setData(m_data);
    QRectF cr = engine.cursorRect(m_cursorPos);
    if (cr.isEmpty()) return;

    int newPos = engine.hitTest(
        QPointF(cr.center().x(), cr.bottom() + 2.0), true);

    if (shift) {
        if (m_selStart < 0) { m_selStart = m_cursorPos; m_selEnd = m_cursorPos; }
        m_selEnd = newPos;
    } else {
        clearSelection();
    }
    m_cursorPos = newPos;
    m_caretVisible = true;
}

void TextEditorController::moveHome(bool shift)
{
    if (!m_data) return;
    clearPending();
    // Home goes to the start of the current line (paragraph for now), matching
    // editors: jump to the first character after the preceding '\n'.
    int target = m_cursorPos;
    while (target > 0 && m_data->text[target - 1] != QLatin1Char('\n')) --target;

    if (shift) {
        if (m_selStart < 0) m_selStart = m_cursorPos;
        m_selEnd = target;
    } else {
        clearSelection();
    }
    m_cursorPos = target;
    m_caretVisible = true;
}

void TextEditorController::moveEnd(bool shift)
{
    if (!m_data) return;
    clearPending();
    int len = static_cast<int>(m_data->text.size());
    int target = m_cursorPos;
    while (target < len && m_data->text[target] != QLatin1Char('\n')) ++target;

    if (shift) {
        if (m_selStart < 0) m_selStart = m_cursorPos;
        m_selEnd = target;
    } else {
        clearSelection();
    }
    m_cursorPos = target;
    m_caretVisible = true;
}

void TextEditorController::copy()
{
    if (!hasSelection() || !m_data) return;
    QGuiApplication::clipboard()->setText(selectedText());
}

void TextEditorController::cut()
{
    if (!hasSelection() || !m_data) return;
    copy();
    deleteSelection();
    m_dirty = true;
    emit textChanged();
}

void TextEditorController::paste()
{
    if (!m_data) return;
    QString clipText = QGuiApplication::clipboard()->text();
    if (!clipText.isEmpty())
        insertText(clipText);
}

void TextEditorController::applyToRange(int lo, int hi,
                                        const std::function<void(TextSpan&)>& fn)
{
    if (!m_data) return;
    const int len = static_cast<int>(m_data->text.size());
    lo = std::clamp(lo, 0, len);
    hi = std::clamp(hi, 0, len);
    if (lo >= hi) return;

    normalizeSpanCoverage(*m_data);

    std::vector<TextSpan> out;
    out.reserve(m_data->spans.size() + 2);
    for (const auto& span : m_data->spans) {
        if (span.end <= lo || span.start >= hi) { out.push_back(span); continue; }

        if (span.start < lo) {
            TextSpan left = span;
            left.end = lo;
            out.push_back(left);
        }
        TextSpan mid = span;
        mid.start = std::max(span.start, lo);
        mid.end = std::min(span.end, hi);
        fn(mid);
        out.push_back(mid);
        if (span.end > hi) {
            TextSpan right = span;
            right.start = hi;
            out.push_back(right);
        }
    }
    m_data->spans = std::move(out);
    mergeSpans();
    m_dirty = true;
}

void TextEditorController::modifyCharStyle(const std::function<void(TextSpan&)>& fn)
{
    if (!m_data) return;
    if (hasSelection()) {
        applyToRange(selLow(), selHigh(), fn);
    } else {
        if (!m_hasPending) { m_pendingStyle = currentStyle(); m_hasPending = true; }
        fn(m_pendingStyle);
        // On an empty layer, also mutate the default run so the change is
        // visible immediately and survives commit.
        if (m_data->text.isEmpty() && !m_data->spans.empty())
            fn(m_data->spans.front());
    }
    m_dirty = true;
    emit textChanged();
}

TextSpan TextEditorController::currentStyle() const
{
    if (m_hasPending) return m_pendingStyle;
    if (!m_data) return {};
    const int pos = hasSelection() ? std::min(m_selStart, m_selEnd) : m_cursorPos;
    return spanStyleAt(*m_data, pos);
}

void TextEditorController::applyToParagraphs(const std::function<void(ParagraphStyle&)>& fn)
{
    if (!m_data) return;
    normalizeParagraphs(*m_data);
    const int lo = hasSelection() ? selLow() : m_cursorPos;
    const int hi = hasSelection() ? selHigh() : m_cursorPos;
    const int pLo = paragraphIndexForChar(m_data->text, lo);
    const int pHi = paragraphIndexForChar(m_data->text, hi);
    for (int p = pLo; p <= pHi && p < static_cast<int>(m_data->paragraphs.size()); ++p)
        fn(m_data->paragraphs[p]);
    m_dirty = true;
    emit textChanged();
}

void TextEditorController::mergeSpans()
{
    if (!m_data) return;
    normalizeSpanCoverage(*m_data);
}

void TextEditorController::blinkCaret()
{
    m_caretVisible = !m_caretVisible;
    emit caretChanged();
}
