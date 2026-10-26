#pragma once

#include <QList>
#include <QString>

class QDialog;
class QHBoxLayout;
class QLabel;
class QPixmap;
class QPushButton;
class QWidget;

// Shared chrome for the two dialog families that do not edit anything: the
// message dialogs (AppMessageDialog — notices, warnings, errors and decisions)
// and the loading/progress dialogs (ProgressDialog). Both take their width
// envelope, interior rhythm, action-button metrics and action ordering from
// here, which is what makes them look like one family regardless of the
// feature that raised them. Changing the house style for them is a change to
// this file plus Theme::appDialogStyleSheet(), never to a single dialog.
//
// Purpose-built editing and configuration dialogs (ToolDialog,
// ExportImageDialog, the colour-management dialogs, …) lay themselves out and
// are deliberately out of scope.
namespace AppDialogChrome {

// Width envelopes. Height is *never* a constant in either family: it is
// derived from the laid-out content by sizeToContent(), so a one-line notice
// is short and a five-line one grows instead of clipping.
inline constexpr int kMessageMinWidth = 420;
inline constexpr int kMessageMaxWidth = 500;

// Progress dialogs get a single width rather than a range: their message is
// elided to one line, so nothing about an in-flight operation can make the
// window jump sideways between two updates.
inline constexpr int kProgressWidth = 440;

// Interior rhythm. One margin on all four sides, one gap between the stacked
// sections (text block, details, actions) and a tighter gap inside the text
// block itself.
inline constexpr int kContentMargin  = 20;
inline constexpr int kSectionSpacing = 16;
inline constexpr int kTextSpacing    = 6;
inline constexpr int kActionSpacing  = 8;

// Action buttons are floors, not fixed sizes: a long label still grows the
// button, a short one never collapses to the width of its text.
inline constexpr int kActionMinWidth = 92;
inline constexpr int kActionMinHeight = 28;

inline constexpr int kBadgeSize = 32;

// How much room the details pane may claim once disclosed, before it scrolls.
inline constexpr int kDetailsMaxHeight = 180;

enum class Severity { Information, Warning, Error, Question };

// Visual weight of an action, not its position: ordering is decided by
// AppMessageDialog from the caller's button roles.
enum class ActionKind { Primary, Secondary, Destructive };

// Badge for the message dialogs, painted from the active theme's status
// colours so the four kinds stay distinguishable in every theme.
QPixmap severityBadge(Severity severity, qreal devicePixelRatio);

// Themed action button with the family's size floors. The object name it gets
// is what Theme hangs the look on (okBtn / cancelBtn / destructiveBtn).
QPushButton* createActionButton(const QString& label, ActionKind kind, QWidget* parent);

// Empty, right-aligned action row: widgets appended to it land at the right
// edge in call order, so the last one added sits furthest right. Anything that
// belongs on the *left* (a details disclosure) is inserted at index 0.
QHBoxLayout* createActionRow();

// Turns a built layout into a window size. `width` is the width the dialog
// has settled on — the caller picks it, because only the caller knows how wide
// its own content wants to be before wrapping (see messageWidthFor()). The
// height is then asked of the layout *for that width*, the only way a
// word-wrapped label reports an honest height, which is why neither family
// needs a height constant. Call it again whenever content is shown or hidden.
void sizeToContent(QDialog* dialog, int width);

// The width a message dialog should take: wide enough for the longest line in
// `labels` to avoid wrapping, clamped into [kMessageMinWidth, kMessageMaxWidth]
// so a one-line notice lands on the minimum and a paragraph grows at most to
// the maximum instead of stretching across the screen.
int messageWidthFor(const QList<QLabel*>& labels);

} // namespace AppDialogChrome
