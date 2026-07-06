#pragma once

#include <QDialog>

class QLabel;
class QProgressBar;
class QPushButton;
class QCloseEvent;

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

private:
    void applyTheme();

    QLabel* m_messageLabel = nullptr;
    QProgressBar* m_progressBar = nullptr;
    QPushButton* m_cancelButton = nullptr;
    bool m_cancelable = false;
    bool m_canceled = false;
};
