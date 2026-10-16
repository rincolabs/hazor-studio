#pragma once

#include <QList>
#include <QMessageBox>
#include <QString>

class QWidget;

// Centralized presentation for message/confirmation/choice dialogs. Every
// consumer describes *what* to show (title, text, icon, buttons) and reads
// back the user's decision; this is the only place in the app that builds a
// QMessageBox, assigns icons/roles, picks the default/escape button and
// applies the themed button look (see ClassicTheme's QPushButton#okBtn /
// #cancelBtn). Changing the look or structure of every dialog in the app
// means editing AppMessageDialog.cpp, not its callers.
namespace AppMessageDialog {

enum class Icon { Information, Warning, Error, Question };

// One button in a choice() dialog. `role` drives both Qt's default/escape
// button detection and the themed objectName (AcceptRole -> okBtn,
// RejectRole -> cancelBtn), matching the convention already used by the
// app's purpose-built dialogs.
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
int choice(QWidget* parent, const QString& title, const QString& text, Icon icon,
           const QList<ButtonSpec>& buttons, int defaultIndex = -1,
           const QString& informativeText = QString(),
           const QString& detailedText = QString());

} // namespace AppMessageDialog
