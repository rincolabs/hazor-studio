#pragma once

#include <QWidget>
#include <QPainter>

#include "theme/Theme.hpp"
#include "theme/ThemeManager.hpp"

// Vertical separator for use inside ToolOptionsBar pages.
// Draws a single 1 px line centered in the available height, with
// configurable line height ratio and color. Fully independent from QSS.
class ToolBarSeparator : public QWidget
{
public:
    // lineHeightRatio: fraction of the widget height the line occupies (0..1).
    explicit ToolBarSeparator(QWidget* parent = nullptr, qreal lineHeightRatio = 0.55)
        : QWidget(parent)
        , m_lineHeightRatio(lineHeightRatio)
    {
        setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
        setFixedWidth(9);
        setAttribute(Qt::WA_NoSystemBackground, true);

        QObject::connect(ThemeManager::instance(), &ThemeManager::themeChanged,
                         this, [this]() { update(); });
    }

    QSize sizeHint() const override { return QSize(9, 20); }

protected:
    void paintEvent(QPaintEvent*) override
    {
        QColor color;
        if (auto* t = ThemeManager::instance()->current())
            color = t->colorBorder;
        if (!color.isValid())
            color = QColor(80, 85, 92);

        const int lineH = qMax(1, qRound(height() * m_lineHeightRatio));
        const int x     = width() / 2;
        const int y0    = (height() - lineH) / 2;

        QPainter p(this);
        p.setPen(QPen(color, 1));
        p.drawLine(x, y0, x, y0 + lineH - 1);
    }

private:
    qreal m_lineHeightRatio;
};
