#pragma once

#include <QList>
#include <QMessageBox>
#include <QString>

class QWidget;

// Centralized presentation for message/confirmation/choice dialogs. Every
// consumer describes *what* to show (title, text, icon, buttons) and reads
// back the user's decision; this is the only place in the app that builds one
// of these dialogs, assigns the severity badge, orders the action buttons,
// picks the default/escape button and applies the themed look. Geometry and
// the house style come from AppDialogChrome and
// Theme::appDialogStyleSheet() — the same two sources the loading/progress
// dialog uses. Changing the look or structure of every message dialog in the
// app means editing those, not their callers.
namespace AppMessageDialog {

enum class Icon { Information, Warning, Error, Question };

// One button in a choice() dialog. `role` carries the action's *semantics*,
// not its position: it decides the button's look (AcceptRole -> primary,
// DestructiveRole -> destructive, everything else -> secondary) and where the
// button lands in the row. Callers list their buttons in whatever order reads
// best in their own code and index the result by that same order.
struct ButtonSpec {
    QString label;
    QMessageBox::ButtonRole role;
};

// Dismiss-only notices (single "OK" button).
void information(QWidget* parent, const QString& title, const QString& text,
                  const QString& detailedText = QString());
void warning(QWidget* parent, const QString& title, const QString& text,
             const QString& detailedText = QString());
void error(QWidget* parent, const QString& title, const QString& text,
           const QString& detailedText = QString());

// Yes/No question (Question icon). Defaults to "No" so pressing Enter never
// arms a destructive action. Returns true if the user answered "Yes".
bool confirm(QWidget* parent, const QString& title, const QString& text);

// General decision dialog with caller-defined buttons (e.g. Save / Discard /
// Cancel). Returns the index into `buttons` that was clicked, or -1 if the
// dialog was dismissed without matching any of them. `defaultIndex` picks
// the button Enter triggers; pass -1 to leave no explicit default.
//
// The displayed order is derived from the roles, not from `buttons`: the
// reject action sits at the left edge of the group, the accept action at the
// right edge, and anything in between keeps its listed order.
int choice(QWidget* parent, const QString& title, const QString& text, Icon icon,
           const QList<ButtonSpec>& buttons, int defaultIndex = -1,
           const QString& informativeText = QString(),
           const QString& detailedText = QString());

} // namespace AppMessageDialog
