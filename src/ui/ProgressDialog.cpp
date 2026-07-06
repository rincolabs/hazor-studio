#include "ProgressDialog.h"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QVBoxLayout>
#include <algorithm>

ProgressDialog::ProgressDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Progress"));
    setWindowModality(Qt::ApplicationModal);
    setModal(true);
    setFixedSize(360, 112);
    setSizeGripEnabled(false);

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(14, 12, 14, 12);
    root->setSpacing(8);

    m_messageLabel = new QLabel(tr("Processing"), this);
    m_messageLabel->setObjectName(QStringLiteral("progressMessage"));
    m_messageLabel->setMinimumHeight(18);
    root->addWidget(m_messageLabel);

    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setObjectName(QStringLiteral("progressBar"));
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setTextVisible(false);
    m_progressBar->setFixedHeight(14);
    row->addWidget(m_progressBar, 1);

    m_cancelButton = new QPushButton(tr("Cancel"), this);
    m_cancelButton->setObjectName(QStringLiteral("progressCancelButton"));
    m_cancelButton->setFixedWidth(78);
    row->addWidget(m_cancelButton, 0);

    root->addLayout(row);

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
    m_messageLabel->setText(message);
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
    m_cancelButton->setVisible(cancelable);
    m_cancelButton->setEnabled(cancelable && !m_canceled);
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

void ProgressDialog::applyTheme()
{
    const auto* t = ThemeManager::instance()->current();
    if (!t)
        return;

    setStyleSheet(QStringLiteral(R"(
        QDialog {
            background: %1;
            color: %2;
            border: 1px solid %3;
            font-family: "%4";
            font-size: %5px;
        }
        QLabel#progressMessage {
            color: %2;
            background: transparent;
        }
        QProgressBar#progressBar {
            background: %6;
            border: 1px solid %3;
            border-radius: %7px;
        }
        QProgressBar#progressBar::chunk {
            background: %8;
            border-radius: %7px;
        }
        QPushButton#progressCancelButton {
            background: %6;
            color: %2;
            border: 1px solid %3;
            border-radius: %9px;
            padding: 4px 10px;
        }
        QPushButton#progressCancelButton:hover {
            background: %10;
        }
        QPushButton#progressCancelButton:pressed {
            background: %11;
        }
        QPushButton#progressCancelButton:disabled {
            color: %12;
            background: %6;
        }
    )")
        .arg(t->colorPanelBackground.name())
        .arg(t->colorTextPrimary.name())
        .arg(t->colorBorder.name())
        .arg(t->fontFamily)
        .arg(t->fontSizeMD)
        .arg(t->colorSurface.name())
        .arg(t->radiusSM)
        .arg(t->colorAccent.name())
        .arg(t->radiusMD)
        .arg(t->colorSurfaceHover.name())
        .arg(t->colorSurfacePressed.name())
        .arg(t->colorTextDisabled.name()));
}
