#include "ScrollBarStyle.hpp"

#include <QStringList>

QString scrollBarQss(const QColor& track,
                     const QColor& handle,
                     const ScrollBarQssOptions& opts)
{
    QStringList out;
    const QString trackName = track.name();
    const QString handleName = handle.name();

    if (opts.vertical) {
        out << QStringLiteral("QScrollBar:vertical { background: %1; width: %2px; border: none; }")
                   .arg(trackName)
                   .arg(opts.thickness);
        out << QStringLiteral("QScrollBar::handle:vertical { background: %1; min-height: %2px; "
                              "border-radius: %3px; margin: %4px; }")
                   .arg(handleName)
                   .arg(opts.minLength)
                   .arg(opts.borderRadius)
                   .arg(opts.handleMargin);
        out << QStringLiteral("QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height: 0; }");
    } else if (opts.hideOtherOrientation) {
        out << QStringLiteral("QScrollBar:vertical { width: 0; height: 0; }");
    }

    if (opts.horizontal) {
        out << QStringLiteral("QScrollBar:horizontal { background: %1; height: %2px; border: none; }")
                   .arg(trackName)
                   .arg(opts.thickness);
        out << QStringLiteral("QScrollBar::handle:horizontal { background: %1; min-width: %2px; "
                              "border-radius: %3px; margin: %4px; }")
                   .arg(handleName)
                   .arg(opts.minLength)
                   .arg(opts.borderRadius)
                   .arg(opts.handleMargin);
        out << QStringLiteral("QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal { width: 0; }");
    } else if (opts.hideOtherOrientation) {
        out << QStringLiteral("QScrollBar:horizontal { width: 0; height: 0; }");
    }

    // Flatten the track and remove the arrows so the scrollbar is fully
    // QSS-driven — this is what stops the dotted native fallback on Windows.
    out << QStringLiteral(
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical, "
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: transparent; }");
    out << QStringLiteral(
        "QScrollBar::up-arrow, QScrollBar::down-arrow, "
        "QScrollBar::left-arrow, QScrollBar::right-arrow { background: none; border: none; width: 0; height: 0; }");

    return out.join(QLatin1Char(' '));
}
