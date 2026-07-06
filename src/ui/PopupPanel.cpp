#include "PopupPanel.hpp"

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

#include <QApplication>
#include <QGraphicsDropShadowEffect>
#include <QKeyEvent>
#include <QScreen>
#include <QVBoxLayout>

PopupPanel::PopupPanel(QWidget* parent)
    : QWidget(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_StyledBackground, true);
    setFocusPolicy(Qt::StrongFocus);

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(0, 0, 0, 0);
    m_layout->setSpacing(0);

    auto* shadow = new QGraphicsDropShadowEffect(this);
    shadow->setBlurRadius(18);
    shadow->setOffset(0, 5);
    shadow->setColor(QColor(0, 0, 0, 95));
    setGraphicsEffect(shadow);

    applyTheme();
    connect(ThemeManager::instance(), &ThemeManager::themeChanged, this, &PopupPanel::applyTheme);
}

void PopupPanel::setContentWidget(QWidget* content)
{
    if (m_content == content)
        return;

    if (m_content)
        m_content->setParent(nullptr);

    m_content = content;
    if (!m_content)
        return;

    m_content->setParent(this);
    m_layout->addWidget(m_content);
}

void PopupPanel::showAnchoredTo(QWidget* anchor)
{
    if (!anchor)
        return;

    adjustSize();
    QPoint pos = anchor->mapToGlobal(QPoint(0, anchor->height() + 4));
    const QSize panelSize = sizeHint();
    if (auto* screen = anchor->screen() ? anchor->screen() : qApp->primaryScreen()) {
        const QRect available = screen->availableGeometry();
        if (pos.x() + panelSize.width() > available.right())
            pos.setX(qMax(available.left(), available.right() - panelSize.width()));
        if (pos.y() + panelSize.height() > available.bottom())
            pos.setY(qMax(available.top(), anchor->mapToGlobal(QPoint(0, -panelSize.height() - 4)).y()));
    }

    move(pos);
    show();
    raise();
    activateWindow();
    setFocus(Qt::PopupFocusReason);
}

void PopupPanel::toggleAnchoredTo(QWidget* anchor)
{
    if (isVisible()) {
        hide();
        return;
    }

    showAnchoredTo(anchor);
}

void PopupPanel::keyPressEvent(QKeyEvent* event)
{
    if (event && event->key() == Qt::Key_Escape) {
        hide();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void PopupPanel::applyTheme()
{
    auto* t = ThemeManager::instance()->current();
    setStyleSheet(QStringLiteral(
        "PopupPanel {"
        "  background: %1;"
        "  border: 1px solid %2;"
        "  border-radius: %3px;"
        "}")
        .arg(t->colorSurface.name())
        .arg(t->colorBorder.name())
        .arg(t->radiusMD));
}
