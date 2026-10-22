#pragma once

#include <QDialog>
#include <QString>

class QLabel;
class QProgressBar;
class QPushButton;
class QCloseEvent;
class QResizeEvent;

// The app's loading/progress dialog: a status line, a progress bar that can be
// determinate or indeterminate, and a Cancel action that is only present when
// the operation can actually be cancelled. Its geometry and look come from
// AppDialogChrome and Theme::appDialogStyleSheet(), the same sources
// AppMessageDialog uses, so the two families match.
//
// The window never resizes while an operation runs: the width is fixed, and
// the status line is elided to one line rather than wrapped, so a longer
// message cannot make the dialog grow mid-operation. Only setCancelable()
// changes the height, and it is set before the dialog is shown.
class ProgressDialog : public QDialog {
    Q_OBJECT

public:
    explicit ProgressDialog(QWidget* parent = nullptr);

    void setMessage(const QString& message);
    void setProgressValue(int value);
    void setIndeterminate(bool indeterminate);
    void setCancelable(bool cancelable);
    bool wasCanceled() const { return m_canceled; }

signals:
    void cancelRequested();

protected:
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;

private:
    void applyTheme();
    void updateMessageElide();

    QLabel* m_messageLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QWidget* m_actionRow = nullptr;
    QPushButton* m_cancelButton = nullptr;
    QString m_message;
    bool m_cancelable = false;
    bool m_canceled = false;
};
