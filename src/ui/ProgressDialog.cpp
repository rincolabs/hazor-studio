#include "ProgressDialog.h"

#include "AppDialogChrome.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QCloseEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QVBoxLayout>
#include <algorithm>

namespace {
// Intrinsic to the control, not a layout prop: a bar thinner than this reads
// as a divider and a thicker one as a button.
constexpr int kProgressBarHeight = 14;
} // namespace

ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Progress"));
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setWindowModality(Qt::ApplicationModal);
    setModal(true);
    setSizeGripEnabled(false);

    // Fixed width, free height: an in-flight operation can change the status
    // text and the progress value, and neither may move the window.
    setFixedWidth(AppDialogChrome::kProgressWidth);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(AppDialogChrome::kContentMargin, AppDialogChrome::kContentMargin,
                             AppDialogChrome::kContentMargin, AppDialogChrome::kContentMargin);
    root->setSpacing(AppDialogChrome::kSectionSpacing);

    // Status line and bar are one block, so they sit at the tighter spacing;
    // the action row is a separate block and gets the section spacing.
    auto* statusBlock = new QVBoxLayout;
    statusBlock->setContentsMargins(0, 0, 0, 0);
    statusBlock->setSpacing(AppDialogChrome::kTextSpacing);

    m_message = tr("Processing");
    m_messageLabel = new QLabel(m_message, this);
    m_messageLabel->setObjectName(QStringLiteral("appDialogStatus"));
    // Ignored horizontally: the label must take the width the layout gives it
    // and elide inside it, never ask for the width its full text would need.
    m_messageLabel->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    statusBlock->addWidget(m_messageLabel);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("appDialogProgress"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(kProgressBarHeight);
    statusBlock->addWidget(m_progressBar);

    root->addLayout(statusBlock);

    // Wrapped in a widget rather than added as a bare layout so that hiding it
    // for a non-cancelable operation collapses its spacing too, instead of
    // leaving an empty band under the bar.
    m_actionRow = new QWidget(this);
    auto* actions = AppDialogChrome::createActionRow();
    m_actionRow->setLayout(actions);
    m_cancelButton = AppDialogChrome::createActionButton(
        tr("Cancel"), AppDialogChrome::ActionKind::Secondary, m_actionRow);
    actions->addWidget(m_cancelButton);
    m_actionRow->setVisible(false);
    root->addWidget(m_actionRow);

    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (!m_cancelable)
            return;
        m_canceled = true;
        m_cancelButton->setEnabled(false);
        emit cancelRequested();
    });

    connect(ThemeManager::instance(), &ThemeManager::themeChanged,
            this, &ProgressDialog::applyTheme);
    applyTheme();
}

void ProgressDialog::setMessage(const QString& message)
{
    m_message = message;
    updateMessageElide();
}

void ProgressDialog::setProgressValue(int value)
{
    setIndeterminate(value < 0);
    if (value >= 0)
        m_progressBar->setValue(std::clamp(value, 0, 100));
}

void ProgressDialog::setIndeterminate(bool indeterminate)
{
    if (indeterminate) {
        if (m_progressBar->minimum() != 0 || m_progressBar->maximum() != 0)
            m_progressBar->setRange(0, 0);
    } else if (m_progressBar->maximum() == 0) {
        m_progressBar->setRange(0, 100);
    }
}

void ProgressDialog::setCancelable(bool cancelable)
{
    m_canceled = false;
    m_cancelable = cancelable;
    m_cancelButton->setEnabled(cancelable);

    // isHidden(), not isVisible(): setCancelable() runs before show(), when a
    // child of a hidden window reports itself invisible either way.
    if (!m_actionRow->isHidden() == cancelable)
        return;

    // The only thing that legitimately changes this dialog's height, and it is
    // decided before the operation starts rather than during it.
    m_actionRow->setVisible(cancelable);
    AppDialogChrome::sizeToContent(this, AppDialogChrome::kProgressWidth);
}

void ProgressDialog::closeEvent(QCloseEvent* event)
{
    if (!m_cancelable) {
        event->ignore();
        return;
    }

    if (!m_canceled) {
        m_canceled = true;
        m_cancelButton->setEnabled(false);
        emit cancelRequested();
    }
    event->ignore();
}

void ProgressDialog::resizeEvent(QResizeEvent* event)
{
    QDialog::resizeEvent(event);
    updateMessageElide();
}

void ProgressDialog::updateMessageElide()
{
    if (!m_messageLabel)
        return;

    // Derived from the window width rather than read off the label: the label
    // has not been given its share of the row yet the first time through, and
    // a resize event arrives before the layout re-runs.
    const int available = width() - 2 * AppDialogChrome::kContentMargin;
    const QString shown = available > 0
        ? m_messageLabel->fontMetrics().elidedText(m_message, Qt::ElideRight, available)
        : m_message;

    m_messageLabel->setText(shown);
    m_messageLabel->setToolTip(shown == m_message ? QString() : m_message);
}

void ProgressDialog::applyTheme()
{
    const Theme* t = ThemeManager::instance()->current();
    if (!t)
        return;

    setStyleSheet(t->appDialogStyleSheet());

    // A theme swap can change the font, so both the elide and the height that
    // depends on it have to be redone.
    updateMessageElide();
    AppDialogChrome::sizeToContent(this, AppDialogChrome::kProgressWidth);
}
