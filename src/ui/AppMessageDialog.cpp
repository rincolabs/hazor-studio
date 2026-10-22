#include "AppMessageDialog.hpp"
#include "AppDialogChrome.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QCoreApplication>
#include <QDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QAbstractTextDocumentLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextBlock>
#include <QTextDocument>
#include <QtMath>
#include <QVBoxLayout>

namespace AppMessageDialog {

namespace {

QString tr(const char* s) { return QCoreApplication::translate("AppMessageDialog", s); }

AppDialogChrome::Severity toSeverity(Icon icon)
{
    switch (icon) {
    case Icon::Information: return AppDialogChrome::Severity::Information;
    case Icon::Warning:     return AppDialogChrome::Severity::Warning;
    case Icon::Error:       return AppDialogChrome::Severity::Error;
    case Icon::Question:    return AppDialogChrome::Severity::Question;
    }
    return AppDialogChrome::Severity::Information;
}

AppDialogChrome::ActionKind toActionKind(QMessageBox::ButtonRole role)
{
    switch (role) {
    case QMessageBox::AcceptRole:
    case QMessageBox::YesRole:
    case QMessageBox::ApplyRole:
        return AppDialogChrome::ActionKind::Primary;
    case QMessageBox::DestructiveRole:
        return AppDialogChrome::ActionKind::Destructive;
    default:
        return AppDialogChrome::ActionKind::Secondary;
    }
}

bool isRejectRole(QMessageBox::ButtonRole role)
{
    return role == QMessageBox::RejectRole || role == QMessageBox::NoRole;
}

bool isAcceptRole(QMessageBox::ButtonRole role)
{
    return role == QMessageBox::AcceptRole || role == QMessageBox::YesRole
           || role == QMessageBox::ApplyRole;
}

// The one ordering rule for the whole app: the way out of the dialog sits at
// the left edge of the group, the action the dialog is *for* sits at the right
// edge, and anything in between (a second, non-primary choice such as
// "Discard" or "Use CPU") keeps the order the caller listed it in. Returns
// indices into the caller's list, so a caller can still read its own order
// back from choice()'s return value.
QList<int> displayOrder(const QList<ButtonSpec>& buttons)
{
    QList<int> reject, middle, accept;
    for (int i = 0; i < buttons.size(); ++i) {
        if (isRejectRole(buttons[i].role))
            reject.append(i);
        else if (isAcceptRole(buttons[i].role))
            accept.append(i);
        else
            middle.append(i);
    }
    return reject + middle + accept;
}

// Which button Escape (and the window's close button) stands for. Mirrors the
// rule the app relied on while these dialogs were QMessageBoxes: an
// unambiguous way out is honoured, anything ambiguous dismisses with no
// decision at all (choice() returns -1, which every caller already treats as
// "cancel").
int escapeIndex(const QList<ButtonSpec>& buttons)
{
    if (buttons.size() == 1)
        return 0;

    int found = -1;
    for (int i = 0; i < buttons.size(); ++i) {
        if (!isRejectRole(buttons[i].role))
            continue;
        if (found >= 0)
            return -1;
        found = i;
    }
    return found;
}

// The dialog itself: a severity badge beside a text block, an optional
// disclosed details pane, and the action row. Nothing in here is sized by
// hand — the width envelope and the spacing come from AppDialogChrome and the
// height falls out of the content.
class MessageDialog : public QDialog {
public:
    MessageDialog(QWidget* parent, Icon icon, const QString& title, const QString& text,
                  const QString& informativeText, const QString& detailedText,
                  const QList<ButtonSpec>& buttons, int defaultIndex);

    int clickedIndex() const { return m_clickedIndex; }

    void reject() override;

private:
    void toggleDetails();
    int disclosedDetailsHeight() const;

    QPlainTextEdit* m_detailsView = nullptr;
    QPushButton* m_detailsToggle = nullptr;
    int m_width = AppDialogChrome::kMessageMinWidth;
    int m_clickedIndex = -1;
    int m_escapeIndex = -1;
};

MessageDialog::MessageDialog(QWidget* parent, Icon icon, const QString& title,
                             const QString& text, const QString& informativeText,
                             const QString& detailedText,
                             const QList<ButtonSpec>& buttons, int defaultIndex)
    : QDialog(parent)
    , m_escapeIndex(escapeIndex(buttons))
{
    setWindowTitle(title);
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);
    setSizeGripEnabled(false);
    if (const Theme* theme = ThemeManager::instance()->current())
        setStyleSheet(theme->appDialogStyleSheet());

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(AppDialogChrome::kContentMargin, AppDialogChrome::kContentMargin,
                             AppDialogChrome::kContentMargin, AppDialogChrome::kContentMargin);
    root->setSpacing(AppDialogChrome::kSectionSpacing);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(AppDialogChrome::kSectionSpacing);

    auto* badge = new QLabel(this);
    badge->setPixmap(AppDialogChrome::severityBadge(toSeverity(icon), devicePixelRatioF()));
    badge->setFixedSize(AppDialogChrome::kBadgeSize, AppDialogChrome::kBadgeSize);
    badge->setAttribute(Qt::WA_TransparentForMouseEvents);
    body->addWidget(badge, 0, Qt::AlignTop);

    auto* textColumn = new QVBoxLayout;
    textColumn->setContentsMargins(0, 0, 0, 0);
    textColumn->setSpacing(AppDialogChrome::kTextSpacing);

    QList<QLabel*> textLabels;

    // `text` is a heading only when there is a description under it to head.
    // On its own it is the whole message, and a message several lines long set
    // in heading weight reads as shouting rather than as hierarchy — the two
    // styles share a size, so only the emphasis differs between the forms.
    const bool hasDescription = !informativeText.isEmpty();

    auto* titleLabel = new QLabel(text, this);
    titleLabel->setObjectName(hasDescription ? QStringLiteral("appDialogTitle")
                                             : QStringLiteral("appDialogMessage"));
    titleLabel->setWordWrap(true);
    titleLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    textColumn->addWidget(titleLabel);
    textLabels.append(titleLabel);

    if (hasDescription) {
        auto* descriptionLabel = new QLabel(informativeText, this);
        descriptionLabel->setObjectName(QStringLiteral("appDialogDescription"));
        descriptionLabel->setWordWrap(true);
        descriptionLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
        textColumn->addWidget(descriptionLabel);
        textLabels.append(descriptionLabel);
    }

    // The text has to start level with the badge's top edge. The row is as
    // tall as its taller side, and a QVBoxLayout hands leftover height to the
    // labels themselves, so without a trailing stretch a one-line message
    // would sit centred in a tall label instead of next to the badge.
    textColumn->addStretch(1);
    body->addLayout(textColumn, 1);
    root->addLayout(body);

    if (!detailedText.isEmpty()) {
        m_detailsView = new QPlainTextEdit(detailedText, this);
        m_detailsView->setObjectName(QStringLiteral("appDialogDetails"));
        m_detailsView->setReadOnly(true);
        m_detailsView->setFrameShape(QFrame::NoFrame);
        m_detailsView->setLineWrapMode(QPlainTextEdit::WidgetWidth);
        m_detailsView->setMaximumHeight(AppDialogChrome::kDetailsMaxHeight);
        // Ignored horizontally so disclosing the report changes the height
        // only: a text view's own width hint would otherwise shove the dialog
        // out to the maximum width the moment it appears.
        m_detailsView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        m_detailsView->setVisible(false);
        root->addWidget(m_detailsView);
    }

    auto* actionRow = AppDialogChrome::createActionRow();

    if (m_detailsView) {
        m_detailsToggle = new QPushButton(tr("Show Details"), this);
        m_detailsToggle->setObjectName(QStringLiteral("appDialogDetailsToggle"));
        m_detailsToggle->setAutoDefault(false);
        m_detailsToggle->setDefault(false);
        m_detailsToggle->setFlat(true);
        connect(m_detailsToggle, &QPushButton::clicked, this, &MessageDialog::toggleDetails);
        actionRow->insertWidget(0, m_detailsToggle);
    }

    QList<QPushButton*> created;
    created.reserve(buttons.size());
    for (const ButtonSpec& spec : buttons) {
        const int index = static_cast<int>(created.size());
        const QMessageBox::ButtonRole role = spec.role;
        QPushButton* button =
            AppDialogChrome::createActionButton(spec.label, toActionKind(role), this);
        connect(button, &QPushButton::clicked, this, [this, index, role]() {
            m_clickedIndex = index;
            done(isAcceptRole(role) ? QDialog::Accepted : QDialog::Rejected);
        });
        created.append(button);
    }
    for (int index : displayOrder(buttons))
        actionRow->addWidget(created[index]);

    root->addLayout(actionRow);

    if (defaultIndex >= 0 && defaultIndex < static_cast<int>(created.size())) {
        created[defaultIndex]->setDefault(true);
        created[defaultIndex]->setFocus();
    }

    // Settled once, from the text: disclosing the details below must change
    // the height only, never shuffle the message into a different wrap.
    m_width = AppDialogChrome::messageWidthFor(textLabels);
    setMinimumWidth(AppDialogChrome::kMessageMinWidth);
    AppDialogChrome::sizeToContent(this, m_width);
}

void MessageDialog::reject()
{
    // Escape and the window's close button both land here. They mean "the
    // dialog's way out", which is the escape button when there is one.
    if (m_clickedIndex < 0)
        m_clickedIndex = m_escapeIndex;
    QDialog::reject();
}

void MessageDialog::toggleDetails()
{
    const bool show = !m_detailsView->isVisible();
    m_detailsView->setVisible(show);
    m_detailsToggle->setText(show ? tr("Hide Details") : tr("Show Details"));

    // Twice on purpose. The first pass gives the pane the dialog's width, which
    // is what decides how many lines the report wraps to; only then can the
    // second pass ask for a height that fits those lines instead of the full
    // cap. Collapsed, the pane is an empty layout item and leaves no band.
    AppDialogChrome::sizeToContent(this, m_width);
    if (show) {
        m_detailsView->setMaximumHeight(disclosedDetailsHeight());
        AppDialogChrome::sizeToContent(this, m_width);
    }
}

int MessageDialog::disclosedDetailsHeight() const
{
    // QPlainTextDocumentLayout reports its document height in lines, not
    // pixels — the pixel height is that count against the view's own metrics.
    // Summed from the blocks as they were actually laid out, not from font
    // metrics: QPlainTextEdit reports its document height in lines, and
    // re-deriving pixels from a line count gets the pitch wrong often enough
    // to leave the pane one scrollbar step short of its own content.
    const QTextDocument* doc = m_detailsView->document();
    qreal content = 0.0;
    for (QTextBlock block = doc->begin(); block.isValid(); block = block.next())
        content += doc->documentLayout()->blockBoundingRect(block).height();

    const int needed = qCeil(content + 2 * doc->documentMargin())
                       + 2 * m_detailsView->frameWidth();
    return qMin(AppDialogChrome::kDetailsMaxHeight, needed);
}

void present(QWidget* parent, Icon icon, const QString& title, const QString& text,
             const QString& detailedText)
{
    const QList<ButtonSpec> buttons = { { tr("OK"), QMessageBox::AcceptRole } };
    MessageDialog dialog(parent, icon, title, text, QString(), detailedText, buttons,
                         /*defaultIndex=*/0);
    dialog.exec();
}

} // namespace

void information(QWidget* parent, const QString& title, const QString& text,
                  const QString& detailedText)
{
    present(parent, Icon::Information, title, text, detailedText);
}

void warning(QWidget* parent, const QString& title, const QString& text,
             const QString& detailedText)
{
    present(parent, Icon::Warning, title, text, detailedText);
}

void error(QWidget* parent, const QString& title, const QString& text,
           const QString& detailedText)
{
    present(parent, Icon::Error, title, text, detailedText);
}

bool confirm(QWidget* parent, const QString& title, const QString& text)
{
    const QList<ButtonSpec> buttons = {
        { tr("Yes"), QMessageBox::AcceptRole },
        { tr("No"),  QMessageBox::RejectRole },
    };
    return choice(parent, title, text, Icon::Question, buttons, /*defaultIndex=*/1) == 0;
}

int choice(QWidget* parent, const QString& title, const QString& text, Icon icon,
           const QList<ButtonSpec>& buttons, int defaultIndex,
           const QString& informativeText, const QString& detailedText)
{
    MessageDialog dialog(parent, icon, title, text, informativeText, detailedText, buttons,
                         defaultIndex);
    dialog.exec();
    return dialog.clickedIndex();
}

} // namespace AppMessageDialog
