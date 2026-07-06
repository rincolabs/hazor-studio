#include "MainHost.hpp"
#include "TitleBar.hpp"
#include "MainWindow.hpp"

#include <QVBoxLayout>
#include <QGuiApplication>
#include <QEvent>
#include <QWindowStateChangeEvent>
#include <QCloseEvent>

static bool supportsCustomTitleBar()
{
    QString pf = QGuiApplication::platformName();
    if (pf == "cocoa")    return false;
    if (pf == "wayland")  return false;
    if (pf == "offscreen") return false;
    return true;
}

MainHost::MainHost(QWidget* parent)
    : QWidget(parent)
{
    m_useNativeTitleBar = !supportsCustomTitleBar();

    setWindowTitle(tr("Image Editor"));

    auto* lay = new QVBoxLayout(this);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    if (!m_useNativeTitleBar) {
        m_titleBar = new TitleBar(this);
        lay->addWidget(m_titleBar);

        connect(m_titleBar, &TitleBar::minimizeClicked, this, &MainHost::onMinimize);
        connect(m_titleBar, &TitleBar::maximizeClicked, this, &MainHost::onMaximize);
        connect(m_titleBar, &TitleBar::closeClicked, this, &MainHost::onClose);
    }

    m_mainWindow = new MainWindow(this);
    lay->addWidget(m_mainWindow, 1);

    if (!m_useNativeTitleBar) {
        setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
        m_mainWindow->setWindowTitle(QString());
    }

    resize(1400, 850);
}

void MainHost::onMinimize()
{
    showMinimized();
}

void MainHost::onMaximize()
{
    if (isMaximized()) {
        showNormal();
    } else {
        showMaximized();
    }
    if (m_titleBar)
        m_titleBar->updateMaximizeButton(isMaximized());
}

void MainHost::onClose()
{
    close();
}

void MainHost::onWindowStateChanged(Qt::WindowStates state)
{
    if (m_titleBar)
        m_titleBar->updateMaximizeButton(state.testFlag(Qt::WindowMaximized));
}

bool MainHost::event(QEvent* e)
{
    if (e->type() == QEvent::WindowStateChange)
        onWindowStateChanged(windowState());
    return QWidget::event(e);
}
