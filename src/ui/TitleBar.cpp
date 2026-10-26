#include "TitleBar.hpp"
#include "IconUtils.hpp"
#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QDesktopServices>
#include <QFrame>
#include <QGuiApplication>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QMenuBar>
#include <QMouseEvent>
#include <QUrl>
#include <QWindow>

bool TitleBar::isSupportedPlatform()
{
    const QString platform = QGuiApplication::platformName();
    if (platform == QLatin1String("cocoa"))     return false;
    if (platform == QLatin1String("wayland"))   return false;
    if (platform == QLatin1String("offscreen")) return false;
    return true;
}

TitleBar::TitleBar(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(kBarHeight);
    // Ensure the `TitleBar { background }` stylesheet rule is actually painted
    // across the whole bar, so transparent children (icon, window buttons) show
    // colorSurface instead of the darker window background behind them.
    setAttribute(Qt::WA_StyledBackground, true);
    auto* t = ThemeManager::instance()->current();
    setStyleSheet(t->titleBarStyleSheet());

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(t->spaceSM, 0, 0, 0);
    m_layout->setSpacing(t->spaceSM);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setPixmap(QPixmap(":/icons/app-icon.png"));
    m_iconLabel->setFixedSize(24, 24);
    m_iconLabel->setScaledContents(true);
    m_layout->addWidget(m_iconLabel);

    // The main menu bar (when set) is inserted at index 1 with a stretch factor,
    // so it fills the space between the icon and the window controls and shows
    // its overflow button when the window is too narrow — like a native menu.

    auto makeBtn = [this](const QString& iconPath, const QString& objName) -> QPushButton* {
        auto* btn = new QPushButton(this);
        btn->setObjectName(objName);
        btn->setIcon(makeIcon(iconPath));
        btn->setIconSize(QSize(20, 20));
        btn->setFixedSize(34, kBarHeight);
        btn->setFocusPolicy(Qt::NoFocus);
        return btn;
    };

    // Qt's push-button label leaves only a very narrow built-in gap after the
    // icon. Keep the translated label intact while adding one text-space of
    // breathing room between the heart and the word.
    m_sponsorBtn = new QPushButton(QString(QChar::Nbsp) + tr("Sponsor"), this);
    m_sponsorBtn->setObjectName(QStringLiteral("sponsorBtn"));
    m_sponsorBtn->setIcon(makeIcon(QStringLiteral(":/icons/sponsor-heart.png")));
    m_sponsorBtn->setIconSize(QSize(16, 16));
    m_sponsorBtn->setFixedSize(88, kBarHeight);
    m_sponsorBtn->setFocusPolicy(Qt::NoFocus);

    auto* windowControlsSeparator = new QFrame(this);
    windowControlsSeparator->setObjectName(QStringLiteral("titleBarWindowSeparator"));
    windowControlsSeparator->setFrameShape(QFrame::VLine);
    windowControlsSeparator->setFrameShadow(QFrame::Plain);
    windowControlsSeparator->setFixedSize(1, 16);

    m_minBtn   = makeBtn(":/icons/window-minimize.png", "minBtn");
    m_maxBtn   = makeBtn(":/icons/window-maximize.png", "maxBtn");
    m_closeBtn = makeBtn(":/icons/window-close.png",    "closeBtn");

    m_layout->addWidget(m_sponsorBtn);
    m_layout->addSpacing(t->spaceSM);
    m_layout->addWidget(windowControlsSeparator, 0, Qt::AlignVCenter);
    m_layout->addSpacing(t->spaceSM);
    m_layout->addWidget(m_minBtn);
    m_layout->addWidget(m_maxBtn);
    m_layout->addWidget(m_closeBtn);

    connect(m_sponsorBtn, &QPushButton::clicked, this, []() {
        QDesktopServices::openUrl(
            QUrl(QStringLiteral("https://github.com/sponsors/allanesquina")));
    });
    connect(m_minBtn, &QPushButton::clicked, this, &TitleBar::minimizeClicked);
    connect(m_maxBtn, &QPushButton::clicked, this, &TitleBar::maximizeClicked);
    connect(m_closeBtn, &QPushButton::clicked, this, &TitleBar::closeClicked);

    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, [this]() {
        auto* t = ThemeManager::instance()->current();
        setStyleSheet(t->titleBarStyleSheet());
    });
}

void TitleBar::setTitleText(const QString& text)
{
    if (!m_titleLabel) {
        m_titleLabel = new QLabel(this);
        m_titleLabel->setObjectName(QStringLiteral("titleBarTitle"));
        // The label ignores mouse events, so pressing it falls through to the
        // bar itself and still drags the window.
        m_titleLabel->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        // Same slot the menu bar would occupy, with the same stretch, so the
        // window controls stay flush right.
        m_layout->insertWidget(1, m_titleLabel, 1);
    }
    m_titleLabel->setText(text);
}

void TitleBar::setMenuBar(QWidget* menuBar)
{
    if (!menuBar || m_menuBar == menuBar)
        return;
    m_menuBar = menuBar;
    menuBar->setParent(this);
    // Match the bar height so QMenuBar items are vertically centered. Without
    // this, QMenuBar's Minimum vertical size policy leaves it at its natural
    // height (~20 px) and the layout places it at the top of the 34 px bar.
    menuBar->setFixedHeight(kBarHeight);
    // Insert right after the app icon (index 0), before the window controls,
    // with a stretch factor of 1 so it absorbs the available width and reduces
    // naturally (overflow button) as the window narrows.
    m_layout->insertWidget(1, menuBar, 1);
    // The menu fills the bar, so route clicks on its empty area to window drag.
    menuBar->installEventFilter(this);
}

void TitleBar::updateMaximizeButton(bool maximized)
{
    m_maxBtn->setIcon(makeIcon(maximized
        ? ":/icons/window-restore.png"
        : ":/icons/window-maximize.png"));
}

void TitleBar::mousePressEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        if (auto* w = window()->windowHandle())
            w->startSystemMove();
        e->accept();
        return;
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent* e)
{
    if (e->button() == Qt::LeftButton) {
        emit maximizeClicked();
        e->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(e);
}

bool TitleBar::eventFilter(QObject* obj, QEvent* e)
{
    if (obj == m_menuBar &&
        (e->type() == QEvent::MouseButtonPress || e->type() == QEvent::MouseButtonDblClick)) {
        auto* me = static_cast<QMouseEvent*>(e);
        auto* mb = qobject_cast<QMenuBar*>(m_menuBar);
        // Only the empty area (no menu item under the cursor) acts as a handle;
        // clicks on items still open their menus normally.
        if (me->button() == Qt::LeftButton && mb && !mb->actionAt(me->position().toPoint())) {
            if (e->type() == QEvent::MouseButtonDblClick) {
                emit maximizeClicked();
            } else if (auto* w = window()->windowHandle()) {
                w->startSystemMove();
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}
