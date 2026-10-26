#include "AppDialogChrome.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QDialog>
#include <QFont>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLayout>
#include <QPainter>
#include <QPen>
#include <QPixmap>
#include <QPushButton>
#include <QRectF>
#include <QStringList>

namespace AppDialogChrome {

namespace {

QColor severityColor(Severity severity, const Theme& theme)
{
    switch (severity) {
    case Severity::Information: return theme.colorAccent;
    case Severity::Warning:     return theme.colorWarning;
    case Severity::Error:       return theme.colorDanger;
    case Severity::Question:    return theme.colorAccent;
    }
    return theme.colorAccent;
}

// Information, warning and question are drawn as characters because every font
// has them; the error cross is drawn as two strokes because "✕" is not in
// Inter's coverage on every platform and a missing glyph box would be worse
// than no icon at all.
QString severityGlyph(Severity severity)
{
    switch (severity) {
    case Severity::Information: return QStringLiteral("i");
    case Severity::Warning:     return QStringLiteral("!");
    case Severity::Question:    return QStringLiteral("?");
    case Severity::Error:       break;
    }
    return QString();
}

} // namespace

QPixmap severityBadge(Severity severity, qreal devicePixelRatio)
{
    const Theme* theme = ThemeManager::instance()->current();
    if (!theme)
        return QPixmap();

    const qreal dpr = devicePixelRatio > 0.0 ? devicePixelRatio : 1.0;
    QPixmap badge(QSize(kBadgeSize, kBadgeSize) * dpr);
    badge.setDevicePixelRatio(dpr);
    badge.fill(Qt::transparent);

    const QColor tint = severityColor(severity, *theme);

    QPainter painter(&badge);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    // Inset by the pen's half width so the ring is not clipped by the edge.
    const QRectF circle(0.75, 0.75, kBadgeSize - 1.5, kBadgeSize - 1.5);
    QColor fill = tint;
    fill.setAlpha(46);
    painter.setBrush(fill);
    painter.setPen(QPen(tint, 1.5));
    painter.drawEllipse(circle);

    painter.setPen(QPen(tint, 2.0, Qt::SolidLine, Qt::RoundCap));
    const QString glyph = severityGlyph(severity);
    if (glyph.isEmpty()) {
        const qreal arm = kBadgeSize * 0.22;
        const QPointF c = circle.center();
        painter.drawLine(QPointF(c.x() - arm, c.y() - arm), QPointF(c.x() + arm, c.y() + arm));
        painter.drawLine(QPointF(c.x() + arm, c.y() - arm), QPointF(c.x() - arm, c.y() + arm));
    } else {
        QFont font(theme->fontFamily);
        font.setPixelSize(kBadgeSize / 2);
        font.setWeight(QFont::Bold);
        painter.setFont(font);
        painter.drawText(circle, Qt::AlignCenter, glyph);
    }

    return badge;
}

QPushButton* createActionButton(const QString& label, ActionKind kind, QWidget* parent)
{
    auto* button = new QPushButton(label, parent);

    switch (kind) {
    case ActionKind::Primary:     button->setObjectName(QStringLiteral("okBtn")); break;
    case ActionKind::Secondary:   button->setObjectName(QStringLiteral("cancelBtn")); break;
    case ActionKind::Destructive: button->setObjectName(QStringLiteral("destructiveBtn")); break;
    }

    button->setMinimumWidth(kActionMinWidth);
    button->setMinimumHeight(kActionMinHeight);

    // QPushButton is auto-default inside a QDialog, which hands Return to
    // whichever button happens to hold focus. These dialogs name their default
    // explicitly instead, so the key never depends on focus history.
    button->setAutoDefault(false);
    button->setDefault(false);

    return button;
}

QHBoxLayout* createActionRow()
{
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(kActionSpacing);
    row->addStretch(1);
    return row;
}

void sizeToContent(QDialog* dialog, int width)
{
    QLayout* layout = dialog ? dialog->layout() : nullptr;
    if (!layout)
        return;

    layout->activate();

    const int settled = qMax(width, layout->totalMinimumSize().width());

    // A word-wrapped label only knows its height once the width is settled, so
    // the height is asked of the layout for the width chosen above. Falling
    // back to the plain size hint covers layouts with nothing wrapping in them.
    int height = layout->hasHeightForWidth() ? layout->totalHeightForWidth(settled) : -1;
    if (height < 0)
        height = layout->totalSizeHint().height();
    height = qMax(height, layout->totalMinimumSize().height());

    dialog->resize(settled, height);
}

int messageWidthFor(const QList<QLabel*>& labels)
{
    // QLabel's own width hint for wrapped text is a guess about a pleasant
    // aspect ratio, not about this envelope, and it never asks for more than
    // the minimum here. Measuring the text is what lets a long message claim
    // the extra 80px instead of wrapping into a tall, narrow column.
    int widest = 0;
    for (QLabel* label : labels) {
        if (!label)
            continue;
        label->ensurePolished();
        const QFontMetrics metrics = label->fontMetrics();
        const QStringList lines = label->text().split(QLatin1Char('\n'));
        for (const QString& line : lines)
            widest = qMax(widest, metrics.horizontalAdvance(line));
    }

    const int chrome = 2 * kContentMargin + kBadgeSize + kSectionSpacing;
    return qBound(kMessageMinWidth, chrome + widest, kMessageMaxWidth);
}

} // namespace AppDialogChrome
