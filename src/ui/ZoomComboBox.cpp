#include "ZoomComboBox.hpp"

#include <QEvent>
#include <QFontMetrics>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSignalBlocker>
#include <QTimer>
#include <QValidator>

#include <algorithm>
#include <cmath>

namespace {
class ZoomTextValidator : public QValidator {
public:
    explicit ZoomTextValidator(QObject* parent = nullptr)
        : QValidator(parent)
    {
    }

    State validate(QString& input, int& pos) const override
    {
        Q_UNUSED(pos)

        const QString trimmed = input.trimmed();
        if (trimmed.isEmpty())
            return Intermediate;

        int percentCount = 0;
        int dotCount = 0;
        bool seenDigit = false;
        bool seenPercent = false;

        for (int i = 0; i < input.size(); ++i) {
            const QChar ch = input.at(i);
            if (ch.isSpace())
                continue;
            if (ch.isDigit()) {
                if (seenPercent)
                    return Invalid;
                seenDigit = true;
                continue;
            }
            if (ch == QLatin1Char('.')) {
                if (seenPercent || ++dotCount > 1)
                    return Invalid;
                continue;
            }
            if (ch == QLatin1Char('%')) {
                if (++percentCount > 1)
                    return Invalid;
                seenPercent = true;
                continue;
            }
            return Invalid;
        }

        if (trimmed == QStringLiteral(".") || trimmed == QStringLiteral("%"))
            return Intermediate;

        return seenDigit ? Acceptable : Intermediate;
    }
};
}

ZoomComboBox::ZoomComboBox(QWidget* parent)
    : QComboBox(parent)
{
    setEditable(true);
    setCompleter(nullptr);
    setInsertPolicy(QComboBox::NoInsert);
    setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    setMinimumContentsLength(6);

    const int fixedWidth = fontMetrics().horizontalAdvance(QStringLiteral("10000%")) + 44;
    setFixedWidth(fixedWidth);
    setFocusPolicy(Qt::StrongFocus);

    addItems({
        QStringLiteral("10%"),
        QStringLiteral("25%"),
        QStringLiteral("50%"),
        QStringLiteral("66.7%"),
        QStringLiteral("75%"),
        QStringLiteral("100%"),
        QStringLiteral("125%"),
        QStringLiteral("150%"),
        QStringLiteral("200%"),
        QStringLiteral("300%"),
        QStringLiteral("400%"),
        QStringLiteral("800%"),
        QStringLiteral("1600%"),
        QStringLiteral("3200%")
    });

    if (auto* editor = lineEdit()) {
        editor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        editor->setValidator(new ZoomTextValidator(editor));
        editor->installEventFilter(this);
        connect(editor, &QLineEdit::returnPressed, this, &ZoomComboBox::commitEditorText);
        connect(editor, &QLineEdit::editingFinished, this, &ZoomComboBox::commitEditorText);
    }

    connect(this, QOverload<int>::of(&QComboBox::activated),
            this, [this](int index) {
        if (index < 0)
            return;
        if (auto* editor = lineEdit())
            editor->setText(itemText(index));
        commitEditorText();
    });

    setZoom(m_lastValidZoom);
}

void ZoomComboBox::setZoomRange(float minZoom, float maxZoom)
{
    if (minZoom <= 0.0f || maxZoom < minZoom)
        return;

    m_minZoom = minZoom;
    m_maxZoom = maxZoom;
    setZoom(std::clamp(m_lastValidZoom, m_minZoom, m_maxZoom));
}

void ZoomComboBox::setZoom(float zoom)
{
    const float clampedZoom = std::clamp(zoom, m_minZoom, m_maxZoom);
    if (m_updating)
        return;

    m_updating = true;
    m_lastValidZoom = clampedZoom;

    const QSignalBlocker blocker(this);
    const QString text = m_editing ? editorTextForZoom(clampedZoom) : formatZoom(clampedZoom);
    setCurrentText(text);
    if (auto* editor = lineEdit())
        editor->setText(text);

    m_updating = false;
}

QString ZoomComboBox::formatZoom(float zoom)
{
    const double percent = static_cast<double>(zoom) * 100.0;
    const double roundedInteger = std::round(percent);
    if (std::abs(percent - roundedInteger) < 0.05)
        return QStringLiteral("%1%").arg(static_cast<int>(roundedInteger));

    const double roundedTenth = std::round(percent * 10.0) / 10.0;
    return QStringLiteral("%1%").arg(roundedTenth, 0, 'f', 1);
}

bool ZoomComboBox::parseZoomText(const QString& text, float minZoom, float maxZoom, float& zoom)
{
    QString normalized = text.trimmed();
    if (normalized.endsWith(QLatin1Char('%')))
        normalized.chop(1);
    normalized = normalized.trimmed();

    bool ok = false;
    const double percent = normalized.toDouble(&ok);
    if (!ok || !std::isfinite(percent))
        return false;

    const double parsedZoom = percent / 100.0;
    if (parsedZoom < static_cast<double>(minZoom) || parsedZoom > static_cast<double>(maxZoom))
        return false;

    zoom = static_cast<float>(parsedZoom);
    return true;
}

bool ZoomComboBox::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == lineEdit()) {
        switch (event->type()) {
            case QEvent::FocusIn:
                beginEditing();
                break;
            case QEvent::FocusOut:
                commitEditorText();
                break;
            case QEvent::MouseButtonPress:
            case QEvent::MouseButtonDblClick:
                beginEditing();
                if (auto* editor = lineEdit())
                    editor->setFocus(Qt::MouseFocusReason);
                selectEditorText();
                return true;
            case QEvent::KeyPress: {
                auto* keyEvent = static_cast<QKeyEvent*>(event);
                if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
                    commitEditorText();
                    return true;
                }
                if (keyEvent->key() == Qt::Key_Escape) {
                    cancelEditing();
                    return true;
                }
                break;
            }
            default:
                break;
        }
    }

    return QComboBox::eventFilter(watched, event);
}

void ZoomComboBox::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        commitEditorText();
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Escape) {
        cancelEditing();
        event->accept();
        return;
    }

    QComboBox::keyPressEvent(event);
}

void ZoomComboBox::beginEditing()
{
    if (m_updating || m_editing)
        return;

    m_editing = true;
    const QSignalBlocker blocker(this);
    const QString text = editorTextForZoom(m_lastValidZoom);
    setCurrentText(text);
    if (auto* editor = lineEdit()) {
        editor->setText(text);
        selectEditorText();
    }
}

void ZoomComboBox::commitEditorText()
{
    if (m_updating || m_committing)
        return;

    m_committing = true;
    float requestedZoom = m_lastValidZoom;
    const QString text = lineEdit() ? lineEdit()->text() : currentText();
    if (!parseZoomText(text, m_minZoom, m_maxZoom, requestedZoom)) {
        m_editing = false;
        restoreLastValidZoom();
        m_committing = false;
        return;
    }

    m_editing = false;

    if (std::abs(requestedZoom - m_lastValidZoom) >= 0.0001f) {
        emit zoomRequested(requestedZoom);
        m_committing = false;
        return;
    }

    restoreLastValidZoom();
    m_committing = false;
}

void ZoomComboBox::cancelEditing()
{
    if (!m_editing) {
        restoreLastValidZoom();
        return;
    }

    m_editing = false;
    restoreLastValidZoom();
}

void ZoomComboBox::restoreLastValidZoom()
{
    setZoom(m_lastValidZoom);
}

void ZoomComboBox::selectEditorText()
{
    if (auto* editor = lineEdit())
        QTimer::singleShot(0, editor, &QLineEdit::selectAll);
}

QString ZoomComboBox::editorTextForZoom(float zoom)
{
    QString text = formatZoom(zoom);
    if (text.endsWith(QLatin1Char('%')))
        text.chop(1);
    return text;
}
